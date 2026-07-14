# 仿真已迁出

仿真设计、协议回归、grblHAL 二进制与硬件仿真台架统一放在：

**https://github.com/zhuguang-ZFG/fz**

本地：`D:/Users/zhugu/fz`（或环境变量 `FZ_ROOT`）

## Agent 主动门禁（vibe coding，优先于烧录）

```powershell
$env:FZ_ROOT='D:\Users\zhugu\fz'
$env:GRBL_ROOT='D:\Users\Grbl_Esp32'
python $env:FZ_ROOT\scripts\agent_gate.py
# 或本仓转发：
.\tools\agent_gate.ps1
# 失败读： $env:FZ_ROOT\results\agent_gate_last.json
```

手册：`fz/docs/AGENT_VIBE_CODING.md`

## 仅协议

```powershell
python $env:FZ_ROOT\protocol_sim\run_regression.py --start-sim
```

设计文档：

- `fz/docs/specs/2026-07-14-pre-release-firmware-defect-gate-design.md`（**上线门禁**）
- `fz/docs/specs/2026-07-14-hardware-sim-optimization-design.md`
- `fz/docs/specs/2026-07-14-software-fullchain-sim-design.md`

产品实机清单仍用本仓 `docs/ACCEPTANCE_CHECKLIST.md`（门禁 G3b）。

本目录下旧的 `sim_regression/`、`grblhal_sim/` 视为遗留副本，**新改动请只提交到 fz 仓**。
