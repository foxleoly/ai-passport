package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestHerdrAgentSummary(t *testing.T) {
	raw := `{"result":{"snapshot":{"agents":[
		{"agent_status":"idle"},
		{"agent_status":"working"},
		{"agent_status":"working"}
	]}}}`
	var s HerdrSnapshot
	if err := json.Unmarshal([]byte(raw), &s); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	total, working := s.AgentSummary()
	if total != 3 || working != 2 {
		t.Fatalf("summary = (%d,%d), want (3,2)", total, working)
	}
}

// A missing herdr must report the count as unavailable, never as zero: the
// device shows the difference between "none running" and "cannot know".
func TestSubagentsUnavailableWithoutHerdr(t *testing.T) {
	for _, bin := range []string{"", "/nonexistent/herdr"} {
		if got := readSubagents(bin); got.Available {
			t.Errorf("readSubagents(%q) = %+v, want unavailable", bin, got)
		}
	}
	if herdrAvailable("") || herdrAvailable("/nonexistent/herdr") {
		t.Fatal("herdrAvailable must be false for empty or missing binaries")
	}
	if !herdrAvailable("sh") {
		t.Fatal(`herdrAvailable("sh") should be true`)
	}
}

// A binary that is present but answers nonsense is also just "unavailable".
func TestSubagentsUnavailableOnBadOutput(t *testing.T) {
	if got := readSubagents("/bin/echo"); got.Available {
		t.Fatalf("readSubagents(echo) = %+v, want unavailable", got)
	}
}

func TestSubagentsFromASnapshot(t *testing.T) {
	dir := t.TempDir()
	fake := filepath.Join(dir, "fake-herdr")
	body := "#!/bin/sh\necho '{\"result\":{\"snapshot\":{\"agents\":[{\"agent_status\":\"working\"},{\"agent_status\":\"idle\"}]}}}'\n"
	if err := os.WriteFile(fake, []byte(body), 0o755); err != nil {
		t.Fatalf("write: %v", err)
	}

	got := readSubagents(fake)
	if !got.Available || got.Total != 2 || got.Working != 1 {
		t.Fatalf("readSubagents = %+v, want available (2,1)", got)
	}
}
