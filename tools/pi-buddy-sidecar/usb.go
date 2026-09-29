// Tethered USB data channel: the Mac talks to the Pi Agent Buddy over its
// USB-Serial-JTAG port (the same wire the console/flash port uses). The
// firmware's pibud_usbc reads heartbeat JSON lines we write and writes act
// JSON lines we read on button press. This is the reliable (no-CoreBluetooth)
// twin of the BLE path, for validating the P3 data/control loop.
package main

import (
	"bufio"
	"encoding/json"
	"os"
	"strings"
	"time"
)

// makeHeartbeat builds one heartbeat JSON (P1 aggregation) from herdr + JSONL.
func makeHeartbeat(herdrBin string, lines int) []byte {
	snap, focus := loadSnapshot(herdrBin)
	agentsTotal, agentsWorking := 0, 0
	if snap != nil {
		agentsTotal, agentsWorking = snap.AgentSummary()
	}
	path := ""
	if focus != nil {
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
	return []byte(MustJSON(hb))
}

// runUSB drives the tethered loop: push heartbeats to the device port and
// apply device acts read back from it. Runs until interrupted.
func runUSB(usbPath, target, approveText, denyText, herdrBin string) {
	openBleLog()
	f, err := os.OpenFile(usbPath, os.O_RDWR, 0)
	if err != nil {
		bleLogf("USB: open %s failed: %v (is the Pi Agent Buddy connected?)\n", usbPath, err)
		return
	}
	defer f.Close()
	bleLogf("USB: tethered data channel on %s\n", usbPath)

	// Reader: act JSON lines from the device (device->Mac).
	br := bufio.NewReader(f)
	go func() {
		for {
			line, rerr := br.ReadString('\n')
			if rerr != nil {
				// Port error / closed: act on the partial line, then back off.
				if strings.TrimSpace(line) != "" {
					handleDeviceLine(line, target, approveText, denyText, herdrBin)
				}
				time.Sleep(500 * time.Millisecond)
				continue
			}
			handleDeviceLine(line, target, approveText, denyText, herdrBin)
		}
	}()

	// Writer: heartbeat JSON to the device (Mac->device) every 2s.
	tick := time.NewTicker(2 * time.Second)
	defer tick.Stop()
	for range tick.C {
		hb := makeHeartbeat(herdrBin, 50)
		if _, werr := f.Write(append(hb, '\n')); werr != nil {
			bleLogf("USB: heartbeat write: %v\n", werr)
		}
	}
}

// handleDeviceLine parses a line the device sent; only {"cmd":"act"} acts are
// acted on. Echoed heartbeats and console log lines are ignored.
func handleDeviceLine(raw, target, approveText, denyText, herdrBin string) {
	line := strings.TrimSpace(raw)
	if line == "" || !strings.HasPrefix(line, "{") {
		return
	}
	var a deviceAct
	if json.Unmarshal([]byte(line), &a) != nil || a.Cmd != "act" {
		return
	}
	handleAct(a, target, approveText, denyText, herdrBin)
}
