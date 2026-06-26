# Code Review - 阶段 3/4 BT 假连接恢复 + TX 拥塞流控

## Summary

Files reviewed: `Grbl_Esp32/src/Config.h`, `Grbl_Esp32/src/Serial.cpp`, `Grbl_Esp32/src/WebUI/BTState.cpp`, `Grbl_Esp32/src/WebUI/BTState.h`

Review rounds: 2 (初始 review + 修复后 re-review)

Build results:
- `platformio run -e release` (default `custom_3axis_hr4988.h`): **PASS** (RAM 28.6%, Flash 74.4%)
- `PLATFORMIO_BUILD_FLAGS=-DMACHINE_FILENAME=mpcnc_v1p2.h pio run -e release`: **PASS**

---

## Round 1 Issues (全部已修复)

| ID | 严重度 | 问题 | 状态 |
|----|--------|------|------|
| COR-001 | Critical | `bt_tx_flush()` 死代码路径 | ✅ 已修复 |
| COR-002 | Critical | TX ring buffer 跨任务无同步 | ✅ 已修复 |
| COR-003 | High | `bt_tx_flush()` 部分写入回插破坏 FIFO | ✅ 已修复 |
| COR-004 | High | `SerialBT.end()` 回调中断恢复状态机 | ✅ 已修复 |
| COR-005 | Medium | 空字符串越界访问 | ✅ 已修复 |
| COR-006 | Medium | `volatile` 无内存序保证 | ✅ 已修复 |
| ARC-001 | Medium | `bt_message_is_critical()` 放在通用 `Serial.cpp` | ✅ 已移到 BTState.cpp |
| ARC-002 | Low | Include 顺序不符合约定 | ✅ 已修复 |
| COR-007 | Low | 恢复计数器语义与宏名不一致 | ✅ 已修复 |
| COR-008 | Low | `bt_tx_send(nullptr)` 返回 `true` | ✅ 已修复 |

## Round 2 Issues (全部已修复)

| ID | 严重度 | 问题 | 状态 |
|----|--------|------|------|
| ARC-1 | High | `bt_tx_ring_reset()` 未加互斥锁，与 `bt_tx_ring_push()` 竞争 | ✅ 已修复 |
| C1 | Low | `bt_tx_message_is_critical()` 中 `[BT-EOL`/`[PaperDiag]`/`[BTState]` 比较死代码（比较位置在 `[MSG:` 内但偏移未跳过前缀） | ✅ 已修复 |
| C2 | Medium | `CLIENT_ALL` 无条件 `critical=true` + 分类器 `[MSG:` 默认 true → Debug 级别消息泛溢 ring buffer | ✅ 已修复 |
| C4/ARC-2 | Low | `bt_recovery_attempts` 非原子但跨任务共享 | ✅ 已修复 |

## 未修复（已知限制，不阻断合并）

| ID | 严重度 | 问题 | 说明 |
|----|--------|------|------|
| C3 | Low | 非关键消息部分写入时尾部被截断 | `SerialBT.write()` 不提供 `availableForWrite()`；部分字节已进入 BT TX FIFO，无法回收。当前行为：部分发出+丢弃剩余。对非关键调试消息可接受。 |
| TST-001 | Medium | 硬件测试计划未覆盖阶段 3/4 逻辑 | 项目无单元测试框架，需实机验证。后续追加 `$BTDiag` 诊断命令或手动测试。 |

---

## 安全性

无安全问题。

## 架构 (其他)

无额外架构问题。

## 测试

- 编译验证通过：`release`（`custom_3axis_hr4988`）、`mpcnc_v1p2`。
- 项目无单元测试框架，所有运行时分支均未被执行。
- 未覆盖场景：ring buffer 环绕、静默超时触发、恢复状态机各 step、消息关键性分类边界。
- 建议后续追加串口诊断命令辅助实机测试。

## Pre-Existing Issues (not blocking)
- 项目缺少单元/功能测试基础设施。

## Low-Confidence Observations
- C3（非关键部分写入截断）是 `BluetoothSerial` API 限制，实际影响极小。