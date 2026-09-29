// pi-buddy sidecar — P1 data-flow prototype.
//
// Reads the live pi session (JSONL) + herdr agent snapshot, and emits one
// heartbeat JSON to stdout. No BLE yet (that lands in P2). This de-risks the
// observation side of the product: JSONL events + herdr agent_status -> payload.
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
	"os/exec"
	"path/filepath"
	"strings"
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
		herdrBin    string
		lines       int
		act         string
		target      string
		approveText string
		denyText    string
		execAct     bool
		bleMode     bool
		usbMode     bool
		usbPath     string
	)
	flag.StringVar(&sessionPath, "session", "", "pi session JSONL path (default: herdr focus / $PI_SESSION_FILE)")
	flag.StringVar(&herdrBin, "herdr", "herdr", "herdr binary path")
	flag.IntVar(&lines, "lines", 50, "tail N JSONL lines for the heartbeat")
	flag.StringVar(&act, "act", "", "perform a device act now: approve|deny|interrupt (dry-run unless --exec)")
	flag.StringVar(&target, "target", "", "herdr agent target (pane id, e.g. w4:p9); default = focused agent")
	flag.StringVar(&approveText, "approve", "", "text sent on approve (default yes)")
	flag.StringVar(&denyText, "deny", "", "text sent on deny (default no)")
	flag.BoolVar(&execAct, "exec", false, "actually run the herdr act command (default: print only)")
	flag.BoolVar(&bleMode, "ble", false, "run the BLE central loop: connect Pi-*, push heartbeats, apply device acts")
	flag.BoolVar(&usbMode, "usb", false, "run the tethered USB data loop: push heartbeats to /dev/cu.*, apply device acts")
	flag.StringVar(&usbPath, "usb-path", "/dev/cu.usbmodem1101", "USB-Serial-JTAG device for --usb")
	flag.Parse()

	if usbMode {
		runUSB(usbPath, target, approveText, denyText, herdrBin)
		return
	}

	if bleMode {
		runBLE(target, approveText, denyText, herdrBin)
		return
	}

	snap, focus := loadSnapshot(herdrBin)
	agentsTotal, agentsWorking := 0, 0
	if snap != nil {
		agentsTotal, agentsWorking = snap.AgentSummary()
	}

	if act != "" {
		t := target
		if t == "" && focus != nil {
			t = focus.PaneID
		}
		if t == "" {
			fmt.Fprintln(os.Stderr, "error: no target (set --target or have a focused herdr agent)")
			os.Exit(2)
		}
		fmt.Fprintf(os.Stderr, "act: %s -> %s\n", PreviewAct(act, t, approveText, denyText, herdrBin), t)
		if execAct {
			out, err := ExecAct(act, t, approveText, denyText, herdrBin)
			if err != nil {
				fmt.Fprintln(os.Stderr, "act failed:", err, out)
				os.Exit(1)
			}
			fmt.Fprintln(os.Stderr, "act ok:", out)
		}
		return
	}

	path := sessionPath
	if path == "" && focus != nil {
		path = focus.AgentSession.Value
	}
	if path == "" {
		path = os.Getenv("PI_SESSION_FILE")
	}

	var recs []map[string]any
	if path != "" {
		recs = tailRecords(path, lines)
	}
	ev := Aggregate(recs)
	hb := BuildHeartbeat(focus, ev, agentsTotal, agentsWorking)

	if focus != nil {
		fmt.Fprintf(os.Stderr, "focus: %s (%s) cwd=%s jsonl=%s\n",
			focus.TerminalTitle, focus.AgentStatus, focus.Cwd, focus.AgentSession.Value)
	}
	if path == "" {
		fmt.Fprintln(os.Stderr, "warn: no session JSONL found; emitting state-only heartbeat")
	}
	fmt.Println(MustJSON(hb))
}

func loadSnapshot(bin string) (*HerdrSnapshot, *AgentState) {
	out, err := exec.Command(bin, "api", "snapshot").Output()
	if err != nil {
		return nil, nil
	}
	var s HerdrSnapshot
	if err := json.Unmarshal(out, &s); err != nil {
		return nil, nil
	}
	return &s, s.Focus()
}

// tailRecords reads the last n JSONL lines and decodes each top-level object.
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

// deviceAct is the device -> sidecar act JSON.
type deviceAct struct {
	Cmd  string `json:"cmd"`
	Kind string `json:"kind"`
	ID   string `json:"id"`
	Tool string `json:"tool"`
}

func runBLE(target, approveText, denyText, herdrBin string) {
	openBleLog()
	ble := BleNew()
	bleLogf("BLE: waiting for CoreBluetooth poweredOn (grant Bluetooth permission if prompted)\n")
	for {
		st := ble.State()
		switch st {
		case cbStatePoweredOn:
			goto scanning
		case cbStateUnauthorized:
			bleLogf("BLE: CoreBluetooth unauthorized; grant Bluetooth access to this terminal app and retry\n")
			return
		case cbStateUnsupported:
			bleLogf("BLE: CoreBluetooth unsupported on this host\n")
			return
		}
		ble.Poll(500) // advance event pump while state settles
	}
scanning:
	bleLogf("BLE: scanning for Pi-* (NUS) ...\n")
	ble.Scan()

	hbTick := time.NewTicker(2 * time.Second)
	defer hbTick.Stop()
	var rx []byte

	for {
		select {
		case <-hbTick.C:
			snap, focus := loadSnapshot(herdrBin)
			_, working := 0, 0
			if snap != nil {
				_, working = snap.AgentSummary()
			}
			evt := Aggregate(tailRecords(sessionPathOf(focus), 50))
			hb := BuildHeartbeat(focus, evt, snapOrZeroAgents(snap), working)
			ble.WriteRX([]byte(MustJSON(hb) + "\n"))
		default:
		}

		ev := ble.Poll(300)
		switch ev.Kind {
		case pbEvDiscovered:
			bleLogf("BLE: saw device %q\n", string(ev.Data))
		case pbEvConnected:
			bleLogf("BLE: NUS connected; pushing heartbeats\n")
		case pbEvDisconn:
			bleLogf("BLE: disconnected; re-scanning\n")
			rx = rx[:0]
			ble.Scan()
		case pbEvTxData:
			rx = append(rx, ev.Data...)
			for {
				i := indexByte(rx, byte('\n'))
				if i < 0 {
					break
				}
				line := strings.TrimSpace(string(rx[:i]))
				rx = append([]byte{}, rx[i+1:]...)
				if line == "" {
					continue
				}
				var a deviceAct
				if json.Unmarshal([]byte(line), &a) != nil || a.Cmd != "act" {
					continue
				}
				handleAct(a, target, approveText, denyText, herdrBin)
			}
		}
	}
}

func handleAct(a deviceAct, target, approveText, denyText, herdrBin string) {
	kind := a.Kind
	if target == "" {
		if _, focus := loadSnapshot(herdrBin); focus != nil {
			target = focus.PaneID
		}
	}
	if target == "" {
		bleLogf("act %s ignored: no herdr target\n", kind)
		return
	}
	bleLogf("device act: %s -> %s\n", PreviewAct(kind, target, approveText, denyText, herdrBin), target)
	if out, err := ExecAct(kind, target, approveText, denyText, herdrBin); err != nil {
		bleLogf("act failed: %s %s\n", err, out)
	} else {
		bleLogf("act ok: %s\n", out)
	}
}

func indexByte(b []byte, c byte) int {
	for i := range b {
		if b[i] == c {
			return i
		}
	}
	return -1
}

// sessionPathOf resolves the JSONL path from a focus (or $PI_SESSION_FILE).
func sessionPathOf(focus *AgentState) string {
	if focus != nil {
		return focus.AgentSession.Value
	}
	return os.Getenv("PI_SESSION_FILE")
}

// snapOrZeroAgents returns total agents from a snapshot (0 when nil).
func snapOrZeroAgents(s *HerdrSnapshot) int {
	if s == nil {
		return 0
	}
	total, _ := s.AgentSummary()
	return total
}
