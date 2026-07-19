# Code Review — Grbl_Esp32 全项目深度审查（第二轮）

**日期：** 2026-07-19  
**分支：** `Branch_736afa70` @ `03def2c`  
**工作树额外：** 未提交的 `Settings.cpp` / `Settings.h`（M1 强化）  
**范围：** 产品全路径（纸路/协议/GCode、WebUI/安全、运动/Settings、CI）  
**方式：** 主审 + 3 路并行 deep review（paper/protocol、WebUI security、motion/core）+ Settings 主线复核  
**模式：** 只读；未改固件逻辑（本文件为报告）

---

## Executive Summary

| 维度 | 结论 |
|------|------|
| 上一轮 Blocker/High 修复 | **S1/P1/P2/W1–W4/M1–M3/M6/M7 均源码确认已修** |
| 明确未修 | **M5**（PWM min>min typo 仍在）；**P6**（0x18 全 client 忽略，设计如此） |
| 新发现 | **0 Blocker · 0 产品默认 High · 若干 Medium（含 1 并发 TOCTOU + 0x18 语义）· 多 Low** |
| 未提交风险 | **M1 的 nvs_commit 强化仍在工作树，未进 commit** — 丢改则回退 |
| Ready to ship? | **产品默认（仅 BT + 纸路）接近可测**；建议先提交 Settings、修 P6 语义、再做缺纸/急停 HIL |

**默认产品配置（影响风险落地）：**

| 开关 | 默认 | 含义 |
|------|------|------|
| `ENABLE_BLUETOOTH` | 开 | 无认证 G-code（CNC 惯例） |
| `ENABLE_WIFI` / `HTTP` / `OTA` / `TELNET` / `SD` | 关 | Web 类问题多为潜伏 |
| `SPINDLE_TYPE` | `NONE`（NullSpindle） | PWM/激光类问题潜伏 |
| 机器 | `custom_3axis_hr4988` | 纸路 + I2S 595；限位默认关 |

---

## 一、上一轮票状态（HEAD 再读）

| ID | 原判定 | 本轮 | 证据锚点 | 备注 |
|----|--------|------|----------|------|
| **S1** | Blocker | **已修** | `GCode.cpp:1784–1789` 失败路径先 `program_flow=Running` 再 return | 粘滞 CompletedM30 已闭合 |
| **P1** | High | **已修** | `ProtocolDecisionCore.h:270–285` + `Protocol.cpp:166` | G28/G30/G38 + `$H`/`$J` 均 defer；在 `execute_line` 前 |
| **P2** | High | **已修** | `PaperSystem.cpp:596–606` Idle 门禁 | 统一入口 |
| **P3** | Medium | **已修** | Custom `paper_system.cpp` 工件坐标 | 非零 G54 可触发首页 |
| **P4** | Medium | **已修** | `PaperSystem.cpp:963` 成功统一 mark | M721/ESP910 走同一成功点 |
| **P5** | Medium | **部分** | disconnect 用 reducer；ack/poll 手写 | 见 **F2** |
| **P6** | Medium | **未修（设计）** | `Serial.cpp:268–274` + `PaperSystem.cpp:82–88` | 见 **F3** |
| **P7** | Medium | **已修** | `Serial.cpp` FeedHold/SafetyDoor → `paper_request_user_stop` | |
| **W1** | High | **已修** | `WebServer.cpp:606–612` `!(admin_ok \|\| user_ok)` | |
| **W2** | High | **已修（有意 WG）** | `WebSettings.cpp:1160–1165` ESP910=WG；911–930=WU | 产品按钮/BT 有意保留 ESP910 WG |
| **W3** | High | **已修** | `Serial2Socket.cpp:96–117` 分块 + room | |
| **W4** | High | **已修** | SPIFFS list/upload + ESP700/701 + SD 均有 traversal | |
| **W5** | Medium | **已修** | `WebServer.cpp:327–332` `write(buf,v)` | |
| **W7** | Medium | **已修** | `=`→`==` 已清 | |
| **M1** | High | **已修（未提交）** | 工作树 `Settings.cpp` `nvs_commit_settings` + 各 setter | **务必 commit** |
| **M2** | High→Med | **已修** | `Stepper.cpp:213–238` overrun + 始终 re-arm | 残留见 **N1** |
| **M3** | High→Med | **已修** | `SettingsDefinitions.cpp:243–276` | |
| **M4** | Medium | **残留** | `Config.h` `BLOCK_BUFFER_SIZE=250` | 无算法收敛 |
| **M5** | Medium | **未修** | `PWMSpindle.cpp:95` min>min | 产品 NullSpindle → 潜伏 |
| **M6** | Medium | **已修** | underrun 仅 laser rate mode 关 PWM | |
| **M7** | Medium | **已修** | `I2SOut` atomic port data | 模式切换微窗残留 |

---

## 二、新发现（按优先级）

### 🟠 Medium

#### F3 / P6 — 换纸 60s 内 **所有 client** 的 0x18 软复位被吞

| 字段 | 内容 |
|------|------|
| **Severity** | **Medium（产品默认可触）** |
| **File** | `Serial.cpp:268–274`；`PaperSystem.cpp:82–88`；窗口 `PaperSystem.cpp:630`（+60s） |
| **证据** | `paper_should_ignore_host_reset()` 只看 `running + deadline`，**不区分 client**；`execute_realtime_command(Cmd::Reset)` 命中即 `break` |
| **后果** | 换纸最长约 60s 内，USB/串口上位机发的急停软复位同样失效。FeedHold/SafetyDoor 仍可中止纸路，但 **0x18 通道对所有 client 关闭** |
| **Fix** | 仅 `client == CLIENT_BT` 时忽略；或缩短窗口/仅弹纸阶段；或忽略计数上限 |

#### F1 — `paper_auto_change()` busy 检查与 `running=true` 非同一临界区（TOCTOU）

| 字段 | 内容 |
|------|------|
| **Severity** | **Medium（开 WiFi+HTTP 时）；默认仅 BT 时偏低** |
| **File** | `PaperSystem.cpp:607` vs `624–625` |
| **证据** | 607 在临界区外读 `paper_auto_change_running`；624–625 才 `portENTER_CRITICAL` 置 true。注释自陈临界区只防 stop/running 窄序 |
| **并发面** | 主协议任务（M30/BT/`[ESP910]` 串口）与 WebServer 任务（HTTP `[ESP910]`）可交错；`protocol_buffer_synchronize()` 在置 running 前还会 yield 实时字符 |
| **后果** | 理论双入 bit-bang 同一纸路电机 / I2S |
| **Fix** | busy 检查 + `running=true` + 清 stop **同一** `portENTER/EXIT_CRITICAL`；失败路径在临界区内 return Busy |

#### M5 — PWMSpindle min/max 告警 typo（仍未修）

| 字段 | 内容 |
|------|------|
| **Severity** | Medium（潜伏；产品 `SPINDLE_TYPE=NONE`） |
| **File** | `Spindles/PWMSpindle.cpp:95` |
| **证据** | `spindle_pwm_min_value->get() > spindle_pwm_min_value->get()` 恒 false |
| **Fix** | 右侧改为 `spindle_pwm_max_value->get()` |

#### N2 — `map_uint32_t` 除零 UB（主轴路径）

| 字段 | 内容 |
|------|------|
| **Severity** | Medium（潜伏；产品 NullSpindle） |
| **File** | `NutsBolts.cpp:193–195`；调用 `PWMSpindle.cpp:147` 等 |
| **后果** | `$30==$31` 时 S 指令除零 |
| **Fix** | `if (in_max == in_min) return out_min;` 并在 spindle init 校验 |

#### M1-R — `Coordinates::set` 提交失败静默；工作树未入库

| 字段 | 内容 |
|------|------|
| **Severity** | Medium（流程/可靠性） |
| **File** | 工作树 `Settings.cpp:765–772` |
| **证据** | `nvs_set_blob` 成功才 `nvs_commit_settings()`；commit 失败无返回/告警。RAM 已 `memcpy` |
| **另** | **整份 M1 修复未 commit**；`git status` 显示 `Settings.cpp`/`Settings.h` 仍 modified |
| **Fix** | commit 当前 Settings；commit 失败时 `grbl_sendf` 并考虑回滚或返回错误（G10 路径） |

### 🟡 Low

| ID | 问题 | 位置 | 说明 |
|----|------|------|------|
| **F2** | BT ack 双实现（reducer vs 手写） | `PaperBtAckCore.h` vs `PaperSystem.cpp:1002–1060` | 易腐化；当前未见活 bug |
| **F4** | after_origin 只判 X/Y，before_motion 判 X/Y/Z | Custom `paper_system.cpp` | 不对称；维护易误判 |
| **F5** | license/paper 弱符号默认 fail-open | `Grbl.cpp:150+` | Custom 覆盖后安全；构建错配时运动全开 |
| **W-N1** | `strncpy` 未写 `line[255]='\0'` | `WebServer.cpp:492–495` | 潜伏 HTTP |
| **W-N2** | SD `.gz` 路径在 strip `/SD` 前拼接 | `WebServer.cpp:293` vs 303 | 功能瑕疵，非安全 |
| **N1** | step_isr_overrun 非原子 RMW | `Stepper.cpp:217–232` | 极端嵌套少计 1 tick；建议 `fetch_add` |
| **N3** | 强制 1 步 + AMASS-0 步率失真 | `Stepper.cpp:884–921` | 短笔画可闻抖动 |
| **N4** | `limits_init` 重复 attach 未先 detach | `Limits.cpp:263–300` | 依赖 Arduino 覆盖语义 |
| **N6** | isrPeriod 16-bit 静默截断 | `Stepper.cpp:924–926` | 极慢进给跑快 |
| **N7 / M4** | planner 全量 replan + buffer=250 | `Planner.cpp` / `Config.h` | 主循环长占用 |
| **CI-1** | `permissions: contents: write` 全局 | `.github/workflows/firmware-build.yml:20–21` | PR 构建也拿写权限；宜 job 级限制 |
| **CI-2** | test_drive 仅 `rm -rf .pio/build/release/src` | 同文件 ~91–97 | 依赖缓存时偶发串味风险；更稳妥 `pio run -t clean` 或独立 env |

---

## 三、Settings 工作树专项（M1 强化）

当前 diff 意图正确，审查结论：

| 点 | 判定 |
|----|------|
| `nvs_commit_settings()` 返回 bool + 日志 | 好 |
| `nvs_erase_key_settings`：NOT_FOUND 当成功；否则 erase+commit | 好 |
| 各 setter：写失败返回 `NvsSetFailed`；成功后同步 `_storedValue` | 好 |
| `FloatSetting` 构造初始化 `_storedValue` | 好（修未初始化） |
| `Coordinates` 零初始化 + load 要求 `len==sizeof` | 好（拒残缺 blob） |
| `eraseNVS` 移出 header + commit | 好 |
| `Coordinates::set` commit 失败静默 | 见 M1-R |
| **未 git add/commit** | **流程风险：重启/切分支可丢** |

---

## 四、已验证仍然正确

| 项 | 状态 |
|----|------|
| S1 fail-closed program_flow | OK |
| P1 should_defer_motion 覆盖系统回零/探针 | OK |
| P2 Idle 门禁 | OK |
| notCycleOrHold \|\| | OK |
| Busy 重入返回非 Ok | OK（逻辑层；原子性见 F1） |
| 超时路径走 `paper_change_cleanup_common` | OK |
| Feed-hold / SafetyDoor → paper_request_user_stop | OK |
| SD/SPIFFS path traversal helpers | OK |
| Serial2Socket 分块 | OK |
| Web 登录括号 | OK |
| Stepper 始终 re-arm + st_reset 清 overrun | OK |
| $21 POST → limits_init；$20 依赖 $22 | OK |
| I2S port_data atomic | OK |

---

## 四·补 — 本轮已落地修复（commit）

| commit | 内容 |
|--------|------|
| `ad4d1a6` | M1 nvs_commit 全路径 + `_storedValue` 同步；F3/P6 0x18 仅 BT；F1 busy TOCTOU 单临界区；M5 PWM min>max；N2 map 除零守卫 |
| `d070ae2` | 本审查报告 |
| `59f4304` | W-N1 strncpy NUL 终止；W-N2 SD `.gz` 路径重算；CI 最小权限 + test_drive `-t clean` |

**验证：** `pio run -e release` SUCCESS（Flash 74.7%）。

**评估后不改（有据）：**
- **F4** — before_motion（执行前，X/Y/Z 齐全判工件零）与 after_origin（执行后，判 X/Y 落点）是互补钩子，非 bug；改动触及真机调过的换纸触发语义，无 HIL 不动。
- **F2** — BT ack 双实现无活 bug，属 EMI 敏感产品路径，投机重构风险 > 收益。
- **N1/N3/N4/N6** — ESP32 同优先级 timer ISR 不自嵌套 + busy CAS 兜底 + CAP 有界，overrun RMW 竞态硬件不可达；其余步率失真属硬件层，无 HIL 不动。
- **M4/N7** — planner buffer=250 为吞吐/负载权衡，需实测数据而非静态改。

---

## 五、Must Fix 优先级（原始，供追踪）

1. **提交 Settings（M1）** — 避免工作树丢失  
2. **F3/P6** — 0x18 仅忽略 BT（或缩窗）— 产品默认可触  
3. **F1** — busy/`running` 同一临界区 — 开网前必做；默认也建议修  
4. **M5 + N2** — PWM typo + map 除零 — 通用机/将来开主轴时必修  
5. **F2** — BT ack 统一 reducer — 防腐化  
6. **CI-1** — release 步骤再提 `contents: write`  
7. Medium 残留 M4/N1 与 HIL  

---

## 六、测试建议（修后 / ship 前）

| 场景 | 期望 |
|------|------|
| M30 → 缺纸/急停 → 再 `G0 X10` | 仅一次失败；不重复 PAGE_END/换纸 |
| 换纸中灌 `G28` / `$H` / `G38.2` | 拒绝/defer，XYZ 不动 |
| 换纸中 USB 发 0x18（修 F3 后） | **应**能软复位；BT 误发可忽略 |
| Alarm 下 `[ESP910]` / M721 | 拒绝 |
| `$100` 改后断电 | 值仍在（验证 nvs_commit 已入库固件） |
| 并发：换纸中 HTTP ESP910（若开 WiFi） | 返回 Busy，不双入 |
| `agent_gate` standard / `pio run -e release` | pass |

---

## 七、本轮未做

- 未 flash、未跑 PlatformIO / `agent_gate` / 真机 HIL  
- 未对上游 bdring/Grbl_Esp32 全量 diff  
- 未审计 WebUI 前端静态资源与 TLS  

---

## 八、结论

上一轮 **S1 Blocker 与纸路/Web/Settings/Stepper 主票已在源码落地**；本轮无新的产品默认 Blocker。  

剩余最值得立刻处理的是：

1. **把未提交的 Settings（M1）收进版本库**  
2. **P6/F3：0x18 忽略范围过宽（产品可触）**  
3. **F1：换纸 busy TOCTOU（开 WiFi 后升格）**  
4. **M5 typo + map 除零（潜伏主轴）**

**Ready to ship?**  
- **仅 BT 写字机默认配置：** 接近 — 建议完成 (1)(2) 并做缺纸/急停多行 HIL 后再标 yes。  
- **开 WiFi/HTTP 前：** 必须 (3) + 确认 W1–W4 已在目标镜像。  
- **通用 CNC/主轴机：** 必须 (4)。

---

# Code Review — 第三轮全项目深度审查

**日期：** 2026-07-20  
**分支：** `Branch_736afa70` @ `add0416`（工作树干净）  
**范围：** 整仓 HEAD，7 路并行子系统 deep review + 主审交叉核实  
**验证：** `pio run -e release` SUCCESS（RAM 28.6% / Flash 74.7%）

## 本轮新发现（前两轮未捕获）

### 已落地修复（源码，本次 commit）

| ID | 位置 | 问题 | 修复 | 产品可触 |
|----|------|------|------|:---:|
| **B1** | `Report.cpp:818` `report_gcode_comment` | `msg[80]` 无界拷贝，BT 发 `(MSG:≥84字符)` 冲爆栈 | 拷贝加 `sizeof(msg)-1` 上界 | ✅ 两代理印证 |
| **B2** | `GCode.cpp` `pending_m_code` static | M700-721/M800 在 STEP2 置位后若该行 FAIL 则残留，下一行任意 G 码用本行 P/Q 误触发纸路/授权；软复位清不掉 | `gc_execute_line` 入口 + `gc_init()` 复位；删死变量 | ✅ 核心纸路 |
| **B3** | `ProcessSettings.cpp:53` | `$RST=$` 保护判断比 `"Line0"` 而真实 name `"GCode/Line0"`，永不匹配→每次抹掉 $N0/$N1 | 改比 `GCode/Line0`/`GCode/Line1` | ✅ |
| **B4** | `WebSettings.cpp:113` `split_params` | `static keyval_t params[10]` 无边界，BT(auth 关=admin) 发 12+ 个 `k=v` 越界写 .bss | 循环加 `i>=9` 边界 | ✅ |
| **W1** | `Report.cpp:736,759` | WCO/OVR 两 switch 缺 `break`，busy 刷新档被 idle 档覆盖→Cycle 期状态行膨胀 3×，加剧 BT planner 饿死 | 两处补 `break` | ✅ 两代理印证 |
| **W5** | `ProcessSettings.cpp` `motor_disable` | `strdup` 后 `trim` 移指针、从不 free，guest 可反复调→堆耗尽 | 保留 base 指针，各返回路径 free | ✅ |
| **W7** | `UserOutput.cpp:46` `set_level` | 守卫 `_number`(0-3) 而非 `_pin`→永假→引脚未定义仍 `digitalWrite(255)` | 改守卫 `_pin==UNDEFINED_PIN` 且与 isOn 无关 | ✅ |
| **W8** | `SDCard.cpp:105`/`Report.cpp:255`/`Report.cpp:846`/`Error.cpp` | SD readFileLine 差一栈溢出；verbose 错误 NULL 传 `%s`；report_hex_msg 溢出；Eol=111 缺映射 | SD 预留终止符字节；NULL 防护；snprintf+offset 加界；补 Eol 映射 | SD/VFD 默认关 |
| **Sug** | `GCode.cpp` | G38.5 误映射 ProbeAway→ProbeAwayNoError；命令号 uint8 回绕别名(G256→G0)→越界哨兵拒绝；G93 恒真 `\|\|`→`&&` | 逐条修 | 探针默认无 |
| **Sug** | `Settings.cpp:446,542` | StringSetting 长度校验要求 min&&max 同时非零→改独立评估；enum 数值 uint8 回绕→宽类型+范围校验 | 逐条修 | ✅ |
| **W9** | `Stepper.cpp:79,929,291` | `segment_buffer_head` 非 volatile、payload 发布无屏障→编译器可重排，ISR 读半写段 | head 加 volatile + release/acquire 屏障 | 待验证(上游继承) |

### 降级/豁免（评估后不改，有据）

- **Limits-W2 / Protocol-F-W3**（硬限位 ISR 调 `grbl_msg_sendf`/`mc_reset`）：**产品机器 `custom_3axis_hr4988.h:81` 定义 `ENABLE_SOFTWARE_DEBOUNCE`**，走 `xQueueSendFromISR` 安全路径→对产品不可触，降为 Config.h 默认构建（其它机器）潜伏缺陷。
- **W2 换纸期 G 码解析器重入**（`Protocol.cpp:111-116` 有意保留 G92/G10 等非运动行）：与 B2 同源（全局 parser 状态被重入污染）。触及真机换纸触发语义，**无 HIL 不动**；修法备选：换纸期短路非实时行 / `gc_execute_line` 重入 guard。
- **W10 `stepper_pulse_func`+电机原语非 IRAM**：NVS 写盘(cache off)与步进 ISR 重叠时 panic，需"运动中写 NVS"才触发（通常空闲时存盘），上游继承，标待验证。
- **W11 pen plotter 上 `PARKING_ENABLE`**：安全门事件可能驱动 Z 到 -5.0；纸路 e-stop 是否抑制需 HIL 确认。
- **BT 无配对 PIN**（W3/W4）：RF 范围内可触发 ESP910 换纸/motion，属 BT 写字机固有设计取舍，需产品签署。

## HIL 建议（本轮修复项）

| 场景 | 期望 |
|------|------|
| BT 发 `(MSG:<90 字符>)` | 不崩溃，注释截断到 ~75 字符 |
| 流水线发 `M711 P50 G2`(无 F) 后接 `G0 X10` | G2 报错；G0 **不**触发走纸；无残留 |
| 换纸中/后发 M800，再发普通 G 码 | 授权仅一次；后续行不误设授权 |
| `$RST=$` 后查 `$N0`/`$N1` | 启动行**保留** |
| BT 发 `[ESP401]` 12+ 个 `k=v` | 返回错误，不崩溃 |
| Cycle 中抓 `?` 状态流 | WCO/Ov 出现频率回落（busy 档） |
| 多行连续写字（回归） | 步进无异常卡顿（验证段缓冲屏障无副作用） |
