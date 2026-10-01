// token.go — the shared link code that keeps the WebSocket endpoint from being
// open to every host on the LAN.
//
// The device has to present the code, so it must be typed once into the setup
// page: twelve hex digits are short enough for that and long enough to make
// guessing impractical, given the endpoint also refuses browser-originated
// upgrades. It travels in the WebSocket path over an unencrypted link, so it is only
// as private as the Wi-Fi link itself; this closes the "any host on the network can
// connect" hole, not a passive sniffing one.
package main

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// tokenDigits is the number of hex digits in a link code.
const tokenDigits = 12

// normalizeToken lower-cases a code and drops the separators a keyboard or a
// pasted line may add, so "A1B2-C3D4-E5F6" and "a1b2c3d4e5f6" compare equal.
func normalizeToken(raw string) (string, bool) {
	var b strings.Builder
	for _, r := range raw {
		switch r {
		case '-', '_', ':', ' ', '\t', '\n', '\r':
			continue
		}
		switch {
		case r >= '0' && r <= '9', r >= 'a' && r <= 'f':
			b.WriteRune(r)
		case r >= 'A' && r <= 'F':
			b.WriteRune(r - 'A' + 'a')
		default:
			return "", false
		}
		if b.Len() > tokenDigits {
			return "", false
		}
	}
	if b.Len() != tokenDigits {
		return "", false
	}
	return b.String(), true
}

// newToken returns a fresh code. The printed and stored forms are the same bare
// string, so `cat ~/.pi-buddy/token` can never disagree with what was displayed;
// normalizeToken still accepts the grouped or upper-case variants a human types.
func newToken() (string, error) {
	var raw [tokenDigits / 2]byte
	if _, err := rand.Read(raw[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(raw[:]), nil
}

// defaultTokenPath is where the link code lives between runs.
func defaultTokenPath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".pi-buddy", "token"), nil
}

// loadOrCreateToken returns the link code for this machine, creating and
// persisting one on first use so it stays stable across restarts. An explicit
// value wins over the stored one.
func loadOrCreateToken(path, explicit string) (string, error) {
	if explicit != "" {
		token, ok := normalizeToken(explicit)
		if !ok {
			return "", fmt.Errorf("--token must be %d hex digits", tokenDigits)
		}
		return token, nil
	}

	if data, err := os.ReadFile(path); err == nil {
		token, ok := normalizeToken(string(data))
		if !ok {
			return "", fmt.Errorf("%s does not hold a %d-digit link code", path, tokenDigits)
		}
		return token, nil
	}

	token, err := newToken()
	if err != nil {
		return "", err
	}
	normalized, ok := normalizeToken(token)
	if !ok {
		return "", errors.New("generated link code failed to normalize")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return "", err
	}
	// Store what is actually compared, so the file and the printed code cannot
	// disagree. Owner-only: this is the link's only secret.
	if err := os.WriteFile(path, []byte(normalized+"\n"), 0o600); err != nil {
		return "", err
	}
	return normalized, nil
}
