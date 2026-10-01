package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestNormalizeTokenAcceptsKeyboardVariants(t *testing.T) {
	cases := []string{
		"a1b2c3d4e5f6",
		"A1B2C3D4E5F6",
		"a1b2-c3d4-e5f6",
		"A1B2-C3D4-E5F6",
		" a1b2 c3d4 e5f6\n",
		"a1b2_c3d4:e5f6",
	}
	for _, in := range cases {
		got, ok := normalizeToken(in)
		if !ok || got != "a1b2c3d4e5f6" {
			t.Errorf("normalizeToken(%q) = %q, %v; want a1b2c3d4e5f6, true", in, got, ok)
		}
	}
}

func TestNormalizeTokenRejectsWrongInput(t *testing.T) {
	cases := []string{
		"",
		"a1b2c3d4e5f",     // too short
		"a1b2c3d4e5f6a",   // too long
		"a1b2c3d4e5g6",    // not hex
		"a1b2-c3d4-e5f6!", // stray punctuation
		"------------",
	}
	for _, in := range cases {
		if got, ok := normalizeToken(in); ok {
			t.Errorf("normalizeToken(%q) = %q, true; want rejection", in, got)
		}
	}
}

// The stored file and the printed code must be the same string: tooling that reads
// the file must not disagree with what the user typed into the device.
func TestNewTokenIsAlreadyCanonical(t *testing.T) {
	raw, err := newToken()
	if err != nil {
		t.Fatalf("newToken: %v", err)
	}
	if len(raw) != tokenDigits {
		t.Fatalf("newToken returned %q, want %d digits", raw, tokenDigits)
	}
	normalized, ok := normalizeToken(raw)
	if !ok || normalized != raw {
		t.Fatalf("newToken returned %q, which is not the canonical form (%q, %v)", raw, normalized, ok)
	}
}

func TestNewTokenIsUsable(t *testing.T) {
	seen := map[string]bool{}
	for i := 0; i < 32; i++ {
		raw, err := newToken()
		if err != nil {
			t.Fatalf("newToken: %v", err)
		}
		token, ok := normalizeToken(raw)
		if !ok {
			t.Fatalf("generated token %q does not normalize to a usable code", raw)
		}
		if seen[token] {
			t.Fatalf("generated a duplicate token %q", token)
		}
		seen[token] = true
	}
}

func TestLoadOrCreateTokenReusesStoredValue(t *testing.T) {
	path := filepath.Join(t.TempDir(), "token")

	first, err := loadOrCreateToken(path, "")
	if err != nil {
		t.Fatalf("first call: %v", err)
	}
	second, err := loadOrCreateToken(path, "")
	if err != nil {
		t.Fatalf("second call: %v", err)
	}
	if first != second {
		t.Fatalf("token changed between runs: %q then %q", first, second)
	}

	info, err := os.Stat(path)
	if err != nil {
		t.Fatalf("stat: %v", err)
	}
	if perm := info.Mode().Perm(); perm != 0o600 {
		t.Fatalf("token file mode is %o, want 600", perm)
	}
}

func TestLoadOrCreateTokenExplicitWins(t *testing.T) {
	path := filepath.Join(t.TempDir(), "token")
	got, err := loadOrCreateToken(path, "A1B2-C3D4-E5F6")
	if err != nil {
		t.Fatalf("explicit token: %v", err)
	}
	if got != "a1b2c3d4e5f6" {
		t.Fatalf("explicit token normalized to %q", got)
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatal("an explicit token must not write the stored file")
	}
	if _, err := loadOrCreateToken(path, "not-a-token"); err == nil {
		t.Fatal("a malformed --token must be rejected, not silently accepted")
	}
}
