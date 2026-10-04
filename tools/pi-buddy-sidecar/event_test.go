package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// Fixture pi-session JSONL lines covering the shapes we parse.
var fixture = []string{
	`{"type":"session","id":"x"}`,
	`{"type":"model_change","provider":"agnes","modelId":"agnes-3.0-flash"}`,
	`{"type":"message","message":{"role":"assistant","model":"agnes-3.0-flash","provider":"agnes","stopReason":"toolUse","usage":{"totalTokens":1000,"input":400,"output":100,"cacheRead":500,"cacheWrite":0,"cost":{"total":0.01}},"content":[{"type":"thinking","thinking":"..."},{"type":"tool_use","name":"read","input":{"path":"main/x.c"}}]}}`,
	`{"type":"message","message":{"role":"toolResult","toolName":"read","isError":false,"content":[]}}`,
	`{"type":"message","message":{"role":"assistant","model":"agnes-3.0-flash","provider":"agnes","stopReason":"end","usage":{"totalTokens":500,"input":100,"output":50,"cacheRead":300,"cacheWrite":50,"cost":{"total":0.02}},"content":[{"type":"tool_use","name":"bash","input":{"command":"go build"}}]}}`,
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
	if e.InTokens != 500 {
		t.Fatalf("inTokens = %d, want 500", e.InTokens)
	}
	if e.OutTokens != 150 {
		t.Fatalf("outTokens = %d, want 150", e.OutTokens)
	}
	if e.CacheTokens != 850 {
		t.Fatalf("cacheTokens = %d, want 850", e.CacheTokens)
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

// The published idle flag is the only state source now that herdr is gone: no
// publisher means pi is not running.
func TestSessionStateState(t *testing.T) {
	if got := (*SessionState)(nil).State(); got != "offline" {
		t.Fatalf("nil session state = %q, want offline", got)
	}
	if got := (&SessionState{Idle: true}).State(); got != "idle" {
		t.Fatalf("idle session state = %q, want idle", got)
	}
	if got := (&SessionState{Idle: false}).State(); got != "running" {
		t.Fatalf("working session state = %q, want running", got)
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

// The device's token readout is the cumulative session total, so the heartbeat
// must aggregate the whole file (line 0 = all), not just a recent tail.
func TestAggregateFileWindowCumulativeAndCached(t *testing.T) {
	d := t.TempDir()
	p := filepath.Join(d, "s.jsonl")
	first := `{"type":"message","timestamp":"2026-10-01T10:00:00Z","message":{"role":"assistant","usage":{"totalTokens":100}}}`
	second := `{"type":"message","timestamp":"2026-10-01T11:00:00Z","message":{"role":"assistant","usage":{"totalTokens":250}}}`
	write := func(lines ...string) {
		t.Helper()
		if err := os.WriteFile(p, []byte(strings.Join(lines, "\n")+"\n"), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	write(first)
	if got := aggregateFileWindow(p, time.Time{}, time.Time{}); got.Tokens != 100 {
		t.Fatalf("tokens = %d, want 100", got.Tokens)
	}
	// An unchanged file is served from the cache, not re-parsed.
	if got := aggregateFileWindow(p, time.Time{}, time.Time{}); got.Tokens != 100 {
		t.Fatalf("cached tokens = %d, want 100", got.Tokens)
	}
	// Growing the file invalidates the cache and totals every record.
	write(first, second)
	if got := aggregateFileWindow(p, time.Time{}, time.Time{}); got.Tokens != 350 {
		t.Fatalf("cumulative tokens = %d, want 350", got.Tokens)
	}
}

// The daily readout must count all of today's session records and drop the rest.
func TestAggregateWindowFiltersByRecordTimestamp(t *testing.T) {
	start := time.Date(2026, 10, 1, 0, 0, 0, 0, time.UTC)
	recs := []map[string]any{
		{"type": "message", "timestamp": "2026-09-30T23:00:00Z",
			"message": map[string]any{"role": "assistant", "usage": map[string]any{"totalTokens": 100}}},
		{"type": "message", "timestamp": "2026-10-01T12:00:00Z",
			"message": map[string]any{"role": "assistant", "usage": map[string]any{"totalTokens": 250}}},
	}
	if got := aggregate(recs, time.Time{}, time.Time{}); got.Tokens != 350 {
		t.Fatalf("no-window tokens = %d, want 350", got.Tokens)
	}
	if got := aggregate(recs, start, start.AddDate(0, 0, 1)); got.Tokens != 250 {
		t.Fatalf("window tokens = %d, want 250 (yesterday excluded)", got.Tokens)
	}
}

// The 7- and 30-day windows end at the next local midnight and include today.
func TestMidnightAfterIsNextLocalMidnight(t *testing.T) {
	loc := time.FixedZone("UTC+8", 8*3600)
	now := time.Date(2026, 10, 1, 23, 30, 0, 0, loc)
	until := midnightAfter(now)
	if want := time.Date(2026, 10, 2, 0, 0, 0, 0, loc); !until.Equal(want) {
		t.Fatalf("midnightAfter = %v, want %v", until, want)
	}
	// [until-7d, until) is today plus the previous six days.
	if since := until.AddDate(0, 0, -7); !since.Equal(time.Date(2026, 9, 25, 0, 0, 0, 0, loc)) {
		t.Fatalf("7d window start = %v, want 2026-09-25T00:00+08:00", since)
	}
}

// The device parses cmd/epoch/tz, so the shape has to match exactly or the clock
// silently never appears.
func TestMakeTimeSyncShape(t *testing.T) {
	loc := time.FixedZone("UTC+8", 8*3600)
	now := time.Date(2026, 10, 1, 21, 0, 0, 0, loc)

	var got map[string]any
	if err := json.Unmarshal(makeTimeSync(now), &got); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if got["cmd"] != "time" {
		t.Fatalf("cmd = %v, want time", got["cmd"])
	}
	if want := float64(now.Unix()); got["epoch"] != want {
		t.Fatalf("epoch = %v, want %v", got["epoch"], want)
	}
	if want := float64(8 * 3600); got["tz"] != want {
		t.Fatalf("tz = %v, want %v", got["tz"], want)
	}
}
