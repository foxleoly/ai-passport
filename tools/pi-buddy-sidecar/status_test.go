package main

import (
	"encoding/json"
	"path/filepath"
	"testing"
	"time"
)

func TestMarshalLinkStatusShape(t *testing.T) {
	now := time.Date(2026, 10, 1, 12, 0, 0, 0, time.UTC)

	data, err := marshalLinkStatus(true, "192.168.145.74:58501", 4242, now)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}

	var got linkStatus
	if err := json.Unmarshal(data, &got); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if !got.Connected || got.Device != "192.168.145.74:58501" || got.PID != 4242 {
		t.Fatalf("unexpected status %+v", got)
	}
	if got.Updated != "2026-10-01T12:00:00Z" {
		t.Fatalf("updated = %q, want RFC3339 UTC", got.Updated)
	}
}

// A reader tells "idle" from "linked" by the device field, so it must be absent
// rather than an empty string.
func TestMarshalLinkStatusOmitsDeviceWhenIdle(t *testing.T) {
	data, err := marshalLinkStatus(false, "", 1, time.Now())
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	var raw map[string]any
	if err := json.Unmarshal(data, &raw); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if _, present := raw["device"]; present {
		t.Fatalf("device must be omitted while idle: %v", raw)
	}
	if raw["connected"] != false {
		t.Fatalf("connected = %v, want false", raw["connected"])
	}
}

func TestStatusPathLivesInStateDir(t *testing.T) {
	path, err := statusPath()
	if err != nil {
		t.Fatalf("statusPath: %v", err)
	}
	if filepath.Base(path) != "status.json" || filepath.Base(filepath.Dir(path)) != ".pi-buddy" {
		t.Fatalf("unexpected status path %q", path)
	}
}

// The open-connection count decides what a closing connection publishes; getting
// it wrong is what would strand the file on "offline" after a reconnect. The
// publisher is replaced so this stays off the filesystem and the transitions
// themselves are asserted.
func TestLinkOpenTracking(t *testing.T) {
	type state struct {
		connected bool
		device    string
	}
	var calls []state

	original := publish
	publish = func(connected bool, device string) {
		calls = append(calls, state{connected, device})
	}
	t.Cleanup(func() { publish = original })

	linkMu.Lock()
	linkOpen = 0
	linkMu.Unlock()

	noteLinkOpened("a")
	noteLinkOpened("b")
	noteLinkClosed()
	noteLinkClosed()
	noteLinkClosed() // a stray close must not drive the counter negative

	want := []state{
		{true, "a"},
		{true, "b"},
		{true, ""}, // one connection is still open
		{false, ""},
		{false, ""},
	}
	if len(calls) != len(want) {
		t.Fatalf("published %d states, want %d: %v", len(calls), len(want), calls)
	}
	for i, w := range want {
		if calls[i] != w {
			t.Errorf("state %d = %+v, want %+v", i, calls[i], w)
		}
	}
}
