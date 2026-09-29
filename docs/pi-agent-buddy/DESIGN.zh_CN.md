<p align="right">
  <a href="DESIGN.md">English</a> · <strong>简体中文</strong>
</p>

# Pi Agent Buddy — 设计与实施计划（分支内工作文档）

> 分支：`feature/pi-agent-buddy`（从 `main` 拉出）。本文档是分支内规划工作稿，非上游
> 维护文档；随实现保持同步。屏幕 mockup 见 `/tmp/pi-buddy-mockup/`（HOME 页已按
> pi.dev 风格定稿）。

## 1. 产品定义

**一句话**：一块贴身的**编码 agent 状态看板 + 轻控制遥控器**。实时看 pi 在写什么 /
卡在哪 / 花了多少 token，必要时上手一按就中断。

- **范围内**：固件（BLE NUS 外设 + LE 绑定 + 换行 JSON 协议 + 状态机 + 6 视图 + 3
  按键）+ 电脑 **sidecar**（Go CLI，BLE central），观察 pi 会话并驱动 herdr。
- **非目标**：不是安全 token / passkey 凭证持有者；不是电子宠物（mascot 只是 16px
  状态头像，不做 132px 宠物舞台）；不用 NFC（NTAG213 无 MCU API）；"批准" = pi 的
  **通用远程输入**（`herdr agent prompt` / `send-keys`），不是结构化权限弹窗。

## 2. 屏幕设计（充分利用 240×320）

像素预算（8px 等宽，约 30 列；顶状态 / 底操作常驻）：

| 区域 | 高度 | 内容 |
| --- | --- | --- |
| 状态条 | ~22px | model（左）· state chip + 16px mascot（中）· 电池（右上，repo 不变量） |
| 主体 | ~256px | 各视图内容 |
| 操作条 | ~22px | 按键提示，随上下文变 |

### 2.1 六个视图
1. **HOME / 整体**（开机默认落点）——pi.dev 风格：暖白 + 等宽 + 三色 logo + 全局摘要。
2. **LIVE**（默认看板）——状态条 + 活动流（~15 行实时日志）+ 焦点条（当前 tool 全文
   + token/成本）+ 操作条。
3. **STATS**——model / tokens / cost / uptime / 子代理 / 错误 / 最近工具 / 分支 / 状态。
4. **MENU**——BLE / 亮度 / follow-live / 声音 / 时钟 / transcript / 取消配对 / 复位。
5. **PAIRING**（弹层）——六位配对码大字居中。
6. **APPROVAL**（弹层）——agent 等输入时：WAITING 横幅 + 等待的 prompt + ALLOW/DENY。

`APPROVAL` / `PAIRING` 是**状态驱动弹层**（自动出现），不靠按键切；`HOME/LIVE/
STATS/MENU` 用"切视图"手势循环。

### 2.2 活动流每行 = 一条 agent 事件（颜色分级）
| 行 | 触发源 | 颜色 |
| --- | --- | --- |
| `read main/x.c` | `tool_use`(name+首参) | ok 绿 |
| `✓ / ✗ bash` | `toolResult.isError` | 绿 / 红 |
| `thinking …` | `thinking` block | 黄 |
| `[model] agnes→…` | `model_change` | 青 |
| `scout-1 running` | herdr agent snapshot | 蓝 |

auto-follow 最新；回滚缓冲上限 ~48 行（无 PSRAM，控 RAM）。

### 2.3 pi 品牌视觉令牌（取自 pi.dev）
```
暖白底   #F3F2F0 / #EBE7E4      （仅 HOME / 品牌页；数据页仍深色高对比）
墨色     #252F3D  弱化 #5C5752
三色 logo  珊瑚 #F09082 / 钢蓝 #4D9ABF / 琥珀 #F1BE58  （主 accent 柔和蓝 #6A9FCC）
字体     等宽
```
HOME = 暖白 + 等宽 + 三色马赛克（按官方 SVG 路径 1:1 重绘）。数据页是否点缀三色：
待定（Q2，§9）。

## 3. 三键映射（定稿）

| 键 | 短按 | 长按 |
| --- | --- | --- |
| **UP** | 上滚 / 选中上移 | **切视图**（HOME→LIVE→STATS→MENU→…） |
| **DOWN** | 下滚 / 跟随 | **跳到最新** |
| **OK** | **确认/激活**（APPROVAL=批准 / MENU=应用 / LIVE=焦点） | **中断 agent**（STOP，发 Escape） |

不用双按（腕戴误触风险）。OK 长按 = "按住停"（拇指位、防误触）。按钮状态机做
短/长 时长阈值（debounce；回调非阻塞，repo 不变量）。

## 4. 架构

### 固件（设备）——~80% 复用 claude-buddy-port
- ✅ 搬：`buddy_ble*`（NUS + LE 加密 + 6 码绑定）、`buddy_protocol*`（换行 JSON，可
  host 测试）、`buddy_state_reduce`（纯状态机）、`buddy_text_wrap`、`buddy_ui_render`
  + LVGL 锁模式。
- ✏️ 重做：布局/排版（§2）、mascot（单 pi 头像 + 状态）、协议字段（Claude→pi 概念）。
- 仓库规则：从 `main` 分支，buddy 当**参考模式**提取，不整分支 merge demo；UI 全新
  设计（不沿用 demo test menu）。

### Sidecar（电脑，Go CLI，BLE central）
```
读:  tail $PI_SESSION_FILE(JSONL)  -> tool_use/toolResult/thinking/model_change/usage
     herdr api snapshot            -> agent 生命周期 + 子代理
推:  组 heartbeat JSON -> NUS RX(0002)
收:  NUS TX(0003) 设备按键 -> herdr agent send-keys / prompt
```
BLE 走 NUS GATT；macOS central 用 CoreBluetooth（Go `ble`/`bleat` 或 cgo）。控制统一
落 `herdr agent send-keys|prompt`（CLI 已确认），不碰裸 `herdr.sock` 协议。

## 5. 协议合同（pi 版 heartbeat，换行 JSON；最终字段 P1 敲定）
```json
{"cmd":"hb","model":"agnes-3.0-flash","state":"running",
 "tool":"read","arg":"main/buddy_state.c","result_ok":true,
 "sub_total":4,"sub_working":1,"tokens":18432,"cost":0.06}
{"cmd":"prompt","text":"...","waiting":true}
// 设备回: {"cmd":"act","kind":"approve"|"deny"|"interrupt"}
```
- 有界缓冲：关键字段（id/tool）超限则拒绝整条；可显示字段安全截断（buddy 有界策略）。
- 30s 无 snapshot → 状态机判失活、清审批、进离线/睡眠显示（buddy 超时逻辑）。

## 6. 数据源映射（已确认存在）
- `$PI_SESSION_FILE`：活 JSONL。字段：`message.role/content(api,provider,model,usage,
  stopReason)`、`tool_use`、`toolResult(toolName,isError)`、`thinking`、`model_change`。
- `herdr api snapshot`：live agent 快照（状态、子代理、JSONL 路径）。
- `herdr agent send-keys` / `herdr agent prompt`：控制注入口。

## 7. 分期
| 期 | 交付 | 验收 |
| --- | --- | --- |
| **P1** | sidecar 原型：读 JSONL + `herdr api snapshot` → 输出 heartbeat JSON（先打印，再接 BLE） | host 测试 + 真机看打印 |
| **P2** | 固件 pi-buddy：BLE NUS + 协议 + 6 视图 UI + 只读 LIVE/HOME | Build + host tests + 上机 |
| **P3** | 设备按键 → sidecar → `herdr agent send-keys` 中断 | 上机：OK 长按真停 agent |
| **P4** | MENU/PAIRING/APPROVAL + waiting 批准映射 | 上机 |

**MVP = P1+P2+P3，P4 后置。**

## 8. 测试矩阵
- **Host tests**：协议解析、状态机 reduce、排版/裁剪、活动流行格式化（全纯逻辑，与
  ESP-IDF/LVGL 解耦）；sidecar 的 JSONL 解析 + herdr 调用封装。
- **Device tests**：240×320 逐视图/逐行/长字符串渲染、无看门狗 soak、配对、OK 长按
  中断实测。
- **交付四字段**：`Build / Host tests / Device tests / Unverified` 分开报；每个完整
  固件改动后主动问是否刷机（需批准）。

## 9. 待办 / 假设
1. **Q1 默认落点 = HOME**（品牌感 + 全局一眼）——已确认。
2. **Q2 深色页是否点缀 pi 三色**（state chip 用钢蓝/琥珀）还是只有 HOME 是品牌页——
   待定。
3. **sidecar BLE 库**：CoreBluetooth(cgo) vs 纯 Go `ble`——P2 定。
4. **中断键序列**：OK 长按具体发什么键（纯 Escape vs 组合）——P3 上机敲定。
5. **假设**：pi 的"批准"按通用远程输入实现（不做结构化权限弹窗）——已确认。

## 10. 校验与交付门槛
- 迭代：`./tools/validate.sh --static`；交付：`./tools/validate.sh`（需激活
  ESP-IDF 5.5.3）。
- 成功 build ≠ 硬件验收；刷机需你批准；不预置原始固件备份、不默认全片擦除。
- 提交/推送仅在你要求或工作流明确要求时进行。
