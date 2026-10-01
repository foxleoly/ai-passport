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
pi session and drives herdr; the device shows a 4-view board (HOME / LIVE / STATS /
MENU) and gives 3-button control including interrupt.

BLE was **dropped**: macOS 27.0 starves third-party CoreBluetooth scanning, and
NimBLE's ~73 KB of heap does not leave room for `esp_wifi_init` on this 320 KB C3.
Reasoning and measurements: `DESIGN.md` §4.1.

Wi-Fi setup is a self-built captive portal, not `wifi_prov_mgr`: a phone joins
`Pi-Buddy-Setup`, picks the network from a scanned list, and types only the password.
Details: `DESIGN.md` §9.3.

## 2. Git state

- Branch: `feature/pi-agent-buddy` (from `main`).
- Working tree holds uncommitted pibud work: firmware modules, sidecar, docs,
  `tools/validate.sh`, `tools/check_repo.py`, `sdkconfig.defaults`,
  `main/CMakeLists.txt`, and vendored `components/esp_websocket_client` +
  `components/mdns`. Do not commit unless asked.
- `main/pibud_ble*.{c,h}` were deleted; the BLE code is gone, do not resurrect it.
- The earlier TOTP app was deleted; do not resurrect it.

## 3. Done + verified

- **P1 sidecar** `tools/pi-buddy-sidecar/` (Go): reads `$PI_SESSION_FILE` (JSONL)
  plus `herdr api snapshot` and emits one heartbeat JSON; Path C mode is the
  WebSocket server plus mDNS advertising (`dns-sd -R`, supervised); device acts map
  to `herdr agent send-keys` / `prompt`. `go test` passes.
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
- **Gates:** `./tools/validate.sh --static` PASS; `--firmware` PASS (merged image
  verified).
- **Environment:** ESP-IDF 5.5.3 at `~/esp/esp-idf-v5.5.3` (target `esp32c3`).

## 4. Not done (next, in order)

1. **WebSocket authentication (security).** The endpoint listens on the whole LAN,
   is unauthenticated, and can run `herdr agent send-keys`, so any host on the
   network can inject keystrokes into the user's agent panes. Minimal fix: a shared
   token in device NVS and a sidecar flag. See DESIGN.md §9.4.
2. **Unique mDNS instance name.** It is the literal `pibuddy`, so two sidecars make
   the device choose arbitrarily.
3. **P4** — settings NVS persistence, MENU polish, approval overlay, dark-page pi
   accents (open Q2).

## 5. Commands

```bash
# env (each new shell)
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
source ~/esp/esp-idf-v5.5.3/export.sh

# host tests
./tools/validate.sh --static

# full gate (build + merged image + host tests)
./tools/validate.sh

# sidecar: build (CGO needs the 26.5 SDK, see section 6)
cd tools/pi-buddy-sidecar
CGO_ENABLED=1 go build -o /tmp/pi-buddy-sidecar .
go test ./...

# sidecar: run the untethered path. ALWAYS pass --target; without it an act goes to
# the focused herdr pane, which may be the session running the sidecar itself.
/tmp/pi-buddy-sidecar --ws --ws-addr :51820 --target w4:p1

# flash: app partition only, keeps the Wi-Fi credentials in NVS. Needs approval.
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  write_flash 0x10000 build/FoloToy-AI-Passport.bin
```

Provisioning (or re-provisioning) a network is done from a phone browser: join
`Pi-Buddy-Setup`, open `http://192.168.4.1/`, pick the network from the list, type
the password, save. The portal appears when the device has no stored credentials and
reopens by itself after roughly 30 s of failed joins, so a mistyped password is
recoverable without a cable. No computer or `esp_prov.py` is involved.

## 6. Machine-specific env notes (this Mac)

- macOS 27.0 + Xcode 26.6: the 27.0 SDK `.tbd` files reference `arm64e.x1`, which the
  older linker cannot parse, so default linking fails. Use the CLT toolchain:
  `export DEVELOPER_DIR=/Library/Developer/CommandLineTools`. For CGO builds (the
  sidecar links CoreBluetooth) also pass
  `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`.
- ESP-IDF: the large `esp_wifi/lib` submodule was repaired by shallow-fetching its
  pinned commit; the IDF checkout itself is clean.

## 7. Locked decisions

- Views: HOME (pi.dev brand — parchment, monospace, tri-color mosaic logo), LIVE,
  STATS, MENU. There is no BLE pairing overlay any more.
- Buttons: UP short = scroll, UP long = switch view; DOWN short = scroll/follow,
  DOWN long = jump to latest; OK short = confirm/activate, OK long = **interrupt**.
- "approve" means pi generic remote input (`herdr agent prompt` / `send-keys`), not a
  structured permission dialog.
- The Mac is the tethered/untethered host, never the phone.
- Wi-Fi credentials live in the `pibud` NVS namespace, never in the repository, and
  the Wi-Fi driver's own NVS persistence is off so there is one source of truth.
- Provisioning is the self-built captive portal described above; `wifi_prov_mgr`,
  `protocomm`, and `esp_prov.py` are no longer part of the flow.

## 8. Open assumptions (need a call)

- Q2: accent dark data pages with the pi tri-color, or keep them functional-only?
- Interrupt key sequence is Escape (`send-keys <pane> esc`) — confirm it is what
  users expect for "stop" in every case.
- Token aggregation: window sum over roughly the last 50 records, or session total?

## 9. File map

- Firmware: `main/pibud_{types,state,protocol,text_layout,i4,line,form,prov_html,usbc,ws,ui,app}.{c,h}`
- Sidecar: `tools/pi-buddy-sidecar/{go.mod,main.go,event.go,herdr.go,usb.go,ws.go,ble_central.m}`
  (`ble.go` / `ble_central.m` are the dead Path B central, kept for reference only)
- Vendored components: `components/esp_websocket_client`, `components/mdns`
- Docs: `docs/pi-agent-buddy/DESIGN{,.zh_CN}.md`, this handoff
- Gate: `tools/validate.sh` (pibud host tests registered), `tools/check_repo.py`
  (vendored doc roots registered)
- Config: `sdkconfig.defaults` (`CONFIG_BT_ENABLED=n`, Wi-Fi + WS transport +
  protocomm SECURITY_0)
