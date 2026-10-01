<p align="right">
  <a href="HANDOFF.md">English</a> · <strong>简体中文</strong>
</p>

# Pi Agent Buddy — 交接文档

> 分支内工作交接，`feature/pi-agent-buddy`。供新会话直接接手、免重新摸上下文。

## 1. 这是什么

一块贴身的**编码 agent 状态看板 + 轻遥控器**（对接 pi 会话）。设备是 ESP32-C3
板，通过 **Wi-Fi + WebSocket**（免线）或 **USB-Serial-JTAG**（有线）连接电脑侧
Go sidecar；sidecar 观察 pi 会话并驱动 herdr。设备显示 4 视图看板（HOME / LIVE /
STATS / MENU），3 键控制（含中断）。

BLE 已**移除**：macOS 27.0 会让第三方 CoreBluetooth 扫描饿死，且 NimBLE 要占
~73 KB 堆，这颗 320 KB SRAM 的 C3 上再放不下 `esp_wifi_init`。推断与实测数据见
`DESIGN.zh_CN.md` §4.1。

## 2. git 状态

- 分支：`feature/pi-agent-buddy`（从 `main` 拉出）。
- 工作树有未提交的 pibud 改动：固件模块、sidecar、文档、`tools/validate.sh`、
  `tools/check_repo.py`、`sdkconfig.defaults`、`main/CMakeLists.txt`，以及
  vendored 的 `components/esp_websocket_client` + `components/mdns`。除非要求，
  不要 commit。
- `main/pibud_ble*.{c,h}` 已删除；BLE 代码不复存在，勿恢复。
- 之前的 TOTP 应用已删除，勿恢复。

## 3. 已完成 + 已验证

- **P1 sidecar** `tools/pi-buddy-sidecar/`（Go）：读 `$PI_SESSION_FILE`（JSONL）
  与 `herdr api snapshot`，输出一条 heartbeat JSON；Path C 模式是 WebSocket
  服务端 + mDNS 广播（`dns-sd -R`，带守护重注册）；设备动作映射到
  `herdr agent send-keys` / `prompt`。`go test` 通过。
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

- **门槛：** `./tools/validate.sh --static` PASS；`--firmware` PASS（合并镜像已校验）。
- **环境：** ESP-IDF 5.5.3 在 `~/esp/esp-idf-v5.5.3`（target `esp32c3`）。

## 4. 未完成（按顺序）

1. **配网 UX。** `wifi_prov_mgr` 的 SoftAP 方案不带网页表单，所以配 Wi-Fi 得用
   Mac 上的 `esp_prov.py`，而连上 `Pi-Buddy-Setup` 会让 Mac 断网。改成自建
   captive portal（`esp_http_server`），让手机就能配网、Mac 保持在线。见
   `DESIGN.zh_CN.md` §9.3。
2. **WebSocket 鉴权（安全）。** 该端点监听整个局域网、无认证，且能执行
   `herdr agent send-keys`——网内任何主机都能往用户的 agent pane 注入按键。
   最小修法：共享 token（设备 NVS + sidecar 参数）。见 `DESIGN.zh_CN.md` §9.4。
3. **mDNS 实例名唯一化。** 现在硬编码为 `pibuddy`，两个 sidecar 同时广播会让
   设备任选一个。
4. **P4** — settings NVS 持久化、MENU 打磨、批准弹层、深色页 pi 三色点缀（Q2）。

## 5. 命令

```bash
# 环境（每个新 shell）
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
source ~/esp/esp-idf-v5.5.3/export.sh

# host 测试
./tools/validate.sh --static

# 完整门槛（构建 + 合并镜像 + host 测试）
./tools/validate.sh

# sidecar：构建（CGO 需要 26.5 SDK，见第 6 节）
cd tools/pi-buddy-sidecar
CGO_ENABLED=1 go build -o /tmp/pi-buddy-sidecar .
go test ./...

# sidecar：跑免线路径。务必带 --target；不带时动作会打到当前聚焦的 herdr
# pane，而那可能正是跑 sidecar 的这个会话。
/tmp/pi-buddy-sidecar --ws --ws-addr :51820 --target w4:p1

# 刷机：只刷 app 分区，保留 NVS 里的 Wi-Fi 凭据。需批准。
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  write_flash 0x10000 build/FoloToy-AI-Passport.bin
```

给设备配新网络（设备无凭据时会起 SoftAP `Pi-Buddy-Setup`）：

```bash
export IDF_PATH=~/esp/esp-idf-v5.5.3
python $IDF_PATH/tools/esp_prov/esp_prov.py --transport softap \
  --service_name 192.168.4.1 --sec_ver 0 --ssid "<ssid>" --passphrase "<pass>"
```

`esp_prov.py` 需要 `protobuf` 与 `cryptography`，而 ESP-IDF 的 Python 环境并不
提供；请装进一次性虚拟环境，别动共用的那个。

## 6. 本机环境注意（这台 Mac）

- macOS 27.0 + Xcode 26.6：27.0 SDK 的 `.tbd` 引用 `arm64e.x1`，旧链接器解析
  不了，默认链接会失败。用 CLT 工具链：
  `export DEVELOPER_DIR=/Library/Developer/CommandLineTools`。CGO 构建（sidecar
  要链 CoreBluetooth）还要加
  `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`。
- ESP-IDF：大子模块 `esp_wifi/lib` 已按锁定 commit 浅取修复；IDF 仓库本身干净。

## 7. 已定决策

- 视图：HOME（pi.dev 风格——羊皮纸、等宽、三色马赛克 logo）、LIVE、STATS、
  MENU。BLE 配对弹层已不存在。
- 3 键：UP 短=上滚、UP 长=切视图；DN 短=下滚/跟随、DN 长=跳最新；OK 短=确认/
  激活、OK 长=**中断**。
- "批准" = pi 通用远程输入（`herdr agent prompt` / `send-keys`），非结构化权限
  弹窗。
- 电脑（Mac）是有线/无线链路的主机，手机不是。
- Wi-Fi 凭据经配网存入设备 NVS，绝不入库。

## 8. 待拍板假设

- Q2：深色数据页要不要点缀 pi 三色，还是保持纯功能？
- 中断键序列是 Escape（`send-keys <pane> esc`）——确认这是用户对"停止"的预期。
- token 统计：窗口累计（最近约 50 条）还是会话总量？

## 9. 文件清单

- 固件：`main/pibud_{types,state,protocol,text_layout,i4,line,usbc,ws,ui,app}.{c,h}`
- sidecar：`tools/pi-buddy-sidecar/{go.mod,main.go,event.go,herdr.go,usb.go,ws.go,ble_central.m}`
  （`ble.go` / `ble_central.m` 是已废弃的 Path B central，仅留作参考）
- vendored 组件：`components/esp_websocket_client`、`components/mdns`
- 文档：`docs/pi-agent-buddy/DESIGN{,.zh_CN}.md`、本交接
- 门槛：`tools/validate.sh`（已注册 pibud host 测试）、`tools/check_repo.py`
  （已登记 vendored 文档根目录）
- 配置：`sdkconfig.defaults`（`CONFIG_BT_ENABLED=n`，Wi-Fi + WS transport +
  protocomm SECURITY_0）
