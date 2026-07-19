# 仿真已迁出

仿真设计、协议回归、grblHAL 二进制与硬件仿真台架统一放在：

**https://github.com/zhuguang-ZFG/fz**

本地：`D:/Users/zhugu/fz`（或环境变量 `FZ_ROOT`）

## Agent 必须主动门禁（HARD RULE，优先于烧录）

**禁止**改完 GCode/Protocol/Planner/步进/限位/纸路相关后不跑 gate 就声称修好或去烧录排 parser/运动问题。  
Agent **自己调用**，不要等用户说「测一下」。

```powershell
$env:FZ_ROOT='D:\Users\zhugu\fz'
$env:GRBL_ROOT='D:\Users\Grbl_Esp32'
python $env:FZ_ROOT\scripts\agent_gate.py
# 或本仓转发：
.\tools\agent_gate.ps1
# 失败读： $env:FZ_ROOT\results\agent_gate_last.json
# overall_status 必须为 pass 才可声称 host SIL 绿
```

手册：`fz/docs/AGENT_VIBE_CODING.md` · 本仓 `AGENTS.md` Testing Strategy

## 仅协议

```powershell
python $env:FZ_ROOT\protocol_sim\run_regression.py --start-sim
```

设计文档：

- `fz/docs/specs/2026-07-14-pre-release-firmware-defect-gate-design.md`（**上线门禁**）
- `fz/docs/specs/2026-07-14-hardware-sim-optimization-design.md`
- `fz/docs/specs/2026-07-14-software-fullchain-sim-design.md`

产品实机清单仍用本仓 `docs/ACCEPTANCE_CHECKLIST.md`（门禁 G3b）。  
产品不变量 / 勿回退表：本仓 **`docs/AGENT_HANDOFF.md`**（深度审查 `801761e` 后 agent 优先读）。

改 `ProtocolDecisionCore::should_defer_motion` 时同步 fz：

- `native_sim/test_protocol_decision_trace.py`
- `native_sim/scenarios/protocol_input_boundary_sequence.json`

本目录下旧的 `sim_regression/`、`grblhal_sim/` 视为遗留副本，**新改动请只提交到 fz 仓**。
