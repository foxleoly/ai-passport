// session.go — the state the pi extension publishes, replacing the herdr
// snapshot the sidecar used to read. The extension writes one file per pi
// process (session-<pid>.json), so several agents can run at once without
// clobbering each other; the sidecar follows the most recently updated one.
package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
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

// loadSession picks the state the device should show: the pinned pi process
// when one was requested, otherwise the most recently updated one. It returns
// nil when no pi is publishing, which the device renders as offline.
func loadSession(pin int) *SessionState {
	candidates := sessionCandidates(time.Now())
	if pin > 0 {
		for i := range candidates {
			if candidates[i].PID == pin {
				return &candidates[i]
			}
		}
		return nil
	}
	if len(candidates) == 0 {
		return nil
	}
	return &candidates[0]
}

// State maps the published idle flag to the buddy state vocabulary. A nil
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
