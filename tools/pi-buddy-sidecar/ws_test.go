package main

import (
	"strings"
	"testing"
)

// The device browses by service type, so the only job of the instance name is to
// keep two sidecars from competing for one registration.

func TestMdnsInstanceIsStableForOnePort(t *testing.T) {
	first := mdnsInstance("51820")
	if first != mdnsInstance("51820") {
		t.Fatal("instance name must be stable for the same port")
	}
}

func TestMdnsInstanceDiffersBetweenPorts(t *testing.T) {
	if mdnsInstance("51820") == mdnsInstance("51840") {
		t.Fatal("instance names must differ between ports")
	}
}

func TestMdnsInstanceShape(t *testing.T) {
	got := mdnsInstance("51820")
	if !strings.HasPrefix(got, "pibuddy-51820-") {
		t.Fatalf("unexpected instance name %q", got)
	}
	if want := len("pibuddy-51820-") + 4; len(got) != want {
		t.Fatalf("instance name %q has length %d, want %d", got, len(got), want)
	}
}

func TestHostnameIsNeverEmpty(t *testing.T) {
	if hostname() == "" {
		t.Fatal("hostname fallback must never be empty")
	}
}

// fakeConn stands in for a *websocket.Conn; the registry only needs Close. The
// field matters: a zero-size struct's pointers may share one address, which
// would make two distinct connections compare equal.
type fakeConn struct{ closed bool }

func (f *fakeConn) Close() error {
	f.closed = true
	return nil
}

// A reconnecting device must not leave its predecessor registered: the sidecar
// keeps one live connection, and replace hands the superseded one back to close.
func TestWSSessionsSupersedesPrevious(t *testing.T) {
	var s wsSessions
	a, b := &fakeConn{}, &fakeConn{}

	if old := s.replace(a); old != nil {
		t.Fatalf("replace(a) = %v, want nil", old)
	}
	if old := s.replace(b); old != a {
		t.Fatalf("replace(b) = %v, want the superseded a", old)
	}
}

// The superseded socket unwinds asynchronously (its next heartbeat write fails),
// so its release must not clear the connection that already replaced it.
func TestWSSessionsReleaseKeepsReplacement(t *testing.T) {
	var s wsSessions
	a, b := &fakeConn{}, &fakeConn{}

	s.replace(a)
	s.replace(b)
	s.release(a) // a finishes after b took over

	if cur := s.replace(b); cur != b {
		t.Fatalf("release(a) cleared the live connection: replace(b) = %v, want b", cur)
	}

	s.release(b)
	if old := s.replace(a); old != nil {
		t.Fatalf("replace after release(b) = %v, want nil", old)
	}
}

// The read deadline must outlast the ping period, or a healthy link is reaped
// between pings while the device is perfectly fine.
func TestKeepaliveReadDeadlineOutlastsPingPeriod(t *testing.T) {
	if wsPongWait <= wsPingEvery {
		t.Fatalf("wsPongWait %v must exceed wsPingEvery %v", wsPongWait, wsPingEvery)
	}
}
