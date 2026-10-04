package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestActText(t *testing.T) {
	cases := []struct {
		name                      string
		kind, approve, deny, want string
		wantErr                   bool
	}{
		{name: "interrupt", kind: "interrupt", want: ""},
		{name: "approve default", kind: "approve", want: "yes"},
		{name: "approve custom", kind: "approve", approve: "yep", want: "yep"},
		{name: "deny default", kind: "deny", want: "no"},
		{name: "deny custom", kind: "deny", deny: "nope", want: "nope"},
		{name: "unknown kind", kind: "bogus", wantErr: true},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got, err := actText(c.kind, c.approve, c.deny)
			if c.wantErr {
				if err == nil {
					t.Fatalf("actText(%q) = %q, want an error", c.kind, got)
				}
				return
			}
			if err != nil || got != c.want {
				t.Fatalf("actText(%q) = %q, %v; want %q", c.kind, got, err, c.want)
			}
		})
	}
}

// The act channel is a file handshake, so it is worth driving end to end: a
// stand-in extension answers whatever appears in act.json.
func TestRunActRoundTrip(t *testing.T) {
	dir := fakeStateDir(t)
	actPath := filepath.Join(dir, "act.json")
	resultPath := filepath.Join(dir, "act-result.json")

	cases := []struct {
		name       string
		result     ActResult
		wantErr    bool
		wantDetail string
	}{
		{name: "accepted", result: ActResult{OK: true, Detail: "aborted"}, wantDetail: "aborted"},
		{name: "refused", result: ActResult{OK: false, Detail: "no such session"}, wantErr: true, wantDetail: "no such session"},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			// Clear the channel: a leftover request from an earlier subtest would
			// be answered with the wrong id and look like a timeout.
			_ = os.Remove(actPath)
			_ = os.Remove(resultPath)

			stop := make(chan struct{})
			go func() {
				defer close(stop)
				for {
					var req ActRequest
					if readJSONFile(actPath, &req) == nil && req.ID != "" {
						res := c.result
						res.ID = req.ID
						_ = writeJSONAtomic(resultPath, res)
						return
					}
					time.Sleep(10 * time.Millisecond)
				}
			}()

			detail, err := RunAct("interrupt", "", "")
			<-stop
			if c.wantErr && err == nil {
				t.Fatalf("RunAct = %q, nil; want an error", detail)
			}
			if !c.wantErr && err != nil {
				t.Fatalf("RunAct = %q, %v; want success", detail, err)
			}
			if detail != c.wantDetail {
				t.Fatalf("detail = %q, want %q", detail, c.wantDetail)
			}
		})
	}
}

// A stale answer from an earlier act must not be mistaken for this one's.
func TestRunActIgnoresAnotherActsResult(t *testing.T) {
	dir := fakeStateDir(t)
	resultPath := filepath.Join(dir, "act-result.json")
	if err := writeJSONAtomic(resultPath, ActResult{ID: "act-from-a-previous-run", OK: true, Detail: "stale"}); err != nil {
		t.Fatalf("seed: %v", err)
	}

	go func() {
		actPath := filepath.Join(dir, "act.json")
		for {
			var req ActRequest
			if readJSONFile(actPath, &req) == nil && req.ID != "" {
				_ = writeJSONAtomic(resultPath, ActResult{ID: req.ID, OK: true, Detail: "fresh"})
				return
			}
			time.Sleep(10 * time.Millisecond)
		}
	}()

	detail, err := RunAct("approve", "", "")
	if err != nil {
		t.Fatalf("RunAct: %v", err)
	}
	if detail != "fresh" {
		t.Fatalf("detail = %q, want the answer for this act", detail)
	}
}
