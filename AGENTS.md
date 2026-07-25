# AGENTS.md — Grbl_Esp32（写字机运动 / 换纸固件）

> 本仓是 **hutuji 商业现役运动固件** fork。  
> 枢纽契约与防跑偏真值在 `D:/Users/hutuji`，**不要**只凭本仓记忆改架构。

## 必读（动手前）

1. `D:/Users/hutuji/docs/agent-handoff.md`
2. `D:/Users/hutuji/docs/agent-anti-drift.md`
3. `D:/Users/hutuji/docs/protocol.md` **v0.5**（尤其 §9）
4. `D:/Users/hutuji/docs/worktree-inventory.md` + 本仓 `git status --short`
5. `D:/Users/hutuji/docs/agent-edit-allowlist.json`（Grbl 白名单 / 禁止符号）

## 基线分支

- **商业基线**：`fix/panel-hold-after-align-116687e`
- 仿真用纯核心头文件曾在 `Branch_736afa70`；现役分支仅允许 **header-only 恢复** `*Core.h`，禁止借机合并整支换纸改动。

## 硬规则

1. **换纸机械 / 时序 / 运动 / 授权 / U1-Grbl 协议一律不改**（现役含 `M701`/`M704`/`M711`–`M713`/`M721`/`M30` 等）。
2. **可小改（须契约点名）**：消息路由、状态字段、WebUI 网络层——当前仅 `protocol.md` §9：
   - A：`TelnetServer.cpp` keepalive 四参数  
   - B1′：`[PaperStatus]` → `CLIENT_ALL`（仅结果码四处）  
   - B2′：`[ESP901]` 增加 `Changing=`
3. **禁止碰**：`report_realtime_status()`、`report_state_text()`；禁止把 63 处换纸消息全改 `CLIENT_ALL`。
4. **改了本仓任何 `.c/.cpp/.h` 后必须跑**：
   ```powershell
   $env:FZ_ROOT='D:\Users\zhugu\fz'
   $env:GRBL_ROOT='D:\Users\Grbl_Esp32'
   python D:\Users\zhugu\fz\scripts\agent_gate.py --profile standard
   ```
   `overall_status` 非 `pass` **不得**声称修好。细则：`D:/Users/zhugu/fz/docs/AGENT_VIBE_CODING.md`。
5. **不为 fz 绿去改商业常量**：`paper_firmware_contract.json` 应对齐本分支，而不是把本分支改回旧分支。
6. **不覆盖**用户已有工作树改动；凭据不入库。

## hutuji 相关完成定义

| 宣称 | 需要 |
|------|------|
| §9 源码就绪 | §9 三处 + 必要 `*Core.h` + fz `standard` pass |
| §9 完成 | 上表 **且** 烧录 + `hutuji/docs/bringup-sop.md` 阶段 2 实机判据 |
| 可出货 | 仅看 hutuji `release-readiness.md`（本仓绿不够） |

## 路径

| 用途 | 路径 |
|------|------|
| 枢纽仓 | `D:/Users/hutuji` |
| 仿真门禁 | `D:/Users/zhugu/fz` |
| S3 AI 仓 | `D:/Users/xiaozhi-esp32` |
