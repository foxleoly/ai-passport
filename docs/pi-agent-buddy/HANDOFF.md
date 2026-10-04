<p align="right">
  <a href="HANDOFF.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pi Agent Buddy — Handoff

> Branch-local working handoff for `feature/pi-agent-buddy`. Capture of current
> state so a new session can continue without re-deriving context.

## 1. What this is

A wearable **coding-agent status board + light remote** for a pi session. The device
is an ESP32-C3 board that links to a computer-side Go sidecar over **Wi-Fi +
WebSocket** (untethered) or **USB-Serial-JTAG** (tethered). The sidecar observes the
pi session and runs the device's acts through the pi extension; the device shows a
4-view board (HOME / LIVE / STATS /
MENU) and gives 3-button control including interrupt.

BLE was **dropped**: macOS 27.0 starves third-party CoreBluetooth scanning, and
NimBLE's ~73 KB of heap does not leave room for `esp_wifi_init` on this 320 KB C3.
Reasoning and measurements: `DESIGN.md` §4.1.

Wi-Fi setup is a self-built captive portal, not `wifi_prov_mgr`: a phone joins
`Pi-Buddy-Setup`, picks the network from a scanned list, and types only the password.
Details: `DESIGN.md` §9.3.

## 2. Git state

- Branch: `feature/pi-agent-buddy` (from `main`), **17 commits ahead of `main`**.
- The working tree holds uncommitted work (do not commit unless asked): the UI round
  (logo, tri-color bar, 1d/7d/30d, status bar, STATS split, delivery feedback), new
  `main/pibud_format.{c,h}` + `tests/test_pibud_format.c`, the token scope
  (`event.go` / `main.go` `windowTotals`), the link-code corrections, the computer-side
  rewrite (the pi extension became the bridge; herdr became optional), the firmware
  `sub_available` field, and the four docs (`DESIGN{,.zh_CN}.md`, `HANDOFF{,.zh_CN}.md`).
  Vendored `components/esp_websocket_client` + `components/mdns` and
  `sdkconfig.defaults` are already committed.
- **The computer-side tooling is published too**, and `tools/pi-buddy-sidecar` +
  `tools/pi-buddy-extension` here stay the source of truth for it:
  `github.com/foxleoly/pi-agent-buddy` (MIT, `v0.2.0` with CI-built binaries for five
  platforms). Copy those two directories over and commit whenever they change.
- `main/pibud_ble*.{c,h}` were deleted; the BLE code is gone, do not resurrect it.
- The earlier TOTP app was deleted; do not resurrect it.

## 3. Done + verified

- **P1 sidecar** `tools/pi-buddy-sidecar/` (Go): reads the pi session JSONL plus
  the state the pi extension publishes and emits one heartbeat JSON; Path C mode is
  the
  WebSocket server plus mDNS advertising; device acts travel back to the extension
  through `~/.pi-buddy/act.json`. `go test` passes.
- **P2 firmware** `main/pibud_*`: types, state reducer, protocol (cJSON),
  text-layout, i4, line, `pibud_usbc` (Path A), `pibud_ws` (Path C), LVGL UI, and
  `app_main` wiring. Build and host tests PASS.
- **P3 acceptance over Path C, on the real device:** the device joins Wi-Fi from
  provisioned credentials, discovers the sidecar over mDNS, renders live session
  data, and OK-long reaches the pinned pane:

  ```text
  WS: device connected 192.168.145.74:54363
  device act: herdr agent send-keys w4:p1 esc -> w4:p1
  act ok: {"id":"cli:agent:send-keys","result":{"type":"ok"}}
  ```

- **Provisioning portal (verified on device).** The device runs its own captive
  portal on `Pi-Buddy-Setup` with a scanned network picker; a phone configured it
  end to end, and the log shows the full chain: `setup form: ssid "..."` →
  `credentials saved` → `sta ip: ...` → `setup access point closed` →
  `websocket connected`. Replaces `wifi_prov_mgr` + `esp_prov.py`, and the Mac keeps
  its network throughout.
- **Link code (implemented).** The WebSocket endpoint is no longer open to the LAN:
  the device dials `/<link code>` and anything else is refused with 401 before the
  upgrade. The code is generated once into `~/.pi-buddy/token` (mode 600), reprinted
  with `--show-token`, and typed into the setup page, whose field accepts the
  separators and capitalisation a phone keyboard adds. A refused handshake reopens
  the setup portal after three tries; an unreachable sidecar never does.
- **pi extension.** `tools/pi-buddy-extension/pibuddy.ts` (symlinked into
  `~/.pi/agent/extensions/`) shows the link state in the pi footer from the
  sidecar's `~/.pi-buddy/status.json`, and `/pibuddy` reports it and offers to copy
  the pair code to the clipboard.
- **Board UI round (P4, done).** HOME has the 40px pi mosaic mark, a continuous
  tri-color running bar (rounded ends), and the 1d / 7d / 30d token totals; STATS
  splits tokens into total / input / output / cache (each with its share) then
  cost / subs / tool / branch; the status bar shows model · clock · activity dot ·
  WiFi · battery; MENU is brightness (with the current %) + factory reset; the
  action bar acknowledges acts (`sending…` / `sent to your mac` / `not sent -
  check the link`). Token counts use K/M/B/T units; the stat scope is today (local
  day) across all sessions, widened to 7d/30d windows. Brightness persists in NVS
  and factory reset actually wipes it.
- **herdr two-state, on the real device.** With herdr installed the device shows the
  live sub-agent count (`4 subagents (1 work)`, STATS `4/1`); with `--herdr ""` the
  HOME line is empty and STATS reads `--`. Verified twice: once on the wire by reading
  a WebSocket frame (`sub_available:true/false`, and `sub_total` absent rather than
  zero when unknown) and once on the screen. The same run confirmed `abort()` stops
  the agent.
- **Published.** Computer-side tooling: `github.com/foxleoly/pi-agent-buddy` (MIT,
  `v0.2.0`, five platform binaries built by its own CI). Community play: **Pi Agent
  Buddy**, id 857 — published (revision 1796 approved); revision 1871 was pending
  review when this was written.
- **Gates:** `./tools/validate.sh --static` PASS; `--firmware` PASS (merged image
  verified); `go vet` + `go test -count=1 ./...` PASS with **no env workaround**
  (see §6).
- **Environment:** ESP-IDF 5.5.3 at `~/esp/esp-idf-v5.5.3` (target `esp32c3`); the
  Xcode license issue is fixed (`xcode-select` now points at
  `/Library/Developer/CommandLineTools`).

## 4. Not done (next, in order)

1. **The publisher's screenshot protocol.** The current FoloToy publisher skill states
   that firmware published through it must implement `FAP_SCREENSHOT_V1` — a serial
   framebuffer capture, PNG or RGB565LE — and that `validate` / `submit` want a
   matching capture receipt. This firmware does not implement it, so its cover is a
   drawn illustration rather than a real screen. 240×320 RGB565 is 150 KB and only
   ~45 KB of heap is free, so it cannot be one whole-screen snapshot: render in bands
   (preferred) or read the panel's GRAM back.
2. **Community play 857** is published, with revision 1871 pending review. The cover
   in it is the older composite illustration; replacing it needs a fresh one-time
   update authorization from the website once that revision clears.
3. **Real LIVE scrolling.** The LIVE feed is recent-tool text on the device (fed by
   the heartbeat `tool`/`arg`), not transcript lines. Real scrolling needs a protocol
   extension: the sidecar pushes bounded transcript lines and the device renders +
   scrolls them.
4. **MENU polish + APPROVAL overlay.** Brightness (with the current %) and factory
   reset are done; the remaining P4 items are the approval overlay (WAITING +
   ALLOW/DENY, half-wired in `pibud_ui.c`) and any MENU refinement.
5. **The extension has no committed test.** Its logic was checked by hand, but this
   repository has no JavaScript test convention, so nothing runs in the gate.
6. **Known `status.go` limitation:** a transient client can leave a stale `device`
   address in `status.json` (only the last-opened address is tracked).

## 5. Commands

```bash
# env (each new shell): Xcode is already pointed at CLT (§6), no DEVELOPER_DIR.
source ~/esp/esp-idf-v5.5.3/export.sh

# host tests
./tools/validate.sh --static

# full gate (build + merged image + host tests)
./tools/validate.sh

# sidecar: build and test (no CGO env needed since Xcode was fixed, see section 6)
cd tools/pi-buddy-sidecar
go build -o /tmp/pi-buddy-sidecar .
go test ./...

# sidecar: run the untethered path. No --target: the pi extension knows its own
# session, so an act cannot land in the wrong pane.
/tmp/pi-buddy-sidecar --ws --ws-addr :51820

# print the link code the device's setup page asks for (created on first --ws run)
/tmp/pi-buddy-sidecar --show-token

# flash: app partition only, keeps the Wi-Fi credentials in NVS. Needs approval.
# NOTE: ./tools/validate.sh --firmware builds in a temp dir and installs only the
# merged image into build/, so build/FoloToy-AI-Passport.bin is whatever the last
# plain `idf.py build` produced and may be older than the verified one. Flash the
# app image from the archive instead, or run `idf.py build` first.
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  write_flash 0x10000 build/firmware/<sha256>/FoloToy-AI-Passport.bin
```

Provisioning (or re-provisioning) a network is done from a phone browser: join
`Pi-Buddy-Setup`, open `http://192.168.4.1/`, pick the network from the list, type
the password, save. The portal appears when the device has no stored credentials and
reopens by itself after roughly 30 s of failed joins, so a mistyped password is
recoverable without a cable. No computer or `esp_prov.py` is involved.

## 6. Machine-specific env notes (this Mac)

- **Resolved:** `xcode-select -p` = `/Library/Developer/CommandLineTools` and the
  Xcode license is accepted, so `cc`, `go vet`, `go test`, and `go build` all pass
  with no `DEVELOPER_DIR` / `CGO_CFLAGS` workaround. History: the 27.0 SDK `.tbd`
  files reference `arm64e.x1` (which the older linker cannot parse) and the Xcode
  license was previously unaccepted — both reasons this Mac uses the CLT toolchain.
  If a future Xcode update re-selects Xcode.app, run
  `sudo xcode-select -s /Library/Developer/CommandLineTools` (or
  `sudo xcodebuild -license accept`).
- ESP-IDF: the large `esp_wifi/lib` submodule was repaired by shallow-fetching its
  pinned commit; the IDF checkout itself is clean.

## 7. Locked decisions

- Views: HOME (pi.dev brand — parchment, monospace, tri-color mosaic logo), LIVE,
  STATS, MENU. There is no BLE pairing overlay any more.
- Buttons: UP short = scroll, UP long = switch view; DOWN short = scroll/follow,
  DOWN long = jump to latest; OK short = confirm/activate, OK long = **interrupt**.
- "approve" means pi generic remote input (a user message sent through the pi
  extension), not a
  structured permission dialog.
- The Mac is the tethered/untethered host, never the phone.
- Wi-Fi credentials live in the `pibud` NVS namespace, never in the repository, and
  the Wi-Fi driver's own NVS persistence is off so there is one source of truth.
- Provisioning is the self-built captive portal described above; `wifi_prov_mgr`,
  `protocomm`, and `esp_prov.py` are no longer part of the flow.

## 8. Open assumptions (need a call)

- Q2: resolved — dark data pages carry the 24px pi mark in their top-right corner.
- Token stat: resolved — today's (local-day) total across every session, filtered by
  each record's `timestamp`, widened to 7d/30d windows via `windowTotals`.
- Sub-agent count: resolved — read from herdr when it is installed, reported as
  unknown when it is not. Nothing else depends on herdr.
- Interrupt: resolved — OK-long calls the pi extension's `abort()`; verified on the
  device.

## 9. File map

- Firmware: `main/pibud_{types,state,protocol,text_layout,i4,line,form,prov_html,format,usbc,ws,ui,app}.{c,h}`
- Sidecar: `tools/pi-buddy-sidecar/{go.mod,main.go,event.go,session.go,act.go,usb.go,ws.go,token.go,status.go}`
  (pure Go, no cgo: the dead Path B BLE central was deleted, so it builds on any platform)
- Extension: `tools/pi-buddy-extension/pibuddy.ts` (symlinked into `~/.pi/agent/extensions/`)
- Vendored components: `components/esp_websocket_client`, `components/mdns`
- Docs: `docs/pi-agent-buddy/DESIGN{,.zh_CN}.md`, this handoff
- Gate: `tools/validate.sh` (pibud host tests registered), `tools/check_repo.py`
  (vendored doc roots registered)
- Config: `sdkconfig.defaults` (`CONFIG_BT_ENABLED=n`, Wi-Fi + WS transport +
  protocomm SECURITY_0)
