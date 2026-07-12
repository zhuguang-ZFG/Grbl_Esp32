# 写字机固件验收清单（Grbl_Esp32 / 纸路）

对照本仓库自定义纸路 + 社区 Grbl/FluidNC 流控实践。  
FluidNC 状态位说明见：[Serial Protocol — Bf 缓冲](https://wiki.fluidnc.com/support/serial_protocol)。

分支：`Branch_736afa70`  
相关提交：`b036c81`（M30 后跳过原点二次换纸）、`468d302`（parser reset 清标志）。

## 0. 编译

- [x] `pio run -e release` 成功（P2/P3 落地后：RAM 28.6% / Flash 74.5%）
- [x] Flash/RAM 无异常暴涨（参考：RAM ~28%、Flash ~74%）

## 1. 换纸 / M30（P0 已修，必测）

| # | 步骤 | 期望 |
|---|------|------|
| 1.1 | 蓝牙流式写满一页，发 **M30** | 换纸一次；串口可见 `[PaperM30] Auto paper change completed` 或等价日志 |
| 1.2 | M30 后上位机再发 **G0 X0 Y0 Z0**（或回原点） | **不再二次换纸** |
| 1.3 | 软复位（Ctrl-X / 0x18）后再回原点 | 不因残留 `paper_m30_just_completed` 误跳过合法换纸 |
| 1.4 | 同一行内重复触发路径 | 同线去重仍有效（不连换两次） |

## 2. 物理键（P2 冷却）

| # | 步骤 | 期望 |
|---|------|------|
| 2.1 | Idle 双击换纸键（间隔 0.5–5s） | 注入 `[ESP910]`，走自动换纸 |
| 2.2 | 换纸刚结束后 **500ms 内** 按键 | **忽略**（冷却） |
| 2.3 | 换纸进行中再按 | 忽略 / 提示 already running |
| 2.4 | 蓝牙刚连接后 1s 内按键 | BT suppress，忽略 |

## 3. SEG / 缓冲（P3 诊断）

社区侧：主机应用 `?` 状态里的 **Bf**（planner 空闲块数）做流控，而不是无限塞 G 代码。  
本固件：`BLOCK_BUFFER_SIZE=250`，`SEGMENT_BUFFER_SIZE=48`（远大于经典 Grbl）。

| # | 步骤 | 期望 |
|---|------|------|
| 3.1 | 正常 BT 连续写 | 不应频繁 `[SEG underflow]` |
| 3.2 | M30 / 换纸页间断流 | 可见 `[BT] Page-gap/seg empty ...` 类信息，**不应**误报硬 underflow 刷屏 |
| 3.3 | 真饥饿（上位机停发过久且非换纸） | 仍报 `[SEG underflow] B=...`，并尽量 reprime |
| 3.4 | 串口监视 | `LOW_BUFFER` 仅 DEBUG 构建出现（避免占 BT 带宽） |

## 4. 与上位机约定（社区兼容）

- 实时状态：`?` → 标准 Grbl 风格；可选看 `Bf:`（FluidNC/Grbl 习惯）
- 页末：固件可发 `[MSG:PAGE_END_IMMINENT]`（M30 同步前）
- 换纸中：主机应暂停运动行；固件侧有 defer host motion 保护

## 5. 回归烟测

- [ ] 归位 / 限位无异常（含 upstream cherry-pick 后）
- [ ] 探针路径无误触发（若硬件接探针）
- [ ] 面板电机：换纸结束后面板不蠕动（595 使能策略）
- [ ] 纸检测 M701/M704 正常

## 6. 已知非阻塞 nits

- SEG 仍保留 `planner_free > 70` 作启发式兜底（已叠加纸路/程序流语义）
- `[BT] Page-gap/seg empty` 未做限频；若 underflow 标志在页间反复置位可能多打几行 Info（远好于硬 underflow 刷屏）
- 完整自动化单元测试覆盖纸路状态机仍弱（以实机清单为准）
- Claude/MiMo A2A 本轮不可用时，以本地审查 + 编译成功为准

## 7. 本轮落地（P2/P3）

| 项 | 文件 | 状态 |
|----|------|------|
| P2 换纸后 Macro0 冷却 500ms | `Custom/paper_system.cpp` | 已改、`pio run -e release` 过 |
| P3 页间 vs 真 SEG 饥饿语义 | `src/Protocol.cpp` | 已改、reprime 路径保留 |
| 验收清单 | `docs/ACCEPTANCE_CHECKLIST.md` | 本文 |
| 审查垃圾 gitignore | `.gitignore` | `.codex-*` / `.mimo-*` / `.claude-review-*` 等 |

## 审查垃圾文件（勿提交）

根目录 `.mimo-*` / `.codex-*` / `.claude-review-*` / 大型 `*-log.txt` 为 Agent 过程产物，已写入 `.gitignore`。
