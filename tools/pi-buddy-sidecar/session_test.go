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

// fakeStateDir points stateDir() at a fresh temporary home.
func fakeStateDir(t *testing.T) string {
	t.Helper()
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
