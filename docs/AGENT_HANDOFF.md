# Agent 交接说明（产品 fork · 写字机）

给后续 AI / 人类 agent 的**可执行上下文**。改代码前先读本文件 + 根目录 `Agents.md`。

| 项 | 值 |
|----|-----|
| 分支 | `Branch_736afa70` |
| 产品默认机 | `Machines/custom_3axis_hr4988.h`（HR4988 + I2S 595 + 纸路 + BT） |
| 深度审查落地 | `801761e`（2026-07-19）· 已 push |
| 配套 SIL 测试 | fz `e01c263`（G28/G38/`$H` 换纸 defer 期望） |
| 验收 HIL | `docs/ACCEPTANCE_CHECKLIST.md` |

## 1. 产品形态（不要当通用 CNC 想）

- **主通道：蓝牙 SPP**（`ENABLE_BLUETOOTH`；WiFi/HTTP/OTA/Telnet/SD **默认关**）。
- **纸路**：`GRBL_PAPER_SYSTEM` + `Custom/paper_system.cpp` + `src/PaperSystem.cpp`。
- **授权**：`M800` + NVS（`LicenseCore` / `paper_system.cpp`）；未授权挡运动行。
- **仿真真相在 fz**：`D:/Users/zhugu/fz` / `$env:FZ_ROOT`。**禁止**在本仓膨胀新 sim 树。

## 2. 硬规则（违反即流程失败）

1. 动 `GCode` / `Protocol` / `Planner` / `Stepper` / `Limits` / `Paper*` / `BTState` → **自己跑** `agent_gate`（至少 `standard`），再谈 fixed / 可烧录。
2. Host SIL **≠** 纸张机械 / BT 射频 / OTA / 真机验收。宣称可发货必须对照 `ACCEPTANCE_CHECKLIST`（G3b）。
3. 不整仓 merge `upstream/main`；产品分叉在 Protocol/GCode/Serial/纸路/BT。
4. 审查草稿 **勿提交**：`.omk/`、`.cursor-review-*`、`.atom-review-*`、`.mimo-*`、`.claude-review-*`（见 `.gitignore`）。

```powershell
$env:FZ_ROOT='D:\Users\zhugu\fz'
$env:GRBL_ROOT='D:\Users\Grbl_Esp32'
python $env:FZ_ROOT\scripts\agent_gate.py --profile standard
# overall_status 必须 pass
```

## 3. 已落地的不变量（`801761e`）— 改动时勿回退

### 纸路 / 协议

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **S1** | M30 换纸失败时必须清 `gc_state.modal.program_flow = Running`，禁止 sticky `CompletedM30` 导致下行反复 ProgramEnd/换纸 | `GCode.cpp` M30 分支 early return |
| **P1** | 换纸中 `should_defer_motion` 挡 **G0–G3、模态轴字、G28/G30/G38、`$H`/`$J`**；`is_motion_line` **不**因 license 策略把 G28 当 motion | `ProtocolDecisionCore.h`、`Protocol.cpp` |
| **P2** | `paper_auto_change()` 入口仅 **`State::Idle`** 可进（覆盖 ESP910/M721/BT 预约） | `PaperSystem.cpp` |
| **P2b** | 换纸中 **禁止** M711–713/M716 与 `paper_run_motor`/ESP930 驱动纸路（`AnotherInterfaceBusy`） | `PaperSystem.cpp` `paper_reject_if_auto_change_running` |
| M30 成功跳过下一原点换纸 | `paper_m30_just_completed` 不在 `line_begin` 清；仅 consume / 非原点 seek / parser_reset | `Custom/paper_system.cpp` |
| Busy 重入 | 已在 running 时返回 **非 Ok**（`AnotherInterfaceBusy`） | `PaperSystem.cpp` |
| Sensor fail-closed | Step2/6/7 失败走 cleanup + `MessageFailed` + `[PaperStatus]` | `PaperSystem.cpp` |

### 设置 / 步进

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **M1** | 任意 `nvs_set_*` / erase 后 **`nvs_commit`** | `Settings.cpp` `nvs_commit_settings()` |
| **M2** | Stepper timer busy 冲突时 **计 overrun 并恢复**，且 **始终 re-arm** 定时器 | `Stepper.cpp` `onStepperDriverTimer` |
| **M3** | `$21` POST → `limits_init()`；`$20=1` 需 `$22=1`；关 `$22` 清 soft limits | `SettingsDefinitions.cpp` |

### WebUI / 安全（默认 WiFi 关 = 潜伏面；开 WiFi 前必守）

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **W1** | 登录接受 admin **或** user：`!(admin_ok \|\| user_ok)` | `WebServer.cpp` |
| **W3** | Serial2Socket TX **分块**，禁止 `size > TXBUFFERSIZE` 越界 | `Serial2Socket.cpp` |
| **W4** | SPIFFS list/delete/mkdir + ESP700/701 与 SD 一致做 `..` 拒绝 | `WebServer.cpp` / `WebSettings.cpp` |
| SD 流 | 下载 `write(buf, v)` 按实际读长 | `WebServer.cpp` |

### 有意产品折中（不要「顺手改严」）

| 项 | 原因 |
|----|------|
| **ESP910 / ESP901 仍为 `WG`** | 物理键注入 `[ESP910]`、BT 以 `LEVEL_GUEST` 执行；改成 `WU` 会打断按钮与无密码 BT |
| ESP911/912/913/930 为 **`WU` + `notCycleOrHold`** | 调试点动需 user/admin 或 `pwd=` |
| 换纸中短时忽略 host `0x18`（最长约 60s） | 防 BT 假连接复位打断弹纸；**急停请用 feed-hold**（已映射纸路中止） |
| `BLOCK_BUFFER_SIZE=250` / `SEGMENT_BUFFER_SIZE=48` | 为 BT 流式加深度；改动需 re-gate 并评估 replan 成本 |

## 4. 关键代码地图

```
纸路执行     Grbl_Esp32/src/PaperSystem.cpp
纸路 G 钩子  Grbl_Esp32/Custom/paper_system.cpp   (+ weak stubs Grbl.cpp)
换纸 defer   Grbl_Esp32/src/ProtocolDecisionCore.h + Protocol.cpp
M30/解析     Grbl_Esp32/src/GCode.cpp
BT 状态/TX   Grbl_Esp32/src/WebUI/BTState.cpp (+ BTStateCore.h)
设置/NVS     Grbl_Esp32/src/Settings.cpp + SettingsDefinitions.cpp
步进 ISR     Grbl_Esp32/src/Stepper.cpp
机定义       Grbl_Esp32/src/Machines/custom_3axis_hr4988.h
```

Host 可测纯核：`PaperSearchCore.h`、`PaperBtAckCore.h`、`ProtocolDecisionCore.h`、`BTStateCore.h`、`LicenseCore.h`（fz `native_sim` 引用本仓头文件）。

## 5. 改完后怎么验

| 变更类型 | 最低门禁 |
|----------|----------|
| 协议/GCode/设置字符串解析 | `agent_gate` **quick** 或 **standard** |
| 步进/规划/限位/纸路 | **standard**（绿才可声称 host SIL 过） |
| 仅 WebUI 且 WiFi 默认关 | 至少 `pio run -e release`；开 WiFi 相关则 standard |
| 纸路/BT 行为声称「真机 OK」 | **HIL** + `ACCEPTANCE_CHECKLIST`（SIL 不够） |

fz 侧若改 `should_defer_motion` 语义：同步更新  
`native_sim/test_protocol_decision_trace.py` 与  
`native_sim/scenarios/protocol_input_boundary_sequence.json`。

## 6. 仍开放（2026-07-19 加固后二次深审）

Host SIL `agent_gate quick` 仍绿；下列为**残余真实问题**，非「已修又复发」。

| 严重度 | 问题 | 位置 | 建议 |
|--------|------|------|------|
| ~~**High**~~ | ~~换纸中 M711–716 / paper_run_motor 无互斥~~ | — | **已修**：`paper_reject_if_auto_change_running()` + ESP930 busy |
| Medium | 首页换纸判定用**机床** XYZ≈0，原点路径用**工件**坐标；非零 G54 可能永不首页换纸 | `paper_system.cpp` before_motion vs after_origin | 统一为工件坐标或「指令字全 0」 |
| Medium | M721/ESP910 成功不 `paper_mark_first_page_change_done`（BT 路径会 mark）→ 可能二次首页换纸 | `PaperSystem.cpp` / handlers | 成功路径统一 mark |
| Low | `has_g_code(28)` 要求非小数 → **G28.1** 不进 paper defer（一般只设参考点，风险低） | `ProtocolDecisionCore.h` | 若需 fail-closed，用 G28 前缀启发式 |
| Low | Stepper overrun 恢复在一次 ISR 内连跑多 tick，极端负载下拉长 ISR（已 cap=3） | `Stepper.cpp` | 可改为每 tick 只补 1 步 |
| Design | ESP910 **WG**；60s 忽略 0x18；明文 auth | 见 §3 折中 | 开 HTTP 前评估 |

**已确认仍成立（勿回退）：** S1 program_flow 清零；P1 G28/G38/`$H` defer；P2 Idle 门；W1 登录；W3/W4；M1 nvs_commit；M2 re-arm；M3 `$21`→`limits_init`。

## 7. 相关文档

| 文档 | 用途 |
|------|------|
| `Agents.md` | 架构、构建、门禁总则 |
| `docs/ACCEPTANCE_CHECKLIST.md` | 真机验收勾选 |
| `tools/SIMULATION.md` | fz 入口 |
| `配置.md` | 默认机硬件/引脚中文说明 |
| `doc/Commands.txt` | `[ESP…]` 命令 |
| fz `docs/AGENT_VIBE_CODING.md` | 仿真 vibe 手册 |

历史审查草稿（`.omk/`、`*-review-*.md`）**不是**源码真相；以本文件 + git 历史为准。
