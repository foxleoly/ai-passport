package main

import (
	"fmt"
	"os/exec"
)

// BuildActArgs returns the `herdr agent ...` sub-arguments for a device act.
// Pure + host-testable (no exec). kind is one of approve/deny/interrupt.
func BuildActArgs(kind, target, approveText, denyText string) []string {
	switch kind {
	case "interrupt":
		// Stop the current turn / subagent: Escape to the agent.
		return []string{"agent", "send-keys", target, "esc"}
	case "approve":
		return []string{"agent", "prompt", target, orDefault(approveText, "yes")}
	case "deny":
		return []string{"agent", "prompt", target, orDefault(denyText, "no")}
	default:
		return nil
	}
}

func orDefault(s, d string) string {
	if s == "" {
		return d
	}
	return s
}

// ExecAct runs the herdr command for a device act and returns combined output.
func ExecAct(kind, target, approveText, denyText, herdrBin string) (string, error) {
	args := BuildActArgs(kind, target, approveText, denyText)
	if args == nil {
		return "", fmt.Errorf("unknown act kind %q", kind)
	}
	if herdrBin == "" {
		herdrBin = "herdr"
	}
	out, err := exec.Command(herdrBin, args...).CombinedOutput()
	if err != nil {
		return string(out), fmt.Errorf("herdr %v: %w (%s)", args, err, string(out))
	}
	return string(out), nil
}

// PreviewAct returns the exact command line without running it (dry-run).
func PreviewAct(kind, target, approveText, denyText, herdrBin string) string {
	args := BuildActArgs(kind, target, approveText, denyText)
	if args == nil {
		return "<unknown act>"
	}
	if herdrBin == "" {
		herdrBin = "herdr"
	}
	return herdrBin + " " + joinQuoted(args)
}

func joinQuoted(args []string) string {
	out := ""
	for i, a := range args {
		if i > 0 {
			out += " "
		}
		if len(a) > 0 && (a[0] == '-' || containsSpace(a)) {
			out += fmt.Sprintf("%q", a)
		} else {
			out += a
		}
	}
	return out
}

func containsSpace(s string) bool {
	for _, c := range s {
		if c == ' ' || c == '\t' {
			return true
		}
	}
	return false
}
