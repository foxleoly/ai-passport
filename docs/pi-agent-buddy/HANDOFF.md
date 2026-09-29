<p align="right">
  <a href="HANDOFF.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pi Agent Buddy — Handoff

> Branch-local working handoff for `feature/pi-agent-buddy`. Capture of current
> state so a new session can continue without re-deriving context.

## 1. What this is
A wearable **coding-agent status board + light remote** for a pi session. The
device is a BLE NUS peripheral (LE Secure Connections, 6-digit passkey, bonding);
a computer-side Go sidecar observes the pi session + drives herdr. The device shows
6 views and gives 3-button control (incl. interrupt).

## 2. Git state
- Branch: `feature/pi-agent-buddy` (from `main`).
- Working tree has **uncommitted** pibud work (firmware modules, sidecar, docs,
  `tools/validate.sh`, `sdkconfig.defaults`). Do not commit unless asked.
- The earlier TOTP app was deleted; do not resurrect it.

## 3. Done + verified
- **P1 sidecar** `tools/pi-buddy-sidecar/` (Go): reads `$PI_SESSION_FILE` (JSONL)
  + `herdr api snapshot` → emits one heartbeat JSON. Verified on a live session;
  `go test` passes. No BLE/act yet.
- **P2 firmware** `main/pibud_*`: types, state (reducer), protocol (cJSON),
  text-layout, i4, line, BLE (NimBLE NUS+SC+bonding, ported from claude-buddy),
  6-view LVGL UI, `app_main` wiring. `idf.py build` + merged 0x0 image **PASS**;
  pibud host tests **PASS**.
- **Environment**: ESP-IDF 5.5.3 at `~/esp/esp-idf-v5.5.3` (target esp32c3).

## 4. Not done (next, in order)
1. **P3 sidecar** — BLE central (connect `Pi-*`, NUS send heartbeat / receive
   `act`) + herdr control mapping (`act` → `herdr agent send-keys`/`prompt`;
   OK-long = interrupt via Escape). This closes the loop.
2. **On-device** — flash, BLE pairing, UI render, OK-long interrupt, soak.
3. **P4** — settings NVS persistence, MENU/PAIRING polish, dark-page pi accents
   (open Q2).

## 5. Commands
```bash
# env (each new shell)
export DEVELOPER_DIR=/Library/Developer/CommandLineTools   # persist: sudo xcode-select -s /Library/Developer/CommandLineTools
source ~/esp/esp-idf-v5.5.3/export.sh

# host tests
DEVELOPER_DIR=/Library/Developer/CommandLineTools ./tools/validate.sh --static

# firmware build (merged 0x0 image at build/FoloToy-AI-Passport-full.bin)
./tools/validate.sh            # full gate; or: idf.py -B /tmp/pibud_build build

# sidecar (P1) — print heartbeat from the current pi session
cd tools/pi-buddy-sidecar && go run .
go test ./...

# flash (device detected at handoff: /dev/cu.usbmodem1101) — needs approval
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

## 6. Machine-specific env notes (this Mac)
- macOS 27.0 + Xcode 26.6: the 27.0 SDK `.tbd` uses `arm64e.x1`; the old linker
  `ld-1267` can't parse it, so default `cc` linking fails. **Fix: use the CLT
  toolchain** (new `ld-27037.1`): `export DEVELOPER_DIR=/Library/Developer/CommandLineTools`
  (or `sudo xcode-select -s /Library/Developer/CommandLineTools`). Fallback:
  `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`.
- ESP-IDF: the large `esp_wifi/lib` submodule was repaired via shallow fetch of
  its pinned commit (already done; `git status` in the IDF checkout is clean).

## 7. Locked decisions
- 6 views: HOME (pi.dev brand: parchment + mono + tri-color logo) / LIVE /
  STATS / MENU + APPROVAL / PAIRING overlays.
- 3 buttons: UP short=scroll, UP long=switch view; DN short=scroll/follow,
  DN long=jump-to-latest; OK short=confirm/activate, OK long=**interrupt agent**.
- "approve" = pi generic remote input (`herdr agent prompt`/`send-keys`), not a
  structured permission dialog.
- Firmware reuses the claude-buddy architecture (BLE NUS + LE SC + newline JSON +
  pure state machine); UI redesigned per the pi.dev-style mockup.

## 8. Open assumptions (need a call)
- Q2: accent dark data pages with the pi tri-color, or keep them functional-only?
- sidecar BLE lib: CoreBluetooth (cgo) vs pure-Go `ble`.
- interrupt key sequence: plain Escape vs combo (decide on device in P3).
- token aggregation: window sum (last ~50 records) vs session total.

## 9. File map
- Firmware: `main/pibud_{types,state,protocol,text_layout,i4,line,ble,ble_store,ble_lifecycle,ui,app}.{c,h}`
- Sidecar: `tools/pi-buddy-sidecar/{go.mod,main.go,event.go,event_test.go}`
- Docs: `docs/pi-agent-buddy/DESIGN{,.zh_CN}.md`, this handoff
- Gate: `tools/validate.sh` (pibud host tests registered; section-GC flag made portable)
- Config: `sdkconfig.defaults` (BT NimBLE SM_SC + NVS persist enabled)
