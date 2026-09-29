<p align="right">
  <a href="HANDOFF.md">English</a> · <strong>简体中文</strong>
</p>

# Pi Agent Buddy — 交接文档

> 分支内工作交接，`feature/pi-agent-buddy`。供新会话直接接手、免重新摸上下文。

## 1. 这是什么
一块贴身的**编码 agent 状态看板 + 轻遥控器**（对接 pi 会话）。设备是 BLE NUS
外设（LE Secure Connections、六位配对码、绑定）；电脑侧 Go sidecar 观察 pi
会话 + 驱动 herdr。设备 6 视图 + 3 键控制（含中断）。

## 2. git 状态
- 分支：`feature/pi-agent-buddy`（从 `main` 拉出）。
- 工作树有**未提交**的 pibud 改动（固件模块、sidecar、文档、`tools/validate.sh`、
  `sdkconfig.defaults`）。除非要求，不要 commit。
- 之前的 TOTP 应用已删除，勿恢复。

## 3. 已完成 + 已验证
- **P1 sidecar** `tools/pi-buddy-sidecar/`（Go）：读 `$PI_SESSION_FILE`（JSONL）
  + `herdr api snapshot` → 输出一条 heartbeat JSON。活会话验证过；`go test` 过。
  还没做 BLE/act。
- **P2 固件** `main/pibud_*`：types、state（reducer）、protocol（cJSON）、
  text-layout、i4、line、BLE（NimBLE NUS+SC+绑定，移植自 claude-buddy）、
  6 视图 LVGL UI、`app_main` 接线。`idf.py build` + 合并 0x0 镜像 **PASS**；
  pibud host 测试 **PASS**。
- **环境**：ESP-IDF 5.5.3 在 `~/esp/esp-idf-v5.5.3`（target esp32c3）。

## 4. 未完成（按顺序）
1. **P3 sidecar** — BLE central（连 `Pi-*`，NUS 发 heartbeat / 收 `act`）+
   herdr 控制映射（`act` → `herdr agent send-keys`/`prompt`；OK 长按=Escape
   中断）。这步打通整条回路。
2. **上真机** — 刷机、BLE 配对、UI 渲染、OK 长按中断、soak。
3. **P4** — settings NVS 持久化、MENU/PAIRING 打磨、深色页 pi 三色点缀（Q2）。

## 5. 命令
```bash
# 环境（每个新 shell）
export DEVELOPER_DIR=/Library/Developer/CommandLineTools   # 持久: sudo xcode-select -s /Library/Developer/CommandLineTools
source ~/esp/esp-idf-v5.5.3/export.sh

# host 测试
DEVELOPER_DIR=/Library/Developer/CommandLineTools ./tools/validate.sh --static

# 固件构建（合并 0x0 镜像在 build/FoloToy-AI-Passport-full.bin）
./tools/validate.sh            # 完整门槛；或: idf.py -B /tmp/pibud_build build

# sidecar（P1）— 打印当前 pi 会话的 heartbeat
cd tools/pi-buddy-sidecar && go run .
go test ./...

# 刷机（交接时探测到设备 /dev/cu.usbmodem1101）— 需批准
python -m esptool --chip esp32c3 -p /dev/cu.usbmodem1101 -b 460800 \
  write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

## 6. 本机环境注意（这台 Mac）
- macOS 27.0 + Xcode 26.6：27.0 SDK 的 `.tbd` 用 `arm64e.x1`，旧链接器
  `ld-1267` 解析不了，默认 `cc` 链接失败。**解法：用 CLT 工具链**（新
  `ld-27037.1`）：`export DEVELOPER_DIR=/Library/Developer/CommandLineTools`
  （或 `sudo xcode-select -s /Library/Developer/CommandLineTools`）。兜底：
  `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`。
- ESP-IDF：大子模块 `esp_wifi/lib` 已按锁定 commit 浅取修复（已完成；IDF 仓库
  `git status` 干净）。

## 7. 已定决策
- 6 视图：HOME（pi.dev 风格：羊皮纸+等宽+三色 logo）/ LIVE / STATS / MENU +
  APPROVAL / PAIRING 弹层。
- 3 键：UP 短=上滚、UP 长=切视图；DN 短=下滚/跟随、DN 长=跳最新；OK 短=确认/
  激活、OK 长=**中断 agent**。
- "批准" = pi 通用远程输入（`herdr agent prompt`/`send-keys`），非结构化权限弹窗。
- 固件复用 claude-buddy 架构（BLE NUS + LE SC + 换行 JSON + 纯状态机），UI 按
  pi.dev 风格 mockup 重做。

## 8. 待拍板假设
- Q2：深色数据页要不要点缀 pi 三色，还是保持纯功能？
- sidecar BLE 库：CoreBluetooth(cgo) vs 纯 Go `ble`。
- 中断键序列：纯 Escape vs 组合（P3 上机定）。
- token 统计：窗口累计（最近 ~50 条）vs 会话总量。

## 9. 文件清单
- 固件：`main/pibud_{types,state,protocol,text_layout,i4,line,ble,ble_store,ble_lifecycle,ui,app}.{c,h}`
- sidecar：`tools/pi-buddy-sidecar/{go.mod,main.go,event.go,event_test.go}`
- 文档：`docs/pi-agent-buddy/DESIGN{,.zh_CN}.md`、本交接
- 门槛：`tools/validate.sh`（注册 pibud host 测试；段 GC 链接标志已做平台自适应）
- 配置：`sdkconfig.defaults`（BT NimBLE SM_SC + NVS 持久化 已开）
