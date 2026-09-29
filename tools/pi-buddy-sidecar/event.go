package main

// Pure, host-testable logic for the pi-buddy sidecar: parse herdr snapshot
// JSON and aggregate pi session JSONL records into a heartbeat. No I/O here.

import (
	"encoding/json"
	"fmt"
)

// AgentState is one entry in herdr's snapshot.agents.
type AgentState struct {
	Agent        string `json:"agent"`
	AgentStatus  string `json:"agent_status"`
	Cwd          string `json:"cwd"`
	TerminalTitle string `json:"terminal_title"`
	Focused      bool   `json:"focused"`
	PaneID       string `json:"pane_id"`
	AgentSession struct {
		Value string `json:"value"`
	} `json:"agent_session"`
}

// HerdrSnapshot mirrors the `herdr api snapshot` result.
type HerdrSnapshot struct {
	ID string `json:"id"`
	Result struct {
		Snapshot struct {
			Agents          []AgentState `json:"agents"`
			FocusedPaneID   string       `json:"focused_pane_id"`
			FocusedWorkspaceID string    `json:"focused_workspace_id"`
		} `json:"snapshot"`
	} `json:"result"`
}

// Focus returns the agent matching the focused pane (or the first working one).
func (s HerdrSnapshot) Focus() *AgentState {
	snap := s.Result.Snapshot
	for i := range snap.Agents {
		if snap.Agents[i].Focused || snap.Agents[i].PaneID == snap.FocusedPaneID {
			return &snap.Agents[i]
		}
	}
	for i := range snap.Agents {
		if snap.Agents[i].AgentStatus == "working" {
			return &snap.Agents[i]
		}
	}
	if len(snap.Agents) > 0 {
		return &snap.Agents[0]
	}
	return nil
}

// AgentSummary counts herdr agents by status (for the sub-agent line).
func (s HerdrSnapshot) AgentSummary() (total, working int) {
	for _, a := range s.Result.Snapshot.Agents {
		total++
		if a.AgentStatus == "working" {
			working++
		}
	}
	return total, working
}

// NormalizeAgentState maps herdr's agent_status to the buddy state vocabulary.
func NormalizeAgentState(status string) string {
	switch status {
	case "working":
		return "running"
	case "idle":
		return "idle"
	case "unknown", "":
		return "offline"
	default:
		return status
	}
}

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
	Cost         float64
	Records      int
}

// Aggregate folds parsed JSONL records (top-level objects) into Events.
// rec is a decoded top-level record: {type, message?, modelId?, ...}.
func Aggregate(recs []map[string]any) Events {
	var e Events
	for _, r := range recs {
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
				continue
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
	return e
}

func (e *Events) foldUsage(m map[string]any) {
	u, _ := m["usage"].(map[string]any)
	if u == nil {
		return
	}
	if n, ok := toUint(u["totalTokens"]); ok {
		e.Tokens += n
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
	Cmd        string `json:"cmd"`
	Model      string `json:"model"`
	Provider   string `json:"provider,omitempty"`
	State      string `json:"state"`
	Cwd        string `json:"cwd,omitempty"`
	Title      string `json:"title,omitempty"`
	Tool       string `json:"tool,omitempty"`
	Arg        string `json:"arg,omitempty"`
	ResultOK   *bool  `json:"result_ok,omitempty"`
	Thinking   bool   `json:"thinking,omitempty"`
	SubTotal   int    `json:"sub_total,omitempty"`
	SubWorking int    `json:"sub_working,omitempty"`
	Tokens     uint64 `json:"tokens"`
	Cost       float64 `json:"cost"`
	StopReason string `json:"stop_reason,omitempty"`
}

// BuildHeartbeat composes a heartbeat from herdr focus + JSONL events.
func BuildHeartbeat(focus *AgentState, ev Events, subTotal, subWorking int) Heartbeat {
	state := "offline"
	cwd, title, jsonl := "", "", ""
	if focus != nil {
		state = NormalizeAgentState(focus.AgentStatus)
		cwd = focus.Cwd
		title = focus.TerminalTitle
		jsonl = focus.AgentSession.Value
		_ = jsonl
	}
	hb := Heartbeat{
		Cmd:        "hb",
		Model:      ev.Model,
		Provider:   ev.Provider,
		State:      state,
		Cwd:        cwd,
		Title:      title,
		Tool:       ev.LastTool,
		Arg:        ev.LastArg,
		Thinking:   ev.Thinking,
		SubTotal:   subTotal,
		SubWorking: subWorking,
		Tokens:     ev.Tokens,
		Cost:       ev.Cost,
		StopReason: ev.StopReason,
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
