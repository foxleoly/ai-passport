package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// writeSession drops one published state into the fake ~/.pi-buddy.
func writeSession(t *testing.T, dir string, pid int, idle bool, updated time.Time) {
	t.Helper()
	state := SessionState{PID: pid, Idle: idle, SessionFile: fmt.Sprintf("/tmp/%d.jsonl", pid)}
	if !updated.IsZero() {
		state.Updated = updated.UTC().Format(time.RFC3339)
	}
	data, err := json.Marshal(state)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if err := os.WriteFile(filepath.Join(dir, fmt.Sprintf("session-%d.json", pid)), data, 0o600); err != nil {
		t.Fatalf("write: %v", err)
	}
}

// resetPrimary clears the sticky choice so a test starts from a known state.
func resetPrimary() {
	primaryMu.Lock()
	primaryPID = 0
	primaryMu.Unlock()
}

// fakeStateDir points stateDir() at a fresh temporary home.
func fakeStateDir(t *testing.T) string {
	t.Helper()
	resetPrimary()
	home := t.TempDir()
	t.Setenv("HOME", home)
	dir := filepath.Join(home, ".pi-buddy")
	if err := os.MkdirAll(dir, 0o700); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	return dir
}

// A pi that exits without cleaning up must age out, and an unparsable file must
// not take the whole scan down.
func TestSessionCandidatesKeepsOnlyFreshParsable(t *testing.T) {
	dir := fakeStateDir(t)
	now := time.Now()
	writeSession(t, dir, 101, false, now)
	writeSession(t, dir, 202, true, now.Add(-time.Minute))
	if err := os.WriteFile(filepath.Join(dir, "session-303.json"), []byte("{oops"), 0o600); err != nil {
		t.Fatalf("write: %v", err)
	}

	got := sessionCandidates(now)
	if len(got) != 1 || got[0].PID != 101 {
		t.Fatalf("candidates = %+v, want only pid 101", got)
	}
}

// The device follows the agent that is actually active, so the newest state
// wins; an explicit --pi-pid pins one process instead.
func TestLoadSessionPrefersNewestAndHonoursPin(t *testing.T) {
	dir := fakeStateDir(t)
	now := time.Now()
	writeSession(t, dir, 101, false, now.Add(-3*time.Second))
	writeSession(t, dir, 202, false, now)

	if got := loadSession(0); got == nil || got.PID != 202 {
		t.Fatalf("loadSession(0) = %+v, want the newest (202)", got)
	}
	if got := loadSession(101); got == nil || got.PID != 101 {
		t.Fatalf("loadSession(101) = %+v, want the pinned pid", got)
	}
	if got := loadSession(999); got != nil {
		t.Fatalf("loadSession(999) = %+v, want nil for an unknown pid", got)
	}
}

func TestLoadSessionIsNilWhenNobodyPublishes(t *testing.T) {
	fakeStateDir(t)
	if got := loadSession(0); got != nil {
		t.Fatalf("loadSession = %+v, want nil", got)
	}
	if got := loadSession(0).State(); got != "offline" {
		t.Fatalf("state = %q, want offline", got)
	}
}

// Every pi rewrites its state once a second, so with several running, "the newest
// file" alternates constantly. Following that made the device flip between
// sessions; the primary has to stay put.
func TestPrimaryDoesNotFlipWhenSessionsAlternate(t *testing.T) {
	resetPrimary()
	running := SessionState{PID: 1111}
	idle := SessionState{PID: 2222, Idle: true}

	first := pickPrimary([]SessionState{running, idle}, 0)
	if first == nil {
		t.Fatal("no primary")
	}
	want := first.PID

	// The other session writes last, over and over.
	for i := 0; i < 5; i++ {
		got := pickPrimary([]SessionState{idle, running}, 0)
		if got.PID != want {
			t.Fatalf("primary flipped to %d, want it to stay on %d", got.PID, want)
		}
	}
}

// Two sessions that are both working must not trade places either.
func TestPrimaryStaysBetweenTwoWorkingSessions(t *testing.T) {
	resetPrimary()
	a := SessionState{PID: 1111}
	b := SessionState{PID: 2222}

	want := pickPrimary([]SessionState{a, b}, 0).PID
	for i := 0; i < 5; i++ {
		if got := pickPrimary([]SessionState{b, a}, 0).PID; got != want {
			t.Fatalf("primary flipped to %d, want %d", got, want)
		}
	}
}

// A working session wins over an idle one, and the primary only moves on when the
// session it was following is gone.
func TestPrimaryPrefersWorkingAndMovesOnWhenGone(t *testing.T) {
	resetPrimary()
	idle := SessionState{PID: 1111, Idle: true}
	running := SessionState{PID: 2222}

	if got := pickPrimary([]SessionState{idle, running}, 0); got.PID != running.PID {
		t.Fatalf("primary = %d, want the working session %d", got.PID, running.PID)
	}
	// It stays there even when the idle session writes last.
	if got := pickPrimary([]SessionState{idle, running}, 0); got.PID != running.PID {
		t.Fatalf("primary = %d, want it to stay on %d", got.PID, running.PID)
	}
	// Once it disappears, the remaining one takes over.
	if got := pickPrimary([]SessionState{idle}, 0); got.PID != idle.PID {
		t.Fatalf("primary = %d, want the surviving %d", got.PID, idle.PID)
	}
	if got := pickPrimary(nil, 0); got != nil {
		t.Fatalf("primary = %+v, want nil when nobody publishes", got)
	}
}

// The state the device shows is the aggregate: anything working means running.
func TestSessionsStateIsAggregated(t *testing.T) {
	running := SessionState{PID: 1}
	idle := SessionState{PID: 2, Idle: true}

	cases := []struct {
		name string
		all  []SessionState
		want string
	}{
		{"nobody", nil, "offline"},
		{"one working", []SessionState{running}, "running"},
		{"one idle", []SessionState{idle}, "idle"},
		{"mixed", []SessionState{idle, running, idle}, "running"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := (Sessions{All: c.all}).State(); got != c.want {
				t.Fatalf("state = %q, want %q", got, c.want)
			}
		})
	}
}
