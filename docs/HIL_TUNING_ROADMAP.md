# HIL 调优路线 — 需真机数据的优化项

静态审查（九轮）已确认产品热路径**无值得做的纯静态调优**：`Setting::get()` 内联、
调试特性默认关、-O2 优化到位、资源充裕（Flash 74.8% / RAM 28.6%）。真正的调优杠杆
都需要真机数据。本文件是 HIL 阶段的调优路线，按"收益/确定性"排序。

前置：`agent_gate standard` 已 pass（2026-07-20 @ HEAD）；镜像 `.pio/build/release/firmware.bin`。

---

## 调优项 1 — B1 planner 饥饿判据取反（最高收益，可能直接解卡顿）

**假说：** BT 写字/画圆卡顿源于 planner-starve 判据取反（`plan_get_block_buffer_available()`
返回空闲槽数，被当排队数用）。满速流式时 `clientCheckTask` 忙转抢 core-1 CPU；真饥饿时
反走慢轮询。详见 `docs/patches/B1-planner-starve-inversion.md`。

**验证流程：**
1. 刷当前 HEAD（无 B1 补丁）。用代表性图案（含大量短线段/圆弧的写字文件）经 BT 流式发送。
2. 采基线：串口收 `[BT-EOL gap=... ms B=... st=...]` 日志（gap>2s 行 = 卡顿点）。
   记录：单页 gap>2s 事件数、最大 gap、`SEG underflow` 计数。
3. `git apply docs/patches/B1-planner-starve-inversion.patch`，重编刷入。
4. 同一图案复跑，采同样日志。**期望：** gap 事件数/最大 gap 下降，满速段无新卡顿。
5. `agent_gate standard` 必须仍 `overall=pass`。
6. **达标才 land**（补丁转正式 commit，B1 移入 §6g landed 不变量）；不达标或回归**不 land**。

**判据：** 卡顿改善需可量化（gap 事件 ≥30% 下降或最大 gap 明显缩短），否则视为无效。

---

## 调优项 2 — Uart0 TX 缓冲（P2，收益中，改法受限）

**现状：** `Uart::begin` 装 TX 缓冲=0（`Uart.cpp:29`），`Uart0.write` 阻塞到字节进 128B
硬件 FIFO。`client_write(CLIENT_ALL,...)` 的广播报告/`[MSG]`/BT-EOL 调试行都顺带写 Uart0，
115200 下一条 100B 消息可卡主循环 ~9ms，叠加为 segment 饥饿贡献源。

**受限改法（静态可信部分）：** 给 `Uart::begin` 加 `txBufferSize` 参数（默认 0），**仅**
`client_init` 对 Uart0 传 512。写入变"拷入即返"，消息内容/顺序不变。
**不要**改 begin 默认值——该类被 `tmc_serial`（TrinamicUart）和 VFD `_uart` 共用，RS485
半双工读回时序对 TMC 未证安全。

**验证：** 刷入后确认串口输出完整无乱序；BT 流式期间量 `[BT-EOL gap]` 是否进一步下降
（与 B1 叠加评估）。产品串口平时空闲，主要收益在减少 `CLIENT_ALL` 广播的主循环阻塞。

---

## 调优项 3 — 运动参数（禁止静态改，必须实测）

以下参数是吞吐/负载/机械的权衡，**静态改只会破坏已调好的平衡**（前几轮 M4/N7 已豁免）：

| 参数 | 位置 | 实测方法 |
|------|------|----------|
| `BLOCK_BUFFER_SIZE=250` | `Config.h:471` | 量 replan 耗时 vs planner 深度；BT 流式下 `Bf:` 空闲曲线 |
| `SEGMENT_BUFFER_SIZE` | `Config.h` | segment underflow 频率 vs ISR 负载 |
| `$120-122` accel、`$110-112` max_rate | 设置 | 实际写字线质量 vs 丢步；示波器看 step 脉冲 |
| AMASS 电平 | `Stepper.cpp` | 低速段步率失真（可闻抖动）实测 |

**方法：** 单变量扰动 + 量化观测（写字线质量、gap 日志、step 示波），每次只动一个，
跑 `agent_gate` 确认 SIL 不回归。无数据不改。

---

## 调优项 4 — 正向锚边对位（P6，换纸 HIL 必做）

**背景（已 land `31db6d8`…`618b1fb`）：** Step 7 反向回找已移除；Step 6 停在「无纸」侧后同向走 `PANEL_FINAL_STEPS`。相对旧固件，绝对进纸终点会因**停边侧 + 回差不再抵消**而整体偏移——属预期，需真机标定，不是状态机回退。

| 参数 | 默认 | 何时动 |
|------|------|--------|
| `PANEL_FINAL_STEPS` | 320 | 多页对位整体偏前/偏后时优先调 |
| `PANEL_EDGE_APPROACH_STEPS` | 400（≈3.8mm） | 频繁 `EDGE_PASSED` 或慢窗耗尽时按纸长公差/磨损加大 |
| `PANEL_LOCATE_*` | 1500/1500 µs | 仅当慢采噪声/延迟不足时 |

**验证：** 连续 5–10 页 + `ACCEPTANCE_CHECKLIST` §1.9；改宏后 `agent_gate standard` 仍须绿。**禁止**无补偿恢复反向找边（P6）。

---

## 已排除（做过或确认无价值）
- 死 `.bss` 清理、`client_write` strlen 去重 —— 已做（`4dac3f4`，卫生级）
- `Setting::get()` 缓存 —— 无价值（已内联）
- 状态报告字段裁剪 —— WCO/OVR 慢刷新已在 `W1`（四轮）修好
- 调试/回显开关 —— 默认已全关

## 优先级
1. **B1**（可能一举解决卡顿，补丁就绪，验证成本低）
2. **P2 Uart0 TX**（与 B1 叠加评估）
3. **P6 对位宏**（换纸发货前必做项 4；与 B1 正交）
4. **运动参数**（仅在 1/2 不够时，带数据逐个实测）
