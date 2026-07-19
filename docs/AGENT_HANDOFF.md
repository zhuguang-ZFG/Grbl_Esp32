# Agent 交接说明（产品 fork · 写字机）

给后续 AI / 人类 agent 的**可执行上下文**。改代码前先读本文件 + 根目录 `Agents.md`。

| 项 | 值 |
|----|-----|
| 分支 | `Branch_736afa70` |
| 产品默认机 | `Machines/custom_3axis_hr4988.h`（HR4988 + I2S 595 + 纸路 + BT） |
| 深度审查落地 | `801761e`（2026-07-19）· 已 push |
| 二轮审查落地 | `ad4d1a6` + `59f4304`（2026-07-19）· 已 push（M1 强化 / F1 / F3 / M5 / N2 / W-N1 / W-N2 / CI） |
| 配套 SIL 测试 | fz `e01c263`（G28/G38/`$H` 换纸 defer 期望） |
| 验收 HIL | `docs/ACCEPTANCE_CHECKLIST.md` · 本文件 §8（F1/F3/M1 专项） |

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
| **F1** | `paper_auto_change()` 的 busy 检查 + `running=true` + 清 stop 必须在**同一 `portENTER/EXIT_CRITICAL`**；`protocol_buffer_synchronize()` 在 claim **之前**（先排空再抢锁）。禁止把 busy 检查移出临界区——WiFi/BT/协议多任务会 TOCTOU 双入纸路 bit-bang | `PaperSystem.cpp` `paper_auto_change` |
| **F3** | `0x18` 软复位**仅对 `CLIENT_BT` 忽略**；USB/串口/WebUI 的 0x18 **永远急停**（与上游 Grbl_Esp32/FluidNC "reset 全局急停" 铁律一致）。`paper_should_ignore_host_reset(client)` 必须带 client 参数 | `Serial.cpp` `Cmd::Reset` + `PaperSystem.cpp` |
| M30 成功跳过下一原点换纸 | `paper_m30_just_completed` 不在 `line_begin` 清；仅 consume / 非原点 seek / parser_reset | `Custom/paper_system.cpp` |
| Busy 重入 | 已在 running 时返回 **非 Ok**（`AnotherInterfaceBusy`） | `PaperSystem.cpp` |
| Sensor fail-closed | Step2/6/7 失败走 cleanup + `MessageFailed` + `[PaperStatus]` | `PaperSystem.cpp` |

### 设置 / 步进

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **M1** | 任意 `nvs_set_*` / erase 后 **`nvs_commit`**；写/commit 失败返回 `NvsSetFailed` 并同步 `_storedValue`；`Coordinates::load` 要求 **blob 等长**，`set`/`eraseNVS` commit 失败须告警 | `Settings.cpp` `nvs_commit_settings()` / `nvs_erase_key_settings()` |
| **M2** | Stepper timer busy 冲突时 **计 overrun 并恢复**，且 **始终 re-arm** 定时器 | `Stepper.cpp` `onStepperDriverTimer` |
| **M3** | `$21` POST → `limits_init()`；`$20=1` 需 `$22=1`；关 `$22` 清 soft limits | `SettingsDefinitions.cpp` |
| **M5** | PWM min>max 告警比较 **min 与 max**（勿写成 min>min） | `Spindles/PWMSpindle.cpp` |
| **N2** | `map_uint32_t`/`map_float` 在 `in_max==in_min` 时返回 `out_min`（防除零；`$30==$31`） | `NutsBolts.cpp` |

### WebUI / 安全（默认 WiFi 关 = 潜伏面；开 WiFi 前必守）

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **W1** | 登录接受 admin **或** user：`!(admin_ok \|\| user_ok)` | `WebServer.cpp` |
| **W3** | Serial2Socket TX **分块**，禁止 `size > TXBUFFERSIZE` 越界 | `Serial2Socket.cpp` |
| **W4** | SPIFFS list/delete/mkdir + ESP700/701 与 SD 一致做 `..` 拒绝 | `WebServer.cpp` / `WebSettings.cpp` |
| SD 流 | 下载 `write(buf, v)` 按实际读长 | `WebServer.cpp` |
| **W-N1** | `[ESP]` 命令拷进 `line[256]` 后 **补 NUL**（`strncpy` 截断不终止） | `WebServer.cpp` |
| **W-N2** | SD 分支 `path.substring(3)` 剥 `/SD` 后 **重算 `pathWithGz`** | `WebServer.cpp` |

### 有意产品折中（不要「顺手改严」）

| 项 | 原因 |
|----|------|
| **ESP910 / ESP901 仍为 `WG`** | 物理键注入 `[ESP910]`、BT 以 `LEVEL_GUEST` 执行；改成 `WU` 会打断按钮与无密码 BT |
| ESP911/912/913/930 为 **`WU` + `notCycleOrHold`** | 调试点动需 user/admin 或 `pwd=` |
| 换纸中短时忽略 **BT** `0x18`（最长约 60s；见 §3 **F3**） | 防 BT 假连接复位打断弹纸。**USB/串口 0x18 仍全局急停**；feed-hold 亦映射纸路中止 |
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
| ~~Medium~~ | ~~首页机床 vs 工件坐标~~ | — | **已修**：before_motion 按工件坐标判 X0Y0Z0 |
| ~~Medium~~ | ~~M721/ESP910 不 mark first page~~ | — | **已修**：`paper_auto_change` 成功路径统一 `paper_mark_first_page_change_done` |
| Low | `has_g_code(28)` 要求非小数 → **G28.1** 不进 paper defer（一般只设参考点，风险低） | `ProtocolDecisionCore.h` | 若需 fail-closed，用 G28 前缀启发式 |
| Low | Stepper overrun 恢复在一次 ISR 内连跑多 tick，极端负载下拉长 ISR（已 cap=3） | `Stepper.cpp` | 可改为每 tick 只补 1 步 |
| Design | ESP910 **WG**；60s 忽略 **BT** 0x18；明文 auth | 见 §3 折中 | 开 HTTP 前评估 |
| Design | **F1/F3/M1 无 host SIL 覆盖**：临界区并发、client-scoped reset、NVS 持久化均在 `.cpp`（非 `*Core.h`），gate 编不到 | — | **靠 HIL**（§8）；抽核不划算（上游亦无并发保护先例，见调研） |
| Low | BT ack 双实现（`PaperBtAckCore.h` reducer vs `PaperSystem.cpp` 手写）；无活 bug 但易腐化 | `PaperSystem.cpp` | 长期可统一走 reducer |

**Atom minor cleanup (post e5dbdf4):** 成功路径 cooldown / first-page 仅在 `paper_auto_change()` 内 arm；`user_m30` / BT 成功路径只保留各自需要的 `bt_suppress` + Z=0。

**已确认仍成立（勿回退）：** S1 program_flow 清零；P1 G28/G38/`$H` defer；P2 Idle 门；F1 busy 单临界区；F3 0x18 仅 BT；W1 登录；W3/W4；M1 nvs_commit；M2 re-arm；M3 `$21`→`limits_init`；M5/N2 主轴。

**二轮 gate：** `agent_gate standard` overall=pass（31 层，2026-07-19）。注意 gate 覆盖 P1/协议核/纸路模型，**不**执行 F1/F3/M1/M5/N2/W 所在 `.cpp` —— 那些靠 `pio run -e release` 编译 + §8 HIL。

## 8. HIL 专项验证（F1 / F3 / M1 —— host SIL 覆盖不到，必须真机）

二轮修复中这三项只经 `pio run -e release` 编译 + 代码审查，**未被 `agent_gate` 执行**（逻辑在带硬件依赖的 `.cpp`）。发货前须真机验证：

### F3 — 0x18 软复位 client 作用域

| 步骤 | 期望 |
|------|------|
| 触发换纸（M30 / `[ESP910]` / 按钮），弹纸阶段经 **USB** 发 `0x18` | **立即软复位急停**（电机停，进 Alarm/Idle）；串口打印**无** `[PaperAuto] ... ignored` |
| 同上，改经 **BT SPP** 发 `0x18`（60s 窗口内） | **忽略**，换纸继续；打印 `[PaperAuto] Host reset (0x18) ignored ... (BT only)` |
| 换纸中按面板急停 / feed-hold | 纸路中止（`paper_request_user_stop`），两通道一致 |

### F1 — 换纸入口 busy 并发（TOCTOU）

| 步骤 | 期望 |
|------|------|
| **开 WiFi+HTTP**，换纸进行中，用 HTTP `[ESP910]` 与 BT/串口 `[ESP910]` **并发**灌入 | 仅一个执行；其余返回 `AnotherInterfaceBusy`；**纸路电机不双驱/不错位** |
| M30 换纸中，队列尾再来一条会触发换纸的行 | 第二次被拒（Busy），无嵌套 bit-bang |
| 反复上电 + 立即并发触发（压 `protocol_buffer_synchronize` yield 窗口） | 无双入、无 I2S 竞争打印 |

### M1 — NVS 持久化（掉电存活）

| 步骤 | 期望 |
|------|------|
| `$100=250` → **断电** → 上电 `$100` | 仍为 `250`（验证 `nvs_commit`） |
| `G10 L2 P1 X10 Y10` 设 G54 → 断电 → 上电查 G54 | 坐标保留 |
| 人为触发 NVS 写失败（如满区）| 串口报 `nvs ... failed`，不静默 |

失败任一项 → 对应不变量（§3 F1/F3/M1）**回退了**，勿声称修好。

## 9. 相关文档

| 文档 | 用途 |
|------|------|
| `Agents.md` | 架构、构建、门禁总则 |
| `docs/ACCEPTANCE_CHECKLIST.md` | 真机验收勾选 |
| `docs/FIRMWARE_CI.md` | **GitHub 自动编译 / Artifacts / Release 下载** |
| `tools/SIMULATION.md` | fz 入口 |
| `配置.md` | 默认机硬件/引脚中文说明 |
| `doc/Commands.txt` | `[ESP…]` 命令 |
| fz `docs/AGENT_VIBE_CODING.md` | 仿真 vibe 手册 |

历史审查草稿（`.omk/`、`*-review-*.md`）**不是**源码真相；以本文件 + git 历史为准。
