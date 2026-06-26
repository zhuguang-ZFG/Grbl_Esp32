# Code Review - 阶段 3/4 BT 假连接恢复 + TX 拥塞流控

## Summary

Files reviewed: `Grbl_Esp32/src/Config.h`, `Grbl_Esp32/src/Serial.cpp`, `Grbl_Esp32/src/WebUI/BTState.cpp`, `Grbl_Esp32/src/WebUI/BTState.h`

New issues: **7** (2 Critical, 3 High, 2 Medium/Low)

Perspectives: 4/4 (Security, Correctness, Tests, Architecture)

Build results:
- `platformio run -e release` (default `custom_3axis_hr4988.h`): **PASS** (RAM 28.6%, Flash 74.4%)
- `PLATFORMIO_BUILD_FLAGS=-DMACHINE_FILENAME=mpcnc_v1p2.h pio run -e release`: **PASS**

---

## Must Fix Before Merge

### [COR-001] `bt_tx_flush()` 永远不会在 Connected 状态下被调用 — **Critical**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:292` |
| 问题 | `bt_state_update()` 中，`Connected/Congested` 分支无论是否触发静默超时都会执行 `return;`。后面的 `if (s == BTState::Connected && bt_tx_ring_used > 0) { bt_tx_flush(); }` 因此在正常连接时不可达。 |
| 后果 | 拥塞时缓存到 ring buffer 的关键消息永远不会被刷出，最终被新消息覆盖或丢弃，TX 流控失效。 |
| 修复 | 删除该无条件 `return;`，让函数在静默超时未触发时继续执行到 flush 块；或把 flush 逻辑放到 `Connected/Congested` 分支内部。 |

### [COR-002] TX ring buffer 无跨任务同步 — **Critical**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:127-216` |
| 问题 | `bt_tx_send()` 可从 `clientCheckTask`（SD 卡忙时调用 `grbl_sendf`）及主循环调用；`bt_tx_flush()` 在主循环调用。两者同时修改 `head/tail/used`，没有任何 mutex 或 critical section。 |
| 后果 | 上下文切换可能破坏 ring buffer 元数据，导致丢字节、重复字节、读到空闲槽，或 `used` 与 head/tail 不一致。 |
| 修复 | 在 `BTState.cpp` 内新增 `portMUX_TYPE bt_tx_mux`，对所有 ring buffer 读写/修改操作使用 `vTaskEnterCritical`/`vTaskExitCritical`。注意：**不要**在持有锁时调用 `SerialBT.write()`。 |

### [COR-003] `bt_tx_flush()` 部分写入后的回插逻辑会破坏 FIFO 顺序 — **High**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:198-212` |
| 问题 | 从 ring 弹出 `n` 字节后 `tail` 已前进；若 `SerialBT.write()` 只写入 `written < n`，代码把 `remain` 字节写回到 `tail - remain` 位置，会覆盖仍在队列中的旧字节，而不是插到队列头部。 |
| 后果 | 重新插入的字节与队列中剩余字节顺序错乱，部分原始数据被覆盖。 |
| 修复 | 改为“窥视（peek）不弹出”模式：直接读取 `tail` 处连续数据并写入；只有完全写成功后才前进 `tail` 并扣减 `used`。这样无需回插。 |

### [COR-004] 恢复状态机会被 `SerialBT.end()` 触发的回调中断 — **High**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:225-270` |
| 问题 | `bt_execute_recovery()` step 0 调用 `SerialBT.end()`，该函数会在 BT 任务中触发 `ESP_SPP_CLOSE_EVT` / `ESP_SPP_SRV_STOP_EVT` 等回调，把 `bt_state` 改回 `Advertising` 或 `Idle`。下一 tick 主循环看到 `s != Recovering`，就会放弃恢复流程，导致 SPP 不会被重启。 |
| 后果 | 假连接自动恢复高度依赖时序，经常无法重启蓝牙，链路永久断开直到复位。 |
| 修复 | 用独立的恢复活跃标志（如 `bt_recovery_active` 或检查 `bt_recovery_step != 0`）驱动恢复流程，而不是仅凭 `bt_state == Recovering`。必要时在 `bt_execute_recovery()` 开头重新把 `bt_state` 设为 `Recovering`。 |

### [COR-005] `bt_message_is_critical()` 对空字符串越界访问 — **Medium**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/Serial.cpp:392-418` |
| 问题 | `if (text[0] == 'o' && text[1] == 'k')` 在 `text` 为空字符串时访问 `text[1]`，越界。 |
| 后果 | 未定义行为，极端情况下可能崩溃。 |
| 修复 | 先检查 `text[0] != '\0'`，或在调用处传入已知长度。 |

---

## Should Fix

### [COR-006] `bt_state` / `bt_last_event_ms` 跨任务仅用 `volatile` — **Medium**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:29-30` |
| 问题 | BT 回调任务写入、`bt_state_update()` 主循环读取。`volatile` 只阻止编译器重排，不保证 ESP32 上的原子性/内存序。 |
| 修复 | 改为 `std::atomic<BTState>` 与 `std::atomic<uint32_t>`，或用 critical section 保护读写。 |

### [ARC-001] `bt_message_is_critical()` 不应放在通用 `Serial.cpp` — **Medium**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/Serial.cpp:389-420` |
| 问题 | 该函数包含 BT 拥塞策略及 `[BT-EOL]`/`[PaperDiag]`/`[BTState]` 等子系统前缀，把 BT 语义泄漏到通用 client_write 层。 |
| 修复 | 把关键性判断移到 `BTState.cpp`（如 `bt_tx_send()` 内部或 `bt_classify_message()`），`Serial.cpp` 只负责调用 `bt_tx_send(text, len, client)`。 |

### [ARC-002] Include 顺序不符合项目约定 — **Low**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:25` |
| 问题 | `BTState.cpp` 先 `#include "BTConfig.h"` 再 `#include "BTState.h"`，违反 `CodingStyle.md` 中“.cpp 应先包含对应头文件”的约定。 |
| 修复 | 调换顺序：`#include "BTState.h"` 第一。 |

### [COR-007] 恢复尝试计数器语义与宏名不一致 — **Low**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:272-286, 240-258` |
| 问题 | `BT_RECOVERY_MAX_RETRIES` 文档为“最大重试次数”，实际实现允许最多 `MAX_RETRIES + 1` 次 `begin()` 尝试，日志输出也显示 `attempts == 4` 与“max 3”不符。 |
| 修复 | 统一语义：让 `MAX_RETRIES` 表示总尝试次数上限，或改名为 `BT_RECOVERY_MAX_ATTEMPTS`。 |

### [COR-008] `bt_tx_send(nullptr, len > 0, ...)` 返回 `true` — **Low**

| 字段 | 内容 |
|------|------|
| 文件:行 | `Grbl_Esp32/src/WebUI/BTState.cpp:155-158` |
| 问题 | `text == nullptr` 且 `len > 0` 时返回 `true`，与“已发送”语义矛盾。 |
| 修复 | `text == nullptr` 时返回 `false`。 |

---

## Security

No security issues found.

## Architecture (other)

No additional architecture issues beyond [ARC-001] 和 [ARC-002].

## Tests

- 编译验证通过，但项目无单元测试框架，所有运行时分支均未被执行。
- 未覆盖场景：ring buffer 环绕、部分写入回插、静默超时触发、恢复状态机各 step、消息关键性分类边界。

---

## Pre-Existing Issues (not blocking)

- 项目缺少单元/功能测试基础设施。
- 部分历史代码未使用 `std::atomic` 保护共享状态。

## Low-Confidence Observations

- 无。
