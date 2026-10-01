<p align="right">
  <a href="DESIGN.md">English</a> · <strong>简体中文</strong>
</p>

# Pi Agent Buddy — 设计与实施计划（分支内工作文档）

> 分支：`feature/pi-agent-buddy`（从 `main` 拉出）。这是分支内规划草稿，不是
> 上游维护的文档；请随构建结果保持更新。
> 界面 mockup：`/tmp/pi-buddy-mockup/`（HOME 视图已按 pi.dev 风格确认）。

## 1. 产品

一块贴身的**编码 agent 状态看板 + 轻遥控器**。看 pi 正在写什么 / 卡在哪 / 花了
多少 token；一个按键就能停掉当前运行。

- **范围内：** 固件（240×320 LVGL 看板 + 换行 JSON 协议 + 状态机 + 视图 + 3 键）
  与电脑侧 **sidecar**（Go CLI），后者观察 pi 会话并驱动 herdr。
- **链路：** USB-Serial-JTAG（有线）与 Wi-Fi + WebSocket + mDNS 发现（免线）——
  见 §4.1。BLE 已移除，见 §4.1。
- **范围外 / 非目标：** 不是安全令牌或 passkey 凭据载体；不是电子宠物（吉祥物是
  16px 状态头像，不是 132px 养成舞台）；不用 NFC（NTAG213 无 MCU API）；
  "批准" = pi 的**通用远程输入**（`herdr agent prompt` / `send-keys`），
  非结构化权限弹窗。

## 2. 屏幕设计（把 240×320 用满）

像素预算（8px 等宽，约 30 列；顶部状态条 + 底部动作条常驻）：

| 区域 | 高度 | 内容 |
| --- | --- | --- |
| 状态条 | ~22px | 模型（左）· 状态 chip + 16px 吉祥物（中）· 电量（右上，仓库不变式） |
| 主区 | ~256px | 视图内容 |
| 动作条 | ~22px | 按键提示，随上下文变化 |

### 2.1 视图

已实现，由切视图手势循环：
1. **HOME / 总览**（默认落地页）——pi.dev 风格：暖羊皮纸、等宽字体、三色马赛克
   logo、全局状态摘要。
2. **LIVE**（默认看板）——状态条 + ~15 行实时活动流 + 焦点条（当前工具全文 +
   token/成本）+ 动作条。
3. **STATS** —— 模型 / token / 成本 / 运行时长 / 子代理 / 错误数 / 最近工具 /
   分支 / 状态。
4. **MENU** —— 亮度 / 跟随实时 / 声音 / 时钟 / 记录 / 恢复出厂。

计划中但未实现（P4，延后）：**PAIRING** 弹层（六位配对码——BLE 移除后已无意义）
与 **APPROVAL** 弹层（WAITING 横幅 + 待批提示 + ALLOW/DENY），二者会自动出现而非
手动循环。

### 2.2 活动流各行（带颜色）
| 行 | 来源 | 颜色 |
| --- | --- | --- |
| `read main/x.c` | `tool_use`（工具名 + 首个参数） | 成功=绿 |
| `✓ / ✗ bash` | `toolResult.isError` | 绿 / 红 |
| `thinking …` | `thinking` 块 | 黄 |
| `[model] agnes→…` | `model_change` | 青 |
| `scout-1 running` | herdr agent 快照 | 蓝 |

自动跟随最新；回滚缓冲约 48 行（无 PSRAM → 受 RAM 预算约束）。

### 2.3 pi 品牌色（取自 pi.dev）
```text
parchment  #F3F2F0 / #EBE7E4   (仅 HOME / 品牌页；数据页保持深色)
ink        #252F3D  muted #5C5752
logo       coral #F09082 / steel #4D9ABF / amber #F1BE58 (accent #6A9FCC)
type       monospace
```
HOME = 暖羊皮纸 + 等宽 + 三色马赛克（按官方 SVG 路径 1:1 重绘）。深色页是否点缀
三色仍待定（Q2，§9）。

## 3. 三键映射（已定）

| 键 | 短按 | 长按 |
| --- | --- | --- |
| **UP** | 上滚 / 选择上移 | **切视图**（HOME→LIVE→STATS→MENU） |
| **DOWN** | 下滚 / 跟随 | **跳到最新** |
| **OK** | **确认/激活**（APPROVAL=批准 / MENU=应用 / LIVE=聚焦） | **中断 agent**（STOP，发 Escape） |

无双击（穿戴误触风险）。OK 长按 = "按住即停"（拇指、需要明确意图）。按键状态机
负责短/长按时长阈值（去抖；回调保持非阻塞——仓库不变式）。

## 4. 架构

### 4.1 链路

| 路径 | 链路 | 状态 |
| --- | --- | --- |
| **A** | USB-Serial-JTAG（`/dev/usbserjtag` VFS，与控制台共用） | 真机验证通过 |
| **B** | BLE NUS 外设（LE Secure Connections、配对码绑定） | 已废弃 |
| **C** | Wi-Fi STA + WebSocket 客户端，mDNS 发现（`_pibuddy._tcp`） | 真机验证通过 |

**Path B 因两个互相独立的原因被废弃。** 一是 macOS 27.0 会让第三方
CoreBluetooth 扫描饿死（`AD:0(0/0)`），Mac 侧的 central 扫不到；二是这颗芯片上
BLE 与 Wi-Fi 根本不能共存——NimBLE 占 ~73 KB 堆，在 320 KB SRAM 的 C3 上会把
`esp_wifi_init` 饿死（实测：启动时可用 97,596 B，`pibud_ble_init` 后剩 24,388 B，
留给 Wi-Fi 驱动的只有 764 B，而它需要约 50 KB）。故 `CONFIG_BT_ENABLED=n`，BLE
源码先移出构建、随后删除。

### 4.2 固件（设备）
- 数据通道：`pibud_usbc`（Path A）与 `pibud_ws`（Path C）。两者喂同一个换行 JSON
  解析器和同一个应用事件队列，所以 UI 与链路无关。
- 可跑 host 测试的纯逻辑：`pibud_protocol*`（换行 JSON）、`pibud_state_reduce`
  （状态机）、`pibud_text_layout`、`pibud_line`（有界收包）、`pibud_i4`。
- `pibud_ui_render(snapshot)` 在 `bsp_lvgl_lock()` 保护下调用。
- `pibud_ws`：自建 SoftAP 配网门户（`esp_http_server`，SSID `Pi-Buddy-Setup`）
  → 凭据存入自建 NVS 命名空间 → STA → mDNS 浏览 → `esp_websocket_client` →
  应用队列。
- 可跑 host 测试的配网逻辑：`pibud_form`（urlencoded 解码）与 `pibud_prov_html`
  （HTML 转义与网络选择列表）。
- 仓库规则：从 `main` 拉分支；UI 完全重做（不复用 demo 测试菜单）。

### 4.3 sidecar（电脑侧，Go CLI）
```text
read: tail $PI_SESSION_FILE (JSONL)   -> tool_use/toolResult/thinking/model_change/usage
      herdr api snapshot              -> agent lifecycle + subagents
push: heartbeat JSON -> WS text frame (Path C) or USB line (Path A), every 2s
recv: device act JSON -> herdr agent send-keys / prompt
```
Path C 下 sidecar 是 WebSocket **服务端**（`:51820`），并通过 macOS 的
`dns-sd -R` 广播 `_pibuddy._tcp`；由于 `dns-sd` 没有守护模式，sidecar 会守护它并
在退出后重新注册。不带 `Origin` 头的升级请求被接受，来自浏览器的被拒绝。控制走
`herdr agent send-keys|prompt`；不要直接说 `herdr.sock` 的原始协议。

> sidecar 必须显式带 `--target`（例如 `--target w4:p1`）。不带时动作会打到当前
> **聚焦**的 herdr pane，而那可能正是跑 sidecar 的这个会话。

该端点不再对局域网开放：设备必须走 `/<链接码>`，而链接码是 sidecar 首次运行时
生成到 `~/.pi-buddy/token` 的 12 位十六进制码（用 `--show-token` 再次打印）。其他
路径一律在升级前被 401 拒绝，未认证的对端永远碰不到 act 处理逻辑。链接码通过路径
在明文 `ws://` 上传送，所以它堵的是「网内任意主机都能连上」这个洞，而不是被动嗅探。

sidecar 还会把链路状态发布到 `~/.pi-buddy/status.json`（`connected`、`device`、
`pid`、`updated`），让本机任何程序无需 IPC 就能显示它；pid 用来区分「sidecar 已死
留下的陈旧文件」和「真实在线的链路」。

### 4.4 pi 扩展（电脑侧）
`tools/pi-buddy-extension/pibuddy.ts` 读取 `status.json`，把链路状态显示在 pi 的底部
状态栏，`/pibuddy` 则汇报状态并提供把配对码复制到剪贴板（macOS 剪贴板会与就近的
iPhone 共享，而那个码本来就得在手机上输入）。安装方式：把它软链到
`~/.pi/agent/extensions/`。

## 5. 协议契约（pi heartbeat，换行 JSON；字段在 P1 定稿）
```json
{"cmd":"hb","model":"agnes-3.0-flash","state":"running",
 "tool":"read","arg":"main/buddy_state.c","result_ok":true,
 "sub_total":4,"sub_working":1,"tokens":18432,"cost":0.06}
{"cmd":"prompt","text":"...","waiting":true}
// device -> {"cmd":"act","kind":"approve"|"deny"|"interrupt"}
```
- 有界缓冲：关键字段（id/tool）超限则整条拒绝；展示字段安全截断（buddy 有界缓冲
  策略）。
- 30 秒无快照 → 状态机判定过期、清空提示、显示离线/睡眠（buddy 超时逻辑）。

## 6. 数据来源（已确认存在）
- `$PI_SESSION_FILE`：实时 JSONL。字段：`message.role/content(api,provider,model,
  usage,stopReason)`、`tool_use`、`toolResult(toolName,isError)`、`thinking`、
  `model_change`。
- `herdr api snapshot`：实时 agent 快照（状态、子代理、JSONL 路径）。
- `herdr agent send-keys` / `herdr agent prompt`：控制注入点。

## 7. 阶段
| 阶段 | 交付物 | 验收 | 状态 |
| --- | --- | --- | --- |
| **P1** | sidecar：读 JSONL + `herdr api snapshot` → heartbeat JSON | host 测试 + 真实会话打印 | 完成 |
| **P2** | 固件：协议 + 视图 UI（HOME/LIVE/STATS/MENU）+ 只读看板 | 构建 + host 测试 + 上机 | 完成 |
| **P3** | 设备按键 → sidecar → `herdr agent send-keys` 中断 | 上机：OK 长按真的停住 agent | 完成（Path A 与 Path C 均验证） |
| **P4** | MENU/APPROVAL 弹层 + 待批批准映射 | 上机 | 延后 |

**MVP = P1+P2+P3，P4 延后。**

### 7.1 P3 验收过程中发现并修掉的 bug（都是真 bug，都在 `pibud_ws.c`）
1. 缺 `esp_netif_init()` / `esp_event_loop_create_default()` → 每个 Wi-Fi netif
   辅助函数都在 `ESP_ERROR_CHECK` 里 abort，设备开机即重启循环，根本走不到配网。
2. `esp_ip4_addr_t.addr` 是网络字节序；手工移位得到的是字节反序的字符串，设备
   实际去连 `242.145.168.192` 而不是 `192.168.145.242`，每次连接都超时。
3. 已配网分支沿用了先前 `esp_wifi_start()` 留下的 `WIFI_MODE_NULL`，STA 根本没
   起来，`esp_wifi_connect()` 是空操作——正常开机永远不联网。
4. sidecar 广播的是 `_pibuddy.tcp` 而非 `_pibuddy._tcp`；该注册会以
   `kDNSServiceErr_BadParam`（-65540）失败，`dns-sd` 以 255 退出，设备无从发现。

### 7.2 替换配网路径时发现的缺陷
- **恢复路径上的配网 AP 没有自己的 netif。** AP netif 只在首次配网分支里创建，
  于是重开的门户把射频拉起来却没有 DHCP 服务器；客户端能关联但永远拿不到租约。
- **重开的门户扫不了网。** 重连循环让 STA 停在 connecting 状态，而驱动此时拒绝
  扫描，选择列表恰好在最需要它的时候是空的。现在门户会先暂停重连并断开，扫完
  再恢复。
- **重连循环仍在反复扫描一个已经连不上的网络。** 扫描中的 STA 会与 SoftAP 争
  射频，从而拖累用户正在加载的页面，所以门户打开期间限制了重试次数。
- **`nvs_set_str` 被直接传入了定长凭据字段。** 满长 SSID 会把字段填满且没有结束
  符，可能越界读取。
- **配网 POST 用单次 `httpd_req_recv` 读取请求体，** 而它并不保证一次读完。
- **链接码路径只在 init 时构建。** 在真正完成配网的那次开机里，接口初始化时还没有
  链接码，于是客户端发出的请求行目标为空（`GET  HTTP/1.1`）。服务端在任何 handler
  之前就以 400 拒绝，这就是 sidecar 日志里看不到拒绝记录的原因。现在只要链接码被
  加载或被保存，路径都会重新构建。

## 8. 测试矩阵
- **host 测试：** 协议解析、状态机 reduce、文本换行/裁剪、活动流行格式化、
  urlencoded 表单解码、配网页 HTML 转义（全为纯逻辑，与 ESP-IDF/LVGL 解耦）；
  sidecar 的 JSONL 解析 + herdr 调用封装。
- **真机测试：** 240×320 各视图/各行/超长字符串渲染、无看门狗 soak、绑定、
  OK 长按中断。
- **交付四字段：** 分开报告 `Build / Host tests / Device tests / Unverified`；
  每完成一次固件改动，主动询问是否刷机（需批准）。

## 9. 待办项 / 假设
1. **Q1 默认落地页 = HOME** —— 已确认。
2. **Q2 深色页是否点缀 pi 三色** —— 待定。
3. **配网 UX —— 已实现。** `wifi_prov_mgr` 的 SoftAP 方案不带网页表单，配 Wi-Fi
   得用电脑上的 `esp_prov.py`，而连上 `Pi-Buddy-Setup` 会让那台电脑断网。现在设备
   自带 captive portal：扫描结果填充网络选择列表，另留一个文本框供隐藏网络使用；
   未知路径返回重定向，手机才会弹出登录面板；凭据写入自建 NVS 命名空间；STA 一
   上网门户即关闭。仅本地首次配网用，所以 AP 是开放的、表单是明文 HTTP。若 STA
   连不上，约 30 秒后门户会重开，让输错密码不至于把用户锁在门外；门户开着期间
   STA 停止重试，因为扫描中的 STA 会与 SoftAP 争射频。密码框带 Show 切换开关——
   这个码只从手机键盘输入一次，而一次笔误就要付出一整轮重试的代价。
4. **WS 端点鉴权 —— 已实现。** 该端点监听整个局域网且能执行
   `herdr agent send-keys`，所以任何主机都能往用户的 agent pane 注入按键。现在设备
   走 `/<链接码>`，其他一律在升级前被拒；链接码只生成一次，存放在
   `~/.pi-buddy/token`（权限 600），由用户在配网页输入，而 `pibud_token` 会容忍
   手机键盘带来的分隔符与大小写差异。握手被拒三次后会重开配网门户，而「连不上」
   则不会——不能仅仅因为 Mac 睡着了就冒出个开放 AP。
5. **mDNS 实例名** —— 注册名现在是 `pibuddy-<端口>-<主机哈希>`，第二个 sidecar 不再
   抢注同名，也就不会把设备抢走。
6. **中断键序列** —— OK 长按发 Escape（`send-keys <pane> esc`）；已验证。
7. **假设：** pi 的"批准"是通用远程输入（无结构化权限弹窗）——已确认。

## 10. 验证与交付门槛
- 迭代用 `./tools/validate.sh --static`；交付用 `./tools/validate.sh`（需已激活
  ESP-IDF 5.5.3）。
- Path C 交付的最后一次记录：**Build PASS、Host tests PASS、Device tests PASS**
  （真机显示实时会话数据 + OK 长按中断打到钉死的 pane），无遗留待验的板级或仪器
  检查。
- 构建成功 ≠ 硬件验收；刷机需批准；刷前不备份原固件、默认不做整片擦除。
- 仅在被要求时或工作流需要时才 commit / push。
