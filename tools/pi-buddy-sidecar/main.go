// pi-buddy sidecar — reads the live pi session and drives herdr.
//
// Reads the live pi session (JSONL) + herdr agent snapshot, and emits one
// heartbeat JSON to stdout. This is the observation side of the product:
// JSONL events + herdr agent_status -> payload.
//
// Usage:
//
//	pi-buddy-sidecar            # auto: herdr focus -> JSONL
//	pi-buddy-sidecar -session X # explicit JSONL path
package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// bleLogW mirrors --ble logs to a file so app-bundle launches are observable.
var bleLogW io.Writer = os.Stderr

func openBleLog() {
	home, err := os.UserHomeDir()
	if err != nil {
		return
	}
	dir := filepath.Join(home, ".pi-buddy")
	if os.MkdirAll(dir, 0o755) != nil {
		return
	}
	if f, err := os.OpenFile(filepath.Join(dir, "ble.log"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644); err == nil {
		bleLogW = io.MultiWriter(os.Stderr, f)
	}
}

func bleLogf(format string, a ...any) { fmt.Fprintf(bleLogW, format, a...) }

func main() {
	var (
		sessionPath string
		piPid       int
		herdrBin    string
		act         string
		approveText string
		denyText    string
		execAct     bool
		usbMode     bool
		usbPath     string
		wsMode      bool
		wsAddr      string
		token       string
		showToken   bool
	)
	flag.StringVar(&sessionPath, "session", "", "pi session JSONL path (default: the published session, then $PI_SESSION_FILE)")
	flag.IntVar(&piPid, "pi-pid", 0, "follow this pi process (default: the most recently updated one)")
	flag.StringVar(&herdrBin, "herdr", "herdr", "herdr binary for sub-agent counts; optional, empty disables")
	flag.StringVar(&act, "act", "", "perform a device act now: approve|deny|interrupt (dry-run unless --exec)")
	flag.StringVar(&approveText, "approve", "", "text sent on approve (default yes)")
	flag.StringVar(&denyText, "deny", "", "text sent on deny (default no)")
	flag.BoolVar(&execAct, "exec", false, "actually run the act (default: print only)")
	flag.BoolVar(&usbMode, "usb", false, "run the tethered USB data loop: push heartbeats to /dev/cu.*, apply device acts")
	flag.StringVar(&usbPath, "usb-path", "/dev/cu.usbmodem1101", "USB-Serial-JTAG device for --usb")
	flag.BoolVar(&wsMode, "ws", false, "run the untethered WebSocket server: advertise mDNS _pibuddy._tcp, push heartbeats, apply device acts")
	flag.StringVar(&wsAddr, "ws-addr", ":51820", "listen address for --ws")
	flag.StringVar(&token, "token", "", "link code for --ws (default: ~/.pi-buddy/token, created on first use)")
	flag.BoolVar(&showToken, "show-token", false, "print the link code and exit")
	flag.Parse()

	// herdr is optional: it is how pi spawns sub-agents, so it enriches the
	// sub-agent count, but nothing else depends on it. Checked once here so a
	// missing herdr does not spawn a failing process on every heartbeat.
	if !herdrAvailable(herdrBin) {
		bleLogf("herdr not found (%q); sub-agent counts will read as unknown\n", herdrBin)
		herdrBin = ""
	}

	// Printing on demand beats reading it out of a scrolling log, and it is the
	// only workable way to read the code when the sidecar runs without a terminal.
	if showToken {
		path, err := defaultTokenPath()
		if err != nil {
			fmt.Fprintln(os.Stderr, "error:", err)
			os.Exit(1)
		}
		code, err := loadOrCreateToken(path, token)
		if err != nil {
			fmt.Fprintln(os.Stderr, "error:", err)
			os.Exit(1)
		}
		fmt.Println(code) // bare, so it can be copied or piped
		return
	}

	if wsMode {
		runWS(wsAddr, approveText, denyText, token, piPid, herdrBin)
		return
	}

	if usbMode {
		runUSB(usbPath, approveText, denyText, piPid, herdrBin)
		return
	}

	sessions := loadSessions(piPid)

	if act != "" {
		fmt.Fprintf(os.Stderr, "act: %s\n", PreviewAct(act, approveText, denyText))
		if execAct {
			out, err := RunAct(act, approveText, denyText)
			if err != nil {
				fmt.Fprintln(os.Stderr, "act failed:", err, out)
				os.Exit(1)
			}
			fmt.Fprintln(os.Stderr, "act ok:", out)
		}
		return
	}

	path := sessionPath
	if path == "" && sessions.Primary != nil {
		path = sessions.Primary.SessionFile
	}
	if path == "" {
		path = os.Getenv("PI_SESSION_FILE")
	}

	ev := aggregateFileWindow(path, time.Time{}, time.Time{})
	applyDailyUsage(&ev)
	hb := BuildHeartbeat(sessions, ev, readSubagents(herdrBin))

	if sessions.Primary != nil {
		fmt.Fprintf(os.Stderr, "session: pi pid %d, state %s, %d published, cwd=%s jsonl=%s\n",
			sessions.Primary.PID, sessions.State(), len(sessions.All),
			sessions.Primary.Cwd, sessions.Primary.SessionFile)
	} else {
		fmt.Fprintln(os.Stderr, "warn: no pi session published; is the pi-buddy extension installed?")
	}
	if path == "" {
		fmt.Fprintln(os.Stderr, "warn: no session JSONL found; emitting state-only heartbeat")
	}
	fmt.Println(MustJSON(hb))
}

// tailRecords reads the last n JSONL lines and decodes each top-level object.
// n <= 0 means every line, which is what the cumulative token total needs.
func tailRecords(path string, n int) []map[string]any {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()

	var all []string
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1024*1024), 1024*1024)
	for sc.Scan() {
		if ln := strings.TrimRight(sc.Text(), "\r"); ln != "" {
			all = append(all, ln)
		}
	}
	if err := sc.Err(); err != nil {
		// Best-effort tail: return what we read so far rather than fail the heartbeat.
		_ = err
	}
	if n > 0 && len(all) > n {
		all = all[len(all)-n:]
	}
	out := make([]map[string]any, 0, len(all))
	for _, ln := range all {
		var m map[string]any
		if json.Unmarshal([]byte(ln), &m) == nil {
			out = append(out, m)
		}
	}
	return out
}

// sessionAggCache memoizes each session file's aggregate. The heartbeat runs every
// 2s, but a file only grows when pi finishes a turn, and a session can reach
// several MB; size+mtime tells us when re-parsing is actually needed.
type sessionAggEntry struct {
	size    int64
	modTime time.Time
	since   time.Time
	until   time.Time
	events  Events
}

var (
	sessionAggMu    sync.Mutex
	sessionAggCache = map[string]sessionAggEntry{}
)

// aggregateFileWindow returns the aggregate of path restricted to [since, until);
// zero times cover the whole file.
func aggregateFileWindow(path string, since, until time.Time) Events {
	if path == "" {
		return Events{}
	}
	info, err := os.Stat(path)
	if err != nil {
		return Events{}
	}
	// A file not written since the window opened cannot hold any of its records.
	if !since.IsZero() && info.ModTime().Before(since) {
		return Events{}
	}
	sessionAggMu.Lock()
	defer sessionAggMu.Unlock()
	if e, ok := sessionAggCache[path]; ok &&
		e.size == info.Size() && e.modTime.Equal(info.ModTime()) &&
		e.since.Equal(since) && e.until.Equal(until) {
		return e.events
	}
	// ponytail: one full re-parse per turn; switch to incremental offset reads if
	// sessions grow enough for this to matter.
	ev := aggregate(tailRecords(path, 0), since, until)
	sessionAggCache[path] = sessionAggEntry{
		size: info.Size(), modTime: info.ModTime(),
		since: since, until: until, events: ev,
	}
	return ev
}

// windowTotals sums the usage of every session record in the last `days` local
// days (today inclusive), across all projects.
// ponytail: one pass per window; each file's aggregate is cached per window, so
// only the first call after a file changes pays for the parse.
func windowTotals(now time.Time, days int) Events {
	until := midnightAfter(now)
	since := until.AddDate(0, 0, -days)
	var total Events
	for _, path := range sessionFiles() {
		ev := aggregateFileWindow(path, since, until)
		total.Tokens += ev.Tokens
		total.InTokens += ev.InTokens
		total.OutTokens += ev.OutTokens
		total.CacheTokens += ev.CacheTokens
		total.Cost += ev.Cost
		total.Records += ev.Records
	}
	return total
}

// applyDailyUsage fills in the running totals: today's buckets (which the STATS
// page breaks down) plus the 7- and 30-day token totals, leaving the focused
// session's state untouched.
func applyDailyUsage(ev *Events) {
	now := time.Now()
	daily := windowTotals(now, 1)
	ev.Tokens, ev.InTokens, ev.OutTokens, ev.CacheTokens, ev.Cost =
		daily.Tokens, daily.InTokens, daily.OutTokens, daily.CacheTokens, daily.Cost
	ev.Tokens7d = windowTotals(now, 7).Tokens
	ev.Tokens30d = windowTotals(now, 30).Tokens
}

// midnightAfter returns the next local midnight (the exclusive end of today).
func midnightAfter(now time.Time) time.Time {
	y, m, d := now.Date()
	return time.Date(y, m, d, 0, 0, 0, 0, now.Location()).AddDate(0, 0, 1)
}

// makeTimeSync is the {"cmd":"time"} line the device needs. It has no RTC, so the
// clock is refreshed with every heartbeat rather than set once.
func makeTimeSync(now time.Time) []byte {
	_, offset := now.Zone()
	return []byte(MustJSON(map[string]any{
		"cmd":   "time",
		"epoch": now.Unix(),
		"tz":    offset,
	}))
}

// sessionFiles lists every pi session JSONL under ~/.pi/agent/sessions.
func sessionFiles() []string {
	home, err := os.UserHomeDir()
	if err != nil {
		return nil
	}
	paths, _ := filepath.Glob(filepath.Join(home, ".pi", "agent", "sessions", "*", "*.jsonl"))
	return paths
}

// deviceAct is the device -> sidecar act JSON.
type deviceAct struct {
	Cmd  string `json:"cmd"`
	Kind string `json:"kind"`
	ID   string `json:"id"`
	Tool string `json:"tool"`
}

func handleAct(a deviceAct, approveText, denyText string) {
	kind := a.Kind
	bleLogf("device act: %s -> %s\n", PreviewAct(kind, approveText, denyText), kind)
	if out, err := RunAct(kind, approveText, denyText); err != nil {
		bleLogf("act failed: %s %s\n", err, out)
	} else {
		bleLogf("act ok: %s\n", out)
	}
}

// sessionPathOf resolves the JSONL path from the published session (or
// $PI_SESSION_FILE).
func sessionPathOf(session *SessionState) string {
	if session != nil && session.SessionFile != "" {
		return session.SessionFile
	}
	return os.Getenv("PI_SESSION_FILE")
}
