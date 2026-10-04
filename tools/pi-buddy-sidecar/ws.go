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
	"io"
	"net"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/grandcat/zeroconf"
)

// mDNS service the device browses for. The registration type needs the
// transport's own leading underscore ("_pibuddy._tcp"): spelling it "_pibuddy.tcp"
// makes DNSServiceRegister fail with kDNSServiceErr_BadParam (-65540), dns-sd
// exits 255, and the device never discovers us.
const (
	wsMdnsService = "_pibuddy"
	wsMdnsRegType = wsMdnsService + "._tcp"
	wsMdnsDomain  = "local."
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
func runWS(listen, approveText, denyText, tokenFlag string, piPid int, herdrBin string) {
	openBleLog()

	// Without a link code the endpoint is reachable by every host on the LAN, and
	// connecting to it runs the device's acts, so a code is always required.
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
	port, err := strconv.Atoi(portStr)
	if err != nil {
		bleLogf("WS: bad port %q: %v\n", portStr, err)
		return
	}

	// Advertise _pibuddy._tcp.local so the device can find us without a
	// hard-coded IP. This used to shell out to macOS's `dns-sd`, which does not
	// exist on Linux; the library keeps the registration alive on every platform
	// and needs no supervising.
	mdns := advertiseMDNS(port)
	if mdns != nil {
		defer mdns.Shutdown()
	}

	upgrader := websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
		// The device is a raw WebSocket client and sends no Origin header, so
		// allow exactly that case. Rejecting Origin-bearing requests stops a web
		// page the user visits (or a DNS-rebinding probe) from driving the
		// device's acts through this endpoint.
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
		// A reconnecting device opens a new socket while the old one is still
		// open here (nothing notices the peer left until a write fails), so
		// supersede it now instead of letting sessions accumulate.
		if old := wsLive.replace(conn); old != nil {
			bleLogf("WS: superseding the previous device connection\n")
			_ = old.Close()
		}
		serveWS(conn, approveText, denyText, piPid, herdrBin)
	})

	srv := &http.Server{Addr: listen, Handler: mux}
	// Clear any state a previous run left behind before serving.
	publishLinkStatus(false, "")
	bleLogf("WS: serving on %s\n", listen)
	if err := srv.ListenAndServe(); err != nil {
		bleLogf("WS: server stopped: %v\n", err)
	}
}

// advertiseMDNS registers the mDNS service the device browses for. The
// registration is kept alive by the library for as long as the server is not
// shut down, so there is no supervisor loop to keep running.
func advertiseMDNS(port int) *zeroconf.Server {
	instance := mdnsInstance(strconv.Itoa(port))
	server, err := zeroconf.Register(instance, wsMdnsService, wsMdnsDomain, port, []string{"pi-buddy=1"}, nil)
	if err != nil {
		bleLogf("WS: mDNS advertise failed: %v (the device must reach us some other way)\n", err)
		return nil
	}
	bleLogf("WS: advertising mDNS %s.local (instance %s) on port %d\n", wsMdnsRegType, instance, port)
	return server
}

// wsSessions enforces one live device connection. A reconnect (reboot, Wi-Fi
// blip) arrives before this side notices the previous peer is gone — nothing
// here fails until a heartbeat write does — so without this the sidecar holds
// several sessions at once.
type wsSessions struct {
	mu   sync.Mutex
	conn io.Closer
}

// replace installs c as the live connection and returns the connection it
// superseded, if any, for the caller to close.
func (s *wsSessions) replace(c io.Closer) io.Closer {
	s.mu.Lock()
	old := s.conn
	s.conn = c
	s.mu.Unlock()
	return old
}

// release drops c if it is still the live connection, so a superseded socket
// unwinding later cannot clear its replacement.
func (s *wsSessions) release(c io.Closer) {
	s.mu.Lock()
	if s.conn == c {
		s.conn = nil
	}
	s.mu.Unlock()
}

// wsLive is the one live device connection for this sidecar process.
var wsLive wsSessions

// Keepalive: the link is silent between heartbeats and acts, so a peer that
// vanishes without a FIN (Wi-Fi drop, NAT rebind) would keep the session open
// until a write happened to fail. We ping every wsPingEvery and reap the session
// once no pong has pushed the read deadline within wsPongWait.
const (
	wsPongWait  = 30 * time.Second
	wsPingEvery = 10 * time.Second
	wsWriteWait = 10 * time.Second
)

// serveWS pushes heartbeats and reads acts for one device connection.
func serveWS(conn *websocket.Conn, approveText, denyText string, piPid int, herdrBin string) {
	defer conn.Close()
	defer noteLinkClosed()
	defer wsLive.release(conn)

	_ = conn.SetReadDeadline(time.Now().Add(wsPongWait))
	conn.SetPongHandler(func(string) error {
		return conn.SetReadDeadline(time.Now().Add(wsPongWait))
	})

	// Reader: act JSON frames from the device (device->Mac). Gorilla answers the
	// device's own keepalive PINGs and drives the pong handler from in here.
	done := make(chan struct{})
	go func() {
		defer close(done)
		for {
			_, raw, err := conn.ReadMessage()
			if err != nil {
				bleLogf("WS: read: %v\n", err)
				return
			}
			handleDeviceLine(string(raw), approveText, denyText)
		}
	}()

	heartbeat := time.NewTicker(2 * time.Second)
	defer heartbeat.Stop()
	ping := time.NewTicker(wsPingEvery)
	defer ping.Stop()

	for {
		select {
		case <-done: // the reader hit the deadline, or the peer closed
			return
		case <-heartbeat.C:
			_ = conn.SetWriteDeadline(time.Now().Add(wsWriteWait))
			if err := conn.WriteMessage(websocket.TextMessage, makeHeartbeat(piPid, herdrBin)); err != nil {
				bleLogf("WS: heartbeat write: %v\n", err)
				return
			}
			// The device has no RTC; refresh its clock with every heartbeat.
			if err := conn.WriteMessage(websocket.TextMessage, makeTimeSync(time.Now())); err != nil {
				return
			}
		case <-ping.C:
			if err := conn.WriteControl(websocket.PingMessage, nil, time.Now().Add(wsWriteWait)); err != nil {
				bleLogf("WS: ping: %v\n", err)
				return
			}
		}
	}
}
