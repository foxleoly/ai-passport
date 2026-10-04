<p align="right">
  <a href="DESIGN.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pi Agent Buddy — Design & Implementation Plan (branch-local working doc)

> Branch: `feature/pi-agent-buddy` (from `main`). This is a branch-local planning
> draft, not upstream-maintained documentation; keep it current with the build.
> Screen mockups: `/tmp/pi-buddy-mockup/` (HOME view approved in the pi.dev style).

## 1. Product

A wearable **coding-agent status board + light remote control**. Watch what pi is
writing / where it is stuck / how many tokens it spent; stop a run with a button.

- **In scope:** firmware (240×320 LVGL board + newline-JSON protocol + state machine
  + views + 3 buttons) and the computer side: a **sidecar** (Go CLI) plus a **pi
  extension** that publishes the live session state and runs the device's acts
  through pi's own extension API.
- **Transports:** USB-Serial-JTAG (tethered) and Wi-Fi + WebSocket with mDNS
  discovery (untethered) — see §4.1. BLE was dropped; see §4.1.
- **Out of scope / non-goals:** not a security token or passkey credential holder;
  not an e-pet (the mascot is a 16px status avatar, not a 132px pet stage); does not
  use NFC (NTAG213 has no MCU API); "approve" = pi's **generic remote input**
  (a user message delivered through the pi extension), not a structured permission
  dialog.

## 2. Screen design (use the 240×320 fully)

Pixel budget (8px mono, ~30 cols; top status + bottom action bars persistent):

| Region | Height | Content |
| --- | --- | --- |
| Status bar | ~22px | model (L) · clock · activity dot · WiFi · battery (R) |
| Main | ~256px | view content |
| Action bar | ~22px | key hints, context-sensitive |

### 2.1 Views

Implemented and cycled by the view gesture:
1. **HOME / overall** (default landing) — pi.dev style: warm parchment, mono type,
   40px tri-color mosaic logo, a running tri-color progress bar, and the 1d / 7d /
   30d token totals plus subagents and link state.
2. **LIVE** (default board) — status bar + live activity feed + focus strip (current
   tool + tokens/cost) + action bar.
3. **STATS** — token split (total / input / output / cache, each with its share)
   then cost / subagents / tool / branch. The sub-agent count reads `--`: pi's
   extension API exposes no sub-agent data.
4. **MENU** — brightness (shows the current %) + factory reset.

Planned, not implemented (P4, deferred): **PAIRING** overlay (6-digit passkey — moot
now that BLE is dropped) and **APPROVAL** overlay (WAITING banner + awaited prompt +
ALLOW/DENY), which would appear automatically rather than being cycled.

### 2.2 Activity feed lines (color-coded)
| Line | Source | Color |
| --- | --- | --- |
| `read main/x.c` | `tool_use` (name + first arg) | ok = green |
| `✓ / ✗ bash` | `toolResult.isError` | green / red |
| `thinking …` | `thinking` block | yellow |
| `[model] agnes→…` | `model_change` | cyan |

Auto-follows the latest; bounded ~48-line scrollback (no PSRAM → RAM budget).

### 2.3 pi brand tokens (extracted from pi.dev)
```text
parchment  #F3F2F0 / #EBE7E4   (HOME / brand pages only; data pages stay dark)
ink        #252F3D  muted #5C5752
logo       coral #F09082 / steel #4D9ABF / amber #F1BE58 (accent #6A9FCC)
type       monospace
```
HOME = warm parchment + mono + the three-color mosaic, re-drawn 1:1 from the official
SVG paths (https://pi.dev/logo-auto.svg) as five rectangles on a 4x4 cell grid — every
edge in that file is axis-aligned, so no bitmap is needed. The dark pages carry the
same mark at 24px, top-right (Q2, resolved).

## 3. Three-button mapping (final)

| Key | Short | Long |
| --- | --- | --- |
| **UP** | scroll up / selection up | **switch view** (cycle HOME→LIVE→STATS→MENU) |
| **DOWN** | scroll down / follow | **jump to latest** |
| **OK** | **confirm/activate** (APPROVAL=approve / MENU=apply / LIVE=focus) | **interrupt agent** (STOP, send Escape) |

No double-tap (wearable misfire risk). OK long = "hold to stop" (thumb, deliberate).
The button state machine handles short/long duration thresholds (debounce; callbacks
stay non-blocking — repo invariant).

## 4. Architecture

### 4.1 Transports

| Path | Link | Status |
| --- | --- | --- |
| **A** | USB-Serial-JTAG (`/dev/usbserjtag` VFS, shared with the console) | verified on device |
| **B** | BLE NUS peripheral (LE Secure Connections, passkey bonding) | dropped |
| **C** | Wi-Fi STA + WebSocket client, mDNS discovery (`_pibuddy._tcp`) | verified on device |

**Path B is dropped for two independent reasons.** macOS 27.0 starves third-party
CoreBluetooth scan sessions (`AD:0(0/0)`), so the Mac-side central cannot scan; and on
this chip BLE cannot coexist with Wi-Fi at all — NimBLE costs ~73 KB of heap, which
starves `esp_wifi_init` on the 320 KB-SRAM C3 (measured: 97,596 B free at start,
24,388 B after `pibud_ble_init`, 764 B left for the Wi-Fi driver, which needs ~50 KB).
`CONFIG_BT_ENABLED=n`; the BLE sources were removed from the build and then deleted.

### 4.2 Firmware (device)
- Data channels: `pibud_usbc` (Path A) and `pibud_ws` (Path C). Both feed the same
  newline-JSON parser and the same app event queue, so the UI is transport-agnostic.
- Host-testable pure logic: `pibud_protocol*` (newline JSON), `pibud_state_reduce`
  (state machine), `pibud_text_layout`, `pibud_line` (bounded RX), `pibud_i4`,
  `pibud_format` (token units K/M/B/T and the HH:MM clock).
- `pibud_ui_render(snapshot)` under `bsp_lvgl_lock()`.
- `pibud_ws`: self-built SoftAP setup portal (`esp_http_server`, SSID
  `Pi-Buddy-Setup`) → credentials in our own NVS namespace → STA → mDNS browse →
  `esp_websocket_client` → app queue.
- Host-testable provisioning logic: `pibud_form` (urlencoded decoding) and
  `pibud_prov_html` (HTML escaping and the network picker).
- Repo rule: branch from `main`; UI fully redesigned (no demo test menu reuse).

### 4.3 Computer side (Go sidecar + pi extension)

The computer half is two pieces: a Go **sidecar** and a **pi extension**. They talk
through `~/.pi-buddy`, which the extension creates with mode 0700 — anything that can
write there can stop the agent.

```text
sidecar: read  ~/.pi-buddy/session-<pid>.json  <- the extension publishes it every second
         tail  the session JSONL               -> tool_use/toolResult/thinking/model_change/usage
         push  heartbeat JSON -> WS frame (Path C) or USB line (Path A), every 2s
         write ~/.pi-buddy/act.json            -> the extension runs the act
         read  ~/.pi-buddy/act-result.json     <- its answer
```

The extension publishes one state file per pi process, so several agents can run at
once; the sidecar follows the most recently updated one (`--pi-pid` pins one). A
state that stops being rewritten for 10s counts as offline, which is how a pi that
died is noticed.

Path C: the sidecar is the WebSocket **server** (`:51820`) and advertises
`_pibuddy._tcp` in pure Go, so discovery no longer depends on a platform-specific
helper — it used to shell out to macOS's `dns-sd`, which Linux does not have.
Upgrades without an `Origin` header are accepted and browser-originated ones are
rejected.

There is no `--target`: the extension knows its own session, so an act cannot land in
the wrong pane. `interrupt` calls pi's own `abort()`; approve/deny deliver the
configured text as a user message.

herdr is **optional**: pi spawns sub-agents in herdr panes, so when it is installed the
sidecar uses it for the sub-agent count. Nothing else depends on it — a missing herdr
is detected once at startup and the count reads as unknown.

The endpoint is not open to the LAN: the device must dial `/<link code>`, a 12-digit
hex code the sidecar generates once into `~/.pi-buddy/token` (reprint it with
`--show-token`). Anything else is refused with 401 before the upgrade, so an
unauthenticated peer never reaches the act handler. The code travels in the path over
plain `ws://`, so it closes the "any host on the network can connect" hole, not a
passive-sniffing one.

The sidecar also publishes its link state to `~/.pi-buddy/status.json`
(`connected`, `device`, `pid`, `updated`) so anything on the machine can display it
without IPC; the pid lets a reader tell a dead sidecar's stale file from a live link.

### 4.4 pi extension (computer)
`tools/pi-buddy-extension/pibuddy.ts` shows the link state in the pi footer by reading
`status.json`, and `/pibuddy` reports it and offers to copy the pair code to the
clipboard (macOS shares the clipboard with a nearby iPhone, which is where the code
has to be typed). Install by symlinking it into `~/.pi/agent/extensions/`.

## 5. Protocol contract (pi heartbeat, newline JSON; fields finalized in P1)
```json
{"cmd":"hb","model":"agnes-3.0-flash","state":"running",
 "tool":"read","arg":"main/buddy_state.c","result_ok":true,
 "sub_available":false,"sub_total":0,"sub_working":0,
 "tokens":18432,"in_tokens":12000,
 "out_tokens":6000,"cache_tokens":432,"tokens_7d":515000000,
 "tokens_30d":629000000,"cost":0.06}
{"cmd":"time","epoch":1790870019,"tz":28800}
{"cmd":"prompt","text":"...","waiting":true}
// device -> {"cmd":"act","kind":"approve"|"deny"|"interrupt"}
```
- The heartbeat `tokens` total covers **today (local day) across every session**,
  filtered per record by `timestamp`; `tokens_7d` / `tokens_30d` widen the same
  window sum to 7 and 30 days, so today ≤ 7d ≤ 30d. The sidecar sends a `time` line
  with every heartbeat for the status-bar clock.
- Token counts render with `pibud_format_tokens` (K/M/B/T, at most one decimal).
- `sub_available` tells the device whether the sub-agent count means anything. It is
  false whenever herdr is absent: the count comes from herdr, and pi's extension API
  exposes no equivalent. The device shows an unknown count as `--` in STATS and as an
  empty HOME line, rather than presenting zeroes as a reading.
- The device acknowledges each act: the action bar flips to `sending…` then
  `sent to your mac` (green) or `not sent - check the link` (red) for ~2.5s, driven
  by a `PIBUD_EVENT_ACT_RESULT` the transport injects after the USB/WS write.
- Bounded buffers: critical fields (id/tool) that exceed the cap reject the whole
  message; display fields truncate safely (buddy bounded-buffer policy).
- 30s without a snapshot → state machine marks stale, clears prompts, shows
  offline/sleep (buddy timeout logic).

## 6. Data sources (confirmed present)
- The session JSONL: live records. Fields: `message.role/content(api,provider,model,
  usage,stopReason)`, `tool_use`, `toolResult(toolName,isError)`, `thinking`,
  `model_change`. Its path comes from the published session state, then
  `$PI_SESSION_FILE`.
- `~/.pi-buddy/session-<pid>.json` (written by the pi extension): agent state, the
  session JSONL path, cwd and title. This replaces the herdr snapshot.
- `~/.pi-buddy/act.json` / `act-result.json`: the act channel to the extension.

## 7. Phases

> The P1–P4 rows record what each phase delivered at the time. §4.3 is the current
> architecture: the P1/P3 wording about herdr describes that phase, not today.

| Phase | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| **P1** | sidecar: read JSONL + `herdr api snapshot` → heartbeat JSON | host test + real-session print | done |
| **P2** | firmware: protocol + view UI (HOME/LIVE/STATS/MENU) + read-only board | build + host tests + on device | done |
| **P3** | device button → sidecar → `herdr agent send-keys` interrupt | on device: OK-long stops the agent | done over Path A and Path C |
| **P4** | MENU/APPROVAL overlay + wait-approve mapping | on device | deferred |

**MVP = P1+P2+P3, P4 deferred.**

### 7.1 Bugs found and fixed during P3 acceptance (all real, all in `pibud_ws.c`)
1. Missing `esp_netif_init()` / `esp_event_loop_create_default()` → every Wi-Fi netif
   helper aborted inside `ESP_ERROR_CHECK`, boot-looping the device before it ever
   reached provisioning.
2. `esp_ip4_addr_t.addr` is network byte order; hand-shifting it produced a
   byte-reversed host string, so the device dialled `242.145.168.192` instead of
   `192.168.145.242` and every connect timed out.
3. The already-provisioned path inherited `WIFI_MODE_NULL` from an earlier
   `esp_wifi_start()`, so the station never came up and `esp_wifi_connect()` was a
   no-op — the device never joined Wi-Fi on a normal boot.
4. Sidecar advertised `_pibuddy.tcp` instead of `_pibuddy._tcp`; that registration
   fails with `kDNSServiceErr_BadParam` (-65540), `dns-sd` exits 255, and the device
   had nothing to discover.

### 7.2 Defects found while replacing the provisioning path
- **The setup access point had no netif on the recovery path.** The AP netif was
  created only in the first-run branch, so a reopened portal brought the radio up
  without a DHCP server; clients associated and never got a lease.
- **A reopened portal could not scan.** The reconnect loop left the station in the
  connecting state, and the driver refuses to scan then, so the picker was empty
  exactly when it was needed. The portal now pauses the loop and disconnects before
  scanning, then resumes it.
- **The loop kept rescanning for a network it had already failed to join.** A
  scanning station shares the radio with the access point, which degrades the page
  the user is loading, so retries are capped while the portal is open.
- **`nvs_set_str` was handed fixed-size credential fields directly.** A
  maximum-length SSID fills the field with no terminator, so it could read past it.
- **The setup POST read the body with a single `httpd_req_recv`,** which is not
  guaranteed to return the whole body.
- **The link-code path was only built at init.** On the boot that provisions, the code
  is still missing when the interface initialises, so the client sent a request line
  with an empty target (`GET  HTTP/1.1`). A server rejects that with 400 before any
  handler runs, which is why the refusal never appeared in the sidecar's log. The path
  is now rebuilt whenever the code is loaded or saved.

## 8. Test matrix
- **Host tests:** protocol parse, state-machine reduce, text wrap/clip, feed-line
  formatting, urlencoded form decoding, and provisioning-page HTML escaping (all
  pure logic, decoupled from ESP-IDF/LVGL); sidecar JSONL parse, the published
  session state, and the act channel.
- **Device tests:** 240×320 per-view/per-line/long-string rendering, watchdog-free
  soak, bonding, OK-long interrupt.
- **Delivery four fields:** report `Build / Host tests / Device tests / Unverified`
  separately; after each complete firmware change, proactively offer flashing (needs
  approval).

## 9. Open items / assumptions
1. **Q1 default landing = HOME** — confirmed.
2. **Q2 dark pages carry the pi mark** — resolved; the same mosaic, 24px, top-right.
3. **Provisioning UX — implemented.** `wifi_prov_mgr`'s SoftAP scheme ships no web
   form, so it needed `esp_prov.py` on a computer, and joining `Pi-Buddy-Setup` cost
   that computer its network. The device now runs its own captive portal: the scan
   fills a network picker plus a free-text field for hidden networks, an unknown path
   answers with a redirect so phones offer the sign-in sheet, credentials go to our
   own NVS namespace, and the portal closes as soon as the station is online. Local
   first-run only, so the AP is open and the form is plain HTTP. If the station
   cannot join, the portal reopens after about 30 s so a mistyped password cannot
   lock the user out; while it is open the station stops retrying, because a scanning
   station shares the radio with the access point. The password field has a Show toggle,
   since the code is typed once from a phone keyboard and a typo costs a whole retry.
4. **WS endpoint authentication — implemented.** The endpoint listens on the whole
   LAN and can run the device's acts, so any host could stop the user's agent. The
   device now dials `/<link code>` and everything else is
   refused before the upgrade; the code is generated once, kept in `~/.pi-buddy/token`
   (mode 600) and typed into the setup page, where `pibud_token` accepts the
   separators and capitalisation a phone keyboard introduces. A refused handshake
   reopens the setup portal after three tries, while an unreachable sidecar does not —
   an open access point must not appear merely because the Mac is asleep.
5. **mDNS instance name** — the registration is `pibuddy-<port>-<host hash>`, so a
   second sidecar no longer competes for the name and steals the device. It is made
   with a Go mDNS library, not `dns-sd`, so Linux and Windows work too.
6. **Interrupt** — OK-long calls the pi extension's `abort()`; verified on device.
7. **Assumption:** pi "approve" is generic remote input (no structured permission
   dialog) — confirmed.
8. **Sub-agent count — optional, from herdr.** pi spawns sub-agents in herdr panes, so
   the sidecar still asks herdr for the count when it is installed; pi's extension API
   has no equivalent. A missing herdr is detected once at startup and the count reads
   as unknown, so nothing depends on it.
9. **The act channel is a plain directory.** `~/.pi-buddy` holds `act.json` and
   `session-<pid>.json`, so any local process that can write there can stop the
   agent. The extension creates the directory 0700; the sidecar does not re-tighten
   a directory that already exists.

## 10. Validation & delivery gate
- Iterate with `./tools/validate.sh --static`; deliver with `./tools/validate.sh`
  (needs an activated ESP-IDF 5.5.3).
- Last recorded gate for the Path C delivery: **Build PASS, Host tests PASS, Device
  tests PASS** (display of live session data + OK-long interrupt reaching the pinned
  pane over Wi-Fi), no unverified board or instrument checks outstanding.
- A successful build ≠ hardware acceptance; flashing needs approval; no pre-flash
  backup of original firmware, no default full-chip erase.
- Commit/push only when requested or when the workflow requires it.
