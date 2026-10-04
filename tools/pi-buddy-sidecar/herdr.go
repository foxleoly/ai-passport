// herdr.go — optional. herdr is how pi spawns sub-agents (each one lives in a
// herdr pane), so when it is installed the sidecar can report how many are
// running. Nothing else depends on it: without herdr the device shows the count
// as unknown, and every other feature keeps working.
package main

import (
	"encoding/json"
	"os/exec"
)

// AgentState is one entry in herdr's snapshot.agents.
type AgentState struct {
	AgentStatus string `json:"agent_status"`
}

// HerdrSnapshot mirrors the `herdr api snapshot` result.
type HerdrSnapshot struct {
	Result struct {
		Snapshot struct {
			Agents []AgentState `json:"agents"`
		} `json:"snapshot"`
	} `json:"result"`
}

// AgentSummary counts herdr agents by status.
func (s HerdrSnapshot) AgentSummary() (total, working int) {
	for _, a := range s.Result.Snapshot.Agents {
		total++
		if a.AgentStatus == "working" {
			working++
		}
	}
	return total, working
}

// SubagentCount is what the heartbeat reports about sub-agents. Available is
// false whenever herdr is absent or unhelpful, which the device renders as
// unknown rather than as zero.
type SubagentCount struct {
	Total     int
	Working   int
	Available bool
}

// herdrAvailable reports whether bin can be run at all. Checked once at startup
// so a missing herdr does not spawn a failing process on every heartbeat.
func herdrAvailable(bin string) bool {
	if bin == "" {
		return false
	}
	_, err := exec.LookPath(bin)
	return err == nil
}

// readSubagents asks herdr for its agent snapshot. A missing or failing herdr is
// not an error: the count is optional.
func readSubagents(bin string) SubagentCount {
	if bin == "" {
		return SubagentCount{}
	}
	out, err := exec.Command(bin, "api", "snapshot").Output()
	if err != nil {
		return SubagentCount{}
	}
	var s HerdrSnapshot
	if json.Unmarshal(out, &s) != nil {
		return SubagentCount{}
	}
	total, working := s.AgentSummary()
	return SubagentCount{Total: total, Working: working, Available: true}
}
