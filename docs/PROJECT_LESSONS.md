# 项目经验积累

面向本产品 fork（`custom_3axis_hr4988` 写字机 + 纸路 + BT）。按时间追加条目；具体参数以机头 `Grbl_Esp32/src/Machines/custom_3axis_hr4988.h` 与 `配置.md` 为准。

---

## 2026-07-25 — 面板保持 / Z 电流 / 对位步数 / 0x0 镜像

**基线：** `116687e`（确认可写字夹紧 160）  
**分支：** `fix/panel-hold-after-align-116687e`  
**关键提交：** `709ad31` → `49a3500` → `30c6fb9` → `fa59ac6`

### 1. 换纸对位后失能会回退

| 现象 | 根因 | 做法 |
|------|------|------|
| Step8 对位后纸/面板缩一下 | `paper_disable_drivers()` 立刻卸力矩，弹性/间隙回弹 | settle → `paper_enter_panel_low_hold()`（面板 EN 开 + 低 REF） |

**宏：** `PAPER_PANEL_HOLD_AFTER_CHANGE`、`PAPER_REF_DAC_PANEL_HOLD`（默认 40）、`PANEL_FINAL_SETTLE_MS`

**注意：** hold 期间 LED 与面板 STEP 同 595，**禁止刷 LED/595**，否则微步进蠕动。

### 2. 写字期要不要关面板使能

| 方案 | 行为 | 取舍 |
|------|------|------|
| `PAPER_PANEL_HOLD_DURING_WRITE=0` | 首次 Cycle/Jog/Homing 失能面板 | 减 595 串扰，但可能第一笔前又回缩 |
| `=1`（当前） | 写字期面板继续使能 | 防回退；发烫/蠕动风险上升 |

**必须强制清 hold：** 软复位、`[ESP911/912/913]` / M711–716、`paper_disable_drivers()`。  
写字路径用 `paper_release_panel_hold_for_xyz_motion()`（宏=1 时不关使能）；纸路点动用 `paper_force_release_panel_hold()`，**不要共用同一空操作**。

### 3. GPIO25 是 DAC，且 Z 与纸路共脚

- 口语里的「ADC 控电流」实际是 **DAC1（GPIO25）→ HR4988 REF**。
- **纸路三电机 + Z 轴共一根 REF**：跑纸路时切 `PAPER_REF_DAC_*`；空闲/写字用 `Z_REF_DAC`（当前 100）。
- 写字期若仍停在 hold REF=40，**Z 扭矩会被饿掉**。当前：首次 XYZ 时 `Panel hold keep + Z REF=…`，升到 `Z_REF_DAC`，面板使能保持。
- 失能纸路后要把 REF **恢复 `Z_REF_DAC`**，避免停在夹紧/面板运行值上。

**调参方向：** Z 软 → 加大 `Z_REF_DAC`；面板发烫/写字蠕动 → 减小 hold / `Z_REF_DAC`，或把 `PAPER_PANEL_HOLD_DURING_WRITE` 改 0。

### 4. `PANEL_FINAL_STEPS` 只能 HIL 拧

Step8「纸到位后再走固定步」是开环对位，**无仿真可证明最优值**。本轮实机拧过：320 → 330 → 340 → 360 → **350**（当前）。

- 偏后（纸不够进）→ 加大；过头 → 减小。
- 串口确认：`[PaperAuto-8] Final alignment (N steps)...`
- 改宏后务必 **重编 PaperSystem**（必要时删 `.o`），避免只链了旧目标。

### 5. 整片 0x0 固件（`firmware_full_0x0.bin`）

- `platformio.ini`：`extra_scripts = post:merge_firmware.py`
- 产物：`.pio/build/release/firmware_full_0x0.bin`（4MB，boot@0x1000 + part@0x8000 + app@0x10000，0xFF 垫满）
- 烧录：`esptool write_flash 0x0 firmware_full_0x0.bin`（不含 SPIFFS）

**踩坑：**

1. 脚本须 **UTF-8/ASCII**，UTF-16 会 `SyntaxError: null bytes`。
2. SCons post 形参是 `(target, source, env)`；`buildprog` 时 `$PIOENV` / `$BUILD_DIR` 可能为空——**从 `firmware.bin` 节点反推 build 目录**。
3. 成功日志：`Full flash image from 0x0 (4194304 bytes): ...`
4. 说明见 `docs/FIRMWARE_CI.md`。

### 6. 烧录与联调习惯

- COM3 常被奎享/监视器占用 → 先杀占用再 `upload`。
- 产品诊断日志多在 **USB**；蓝牙口看不到 `[PaperAuto*]`，双口抓包更有效。
- NVS `$` 会被上位机 properties 盖回；机头 `DEFAULT_*` ≠ 板上当前值。
- `$1=255` 保持 XYZ 使能，有利于 BT 断流间隙少丢步（与面板 hold 是两件事）。

### 7. 勿回归清单（本轮相关）

| 项 | 规则 |
|----|------|
| M30 纸路失败 | 清 `program_flow` 再 error return |
| 纸路运行中 | 推迟 G0–G3 / G28/30/38 / `$H`/`$J` |
| ESP910 | 保持 WG（客人可换纸）；ESP911–913 仍 WU |
| hold + LED | hold 时不刷 595 |
| 共脚 REF | 写字/空闲回到 `Z_REF_DAC` |

### 8. 验证口令（USB）

```text
[ESP910]          → Panel low-current hold (... during_write=1)
G91 G0 X0.1 F1000 → Panel hold keep + Z REF=100 for write（无 XYZ 失能日志）
[ESP901]          → PanelHold=On/Off
```

Host SIL（`agent_gate`）**不能**证明纸力学 / BT / 对位步数；发货仍要 HIL。

---

## 条目模板（后续追加）

```markdown
## YYYY-MM-DD — 标题

**分支 / 提交：**
### 现象与根因
### 做法与宏
### 踩坑
### 验证
```
