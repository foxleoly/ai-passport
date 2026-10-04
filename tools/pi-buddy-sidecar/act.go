// act.go — device acts travel to the pi extension through ~/.pi-buddy. This
// replaces the herdr keystrokes the sidecar used to inject into a terminal
// pane: the extension knows its own session, so no target is involved and the
// same code works on every platform.
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// ActRequest is what the extension picks up from act.json.
type ActRequest struct {
	ID     string `json:"id"`
	Kind   string `json:"kind"`
	Text   string `json:"text,omitempty"`
	Issued string `json:"issued"`
}

// ActResult is what the extension writes back once it has run the act.
type ActResult struct {
	ID     string `json:"id"`
	OK     bool   `json:"ok"`
	Detail string `json:"detail,omitempty"`
}

// actWait is how long an act waits for the extension's answer. The extension
// polls once a second, so this allows several ticks.
const actWait = 5 * time.Second

// actPoll is how often we look for the answer while waiting.
const actPoll = 100 * time.Millisecond

// actText is the message an approve/deny delivers, or "" for interrupt.
func actText(kind, approveText, denyText string) (string, error) {
	switch kind {
	case "interrupt":
		return "", nil
	case "approve":
		return orDefault(approveText, "yes"), nil
	case "deny":
		return orDefault(denyText, "no"), nil
	default:
		return "", fmt.Errorf("unknown act kind %q", kind)
	}
}

// PreviewAct describes an act without running it (dry-run).
func PreviewAct(kind, approveText, denyText string) string {
	text, err := actText(kind, approveText, denyText)
	if err != nil {
		return "<unknown act>"
	}
	if kind == "interrupt" {
		return "pi extension: abort the current run"
	}
	return fmt.Sprintf("pi extension: send %q", text)
}

// RunAct hands one act to the pi extension and waits for its answer.
func RunAct(kind, approveText, denyText string) (string, error) {
	text, err := actText(kind, approveText, denyText)
	if err != nil {
		return "", err
	}
	dir, err := stateDir()
	if err != nil {
		return "", err
	}
	requestPath := filepath.Join(dir, "act.json")
	resultPath := filepath.Join(dir, "act-result.json")

	id := fmt.Sprintf("act-%d", time.Now().UnixNano())
	request := ActRequest{ID: id, Kind: kind, Text: text, Issued: time.Now().UTC().Format(time.RFC3339Nano)}
	if err := writeJSONAtomic(requestPath, request); err != nil {
		return "", err
	}

	deadline := time.Now().Add(actWait)
	for time.Now().Before(deadline) {
		var result ActResult
		if readJSONFile(resultPath, &result) == nil && result.ID == id {
			if result.OK {
				return result.Detail, nil
			}
			return result.Detail, fmt.Errorf("the pi extension refused the act: %s", result.Detail)
		}
		time.Sleep(actPoll)
	}
	return "", fmt.Errorf("no answer from the pi extension within %s (is it installed, and is pi running?)", actWait)
}

// orDefault returns s, or d when s is empty.
func orDefault(s, d string) string {
	if s == "" {
		return d
	}
	return s
}

// writeJSONAtomic replaces path in one step, so a reader never sees half a
// document.
func writeJSONAtomic(path string, v any) error {
	data, err := json.Marshal(v)
	if err != nil {
		return err
	}
	tmp := fmt.Sprintf("%s.%d.tmp", path, os.Getpid())
	if err := os.WriteFile(tmp, append(data, '\n'), 0o600); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}

// readJSONFile decodes path into v, reporting why it failed.
func readJSONFile(path string, v any) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	return json.Unmarshal(data, v)
}
