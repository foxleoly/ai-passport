// Tethered USB data channel: the Mac talks to the Pi Agent Buddy over its
// USB-Serial-JTAG port (the same wire the console/flash port uses). The
// firmware's pibud_usbc reads heartbeat JSON lines we write and writes act
// JSON lines we read on button press. This is the cable-only path, used when
// Wi-Fi is unavailable or being debugged.
package main

import (
	"bufio"
	"encoding/json"
	"os"
	"strings"
	"time"
)

// makeHeartbeat builds one heartbeat JSON from the published sessions and the
// day's pi sessions. The state is aggregated across sessions; the token/cost
// readouts are today's totals across every session.
func makeHeartbeat(piPid int, herdrBin string) []byte {
	sessions := loadSessions(piPid)
	ev := aggregateFileWindow(sessionPathOf(sessions.Primary), time.Time{}, time.Time{})
	applyDailyUsage(&ev)
	hb := BuildHeartbeat(sessions, ev, readSubagents(herdrBin))
	return []byte(MustJSON(hb))
}

// runUSB drives the tethered loop: push heartbeats to the device port and
// apply device acts read back from it. Runs until interrupted.
func runUSB(usbPath, approveText, denyText string, piPid int, herdrBin string) {
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
					handleDeviceLine(line, approveText, denyText)
				}
				time.Sleep(500 * time.Millisecond)
				continue
			}
			handleDeviceLine(line, approveText, denyText)
		}
	}()

	// Writer: heartbeat JSON to the device (Mac->device) every 2s.
	tick := time.NewTicker(2 * time.Second)
	defer tick.Stop()
	for range tick.C {
		hb := makeHeartbeat(piPid, herdrBin)
		if _, werr := f.Write(append(hb, '\n')); werr != nil {
			bleLogf("USB: heartbeat write: %v\n", werr)
		}
		// The device has no RTC; refresh its clock with every heartbeat.
		if _, werr := f.Write(append(makeTimeSync(time.Now()), '\n')); werr != nil {
			bleLogf("USB: time write: %v\n", werr)
		}
	}
}

// handleDeviceLine parses a line the device sent; only {"cmd":"act"} acts are
// acted on. Echoed heartbeats and console log lines are ignored.
func handleDeviceLine(raw, approveText, denyText string) {
	line := strings.TrimSpace(raw)
	if line == "" || !strings.HasPrefix(line, "{") {
		return
	}
	var a deviceAct
	if json.Unmarshal([]byte(line), &a) != nil || a.Cmd != "act" {
		return
	}
	handleAct(a, approveText, denyText)
}
