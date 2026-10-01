package main

import (
	"strings"
	"testing"
)

// The device browses by service type, so the only job of the instance name is to
// keep two sidecars from competing for one registration.

func TestMdnsInstanceIsStableForOnePort(t *testing.T) {
	if mdnsInstance("51820") != mdnsInstance("51820") {
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
