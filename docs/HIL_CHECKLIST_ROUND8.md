# HIL 验证清单 — 八轮审查后（Branch_736afa70 @ HEAD）

本文件汇总八轮 code review 落地修复后**必须在真机验证**的项。全部修复仅经
`pio run -e release` 静态编译 + 交叉编译验证，**未 flash、未 HIL**。镜像：
`.pio/build/release/firmware.bin`（RAM 28.6% / Flash 74.7%）。

先决条件：`agent_gate standard` 应先在 PC 侧 `overall=pass`（见 AGENT_HANDOFF §2）。

> **✅ 门禁已过（2026-07-20，HEAD @ `add6bac`）：** `agent_gate standard` **overall=pass exit=0**，
> 33 层全 PASS / 0 hard failure。此前偶发失败的 `json_feed_hold_tcp` 本次 PASS（确认是 TCP
> 传输时序 flaky，非代码回归）。`soft_divergence` 的 `user_io.nc`/`parsetest*.nc` 为已知
> allowlist（pending 产品契约复核，不阻断）。**PC 侧关卡已清，可刷机进 HIL。**

---

## P0 — 产品默认配置（仅 BT）必测

### 换纸 / GCode 解析（三~七轮）
- [ ] BT 发 `(MSG:<90+ 字符>)` → **不崩溃/不重启**（B1 三轮：report_gcode_comment 栈溢出）
- [ ] 流式发 `M711 P50 G2`（G2 缺 F 会 FAIL）后接 `G0 X10` → G2 报错、`G0` **不**触发走纸、无残留（B2 三轮：pending_m_code）
- [ ] 换纸/M30 后发 M800 再发普通 G 码 → 授权仅一次、后续行不误设授权
- [ ] `$RST=$` 后查 `$N0`/`$N1` → 启动行**保留**（B3 三轮）
- [ ] 缺纸/急停中止换纸后再发 G 行 → 只失败一次、不连环 PAGE_END（S1 一轮，回归）
- [ ] 换纸中灌 `G28`/`$H`/`G38` → 拒绝/defer，XYZ 不动（P1，回归）

### BT 命令 / 报告（四~六轮）
- [ ] BT 发 `[ESP401]` 带 12+ 个 `k=v` → 返回错误、不崩溃（B4 三轮：split_params）
- [ ] 探针后查 `$G` → 报告 `G38.2`~`G38.5`，无 `G38.1`（L1 四轮）
- [ ] BT 发含引号/反斜杠的设置值，查 `[ESP400]`/`[ESP420]` → JSON 合法、上位机能解析（L7 六轮：JSON 转义）
- [ ] Cycle 中抓 `?` 状态流 → WCO/Ov 出现频率回落到 busy 档（W1 四轮：状态行带宽）
- [ ] 宏按钮/长宏（接近 254 字符）→ 不崩溃（L5 五轮：line[255] 越界）
- [ ] BT 发 `$LocalFS/ListJSON` → **不崩溃/不重启**（L11 九轮：SPIFFS 未挂载除零 panic）

### 运动 / 回零（七~八轮）
- [ ] 多行连续写字回归 → 步进无异常卡顿（W9 三轮段缓冲屏障 + L8 七轮 override 空缓冲，验证无副作用）
- [ ] Idle 下 BT 发 feed/rapid override 实时字符 → 无异常（L8 七轮：plan_cycle_reinitialize 空缓冲早退）
- [ ] `$H` 后查 `$#`/mpos → machine origin 精确（L9 七轮：lround 取整）
- [ ] `$27=0` → 被设置层**拒绝**（返回错误码，非接受）；坏行程配置下 `$H` → 出 `Homing fail` 报警**而非挂死**（L10 / F-H1 七轮）

---

## P1 — HIL 敏感（前几轮延迟到真机确认）

- [ ] **F1**（换纸 busy TOCTOU，开 WiFi 后升格）：见 AGENT_HANDOFF §8
- [ ] **F3**（0x18 仅 BT 忽略、USB 永远急停）：见 §8
- [ ] **M1**（NVS 掉电存活：`$100=250` → 断电 → 仍在）：见 §8

---

## P2 — 待决策项（需 HIL 数据后才改）

### B1 — planner 饥饿判据取反（八轮，潜在收益最大）
补丁：`docs/patches/B1-planner-starve-inversion.{patch,md}`（未 land）。
- [ ] 现 HEAD（无补丁）复现 BT 卡顿，采 `[BT-EOL gap]` 日志基线
- [ ] `git apply` 补丁 → 重刷 → 同页对比 gap 事件减少、满速流式无新卡顿
- [ ] `agent_gate standard` 仍 `overall=pass`
- [ ] 达标才 land 并把 B1 移入 §6g landed 不变量；不达标不 land

---

## 未改项（明确记录，非本次 HIL 目标）
换纸期非运动行 gc_state 重入（W2，需重构决策）、approach cycle_stop 残留竞态
（F-H3 上游同源）、非产品路径（主轴 spindown/startup-line/probe detach、
Trinamic/VFD/Dynamixel/10v/Dac 驱动、telnet 无认证、parking）。详见
AGENT_HANDOFF §6b–§6g。

## 若任一 P0 项失败
对应不变量（L1–L10 / B1–B4 / F1/F3/M1）判定为**回退了**，勿声称修好；
回到该 commit 复查，勿在 HIL 未过时合入 main。
