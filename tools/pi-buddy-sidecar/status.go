// status.go — publishes the WebSocket link state to a small file so anything on
// this machine can show it without talking to the sidecar (the pi extension, a
// shell prompt, a watcher).
//
// The file carries the sidecar's pid, so a reader can tell a stale file left by a
// dead sidecar from a live link, and this file is only ever a convenience: every
// failure to write it is ignored rather than disturbing the link.
package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// linkStatus is the published shape of the WebSocket link.
type linkStatus struct {
	Connected bool   `json:"connected"`
	Device    string `json:"device,omitempty"`
	PID       int    `json:"pid"`
	Updated   string `json:"updated"`
}

// statusPath is where the published state lives.
func statusPath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".pi-buddy", "status.json"), nil
}

// marshalLinkStatus is split out from the write so the published shape is
// testable without touching the filesystem.
func marshalLinkStatus(connected bool, device string, pid int, now time.Time) ([]byte, error) {
	return json.Marshal(linkStatus{
		Connected: connected,
		Device:    device,
		PID:       pid,
		Updated:   now.UTC().Format(time.RFC3339),
	})
}

func publishLinkStatus(connected bool, device string) {
	path, err := statusPath()
	if err != nil {
		return
	}
	data, err := marshalLinkStatus(connected, device, os.Getpid(), time.Now())
	if err != nil {
		return
	}
	if os.MkdirAll(filepath.Dir(path), 0o755) != nil {
		return
	}
	_ = os.WriteFile(path, append(data, '\n'), 0o644)
}

// publish is indirect so tests can exercise the state transitions without writing
// to the user's home directory.
var publish = publishLinkStatus

// Connections can overlap, so the published state tracks how many are open. A
// closing connection that just cleared the flag could otherwise overwrite the
// state of one that has already replaced it, leaving the link reported offline
// until the next event. The last-opened address rides along with the count so a
// close that leaves another connection up does not blank the device field.
var (
	linkMu     sync.Mutex
	linkOpen   int
	linkDevice string
)

func noteLinkOpened(device string) {
	linkMu.Lock()
	linkOpen++
	linkDevice = device
	linkMu.Unlock()
	publish(true, device)
}

func noteLinkClosed() {
	linkMu.Lock()
	if linkOpen > 0 {
		linkOpen--
	}
	remaining := linkOpen
	device := linkDevice
	linkMu.Unlock()
	if remaining > 0 {
		// Another connection is still up: keep the address we last saw rather
		// than reporting "linked" with no device.
		publish(true, device)
		return
	}
	publish(false, "")
}
