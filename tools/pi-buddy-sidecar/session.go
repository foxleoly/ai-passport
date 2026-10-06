// session.go — the state the pi extension publishes, replacing the herdr
// snapshot the sidecar used to read. The extension writes one file per pi
// process (session-<pid>.json), so several agents can run at once without
// clobbering each other.
package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"
)

// SessionState mirrors ~/.pi-buddy/session-<pid>.json, rewritten by the pi
// extension every second.
type SessionState struct {
	PID         int    `json:"pid"`
	SessionFile string `json:"session_file"`
	Cwd         string `json:"cwd"`
	Title       string `json:"title"`
	Idle        bool   `json:"idle"`
	Updated     string `json:"updated"`

	// Parsed from Updated; used for ordering only.
	UpdatedAt time.Time `json:"-"`
}

// sessionStale is how long a published state stays believable. The extension
// rewrites it every second, so this tolerates several missed ticks and still
// notices a pi that died without cleaning up after itself.
const sessionStale = 10 * time.Second

// stateDir is ~/.pi-buddy, the shared control directory.
func stateDir() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".pi-buddy"), nil
}

// sessionCandidates lists every published state that is still fresh, newest
// first.
func sessionCandidates(now time.Time) []SessionState {
	dir, err := stateDir()
	if err != nil {
		return nil
	}
	paths, err := filepath.Glob(filepath.Join(dir, "session-*.json"))
	if err != nil {
		return nil
	}
	var out []SessionState
	for _, path := range paths {
		data, err := os.ReadFile(path)
		if err != nil {
			continue
		}
		var s SessionState
		if json.Unmarshal(data, &s) != nil {
			continue
		}
		ts, err := time.Parse(time.RFC3339, s.Updated)
		if err != nil || now.Sub(ts) > sessionStale {
			continue
		}
		s.UpdatedAt = ts
		out = append(out, s)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].UpdatedAt.After(out[j].UpdatedAt) })
	return out
}

// Sessions is every fresh published state, plus the one the device shows details
// for. Every pi rewrites its file once a second, so with several running "the
// newest file" changes constantly: choosing that per heartbeat made the device
// flip between sessions.
type Sessions struct {
	All     []SessionState
	Primary *SessionState
}

// State is running when any pi is working, idle when at least one is up, and
// offline when nobody publishes. Aggregating keeps the progress bar steady: the
// question it answers is "is anything working", not "what did the last write say".
func (s Sessions) State() string {
	if len(s.All) == 0 {
		return "offline"
	}
	for i := range s.All {
		if !s.All[i].Idle {
			return "running"
		}
	}
	return "idle"
}

// primaryMu guards the sticky choice below; each transport builds heartbeats from
// its own goroutine, and the one-shot path can race with them.
var (
	primaryMu  sync.Mutex
	primaryPID int
)

// pickPrimary keeps following the session already chosen while it stays fresh, so
// the device does not flicker between agents. It only moves on when that session
// disappears, or when another one is working and the current one is not.
func pickPrimary(all []SessionState, pin int) *SessionState {
	if pin > 0 {
		for i := range all {
			if all[i].PID == pin {
				return &all[i]
			}
		}
		return nil
	}

	primaryMu.Lock()
	defer primaryMu.Unlock()

	current := -1
	for i := range all {
		if all[i].PID == primaryPID {
			current = i
			break
		}
	}
	anyRunning := false
	for i := range all {
		if !all[i].Idle {
			anyRunning = true
			break
		}
	}

	// Keep the current one while it is still there and still worth showing: it is
	// working, or nothing else is.
	if current >= 0 && (!all[current].Idle || !anyRunning) {
		return &all[current]
	}
	// Otherwise prefer a working session; All is already newest first.
	for i := range all {
		if !all[i].Idle {
			primaryPID = all[i].PID
			return &all[i]
		}
	}
	if len(all) == 0 {
		primaryPID = 0
		return nil
	}
	primaryPID = all[0].PID
	return &all[0]
}

// loadSessions reads every fresh published state and picks the one to show
// details for. A nil Primary means nobody is publishing, i.e. pi is not running.
func loadSessions(pin int) Sessions {
	all := sessionCandidates(time.Now())
	return Sessions{All: all, Primary: pickPrimary(all, pin)}
}

// loadSession is loadSessions for callers that only need the session shown.
func loadSession(pin int) *SessionState {
	return loadSessions(pin).Primary
}

// State maps one published idle flag to the buddy state vocabulary. A nil
// session means nobody is publishing, i.e. pi is not running.
func (s *SessionState) State() string {
	if s == nil {
		return "offline"
	}
	if s.Idle {
		return "idle"
	}
	return "running"
}
