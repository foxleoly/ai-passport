<p align="right">
  <a href="HANDOFF.md">English</a> · <strong>简体中文</strong>
</p>

# Pi Agent Buddy — 交接文档

> 分支内工作交接，`feature/pi-agent-buddy`。供新会话直接接手、免重新摸上下文。

## 1. 这是什么

一块贴身的**编码 agent 状态看板 + 轻遥控器**（对接 pi 会话）。设备是 ESP32-C3
板，通过 **Wi-Fi + WebSocket**（免线）或 **USB-Serial-JTAG**（有线）连接电脑侧
Go sidecar；电脑侧还有 pi 扩展，扩展发布会话状态并通过 pi 自身的扩展 API 执行设备动作。设备显示 4 视图看板（HOME / LIVE /
STATS / MENU），3 键控制（含中断）。

BLE 已**移除**：macOS 27.0 会让第三方 CoreBluetooth 扫描饿死，且 NimBLE 要占
~73 KB 堆，这颗 320 KB SRAM 的 C3 上再放不下 `esp_wifi_init`。推断与实测数据见
`DESIGN.zh_CN.md` §4.1。

Wi-Fi 配网是自建 captive portal，不再用 `wifi_prov_mgr`：手机连 `Pi-Buddy-Setup`，
从扫描出的列表里选网络，只需输密码。细节见 `DESIGN.zh_CN.md` §9.3。

## 2. git 状态

- 分支：`feature/pi-agent-buddy`（从 `main` 拉出），**领先 `main` 17 个 commit**。
- 工作树还有未提交改动（除非要求，不要 commit）：UI 这一轮（logo、三色条、
  1d/7d/30d、状态条、STATS 拆分、动作反馈）、新增 `main/pibud_format.{c,h}` +
  `tests/test_pibud_format.c`、token 范围（`event.go` / `main.go` 的
  `windowTotals`）、链接码修正、电脑侧重写（pi 扩展成为桥；herdr 变为可选）、
  固件的 `sub_available` 字段，以及四份文档（`DESIGN{,.zh_CN}.md`、
  `HANDOFF{,.zh_CN}.md`）。vendored 的 `components/esp_websocket_client` +
  `components/mdns` 与 `sdkconfig.defaults` 已提交。
- **电脑侧工具也已发布**，而本仓库的 `tools/pi-buddy-sidecar` +
  `tools/pi-buddy-extension` 仍是它的源：`github.com/foxleoly/pi-agent-buddy`
  （MIT，`v0.2.0` 带 CI 构建的五平台二进制）。这两个目录有改动就拷过去提交。
- `main/pibud_ble*.{c,h}` 已删除；BLE 代码不复存在，勿恢复。
- 之前的 TOTP 应用已删除，勿恢复。

## 3. 已完成 + 已验证

- **P1 sidecar** `tools/pi-buddy-sidecar/`（Go）：读 pi 会话 JSONL 加 pi 扩展发布的
  状态，输出一条 heartbeat JSON；Path C 模式是 WebSocket
  服务端 + mDNS 广播；设备动作经 `~/.pi-buddy/act.json` 回到扩展。`go test` 通过。
- **P2 固件** `main/pibud_*`：types、state reducer、protocol（cJSON）、
  text-layout、i4、line、`pibud_usbc`（Path A）、`pibud_ws`（Path C）、LVGL UI、
  `app_main` 接线。构建与 host 测试 **PASS**。
- **P3 在真机上按 Path C 验收通过：** 设备用已配网凭据入网、经 mDNS 发现
  sidecar、渲染实时会话数据，且 OK 长按打到钉死的 pane：

  ```text
  WS: device connected 192.168.145.74:54363
  device act: herdr agent send-keys w4:p1 esc -> w4:p1
  act ok: {"id":"cli:agent:send-keys","result":{"type":"ok"}}
  ```

- **配网门户（真机验证通过）。** 设备自带 captive portal（SSID `Pi-Buddy-Setup`），
  页面里的网络列表来自扫描；手机走完了整个流程，日志显示完整链路：
  `setup form: ssid "..."` → `credentials saved` → `sta ip: ...` →
  `setup access point closed` → `websocket connected`。取代了 `wifi_prov_mgr` 与
  `esp_prov.py`，且全程不影响 Mac 的网络。
- **链接码（已实现）。** WebSocket 端点不再对局域网开放：设备走 `/<链接码>`，其他
  一律在升级前被 401 拒绝。链接码只生成一次到 `~/.pi-buddy/token`（权限 600），可
  用 `--show-token` 重复打印，并由用户在配网页输入（该字段容忍手机键盘带来的分隔
  符与大小写）。握手被拒三次会重开配网门户；连不上则不会。
- **pi 扩展。** `tools/pi-buddy-extension/pibuddy.ts`（软链到
  `~/.pi/agent/extensions/`）会从 sidecar 发布的 `~/.pi-buddy/status.json` 读取链路
  状态并显示在 pi 底部状态栏，`/pibuddy` 汇报状态并提供把配对码复制到剪贴板。
- **看板 UI 一轮（P4，已做）。** HOME 有了 40px pi 马赛克标志、一条圆角的三色
  运行进度条，以及 1d / 7d / 30d token 累计；STATS 把 token 拆成 total / input /
  output / cache（各带占比）再加 cost / subs / tool / branch；状态条显示 模型 ·
  时钟 · 活动圆点 · WiFi · 电量；MENU 精简为 亮度（显示当前 %）+ 恢复出厂；动作
  条会反馈动作（`sending…` / `sent to your mac` / `not sent - check the link`）。
  token 用 K/M/B/T 单位；统计范围是今天（本地日）跨所有 session，并放宽到 7d/30d
  窗口。亮度持久化在 NVS，恢复出厂真正生效。
- **herdr 两态，真机验证。** 装了 herdr 时设备显示实时子代理数（`4 subagents (1 work)`，
  STATS `4/1`）；传 `--herdr ""` 时 HOME 那行留空、STATS 显示 `--`。验证了两次：一次在
  线上直接读 WebSocket 帧（`sub_available:true/false`，未知时 `sub_total` 是**键不存在**
  而不是 0），一次在屏幕上。同一次验证也确认了 `abort()` 能停住 agent。
- **已发布。** 电脑侧工具：`github.com/foxleoly/pi-agent-buddy`（MIT，`v0.2.0`，五平台
  二进制由它自己的 CI 构建）。社区玩法：**Pi Agent Buddy**，id 857——已公开（修订 1796
  已通过）；写这份文档时修订 1871 正在审核。
- **门槛：** `./tools/validate.sh --static` PASS；`--firmware` PASS（合并镜像已
  校验）；`go vet` + `go test -count=1 ./...` **无需任何 env 变通**即 PASS（见
  §6）。
- **环境：** ESP-IDF 5.5.3 在 `~/esp/esp-idf-v5.5.3`（target `esp32c3`）；Xcode
  license 问题已修复（`xcode-select` 现指向 `/Library/Developer/CommandLineTools`）。

## 4. 未完成（按顺序）

1. **发布助手的截屏协议。** 当前 FoloToy 发布助手说明：通过它发布的固件必须实现
   `FAP_SCREENSHOT_V1`——一个串口帧缓冲截屏（PNG 或 RGB565LE），且 `validate` /
   `submit` 要配套的 capture 收据。本固件没有实现，所以封面只能用绘制的示意图。
   240×320 RGB565 是 150 KB，而可用堆只有约 45 KB，所以不能整屏快照：分带渲染
   （首选）或读面板 GRAM。
2. **社区玩法 857** 已公开，其中修订 1871 正在审核。它的封面还是旧的合成示意图；
   要换掉，得等那个修订通过后从官网再取一次性更新授权。
3. **真正的 LIVE 滚动。** LIVE 现在是设备端由 heartbeat 的 `tool`/`arg` 拼出的
   最近工具文本，不是 transcript 行。真滚动需要扩协议：sidecar 下发有界的
   transcript 行，设备负责渲染 + 滚动。
4. **MENU 打磨 + APPROVAL 弹层。** 亮度（带当前 %）与恢复出厂已完成；P4 剩下的
   是批准弹层（WAITING + ALLOW/DENY，`pibud_ui.c` 里已接了一半）和 MENU 细化。
5. **扩展没有入库测试。** 它的逻辑是手工验证的，本仓库还没有 JavaScript 测试约定，
   所以门槛里没有它。
6. **已知 `status.go` 局限：** 瞬时客户端可能在 `status.json` 里留下陈旧的
   `device` 地址（只记录最后打开过的地址）。

## 5. 命令

```bash
# 环境（每个新 shell）：Xcode 已指向 CLT（§6），无需 DEVELOPER_DIR。
source ~/esp/esp-idf-v5.5.3/export.sh

# host 测试
./tools/validate.sh --static

# 完整门槛（构建 + 合并镜像 + host 测试）
./tools/validate.sh

# sidecar：构建并测试（Xcode 修好后不再需要 CGO env，见第 6 节）
cd tools/pi-buddy-sidecar
go build -o /tmp/pi-buddy-sidecar .
go test ./...

# sidecar：跑免线路径。不需要 --target：pi 扩展知道自己的会话，动作不会打到
# 别的 pane。
/tmp/pi-buddy-sidecar --ws --ws-addr :51820

# 打印设备配网页要的链接码（首次 --ws 运行时生成）
/tmp/pi-buddy-sidecar --show-token

# 刷机：只刷 app 分区，保留 NVS 里的 Wi-Fi 凭据。需批准。
# 注意：./tools/validate.sh --firmware 在临时目录构建，只把合并镜像装回 build/，
# 所以 build/FoloToy-AI-Passport.bin 是上一次普通 `idf.py build` 的产物，可能更旧。
# 请刷归档里的 app 镜像，或先跑一次 `idf.py build`。
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  write_flash 0x10000 build/firmware/<sha256>/FoloToy-AI-Passport.bin
```

配网（或重新配网）在手机浏览器里完成：连 `Pi-Buddy-Setup`，打开
`http://192.168.4.1/`，从列表里选网络，输密码，保存。设备没有已存凭据时门户会
出现；连续连不上约 30 秒后它会自行重开，所以密码输错也能不插线救回来。全程不
需要电脑，也不需要 `esp_prov.py`。

## 6. 本机环境注意（这台 Mac）

- **已修复：** `xcode-select -p` = `/Library/Developer/CommandLineTools`，且 Xcode
  license 已接受，所以 `cc`、`go vet`、`go test`、`go build` 都不再需要
  `DEVELOPER_DIR` / `CGO_CFLAGS` 变通。历史原因：27.0 SDK 的 `.tbd` 引用
  `arm64e.x1`（旧链接器解析不了），且 Xcode license 之前未接受 —— 这就是这台
  Mac 用 CLT 工具链的两个原因。若将来更新 Xcode 后又被切回 Xcode.app，跑
  `sudo xcode-select -s /Library/Developer/CommandLineTools`（或
  `sudo xcodebuild -license accept`）。
- ESP-IDF：大子模块 `esp_wifi/lib` 已按锁定 commit 浅取修复；IDF 仓库本身干净。

## 7. 已定决策

- 视图：HOME（pi.dev 风格——羊皮纸、等宽、三色马赛克 logo）、LIVE、STATS、
  MENU。BLE 配对弹层已不存在。
- 3 键：UP 短=上滚、UP 长=切视图；DN 短=下滚/跟随、DN 长=跳最新；OK 短=确认/
  激活、OK 长=**中断**。
- "批准" = pi 通用远程输入（经 pi 扩展发一条用户消息），非结构化权限
  弹窗。
- 电脑（Mac）是有线/无线链路的主机，手机不是。
- Wi-Fi 凭据存在自建 `pibud` NVS 命名空间，绝不入库；Wi-Fi 驱动自带的 NVS 持久
  化已关闭，保证只有一个数据源。
- 配网就是上面那个自建 captive portal；`wifi_prov_mgr`、`protocomm` 与
  `esp_prov.py` 都不再参与流程。

## 8. 待拍板假设

- Q2：已定 —— 深色数据页右上角放 24px 的 pi 标志。
- token 统计：已定 —— 今天（本地日）所有 session 的累计，按每条记录的 timestamp
  过滤，并经 `windowTotals` 放宽到 7d/30d 窗口。
- 中断键序列是 Escape（`send-keys <pane> esc`）——确认这是用户对"停止"的预期。

## 9. 文件清单

- 固件：`main/pibud_{types,state,protocol,text_layout,i4,line,form,prov_html,format,usbc,ws,ui,app}.{c,h}`
- sidecar：`tools/pi-buddy-sidecar/{go.mod,main.go,event.go,session.go,act.go,usb.go,ws.go,token.go,status.go}`
  （纯 Go、无 cgo：已废弃的 Path B BLE central 已删除，任意平台均可构建）
- 扩展：`tools/pi-buddy-extension/pibuddy.ts`（软链到 `~/.pi/agent/extensions/`）
- vendored 组件：`components/esp_websocket_client`、`components/mdns`
- 文档：`docs/pi-agent-buddy/DESIGN{,.zh_CN}.md`、本交接
- 门槛：`tools/validate.sh`（已注册 pibud host 测试）、`tools/check_repo.py`
  （已登记 vendored 文档根目录）
- 配置：`sdkconfig.defaults`（`CONFIG_BT_ENABLED=n`，Wi-Fi + WS transport +
  protocomm SECURITY_0）
