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

- **In scope:** firmware (BLE NUS peripheral + LE bonding + newline-JSON protocol +
  state machine + 6 views + 3 buttons) and a computer **sidecar** (Go CLI, the BLE
  central) that observes the pi session and drives herdr.
- **Out of scope / non-goals:** not a security token or passkey credential holder;
  not an e-pet (the mascot is a 16px status avatar, not a 132px pet stage); does not
  use NFC (NTAG213 has no MCU API); "approve" = pi's **generic remote input**
  (`herdr agent prompt` / `send-keys`), not a structured permission dialog.

## 2. Screen design (use the 240×320 fully)

Pixel budget (8px mono, ~30 cols; top status + bottom action bars persistent):

| Region | Height | Content |
| --- | --- | --- |
| Status bar | ~22px | model (L) · state chip + 16px mascot (C) · battery (top-right, repo invariant) |
| Main | ~256px | view content |
| Action bar | ~22px | key hints, context-sensitive |

### 2.1 Six views
1. **HOME / overall** (default landing) — pi.dev style: warm parchment, mono type,
   tri-color mosaic logo, global status summary.
2. **LIVE** (default board) — status bar + ~15-line live activity feed + focus strip
   (current tool full text + tokens/cost) + action bar.
3. **STATS** — model / tokens / cost / uptime / subagents / errors / last tool /
   branch / state.
4. **MENU** — BLE / brightness / follow-live / sound / clock / transcript / unpair /
   factory reset.
5. **PAIRING** (overlay) — 6-digit passkey centered.
6. **APPROVAL** (overlay) — when the agent awaits input: WAITING banner + the awaited
   prompt + ALLOW/DENY.

`APPROVAL` / `PAIRING` are **state-driven overlays** (appear automatically), not
cycled views; `HOME/LIVE/STATS/MENU` are cycled by the view gesture.

### 2.2 Activity feed lines (color-coded)
| Line | Source | Color |
| --- | --- | --- |
| `read main/x.c` | `tool_use` (name + first arg) | ok = green |
| `✓ / ✗ bash` | `toolResult.isError` | green / red |
| `thinking …` | `thinking` block | yellow |
| `[model] agnes→…` | `model_change` | cyan |
| `scout-1 running` | herdr agent snapshot | blue |

Auto-follows the latest; bounded ~48-line scrollback (no PSRAM → RAM budget).

### 2.3 pi brand tokens (extracted from pi.dev)
```
parchment  #F3F2F0 / #EBE7E4   (HOME / brand pages only; data pages stay dark)
ink        #252F3D  muted #5C5752
logo       coral #F09082 / steel #4D9ABF / amber #F1BE58 (accent #6A9FCC)
type       monospace
```
HOME = warm parchment + mono + the three-color mosaic (re-drawn 1:1 from the official
SVG paths). Whether to accent dark pages with the tri-color is open (Q2, §9).

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

### Firmware (device) — ~80% reuse of claude-buddy-port
- ✅ Port: `buddy_ble*` (NUS + LE encryption + 6-code bonding), `buddy_protocol*`
  (newline JSON, host-testable), `buddy_state_reduce` (pure state machine),
  `buddy_text_wrap`, `buddy_ui_render(snapshot)` + LVGL lock pattern.
- ✏️ Redo: layout/typography (§2), mascot (single pi avatar + states), protocol fields
  (Claude → pi concepts).
- Repo rule: branch from `main`, extract buddy as a **reference pattern**, do not
  merge the demo branch; UI fully redesigned (no demo test menu).

### Sidecar (computer, Go CLI, BLE central)
```
read: tail $PI_SESSION_FILE (JSONL)   -> tool_use/toolResult/thinking/model_change/usage
      herdr api snapshot              -> agent lifecycle + subagents
push: compose heartbeat JSON -> NUS RX(0002)
recv: NUS TX(0003) device act -> herdr agent send-keys / prompt
```
BLE via NUS GATT; macOS central via CoreBluetooth (Go `ble`/`bleat` or cgo). Control
goes through `herdr agent send-keys|prompt` (CLI confirmed); do not speak raw
`herdr.sock` protocol.

## 5. Protocol contract (pi heartbeat, newline JSON; fields finalized in P1)
```json
{"cmd":"hb","model":"agnes-3.0-flash","state":"running",
 "tool":"read","arg":"main/buddy_state.c","result_ok":true,
 "sub_total":4,"sub_working":1,"tokens":18432,"cost":0.06}
{"cmd":"prompt","text":"...","waiting":true}
// device -> {"cmd":"act","kind":"approve"|"deny"|"interrupt"}
```
- Bounded buffers: critical fields (id/tool) that exceed the cap reject the whole
  message; display fields truncate safely (buddy bounded-buffer policy).
- 30s without a snapshot → state machine marks stale, clears prompts, shows
  offline/sleep (buddy timeout logic).

## 6. Data sources (confirmed present)
- `$PI_SESSION_FILE`: live JSONL. Fields: `message.role/content(api,provider,model,
  usage,stopReason)`, `tool_use`, `toolResult(toolName,isError)`, `thinking`,
  `model_change`.
- `herdr api snapshot`: live agent snapshot (status, subagents, JSONL path).
- `herdr agent send-keys` / `herdr agent prompt`: control injection points.

## 7. Phases
| Phase | Deliverable | Acceptance |
| --- | --- | --- |
| **P1** | sidecar prototype: read JSONL + `herdr api snapshot` → emit heartbeat JSON (print first, BLE later) | host test + real-session print |
| **P2** | firmware pi-buddy: BLE NUS + protocol + 6-view UI + read-only LIVE/HOME | Build + host tests + on-device |
| **P3** | device button → sidecar → `herdr agent send-keys` interrupt | on-device: OK long actually stops the agent |
| **P4** | MENU/PAIRING/APPROVAL + waiting approve mapping | on-device |

**MVP = P1+P2+P3, P4 deferred.**

## 8. Test matrix
- **Host tests:** protocol parse, state-machine reduce, text wrap/clip, feed-line
  formatting (all pure logic, decoupled from ESP-IDF/LVGL); sidecar JSONL parse +
  herdr call wrapping.
- **Device tests:** 240×320 per-view/per-line/long-string rendering, watchdog-free
  soak, bonding, OK-long interrupt.
- **Delivery four fields:** report `Build / Host tests / Device tests / Unverified`
  separately; after each complete firmware change, proactively offer flashing (needs
  approval).

## 9. Open items / assumptions
1. **Q1 default landing = HOME** (brand feel + at-a-glance global state) — confirmed.
2. **Q2 accent dark pages with pi tri-color** (state chip uses steel/amber) or keep
   dark pages purely functional with only HOME as the brand page — open.
3. **sidecar BLE lib:** CoreBluetooth (cgo) vs pure-Go `ble` — decide in P2.
4. **interrupt key sequence:** what exactly OK-long sends (plain Escape vs combo) —
   finalize in P3 on hardware.
5. **Assumption:** pi "approve" is generic remote input (no structured permission
   dialog) — confirmed.

## 10. Validation & delivery gate
- Iterate: `./tools/validate.sh --static`; deliver: `./tools/validate.sh` (needs
  activated ESP-IDF 5.5.3).
- A successful build ≠ hardware acceptance; flashing needs approval; no pre-flash
  backup of original firmware, no default full-chip erase.
- Commit/push only when requested or when the workflow requires it.
