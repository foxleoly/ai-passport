package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Fixture pi-session JSONL lines covering the shapes we parse.
var fixture = []string{
	`{"type":"session","id":"x"}`,
	`{"type":"model_change","provider":"agnes","modelId":"agnes-3.0-flash"}`,
	`{"type":"message","message":{"role":"assistant","model":"agnes-3.0-flash","provider":"agnes","stopReason":"toolUse","usage":{"totalTokens":1000,"cost":{"total":0.01}},"content":[{"type":"thinking","thinking":"..."},{"type":"tool_use","name":"read","input":{"path":"main/x.c"}}]}}`,
	`{"type":"message","message":{"role":"toolResult","toolName":"read","isError":false,"content":[]}}`,
	`{"type":"message","message":{"role":"assistant","model":"agnes-3.0-flash","provider":"agnes","stopReason":"end","usage":{"totalTokens":500,"cost":{"total":0.02}},"content":[{"type":"tool_use","name":"bash","input":{"command":"go build"}}]}}`,
	`{"type":"message","message":{"role":"toolResult","toolName":"bash","isError":true,"content":[]}}`,
}

func decodeFixture() []map[string]any {
	var out []map[string]any
	for _, ln := range fixture {
		var m map[string]any
		if err := json.Unmarshal([]byte(ln), &m); err == nil {
			out = append(out, m)
		}
	}
	return out
}

func TestAggregateBasic(t *testing.T) {
	e := Aggregate(decodeFixture())
	if e.Model != "agnes-3.0-flash" {
		t.Fatalf("model = %q, want agnes-3.0-flash", e.Model)
	}
	if e.Provider != "agnes" {
		t.Fatalf("provider = %q, want agnes", e.Provider)
	}
	if e.Tokens != 1500 {
		t.Fatalf("tokens = %d, want 1500", e.Tokens)
	}
	if e.Cost < 0.0299 || e.Cost > 0.0301 {
		t.Fatalf("cost = %f, want ~0.03", e.Cost)
	}
	if e.LastTool != "bash" {
		t.Fatalf("lastTool = %q, want bash", e.LastTool)
	}
	if e.LastArg != "go build" {
		t.Fatalf("lastArg = %q, want 'go build'", e.LastArg)
	}
	if e.LastResultOK == nil || *e.LastResultOK {
		t.Fatalf("lastResultOK = %v, want false (last toolResult isError=true)", e.LastResultOK)
	}
	if e.StopReason != "end" {
		t.Fatalf("stopReason = %q, want end", e.StopReason)
	}
	if e.Records != 6 {
		t.Fatalf("records = %d, want 6", e.Records)
	}
}

func TestNormalizeAgentState(t *testing.T) {
	cases := map[string]string{
		"working": "running",
		"idle":    "idle",
		"unknown": "offline",
		"":        "offline",
		"weird":   "weird",
	}
	for in, want := range cases {
		if got := NormalizeAgentState(in); got != want {
			t.Fatalf("NormalizeAgentState(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestHerdrSnapshotFocus(t *testing.T) {
	raw := `{"id":"cli:api:snapshot","result":{"snapshot":{"agents":[
		{"agent":"pi","agent_status":"idle","pane_id":"w4:p1","agent_session":{"value":"/a.jsonl"}},
		{"agent":"pi","agent_status":"working","pane_id":"w4:p9","focused":true,
		 "terminal_title":"π - ai-passport","cwd":"/w","agent_session":{"value":"/c.jsonl"}}
	],"focused_pane_id":"w4:p9"}}}`
	var s HerdrSnapshot
	if err := json.Unmarshal([]byte(raw), &s); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	f := s.Focus()
	if f == nil || f.PaneID != "w4:p9" {
		t.Fatalf("focus = %+v, want pane w4:p9", f)
	}
	if f.AgentSession.Value != "/c.jsonl" {
		t.Fatalf("focus jsonl = %q, want /c.jsonl", f.AgentSession.Value)
	}
	total, working := s.AgentSummary()
	if total != 2 || working != 1 {
		t.Fatalf("summary = (%d,%d), want (2,1)", total, working)
	}
}

func TestFirstArg(t *testing.T) {
	if got := firstArg(map[string]any{"command": "ls", "path": "/x"}); got != "/x" {
		t.Fatalf("path priority: got %q", got)
	}
	if got := firstArg(map[string]any{"foo": "bar"}); got != "bar" {
		t.Fatalf("fallback first string: got %q", got)
	}
	if got := firstArg("plain"); got != "plain" {
		t.Fatalf("string input: got %q", got)
	}
}

func TestTailRecords(t *testing.T) {
	d := t.TempDir()
	p := filepath.Join(d, "s.jsonl")
	lines := make([]string, 0, 5)
	for i := 0; i < 5; i++ {
		lines = append(lines, `{"type":"message","message":{"role":"assistant","model":"m`+string(rune('0'+i))+`","usage":{"totalTokens":1}}}`)
	}
	if err := os.WriteFile(p, []byte(strings.Join(lines, "\n")+"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	recs := tailRecords(p, 3)
	if len(recs) != 3 {
		t.Fatalf("tailRecords got %d records, want 3", len(recs))
	}
	// last 3 of 5 -> last line is m4
	last := recs[len(recs)-1]
	if m, _ := last["message"].(map[string]any); m["model"] != "m4" {
		t.Fatalf("tail last model = %v, want m4", m["model"])
	}
}
