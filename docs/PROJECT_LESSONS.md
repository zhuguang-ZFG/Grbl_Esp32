# 项目经验积累

面向本产品 fork（`custom_3axis_hr4988` 写字机 + 纸路 + BT）。按时间追加条目；具体参数以机头 `Grbl_Esp32/src/Machines/custom_3axis_hr4988.h` 与 `配置.md` 为准。

---

## 2026-07-25 — 当日汇总（P100 默认 + 0x0 合包）

| 做了什么 | 结果 / 要点 |
|----------|-------------|
| 桌面 `P100自动写字机.properties` → 机头 `DEFAULT_*` | `$RST=$` 后 `$$` 与表一致；提交 `dce1426` |
| 每次 `platformio run -e release` | **自动**生成 `.pio/build/release/firmware_full_0x0.bin`（4MB@0x0） |
| 烧录后 NVS | **不会**被 `DEFAULT_*` 覆盖，必须 `$RST=$`（或逐项 `$` + `$S`） |
| USB 口 | 烧录/软复位后 CH340 可能短暂消失再回 COM3；COM4/5 常为蓝牙，别误连 |
| `merge_firmware.py` | 必须 UTF-8；坏了按下方「合包脚本坏了怎么办」处理 |

详见下列分节；口令与面板 hold 仍见「面板保持…」一节。

---

## 2026-07-25 — P100 默认 `$` 参数入机头

**来源：** 桌面 `P100自动写字机.properties` → `custom_3axis_hr4988.h` 的 `DEFAULT_*`。  
**已验证：** 烧录后 `$RST=$`，`$$` 与 P100 表一致（`$100/101/102=100/100/50`，`$3=7`，`$1=25` 等）。  
**提交：** `dce1426`（分支 `fix/panel-hold-after-align-116687e`）。

| 注意 | 说明 |
|------|------|
| NVS | 只改 `DEFAULT_*` 不改板上旧值；必须 `$RST=$` 或逐项写 |
| `$102=50` | 相对本叉旧默认 400 偏小；落笔不够时优先怀疑此项 |
| `$1=25` | 相对旧 `$1=255`，BT 断流空闲可能再失能 XYZ；面板 hold 管不了 XYZ |
| `$3=7` | XYZ 全反相；与旧「仅 Z 反相」不同，方向不对先查 `$3` |

整片产物仍是 `.pio/build/release/firmware_full_0x0.bin`（见下节）。`配置.md`「可调参数汇总」已同步要点。

---

## 2026-07-25 — 0x0 合包：每次编译自动生成；脚本坏了怎么办

### 正常行为（省事）

- `platformio.ini`：`extra_scripts = post:merge_firmware.py`
- **每次** release 成功编完 `buildprog`，都会合并出：  
  `.pio/build/release/firmware_full_0x0.bin`（4MB：boot@0x1000 + part@0x8000 + app@0x10000，0xFF 垫满）
- 成功日志必有：`Full flash image from 0x0 (4194304 bytes): ...`
- 烧录：`esptool write_flash 0x0 firmware_full_0x0.bin`（不含 SPIFFS）
- **不必**每次手工合包；没看到上面那行再查。

### 怎么判断合包没跑 / 脚本坏了

1. 编译日志**没有** `Full flash image from 0x0 ...`
2. 或 `firmware_full_0x0.bin` 的修改时间停在旧构建，而 `firmware.bin` 已是新的
3. 或 post 报错，例如找不到 `bootloader_qio_80.0.bin`（频率写成了 `80.0` 而不是 `80m`）

编码自检：

```powershell
python -c "print(open(r'D:\Users\Grbl_Esp32\merge_firmware.py','rb').read(8))"
```

| 输出 | 含义 |
|------|------|
| `b'# merge_'` | UTF-8，正常 |
| `b'#\x00 m\x00e...'` / 含 `\x00` | **UTF-16**，脚本坏了 |

### 怎么修（推荐顺序）

```powershell
cd D:\Users\Grbl_Esp32
git checkout HEAD -- merge_firmware.py
python -c "print(open('merge_firmware.py','rb').read(8))"   # 确认 b'# merge_'
platformio run -e release                                   # 日志应出现 Full flash image from 0x0
```

仍失败时核对脚本内 bootloader 名应为 `bootloader_%s_%sm.bin`（`// 1000000` 取整），且 build 目录能从 `firmware.bin` 节点反推（`$PIOENV`/`$BUILD_DIR` 在 post 里可能为空）。

### 为何会再坏

- Windows 下部分编辑器/工具把 `.py` 存成 **UTF-16 LE**（Cursor Write 曾踩过）。
- 改 `merge_firmware.py` 后看右下角编码：**UTF-8**，不要 UTF-16。
- 说明亦见 `docs/FIRMWARE_CI.md` 烧录提示。

---

## 2026-07-25 — 面板保持 / Z 电流 / 对位步数 / 0x0 镜像

**基线：** `116687e`（确认可写字夹紧 160）  
**分支：** `fix/panel-hold-after-align-116687e`  
**关键提交：** `709ad31` → `49a3500` → `30c6fb9` → `fa59ac6` →（P100/合包）`dce1426`

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

### 5. 整片 0x0 固件（摘要）

完整排查/修复步骤见上文 **「0x0 合包：每次编译自动生成；脚本坏了怎么办」**。要点：每次 release 自动出包；脚本须 UTF-8；bootloader 文件名 `…_80m.bin`。

### 6. 烧录与联调习惯

- COM3 常被奎享/监视器占用 → 先杀占用再 `upload`。
- 烧录/复位后 USB 可能短暂丢口再枚举回 **COM3（CH340）**；**COM4/COM5** 多为蓝牙 SPP，串口脚本勿绑错。
- 产品诊断日志多在 **USB**；蓝牙口看不到 `[PaperAuto*]`，双口抓包更有效。
- NVS `$` 会被上位机 properties 盖回；机头 `DEFAULT_*` ≠ 板上当前值；改默认后要 `$RST=$`。
- 当前默认 `$1=25`（P100）；若 BT 断流又出现画圆卡顿，可试把 `$1` 调回 `255` 再 `$S`（与面板 hold 是两件事）。

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
