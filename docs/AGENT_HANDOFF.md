# Agent 交接说明（产品 fork · 写字机）

给后续 AI / 人类 agent 的**可执行上下文**。改代码前先读本文件 + 根目录 `Agents.md`。

| 项 | 值 |
|----|-----|
| 分支 | `Branch_736afa70` |
| 产品默认机 | `Machines/custom_3axis_hr4988.h`（HR4988 + I2S 595 + 纸路 + BT） |
| 深度审查落地 | `801761e`（2026-07-19）· 已 push |
| 二轮审查落地 | `ad4d1a6` + `59f4304`（2026-07-19）· 已 push（M1 强化 / F1 / F3 / M5 / N2 / W-N1 / W-N2 / CI） |
| 三轮审查落地 | `ed1089d`（2026-07-20）· 已 push（B1–B4 Blocker + 内存安全 + 解析加固 + 段缓冲屏障） |
| 四轮审查落地 | `4b29822`（2026-07-20）· 已 push（错误逻辑专项：$G 探针标签 / 全局禁用掩码 / map 反区间 / Trinamic 编译 / VFD 等） |
| 正向锚边 + 量产镜像 | `31db6d8`…`618b1fb`（2026-07-24）· 已 push（P6；见 §6i） |
| 配套 SIL 测试 | fz `e01c263`（G28/G38/`$H` 换纸 defer 期望） |
| 验收 HIL | `docs/ACCEPTANCE_CHECKLIST.md` · 本文件 §8（F1/F3/M1 专项）· §6i P6 对位 |

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
| **B2** | `pending_m_code`（M700-721/M800 延迟执行标志）必须在 `gc_execute_line` **入口**与 `gc_init()` 复位。STEP2 置位后若该行 FAIL 会残留，下一行任意 G 码用**本行 P/Q** 误触发纸路/授权，且软复位清不掉。STEP3 执行仅凭该 static 值，无 command_words 门控 | `GCode.cpp` |
| M30 成功跳过下一原点换纸 | `paper_m30_just_completed` 不在 `line_begin` 清；仅 consume / 非原点 seek / parser_reset | `Custom/paper_system.cpp` |
| Busy 重入 | 已在 running 时返回 **非 Ok**（`AnotherInterfaceBusy`） | `PaperSystem.cpp` |
| Sensor fail-closed | Step2/6 失败走 cleanup + `MessageFailed` + `[PaperStatus]` | `PaperSystem.cpp` |
| **P6** | 换纸 Step 6 采纸尾边沿后**全程正向不换向**（正向锚边，2026-07-24 起）：原 Step 7 反向回找已移除——换向会把机械回差引入 Step 8 最终对位。禁止在无回差补偿前提下恢复"反向找边"；边沿历史仅 RAM（`paper_panel_edge_steps/valid`），快进段停 `PANEL_EDGE_APPROACH_STEPS` 前转慢速采边；仅 attempt==0 成功写入历史，重试成功 deferred；失败/中止/`paper_on_soft_reset_restart` 清 `valid`；采边用对称 `PaperSensorLevel` + 连续 `PAPER_SENSOR_LOST_STREAK` Absent（过渡区不当前沿）；**成功路径统一 `sys_position[Z]=0` sync**（ESP910/M721/按键与 M30/BT 一致） | `PaperSystem.cpp` Step 6、`PaperSystemCore.h`、`Machines/custom_3axis_hr4988.h` |

### 设置 / 步进

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **M1** | 任意 `nvs_set_*` / erase 后 **`nvs_commit`**；写/commit 失败返回 `NvsSetFailed` 并同步 `_storedValue`；`Coordinates::load` 要求 **blob 等长**，`set`/`eraseNVS` commit 失败须告警 | `Settings.cpp` `nvs_commit_settings()` / `nvs_erase_key_settings()` |
| **M2** | Stepper timer busy 冲突时 **计 overrun 并恢复**，且 **始终 re-arm** 定时器 | `Stepper.cpp` `onStepperDriverTimer` |
| **M3** | `$21` POST → `limits_init()`；`$20=1` 需 `$22=1`；关 `$22` 清 soft limits | `SettingsDefinitions.cpp` |
| **M5** | PWM min>max 告警比较 **min 与 max**（勿写成 min>min） | `Spindles/PWMSpindle.cpp` |
| **N2** | `map_uint32_t`/`map_float` 在 `in_max==in_min` 时返回 `out_min`（防除零；`$30==$31`） | `NutsBolts.cpp` |
| **B3** | `settings_restore` 保留启动行时比 **`"GCode/Line0"`/`"GCode/Line1"`**（真实 getName，非 `"Line0"`）；否则 `$RST=$` 每次误清 `$N0`/`$N1` | `ProcessSettings.cpp` |
| **W9** | `segment_buffer_head` 为 `volatile`，生产者发布段前 `atomic_thread_fence(release)`、消费者读 head 后 `acquire`；防编译器重排使步进 ISR 读到半写段 | `Stepper.cpp` |

### WebUI / 安全（默认 WiFi 关 = 潜伏面；开 WiFi 前必守）

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **W1** | 登录接受 admin **或** user：`!(admin_ok \|\| user_ok)` | `WebServer.cpp` |
| **W3** | Serial2Socket TX **分块**，禁止 `size > TXBUFFERSIZE` 越界 | `Serial2Socket.cpp` |
| **W4** | SPIFFS list/delete/mkdir + ESP700/701 与 SD 一致做 `..` 拒绝 | `WebServer.cpp` / `WebSettings.cpp` |
| SD 流 | 下载 `write(buf, v)` 按实际读长 | `WebServer.cpp` |
| **W-N1** | `[ESP]` 命令拷进 `line[256]` 后 **补 NUL**（`strncpy` 截断不终止） | `WebServer.cpp` |
| **W-N2** | SD 分支 `path.substring(3)` 剥 `/SD` 后 **重算 `pathWithGz`** | `WebServer.cpp` |
| **B4** | `split_params` 填 `static keyval_t params[10]` 前查 `i >= 9`（留 NULL 终止槽）；否则 BT `[ESP401/610/103]` 发 12+ 个 `k=v` 越界写 .bss（auth 编译关 = 所有 BT 客户端 admin） | `WebSettings.cpp` |
| **B1** | `report_gcode_comment` 拷贝加 `sizeof(msg)-1` 上界；否则 BT 发 `(MSG:≥84字符)` 冲爆 `msg[80]` 栈 | `Report.cpp` |

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

### 6b. 三轮深审残余（2026-07-20，`ed1089d` 之后）

三轮修了 B1–B4 Blocker + 一批内存安全/解析加固（见 §3 与 `.omk/CODE_REVIEW_ISSUES.md`）。下列为**评估后未改**的残余项：

| 严重度 | 问题 | 位置 | 为何不改 / 建议 |
|--------|------|------|------|
| Warning | 换纸阻塞期 `protocol_service_during_blocking` 重入 `gc_execute_line`，非运动行（G92/G10/G90/G21）被嵌套执行覆写全局 `gc_block`/`gc_state`，M30 after-origin 判定读到污染值 → 首页/换页误判或写字偏移（与 B2 同源） | `Protocol.cpp:111-116`（有意保留非运动行） | 触及真机换纸触发语义，**无 HIL 不动**。修法备选：换纸期只 pump 实时+BT ack、短路完整 G 码行；或 `gc_execute_line` 重入 guard。见记忆 `gcode-global-state-reentrancy` |
| Warning | `stepper_pulse_func` + `motors_step/direction/unstep` + `StandardStepper::*` 非 `IRAM_ATTR`；运动中若发生 NVS/BT 写盘（flash cache off）→ cache-disabled panic | `Stepper.cpp:248` + `Motors.cpp` | 需"运动中写 NVS"才触发（通常空闲存盘），上游继承。待验证；可标 IRAM 或保证运动态不写 NVS |
| Warning | pen plotter 上 `PARKING_ENABLE`，安全门事件可能驱动 Z 到 -5.0 而非纯 hold | `Config.h:599` | 纸路 e-stop（`paper_request_user_stop`）是否抑制 parking 需 HIL 确认 |
| Design | 硬限位 ISR 调 `grbl_msg_sendf`/`mc_reset` **对产品不可触**：`custom_3axis_hr4988.h:81` 定义 `ENABLE_SOFTWARE_DEBOUNCE` → 走 `xQueueSendFromISR`。仅 Config.h 默认构建（其它机器）潜伏 | `Limits.cpp` | 其它机器若关软件去抖需修 |
| Design | BT 无配对 PIN；RF 范围内可触发 ESP910 换纸/motion | `BTConfig.cpp` | BT 写字机固有取舍，需产品签署 |
| ~~待验证~~ | ~~`paper_system_init` 早于 `settings_init`~~ | `Grbl.cpp` | **已核实无依赖**：init 只做传感器 GPIO/DAC（**不**早拉 I2S passthrough——过早 passthrough 会干扰 bootloader，见 `配置.md` §I2S），不读 `Setting*`/NVS。非 bug，勿再标记 |

**已确认仍成立（勿回退）：** S1 program_flow 清零；P1 G28/G38/`$H` defer；P2 Idle 门；F1 busy 单临界区；F3 0x18 仅 BT；B1/B2/B3/B4 三轮 Blocker 修复；W1 登录；W3/W4；M1 nvs_commit；M2 re-arm；M3 `$21`→`limits_init`；M5/N2 主轴；W9 段缓冲屏障。

### 6c. 四轮深审（2026-07-20，`4b29822`）— 错误逻辑专项

修了 12 处确定逻辑错误（详见 `.omk/CODE_REVIEW_ISSUES.md`）。**产品路径**新不变量（勿回退）：

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **L1** | `$G` 探针 modal 报告映射 `ProbeToward→G38.2 … ProbeAwayNoError→G38.5`（对齐 enum；无 G38.1） | `Report.cpp` `report_gcode_modes` |
| **L2** | `motors_set_disable` 仅在 mask 覆盖全部激活轴时才写共享 `STEPPERS_DISABLE_PIN`；部分掩码（`$MD` 单轴）不得禁全部电机 | `Motors.cpp` |
| **L3** | `map_uint32_t` 守卫 `in_max <= in_min`（不止 `==`），防反区间无符号下溢 | `NutsBolts.cpp` |
| **L4** | `jog_execute` 在 `cartesian_to_motors` 取消路径置 `*cancelledInflight=true` | `Jog.cpp` |

**非产品驱动/主轴修复**（库正确性，第三方驱动才编译）：Trinamic SPI `set_disable` 分派（修编译错）、`_disabled` 初始化、Dynamixel2 方向 swap、VFD memmove 方向 + max_rpm 除零守卫、10v deinit 引脚、Dac printf。

**评估后未改：** RMT 通道分配差一（`StandardStepper.cpp` — 修复会改变产品 channel 分配，对产品零收益，触步进硬件路径，无 HIL 不动）；RMT static config 运行时 `$` 重入、VFD 状态机 delay 不可达等（非活 bug / 待验证）。

**验证：** 产品 + `Root_4_Lite_RS485`(VFD) + `spi_daisy_4axis_xyza`(SPI Trinamic+enable) 三配置编译 SUCCESS。

### 6d. 五轮深审（2026-07-20）— 潜伏路径 + System 剩余分支

子代理因 API 额度耗尽中断，主审手工接手 System.cpp / ProcessSettings / I2SOut / 运动学。**产品路径**新不变量（勿回退）：

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **L5** | `user_defined_macro` 的 `toCharArray(line, sizeof(line)-1, 0)` 给尾部 `strcat("\r")` 留位；否则 254 字符宏 → `line[255]` 越界 | `System.cpp` |
| **L6** | `sys_calc_pwm_precision` 守卫 `freq==0`（除零）；`sys_set_analog` 用 `constrain_float(percent,0,100)`（防 uint32 转换溢出） | `System.cpp` |

**自审确认干净（无 bug）：** rt_exec 七个 bit 置位/消费/清除配对完整；`GrblCommand::action` 的 `_cmdChecker` 状态门禁正确；`do_command_or_setting` auth 检查；`doJog`/`report_gcode_modes` 缓冲有界；I2S 产品路径正确（passthrough **延迟到首次电机操作**，init 不早拉——见 `配置.md`）；CoreXY 正逆变换数学互逆一致。`paper_system_init` 早于 `settings_init` **确认无依赖**（只做传感器 GPIO/DAC，不读 Setting）。

**评估后未改（待验证 / 非产品）：** polar_coaster `motors_to_cartesian` 的 X 轴 `*-1` 与逆变换 `atan2(Y,X)` 疑似符号不一致（涉机械装配约定，产品不用 polar，不盲改）。

### 6e. 六轮深审（2026-07-20，`2f2a87f`）— WebUI 网络栈 + 报告热路径

WebUI 剩余网络栈子代理审 + 主审手工审报告热路径。**产品路径**新不变量（勿回退）：

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **L7** | `JSONencoder::quoted` 按 JSON 规则转义（`"`/`\`/控制字符）；否则含引号的设置值经 BT `[ESP400]/[ESP420]` 产生非法/可注入 JSON | `WebUI/JSONEncoder.cpp` |

**WiFi-only 修复**（潜伏至开 WiFi）：`NotificationsService::end` 清 `_token2`（原漏，凭证滞留 RAM）；`Web_Server::end` 先 `detachWS` 再 delete（防 CLIENT_WEBUI 输出 use-after-free）；`dec_level` 加下限（防 >16 深嵌套 count[] 越界）；`ESPResponse::_header_sent` 无条件初始化。

**自审确认干净（无 bug）：** `report_realtime_status`（BT 最热路径）全程 append lambda / snprintf 有界；`report_probe_parameters` / `report_ngc_parameters` / `report_util_axis_values` 缓冲有裕量；`protocol_main_loop` 启动/alarm 恢复/安全门/启动脚本逻辑正确；`run_once`/`reset_variables` 软复位重入（先 paper_on_soft_reset_restart 再 gc_init 复位 pending_m_code）闭环正确。Commands/Serial2Socket/ESPResponse 缓冲与 RX 环形边界正确（子代理验证）。

**评估后未改（设计 / 非产品）：** telnet 无认证（开 WiFi+Telnet 前需签署）；telnet 单客户端 early-return（MAX=1 无害）；`|FS:`/`|Ov:` 用 `%d` 配 uint32（值域内正确，风格）。

**验证：** 产品 SUCCESS；WiFi+HTTP+Notifications 交叉编译 SUCCESS（验证后还原 Config.h）。

### 6f. 七轮深审（2026-07-20，`8644f7a`）— Planner 数学 / Homing 流程 / Settings 定义

**产品路径**新不变量（勿回退）：

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **L8** | `plan_cycle_reinitialize` 在 `head==tail`（空缓冲）时早退；否则 Idle 下 override 变化会全环遍历污染 planned 指针 | `Planner.cpp` |
| **L9** | homing 设 machine origin 用 `lround`（对齐 Planner），非 float→int32 截断 | `Limits.cpp` |
| **L10** | homing fail-closed 零长度块：`$27` min≥0.001 + `limits_go_home` 检查 `plan_buffer_line==PLAN_EMPTY_BLOCK` 走 HomingFail，防 NaN + 无超时死循环 | `SettingsDefinitions.cpp` / `Limits.cpp` |

**非产品修复**（潜伏至主轴/多轴构建）：`Spindle/Delay/SpinDown` 默认值改用 SPINDOWN 宏（原误用 SPINUP）；`$33` PWM 频率 min 0→1（防 `calc_pwm_precision` 除零 bootloop）；`checkHomingEnable` 用 `setStringValue("0")` 而非 `setDefault()` 关软限位；`unit_vec[MAX_N_AXIS]` 零初始化。

**自审确认干净：** Planner 加减速数学逐条核对（梯形 v²=u²+2as 递推、junction 半角几何与方向映射、G93 逆时、除零/负 sqrt 保护）全部正确；homing 方向矩阵/阶段序列/多轴 sqrt(n) 速率/失败清理链/mpos 语义闭环正确；SettingsDefinitions 的 axis_settings 构造/索引、grblName 映射、POST 副作用状态前提（框架强制 Idle/Alarm）正确。Custom/paper_system.cpp 钩子（before_motion/after_origin 工件零判定、user_m30 fail-closed、按钮双击状态机跨任务临界区）逻辑正确、与真机语义一致。

**评估后未改（待验证 / HIL 敏感 / 非产品）：** approach 退出 `cycle_stop` 残留竞态（F-H3，上游同源）；主轴 spindown 时序 / startup-line 真执行副作用 / probe-protection 切 Off 不 detach（F-2/F-5/F-9，非产品，重构风险）。

**F-H1 已修（`3173f54`）：** homing 零长度块 NaN + 死循环，两层 fail-closed（`$27` min≥0.001 + `plan_buffer_line` 返回值检查）。见 L10。

### 6g. 八轮深审（2026-07-20，`ae19c68`）— Serial/BT 热路径 + MotionControl 剩余

**已修（确定、不改运动时序）：**
- `mc_reset` Cycle 分支改为仅 `alarm==None` 时写 `AbortCycle`（对齐 Homing 分支）；否则限位 ISR"先 EXEC_RESET 后置 alarm"路径中真实 alarm 被 AbortCycle 覆盖。产品 `$21` 默认关，潜伏。
- `client_read` 加 `client>=CLIENT_COUNT` 越界防护；删死声明 `check_action_command` 与遮蔽 `uxHighWaterMark`。

**关闭的待验证项：** mc_line 缓冲满等待仅查 `sys.abort` —— **确认安全**（所有异步 alarm 要么置 EXEC_RESET→abort，要么把 mc_line 扣在 `protocol_exec_rt_system` 临界环内，逐点核实）。

> ### ⚠️ 待决策（高价值，需 HIL）—— B1：planner 饥饿判据疑似全线取反
> **位置：** `Serial.cpp:213`、`Protocol.cpp:322/330`、`WebUI/BTState.cpp:48`（阈值均 8）
> **论点：** `plan_get_block_buffer_available()`（`Planner.cpp:437`）返回**空闲槽数** `(SIZE-1)-排队`，非排队数。故 `available < 8` ⟺ 排队≥242 ⟺ planner **接近满**。而注释意图是"饥饿（排队少=空闲多）时 taskYIELD 快搬 BT 字节"。若论点成立：满速流式时忙转抢 core1 CPU（制造卡顿），真饥饿时反而 `vTaskDelay` 慢轮询（抗饥饿失效）——恰是产品 BT 卡顿痛点方向。
> **矛盾证据（须一并查）：** `Protocol.cpp:427/736` 把同一返回值命名 `planner_free` 并按"空闲数"正确使用（`>= BUFFER_LOW_THRESHOLD`）。即代码库对该函数语义**两处相反**——一处当排队、一处当空闲。
> **未改原因：** 触及产品最敏感的 BT 抗饥饿运行时时序（与 M4/N7/换纸重入同属"无 HIL 不动"）。修法明确（统一改用 `plan_get_block_buffer_count()` 排队语义，或翻转为 `available > SIZE-1-8`），但 taskYIELD/vTaskDelay 触发条件反转会显著改变 core1 调度，**必须** HIL + `agent_gate` 验证卡顿实际改善后再落地。
> **HIL 观测手段：** 现成 `[BT-EOL gap]` 日志（`Protocol.cpp:151`，注意其 `B=` 也是空闲数）。

**其它未改（待验证/微优化）：** BT/INPUT 路径无写闸门、满时静默丢字节（B2，需镜像串口闸门，改接收背压语义待验证）；每字节双重临界区 P1、Uart0 TX 缓冲 0 阻塞 P2、strlen 重复 P3（性能项，P2 全局改法涉 TMC/VFD 半双工时序待验证）；MotionControl W1 硬限位竞态根因已由本轮 mc_reset 对称化缓解，但"先置 alarm 再 mc_reset"的调用点侧改动仍需 HIL；pen up/down 绕过软限位 W2（`$20` 默认关）；`PARKING_ENABLE` 未注释 W3（`can_park` 双重闸住，潜伏）。

**二轮 gate：** `agent_gate standard` overall=pass（31 层，2026-07-19）。注意 gate 覆盖 P1/协议核/纸路模型，**不**执行 F1/F3/M1/M5/N2/W 所在 `.cpp` —— 那些靠 `pio run -e release` 编译 + §8 HIL。

### 6h. 九轮深审（2026-07-20，`a5b9aec`）— WebSettings/WiFi/Auth + 纸路 M 命令

**产品路径**新不变量（勿回退）：

| ID | 不变量 | 关键位置 |
|----|--------|----------|
| **L11** | `$LocalFS/ListJSON` occupation 计算守卫 `totalBytes()==0`；BT-only 产品 SPIFFS 未挂载，否则整数除零 panic 重启（BT 可触，auth 内 LEVEL_USER 即可达） | `WebSettings.cpp` `listLocalFilesJSON` |

**自审确认干净：** `paper_system_mcode`（M701/704/711/712/713/716/721 分派）门禁完整——M711-716 各有 `paper_reject_if_auto_change_running` + Idle + `protocol_buffer_synchronize`；M721 缺显式门禁但 `paper_auto_change` 入口有 PAPER_DISABLED/Idle/原子 busy 三道兜底（有意设计）。`InputBuffer` 环形缓冲逐行正确。WebSettings 的 split_params 调用方全部检查返回值、auth 门禁三查找路径统一无漏门、密码 `isPassword()` 遮蔽、BTConfig MAC 缓冲精确。

**评估后未改（非产品 / 认证语义 / 待验证）：** W2 `$RST=@` 越权重置（`wifi_config.reset_settings` 仅 ENABLE_WIFI 链接，产品不可触）；W3 `remove_password` 子串匹配应为行尾 token（真实缺陷，改认证语义需确认上位机行为）；W10 ESP700 截断宏行执行（SPIFFS 宏路径，产品未挂载不可达）；W5-W12 其余 WiFi/notifications/SD-only；ESP550 未注册等 12 条 suggestion。

**九轮 gate：** `agent_gate standard` overall=pass（33 层，2026-07-20 @ HEAD 全绿，`json_feed_hold_tcp` 本次 PASS 确认为 TCP flaky 非回归）。

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

### 三轮修复项 HIL（B1/B2/B3/B4 + 段缓冲屏障 —— 编译过，未真机）

`ed1089d` 只经 `pio run -e release` 编译 + 审查；`agent_gate standard` 唯一硬失败 `json_feed_hold_tcp` 与本改动无关（grblHAL 参考模型 TCP 时序 flaky，非产品固件）。发货前须真机：

| 步骤 | 期望 |
|------|------|
| BT 发 `(MSG:<90 字符>)` | 不崩溃；注释截断到 ~75 字符（B1） |
| 流水线发 `M711 P50 G2`（无 F，G2 会 FAIL）后接 `G0 X10` | G2 报错；`G0` **不**触发走纸；无残留（B2） |
| 换纸中/后发 M800，再发普通 G 码 | 授权仅一次；后续行不误设授权（B2） |
| `$RST=$` 后查 `$N0` / `$N1` | 启动行**保留**（B3） |
| BT 发 `[ESP401]` 带 12+ 个 `k=v` | 返回错误，不崩溃（B4） |
| Cycle 中抓 `?` 状态流 | WCO/Ov 出现频率回落到 busy 档（W1 修复） |
| 多行连续写字（回归） | 步进无异常卡顿（验证段缓冲屏障无副作用；W9） |

### 6i. 正向锚边 + 量产镜像（2026-07-24，`31db6d8`…`618b1fb` + 深审加固）

| Commit | 内容 |
|--------|------|
| `31db6d8` | Step 6 正向锚边；移除 Step 7 / `PANEL_DIR_REVERSE` / `PANEL_BACK_STEPS_MAX`；引入 `PANEL_EDGE_APPROACH_STEPS` + `PANEL_LOCATE_*` |
| `41cd186` | `merge_firmware.py` → `firmware_full_0x0.bin`（release post） |
| `180c2e7` | `EDGE_AMBIGUOUS`（steps==0）fail-closed；merge 越界检查 |
| `382ac2c` | Ambiguous 耗重试；仅 attempt==0 写历史；慢窗日志区分 |
| `618b1fb` | cleanup / soft-reset 清 `paper_panel_edge_valid`；merge 缺段显式报错；文档对齐 |
| `ae7f2d0` | 成功路径统一 Z=0 sync；对称 `PaperSensorLevel` + `PAPER_SENSOR_LOST_STREAK` 原地确认 Absent |
| （本补丁） | 采边成功以循环内 `edge_confirmed` 为准，禁止确认后再采样误杀真边沿 |

**不变量 P6**（§3）：勿无补偿恢复反向找边。Cursor/Bugbot/FluidNC#756 深审后：Z 同步 + 对称无纸确认 + 确认后不复采。`agent_gate standard` 须复绿。

**初始化不会因 P6 报错：** `grbl_init` / 首次 `run_once` 不调边沿清理；软复位仅静默清 `valid`（无 `[PaperStatus]`）。I2S/bootloader 约束仍以 `配置.md` 为准（init 不早拉 passthrough）——与边沿历史无关。

**HIL 必做（SIL 不覆盖）：**

| 步骤 | 期望 |
|------|------|
| 连续换纸 5–10 页 | 对位一致；串口可见首页 learn、后续页 fast approach |
| 相对旧固件目视/尺量进纸终点 | 停边侧已从「有纸」改为「无纸」+ 无换向回差 → **`PANEL_FINAL_STEPS` 可能需重标定**（默认仍 320） |
| 短纸 / 人为 `EDGE_PASSED` | 非末次 backoff 重学或末次 `[PaperStatus] 3`；**不得**错位却报 0 |
| 换纸中 feed-hold / USB `0x18` 中止后再换 | 下页全程重学（历史已清）；无脏快进 |
| 烧录 `firmware_full_0x0.bin` @0x0 | 能起机；`$I` 正常（量产路径） |

详变：`doc/变更说明_正向锚边与量产镜像_2026-07-24.md`；调参路线：`docs/HIL_TUNING_ROADMAP.md` 项 4。

## 9. 相关文档

| 文档 | 用途 |
|------|------|
| `Agents.md` | 架构、构建、门禁总则 |
| `docs/ACCEPTANCE_CHECKLIST.md` | 真机验收勾选 |
| `docs/FIRMWARE_CI.md` | **GitHub 自动编译 / Artifacts / Release 下载** |
| `docs/HIL_TUNING_ROADMAP.md` | HIL 调参（B1 / Uart0 / 运动 / **P6 对位**） |
| `tools/SIMULATION.md` | fz 入口 |
| `配置.md` | 默认机硬件/引脚/**换纸流程与 init 禁忌**（产品侧「wiki」） |
| `doc/变更说明_正向锚边与量产镜像_2026-07-24.md` | P6 + 量产镜像变更说明 |
| `doc/Commands.txt` | `[ESP…]` 命令 |
| fz `docs/AGENT_VIBE_CODING.md` | 仿真 vibe 手册 |

历史审查草稿（`.omk/`、`*-review-*.md`、`a2a_workorder_*.md`）**不是**源码真相；以本文件 + git 历史为准。产品操作说明以 **`配置.md`** 为准（fork 无独立 GitHub Wiki 内容仓）。
