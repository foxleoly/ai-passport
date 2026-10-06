package main

// Pure, host-testable logic for the pi-buddy sidecar: aggregate pi session
// JSONL records into a heartbeat. No I/O here.

import (
	"encoding/json"
	"fmt"
	"time"
)

// Events is the aggregate of the pi session JSONL records we have read.
// Events is the aggregation of a pi session JSONL for one heartbeat.
type Events struct {
	Model        string
	Provider     string
	StopReason   string
	LastTool     string
	LastArg      string
	LastResultOK *bool
	Thinking     bool
	Tokens       uint64
	InTokens     uint64
	OutTokens    uint64
	CacheTokens  uint64
	Tokens7d     uint64
	Tokens30d    uint64
	Cost         float64
	Records      int
}

// Aggregate folds parsed JSONL records (top-level objects) into Events.
// rec is a decoded top-level record: {type, message?, modelId?, ...}.
func Aggregate(recs []map[string]any) Events {
	return aggregate(recs, time.Time{}, time.Time{})
}

// aggregate folds records into Events. A non-zero since restricts it to records
// whose own timestamp falls in [since, until), so a session that spans midnight
// contributes only the part that belongs to the window.
func aggregate(recs []map[string]any, since, until time.Time) Events {
	var e Events
	for _, r := range recs {
		if !since.IsZero() {
			ts := recordTime(r)
			if ts.IsZero() || ts.Before(since) || (!until.IsZero() && !ts.Before(until)) {
				continue
			}
		}
		e.foldRecord(r)
	}
	return e
}

// recordTime parses a record's ISO-8601 timestamp (zero when absent or invalid).
func recordTime(r map[string]any) time.Time {
	s, _ := r["timestamp"].(string)
	if s == "" {
		return time.Time{}
	}
	t, err := time.Parse(time.RFC3339Nano, s)
	if err != nil {
		return time.Time{}
	}
	return t
}

func (e *Events) foldRecord(r map[string]any) {
	e.Records++
	switch r["type"].(string) {
	case "model_change":
		if mid, ok := r["modelId"].(string); ok {
			e.Model = mid
		}
		if p, ok := r["provider"].(string); ok {
			e.Provider = p
		}
	case "message":
		m, _ := r["message"].(map[string]any)
		if m == nil {
			return
		}
		role, _ := m["role"].(string)
		switch role {
		case "assistant":
			if s, ok := m["model"].(string); ok {
				e.Model = s
			}
			if s, ok := m["provider"].(string); ok {
				e.Provider = s
			}
			if s, ok := m["stopReason"].(string); ok {
				e.StopReason = s
			}
			e.foldUsage(m)
			e.foldBlocks(m)
		case "toolResult":
			eok := false
			if err, ok := m["isError"].(bool); ok {
				eok = !err
			}
			if nm, ok := m["toolName"].(string); ok && nm != "" {
				e.LastTool = nm
			}
			e.LastResultOK = &eok
		}
	}
}

func (e *Events) foldUsage(m map[string]any) {
	u, _ := m["usage"].(map[string]any)
	if u == nil {
		return
	}
	if n, ok := toUint(u["totalTokens"]); ok {
		e.Tokens += n
	}
	if n, ok := toUint(u["input"]); ok {
		e.InTokens += n
	}
	if n, ok := toUint(u["output"]); ok {
		e.OutTokens += n
	}
	// The device shows a single "cache" bucket, so read and write fold together.
	if n, ok := toUint(u["cacheRead"]); ok {
		e.CacheTokens += n
	}
	if n, ok := toUint(u["cacheWrite"]); ok {
		e.CacheTokens += n
	}
	c, _ := u["cost"].(map[string]any)
	if c != nil {
		if f, ok := toFloat(c["total"]); ok {
			e.Cost += f
		}
	}
}

// foldBlocks inspects assistant content blocks for tool_use / thinking.
func (e *Events) foldBlocks(m map[string]any) {
	bl, _ := m["content"].([]any)
	for _, item := range bl {
		b, _ := item.(map[string]any)
		if b == nil {
			continue
		}
		switch b["type"].(string) {
		case "tool_use":
			if nm, ok := b["name"].(string); ok {
				e.LastTool = nm
				e.LastArg = firstArg(b["input"])
			}
		case "thinking":
			if _, ok := b["thinking"].(string); ok {
				e.Thinking = true
			}
		}
	}
}

// firstArg picks a short display string from a tool input map.
func firstArg(input any) string {
	im, _ := input.(map[string]any)
	if im == nil {
		if s, ok := input.(string); ok {
			return s
		}
		return ""
	}
	// Prefer a path/command/summary field if present.
	for _, k := range []string{"path", "command", "task", "brief", "content"} {
		if v, ok := im[k].(string); ok && v != "" {
			return v
		}
	}
	// Fallback: first string value.
	for _, v := range im {
		if s, ok := v.(string); ok && s != "" {
			return s
		}
	}
	return ""
}

func toUint(v any) (uint64, bool) {
	switch n := v.(type) {
	case float64:
		return uint64(n), true
	case int:
		return uint64(n), true
	}
	return 0, false
}

func toFloat(v any) (float64, bool) {
	switch n := v.(type) {
	case float64:
		return n, true
	case int:
		return float64(n), true
	}
	return 0, false
}

// Heartbeat is the pi-version payload pushed to the device over NUS RX.
type Heartbeat struct {
	Cmd          string  `json:"cmd"`
	Model        string  `json:"model"`
	Provider     string  `json:"provider,omitempty"`
	State        string  `json:"state"`
	Cwd          string  `json:"cwd,omitempty"`
	Title        string  `json:"title,omitempty"`
	Tool         string  `json:"tool,omitempty"`
	Arg          string  `json:"arg,omitempty"`
	ResultOK     *bool   `json:"result_ok,omitempty"`
	Thinking     bool    `json:"thinking,omitempty"`
	SubAvailable bool    `json:"sub_available"`
	SubTotal     int     `json:"sub_total,omitempty"`
	SubWorking   int     `json:"sub_working,omitempty"`
	Tokens       uint64  `json:"tokens"`
	InTokens     uint64  `json:"in_tokens,omitempty"`
	OutTokens    uint64  `json:"out_tokens,omitempty"`
	CacheTokens  uint64  `json:"cache_tokens,omitempty"`
	Tokens7d     uint64  `json:"tokens_7d,omitempty"`
	Tokens30d    uint64  `json:"tokens_30d,omitempty"`
	Cost         float64 `json:"cost"`
	StopReason   string  `json:"stop_reason,omitempty"`
}

// BuildHeartbeat composes a heartbeat from the published sessions, the JSONL
// events, and the sub-agent count — which is only known when herdr is installed.
// The state is aggregated across sessions; cwd and title come from the session the
// sidecar follows.
func BuildHeartbeat(sessions Sessions, ev Events, subs SubagentCount) Heartbeat {
	cwd, title := "", ""
	if sessions.Primary != nil {
		cwd = sessions.Primary.Cwd
		title = sessions.Primary.Title
	}
	hb := Heartbeat{
		Cmd:          "hb",
		Model:        ev.Model,
		Provider:     ev.Provider,
		State:        sessions.State(),
		Cwd:          cwd,
		Title:        title,
		Tool:         ev.LastTool,
		Arg:          ev.LastArg,
		Thinking:     ev.Thinking,
		SubAvailable: subs.Available,
		SubTotal:     subs.Total,
		SubWorking:   subs.Working,
		Tokens:       ev.Tokens,
		InTokens:     ev.InTokens,
		OutTokens:    ev.OutTokens,
		CacheTokens:  ev.CacheTokens,
		Tokens7d:     ev.Tokens7d,
		Tokens30d:    ev.Tokens30d,
		Cost:         ev.Cost,
		StopReason:   ev.StopReason,
	}
	if ev.LastResultOK != nil {
		hb.ResultOK = ev.LastResultOK
	}
	return hb
}

// MustJSON returns compact JSON of v (panics on error, which won't happen for these types).
func MustJSON(v any) string {
	b, err := json.Marshal(v)
	if err != nil {
		panic(fmt.Sprintf("marshal heartbeat: %v", err))
	}
	return string(b)
}
