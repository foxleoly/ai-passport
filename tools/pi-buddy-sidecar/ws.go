// ws.go — WebSocket server (the untethered twin of the USB data channel).
//
// The Pi Agent Buddy device joins the user's Wi-Fi, discovers this sidecar via
// mDNS (_pibuddy._tcp), and opens a WebSocket. We push heartbeat JSON to it
// every 2s and apply the act JSON it sends on button press (device->Mac),
// driving herdr. The Mac side advertises the mDNS service via macOS's built-in
// `dns-sd -R` (zero extra dependency); the device side uses the ESP32 mDNS
// querier.
package main

import (
	"fmt"
	"hash/fnv"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"time"

	"github.com/gorilla/websocket"
)

// mDNS service the device browses for. The registration type needs the
// transport's own leading underscore ("_pibuddy._tcp"): spelling it "_pibuddy.tcp"
// makes DNSServiceRegister fail with kDNSServiceErr_BadParam (-65540), dns-sd
// exits 255, and the device never discovers us.
const (
	wsMdnsService = "_pibuddy"
	wsMdnsRegType = wsMdnsService + "._tcp"
)

// mdnsInstance names this registration. 'dns-sd' instance names are unique per
// link, so a fixed name makes a second sidecar compete for it and lets the device
// bind to whichever one answers. Port plus a hostname hash stays stable across
// restarts while being distinct per machine; the device browses by service type,
// so the instance name is free to vary.
func mdnsInstance(port string) string {
	sum := fnv.New32a()
	_, _ = sum.Write([]byte(hostname()))
	return fmt.Sprintf("pibuddy-%s-%04x", port, sum.Sum32()&0xffff)
}

func hostname() string {
	name, err := os.Hostname()
	if err != nil || name == "" {
		return "unknown"
	}
	return name
}

// runWS serves the WebSocket tether on listen (e.g. ":51820") and advertises
// itself over mDNS so the device can find us without a hard-coded IP.
func runWS(listen, target, approveText, denyText, herdrBin, tokenFlag string) {
	openBleLog()

	// Without a link code the endpoint is reachable by every host on the LAN, and
	// connecting to it runs herdr input, so a code is always required.
	tokenPath, err := defaultTokenPath()
	if err != nil {
		bleLogf("WS: cannot locate the link code file: %v\n", err)
		return
	}
	token, err := loadOrCreateToken(tokenPath, tokenFlag)
	if err != nil {
		bleLogf("WS: %v\n", err)
		return
	}
	bleLogf("WS: link code %s -- enter it in the device setup page (see it again with --show-token)\n",
		token)

	_, portStr, err := net.SplitHostPort(listen)
	if err != nil {
		bleLogf("WS: bad listen addr %q: %v\n", listen, err)
		return
	}

	// Advertise mDNS _pibuddy._tcp.local (instance "pibuddy") on this port.
	// macOS's dns-sd has no daemon mode and is not supervised; if it goes away
	// the advertisement goes with it and the device silently loses discovery.
	// Keep re-registering for as long as we serve.
	go superviseMDNS(portStr)

	upgrader := websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
		// The device is a raw WebSocket client and sends no Origin header, so
		// allow exactly that case. Rejecting Origin-bearing requests stops a web
		// page the user visits (or a DNS-rebinding probe) from driving herdr
		// through this endpoint.
		CheckOrigin: func(r *http.Request) bool {
			return r.Header.Get("Origin") == ""
		},
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// The device dials "/<link code>"; anything else is refused before the
		// upgrade, so an unauthenticated peer never reaches the act handler.
		if r.URL.Path != "/"+token {
			bleLogf("WS: refused %s: wrong link code\n", r.RemoteAddr)
			http.Error(w, "link code required", http.StatusUnauthorized)
			return
		}
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			bleLogf("WS: upgrade: %v\n", err)
			return
		}
		bleLogf("WS: device connected %s\n", r.RemoteAddr)
		noteLinkOpened(r.RemoteAddr)
		serveWS(conn, target, approveText, denyText, herdrBin)
	})

	srv := &http.Server{Addr: listen, Handler: mux}
	// Clear any state a previous run left behind before serving.
	publishLinkStatus(false, "")
	bleLogf("WS: serving on %s\n", listen)
	if err := srv.ListenAndServe(); err != nil {
		bleLogf("WS: server stopped: %v\n", err)
	}
}

// superviseMDNS keeps a `dns-sd -R` registration alive for our port. The device
// finds us only while the service is registered, so a single unsupervised exec
// leaves the link silently broken as soon as dns-sd stops.
func superviseMDNS(port string) {
	const restartDelay = 2 * time.Second
	instance := mdnsInstance(port)
	for {
		cmd := exec.Command("dns-sd", "-R", instance, wsMdnsRegType, "local", port)
		if err := cmd.Start(); err != nil {
			bleLogf("WS: mDNS advertise failed: %v (device must reach us some other way)\n", err)
			time.Sleep(restartDelay)
			continue
		}
		bleLogf("WS: advertising mDNS %s.local (instance %s) on port %s (pid %d)\n",
			wsMdnsRegType, instance, port, cmd.Process.Pid)
		werr := cmd.Wait()
		bleLogf("WS: mDNS registration exited: %v; re-registering in %s\n", werr, restartDelay)
		time.Sleep(restartDelay)
	}
}

// serveWS pushes heartbeats and reads acts for one device connection.
func serveWS(conn *websocket.Conn, target, approveText, denyText, herdrBin string) {
	defer conn.Close()
	defer noteLinkClosed()

	// Reader: act JSON frames from the device (device->Mac).
	go func() {
		for {
			_, raw, err := conn.ReadMessage()
			if err != nil {
				bleLogf("WS: read: %v\n", err)
				return
			}
			handleDeviceLine(string(raw), target, approveText, denyText, herdrBin)
		}
	}()

	// Writer: heartbeat JSON to the device (Mac->device) every 2s.
	tick := time.NewTicker(2 * time.Second)
	defer tick.Stop()
	for range tick.C {
		hb := makeHeartbeat(herdrBin, 50)
		if err := conn.WriteMessage(websocket.TextMessage, hb); err != nil {
			bleLogf("WS: heartbeat write: %v\n", err)
			return
		}
	}
}

// portOf is a small helper (kept for clarity if listen is ever host:port).
var _ = strconv.Itoa
var _ = fmt.Sprintf
