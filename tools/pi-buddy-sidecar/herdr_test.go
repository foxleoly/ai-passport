package main

import (
	"reflect"
	"strings"
	"testing"
)

func TestBuildActArgs(t *testing.T) {
	cases := []struct {
		kind    string
		target  string
		approve string
		deny    string
		want    []string
	}{
		{"interrupt", "w4:p9", "", "", []string{"agent", "send-keys", "w4:p9", "esc"}},
		{"approve", "w4:p9", "", "", []string{"agent", "prompt", "w4:p9", "yes"}},
		{"approve", "w4:p9", "yes, do it", "", []string{"agent", "prompt", "w4:p9", "yes, do it"}},
		{"deny", "w4:p9", "", "no", []string{"agent", "prompt", "w4:p9", "no"}},
	}
	for _, c := range cases {
		got := BuildActArgs(c.kind, c.target, c.approve, c.deny)
		if !reflect.DeepEqual(got, c.want) {
			t.Fatalf("BuildActArgs(%q) = %v, want %v", c.kind, got, c.want)
		}
	}
	if got := BuildActArgs("frobnicate", "t", "", ""); got != nil {
		t.Fatalf("unknown kind should be nil, got %v", got)
	}
}

func TestPreviewAct(t *testing.T) {
	if p := PreviewAct("interrupt", "w4:p9", "", "", "herdr"); !strings.Contains(p, "herdr agent send-keys w4:p9 esc") {
		t.Fatalf("preview = %q", p)
	}
	if p := PreviewAct("approve", "w4:p9", "yes, do it", "", "herdr"); !strings.Contains(p, `"yes, do it"`) {
		t.Fatalf("preview should quote text with spaces: %q", p)
	}
}
