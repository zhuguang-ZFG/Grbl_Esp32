# 写字机固件验收清单（Grbl_Esp32 / 纸路）

对照本仓库自定义纸路 + 社区 Grbl/FluidNC 流控实践。  
FluidNC 状态位说明见：[Serial Protocol — Bf 缓冲](https://wiki.fluidnc.com/support/serial_protocol)。

| 项 | 值 |
|----|-----|
| 分支 | `Branch_736afa70` |
| 深度审查修复 | **`801761e`**（S1–M3；host SIL standard 已绿） |
| 二轮 / 三轮修复 | `ad4d1a6`+`59f4304`（F1/F3/M1/M5/N2）；**`ed1089d`**（B1–B4 Blocker + 内存安全 + 段缓冲屏障，见 §7b） |
| 正向锚边 P6 | **`31db6d8`…`618b1fb`**（见 §1.9；交接 `AGENT_HANDOFF` §6i） |
| 更早相关 | `b036c81` M30 后跳过原点二次换纸；`468d302` parser reset 清标志；`6f44dce` SD 路径穿越 |
| Agent 交接 | **`docs/AGENT_HANDOFF.md`**（不变量 / 勿回退表） |
| SIL | fz 仓 `agent_gate` — **≠** 本清单真机项 |

## 0. 编译

- [ ] `pio run -e release` 成功（参考：RAM ~28.6% / Flash ~74.7%，`801761e` 附近）
- [ ] Flash/RAM 无异常暴涨
- [ ] （可选）`test_drive.h` 交叉编译：`$env:PLATFORMIO_BUILD_FLAGS='-DMACHINE_FILENAME=test_drive.h'; pio run`

## 1. 换纸 / M30（P0 — 必测）

| # | 步骤 | 期望 |
|---|------|------|
| 1.1 | 蓝牙流式写满一页，发 **M30** | 换纸一次；`[PaperM30] Auto paper change completed` 或等价 |
| 1.2 | M30 后上位机再发 **G0 X0 Y0 Z0**（或回原点） | **不再二次换纸** |
| 1.2b | M30 与原点之间夹 **G90/G21/注释** | 仍只换纸一次 |
| 1.2c | 页末 M30 | 蓝牙侧 `[MSG:PAGE_END_IMMINENT]`（`CLIENT_ALL`） |
| 1.3 | 软复位（0x18）后再回原点 | 不因残留 `paper_m30_just_completed` 误跳过合法换纸 |
| 1.4 | 同一行内重复触发路径 | 同线去重，不连换两次 |
| **1.5** | **M30 时人为缺纸 / 卡纸 / feed-hold 中止换纸，再发任意 G 行** | **只失败一次**；**不得**连环 PAGE_END / 反复换纸（S1） |
| **1.6** | 换纸进行中灌 **G28** 或 **`$H`**（或探针 G38） | **拒绝/defer**（IdleError 类），XYZ 不与纸路并发（P1） |
| **1.7** | **Alarm / 非 Idle** 下发 **M721** 或 **`[ESP910]`** | **拒绝**（Idle 门禁）（P2） |
| 1.8 | 换纸中发 feed-hold / 安全门字符 | 纸路急停中止换纸，不误显示 Hold 却电机仍转 |
| **1.9** | **连续换纸 5–10 页（正向锚边 P6）** | 对位一致；首页 learn、后续页可走快进+慢采；**无**错位却 `[PaperStatus] 0` |
| **1.9b** | **相对改前固件核对进纸终点** | 停边侧变更 + 无换向回差 → 若整体偏前/偏后，只调 `PANEL_FINAL_STEPS` / `PANEL_EDGE_APPROACH_STEPS` / `PANEL_FINAL_SETTLE_MS`（勿恢复 Step7、勿 backlash） |
| **1.9b2** | **对位结束立刻失能回弹** | 串口有 `[PaperAuto-8] Settle hold`；失能后纸边无明显回退 |
| **1.9c** | **短纸或历史偏长触发 EDGE_PASSED / Ambiguous** | 末次须 `[PaperStatus] 3` + cleanup；非末次可 backoff 重学 |
| **1.9d** | **换纸中止（feed-hold）或软复位后再换一页** | 下页全程重学（边沿历史已清）；无脏快进冲边 |

## 2. 物理键

| # | 步骤 | 期望 |
|---|------|------|
| 2.1 | Idle 双击换纸键（间隔 0.5–5s） | 注入 `[ESP910]`，走自动换纸（ESP910 保持 WG） |
| 2.2 | 换纸刚结束后 **500ms 内** 按键 | **忽略**（冷却） |
| 2.3 | 换纸进行中再按 | 忽略 / already running |
| 2.4 | 蓝牙刚连接后 1s 内按键 | BT suppress，忽略 |

## 3. SEG / 缓冲

社区侧：主机应用 `?` 状态里的 **Bf** 做流控。  
本固件：`BLOCK_BUFFER_SIZE=250`，`SEGMENT_BUFFER_SIZE=48`。

| # | 步骤 | 期望 |
|---|------|------|
| 3.1 | 正常 BT 连续写 | 不应频繁 `[SEG underflow]` |
| 3.2 | M30 / 换纸页间断流 | `[BT] Page-gap/seg empty ...`，非硬 underflow 刷屏 |
| 3.3 | 真饥饿（上位机停发且非换纸） | `[SEG underflow]`，并尽量 reprime |
| 3.4 | 串口监视 | `LOW_BUFFER` 仅 DEBUG 构建（避免占 BT） |

## 4. 与上位机约定

- 实时状态：`?` → 标准 Grbl 风格；可选 `Bf:`
- 页末：`[MSG:PAGE_END_IMMINENT]`（M30 同步前）
- 换纸中：主机应停发运动行；固件 defer G0–G3 + G28/G30/G38 + `$H`/`$J`
- 换纸中误发 0x18 可能被短时忽略；**用户急停用 feed-hold**

## 5. 设置 / 限位（有限位硬件时）

| # | 步骤 | 期望 |
|---|------|------|
| 5.1 | Idle 下 `$100` 等改设置后断电再上电 | 值仍在（`nvs_commit`） |
| 5.2 | Idle 下改 `$21` 硬限位 on/off | **无需复位**即可挂/卸 ISR（`limits_init`） |
| 5.3 | `$22=0` 时设 `$20=1` | **拒绝**；提示需先开 homing |
| 5.4 | `$22=0` 且 soft 已开 | soft limits 被清掉 |

## 6. WebUI（仅当编译打开 WiFi+HTTP 时）

| # | 步骤 | 期望 |
|---|------|------|
| 6.1 | user / admin 登录 | **两者均可**（W1） |
| 6.2 | SPIFFS `/files?path=../` 或 ESP700 `../` | **403 / Forbidden** |
| 6.3 | Guest 调 ESP911 点动 | **鉴权失败**；ESP910 仍可（产品 WG） |

默认产品配置 WiFi 关 → §6 可标 N/A。

## 7. 回归烟测

- [ ] 归位 / 限位无异常（若接限位）
- [ ] 探针路径无误触发（若接探针）
- [ ] 面板电机：换纸后面板不蠕动（595 使能策略）
- [ ] 纸检测 M701/M704 正常
- [ ] 未授权时运动被挡；`M800 P…` 后可运动

### 7b. 三轮审查修复（`ed1089d` — P0 必测）

- [ ] BT 发 `(MSG:<90 字符>)` → 不崩溃、不重启（B1 栈溢出）
- [ ] 流式发 `M711 P50 G2`（无 F）后接 `G0 X10` → G2 报错、`G0` **不**走纸、无残留（B2）
- [ ] 换纸/M30 后发 M800 再发普通 G 码 → 授权仅一次、后续行不误设授权（B2）
- [ ] `$RST=$` 后 `$N0`/`$N1` **仍在**（B3 启动行保留）
- [ ] BT 发 `[ESP401]` 带 12+ 个 `k=v` → 返回错误、不崩溃（B4）
- [ ] 多行连续写字回归 → 步进无异常卡顿（段缓冲 release/acquire 屏障无副作用；W9）

## 8. 已知非阻塞 / 设计折中

- SEG `planner_free > 70` 启发式兜底仍在
- ESP910 **WG**：按钮 + BT guest；开 HTTP 时局域网可触发换纸（已知）
- 密码明文 / 无 TLS
- 完整纸路状态机自动化仍弱 → **以本清单 HIL 为准**
- Host SIL 绿 **不能**单独勾选「可量产」
- 换纸阻塞期非运动行（G92/G10 等）仍会被嵌套执行（三轮 W2，与 B2 同源）；未改，需 HIL 确认是否致写字偏移
- BT 无配对 PIN；RF 范围内可触发 ESP910（固有取舍，需签署）

## 9. 审查与仓库卫生

| 项 | 说明 |
|----|------|
| Agent 过程产物 | `.omk/`、`.cursor-review-*`、`.atom-review-*`、`.mimo-*`、`.claude-review-*` — **gitignore，勿提交** |
| 正式交接 | `docs/AGENT_HANDOFF.md`（提交到 git） |
| 过程台账 | 本地 `.omk/CODE_REVIEW_ISSUES.md` 可选，非源码真相 |

## 10. 落地提交索引（便于 bisect）

| Commit | 内容 |
|--------|------|
| `801761e` | S1 program_flow；P1 defer 扩展；P2 Idle 门；W1/W3/W4；M1 nvs_commit；M2 stepper；M3 limits 设置副作用；纸路 ESP 权限拆分 |
| `6f44dce` | SD/SPIFFS upload 路径穿越 |
| `4aa21f3` | 纸路 sensor fail-closed / busy |
| `ad4d1a6` + `59f4304` | 二轮：M1 强化 / F1 busy 临界区 / F3 0x18 仅 BT / M5 / N2 / W-N1 / W-N2 / CI |
| `ed1089d` | 三轮：B1 report_gcode_comment 栈溢出 / B2 pending_m_code 复位 / B3 $RST 启动行 / B4 split_params 越界 / W1 WCO-OVR break / 内存安全 + 解析加固 / W9 段缓冲屏障 |
| `4b29822` | 四轮（错误逻辑）：L1 $G 探针标签 G38.2-38.5 / L2 全局禁用尊重掩码 / L3 map 反区间守卫 / L4 jog cancelledInflight；非产品 Trinamic 编译/VFD/Dynamixel/10v/Dac |
| `31db6d8`…`618b1fb` | **P6 正向锚边** + `firmware_full_0x0.bin`；Ambiguous/history-deferred/cleanup 清边沿；见 `AGENT_HANDOFF` §6i |
| fz `e01c263` | SIL 期望对齐 G38/`$H` defer |

Host SIL 复验：

```powershell
$env:FZ_ROOT='D:\Users\zhugu\fz'
$env:GRBL_ROOT='D:\Users\Grbl_Esp32'
python $env:FZ_ROOT\scripts\agent_gate.py --profile standard
```
