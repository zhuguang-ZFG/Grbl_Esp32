# BT 状态机 + 软件流控设计文档

## 1. 背景与目标

当前 Grbl_Esp32 纯蓝牙写字机存在以下蓝牙稳定性脆弱点：

1. `my_spp_cb` 只处理 `SRV_OPEN` / `CLOSE`，对 `CONG_EVT`、`WRITE_EVT`、`DATA_IND_EVT` 等事件无处理，无法做流控。
2. BT 断开时不清理 `client_buffer[CLIENT_BT]`，重连后可能执行旧半条指令，误触发换纸。
3. `?` 状态查询没有频率限制，高频查询会放大主循环抖动，已被注释确认会导致面板电机微动。
4. 状态报告 `Bf` 字段使用 `512 - SerialBT.available()` 硬编码，不能反映真实接收缓冲。
5. 无“假连接”检测与自动恢复机制；SPP 已连接但底层无数据时固件无法自愈。

本设计目标：

- 把蓝牙连接抽象成可观测、可恢复的状态机。
- 基于 SPP 事件实现软件流控。
- 断连时彻底清理状态，避免旧指令污染。
- 检测假连接并自动恢复。

## 2. 设计范围

**本次实施范围：** 阶段 1 + 阶段 2（状态机、事件处理、断连清理、流控、`Bf` 修正）。

阶段 3（假连接自动恢复）与阶段 4（TX 发送保护）作为后续迭代，不在本次实施。

## 3. 状态机设计

### 3.1 状态定义

```cpp
enum class BTState : uint8_t {
    Idle,         // 蓝牙未启动
    Advertising,  // SPP 已启动，等待连接
    Connected,    // SPP 已连接，数据正常
    Congested,    // SPP 拥塞，暂停 TX（本次预留，主要由 WRITE_EVT/CONG_EVT 维护）
    Recovering,   // 检测到异常，准备/正在重启（本次预留，阶段 3 实现）
};
```

### 3.2 状态转换

| 当前状态 | 事件/条件 | 下一状态 | 动作 |
|---|---|---|---|
| Idle | `BTConfig::begin()` 成功 | Advertising | 注册 SPP 回调 |
| Advertising | `SRV_OPEN_EVT` | Connected | 清空 `client_buffer[CLIENT_BT]`，通知纸张系统 |
| Connected | `CLOSE_EVT` | Advertising | 清空 `client_buffer[CLIENT_BT]`，通知纸张系统 |
| Connected | `CONG_EVT` (cong=true) | Congested | 标记不可发送 |
| Congested | `CONG_EVT` (cong=false) | Connected | 标记可发送 |
| Connected | `WRITE_EVT` (cong=true) | Congested | 标记不可发送 |
| Congested | `WRITE_EVT` (cong=false) | Connected | 标记可发送 |
| * | `BTConfig::end()` / 初始化失败 | Idle | 清理状态 |

## 4. 模块接口

新增文件 `Grbl_Esp32/src/WebUI/BTState.h`：

```cpp
#pragma once

#include <cstdint>
#include "BluetoothSerial.h"  // for esp_spp_cb_event_t

enum class BTState : uint8_t {
    Idle,
    Advertising,
    Connected,
    Congested,
    Recovering,
};

void    bt_state_init(void);
BTState bt_state_get(void);
void    bt_state_on_event(esp_spp_cb_event_t event, esp_spp_cb_param_t* param);
void    bt_state_update(void);      // 主循环调用
bool    bt_state_is_connected(void);
bool    bt_state_can_tx(void);      // 发送前检查（本次在 Report/流控中局部使用）
uint32_t bt_state_last_activity_ms(void);
```

新增文件 `Grbl_Esp32/src/WebUI/BTState.cpp`：

- 维护当前状态、最后活动时间戳、连接句柄/地址。
- 实现状态转换与清理动作。
- 提供 `bt_state_can_tx()` 等内部辅助函数（供后续 TX 保护使用，本次先实现并局部使用）。

## 5. 事件处理

`BTConfig::my_spp_cb()` 不再直接处理纸张系统通知，而是把所有事件转发给 `bt_state_on_event()`；由状态机在合适的转换点调用纸张系统回调：

```cpp
static void my_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_event_t* param) {
    bt_state_on_event(event, param);
}
```

状态机内部在 `SRV_OPEN_EVT` 和 `CLOSE_EVT` 时调用：

```cpp
#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
    paper_btn_arm_bt_suppress();
    paper_bt_on_spp_connected();   // 仅在 Connected 转换时
#endif
```

以及：

```cpp
#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
    paper_bt_on_spp_disconnected(); // 仅在 CLOSE_EVT 转换时
#endif
```

## 6. 断连清理

在 `CLOSE_EVT` 处理中增加：

```cpp
client_reset_read_buffer(CLIENT_BT);
// 同时清空 client_lines[CLIENT_BT] 中的未完成行
```

避免重连后旧半条指令被当成新连接的首条指令，从而误触发 `paper_bt_on_first_host_ack()` 与自动换纸。

## 7. 流控与 `Bf` 修正

### 7.1 真实 RX 缓冲

`WebUI::InputBuffer`（`client_buffer[CLIENT_BT]`）已提供 `available()` / `availableforwrite()` 方法。状态机在 `DATA_IND_EVT` 中更新最后活动时间，但不直接干预读取；实际读取仍由 `clientCheckTask` / `protocol_poll_client` 完成。

### 7.2 `Bf` 字段修正

修改 `Grbl_Esp32/src/Report.cpp` 中生成 `Bf` 字段的逻辑：

```cpp
if (client == CLIENT_BT) {
    bufsize = client_buffer[CLIENT_BT].availableforwrite();  // 真实可用字节
} else {
    bufsize = client_buffer[client].availableforwrite();
}
```

取代原有 `512 - WebUI::SerialBT.available()` 的硬编码。

### 7.3 `?` 频率限制（可选但建议本次做）

在 `execute_realtime_command(Cmd::StatusReport)` 或 `report_realtime_status()` 入口增加最小间隔节流，默认 50ms：

```cpp
static uint32_t last_status_report_ms = 0;
if (millis() - last_status_report_ms < BT_STATUS_REPORT_MIN_INTERVAL_MS) {
    return;
}
last_status_report_ms = millis();
```

该限制仅对 `CLIENT_BT` 生效，串口不受影响。

## 8. 主循环集成

在 `protocol_main_loop()` 中已有 BT 优先轮询逻辑，新增：

```cpp
bt_state_update();
```

用于更新时间戳、检测状态异常（阶段 3 将扩展为假连接检测）。

## 9. 配置项

在 `Grbl_Esp32/src/Config.h` 或机器头文件中新增宏（带默认值）：

```cpp
#ifndef BT_STATUS_REPORT_MIN_INTERVAL_MS
#    define BT_STATUS_REPORT_MIN_INTERVAL_MS 50u
#endif
```

## 10. 测试策略

1. **编译测试**：`custom_3axis_hr4988.h`（纸张）、`mpcnc_v1p2.h` / `test_drive.h`（非纸张）。
2. **单元/模拟测试**：
   - 高频 `?` 测试：确认 50ms 节流生效，面板电机不微动。
   - 断连清理测试：连接 → 发送半条 `G0 X` → 断开 → 重连，确认旧指令不执行。
3. **实机测试**：
   - 连续 100+ 页写字 + 上位机高频状态查询。
   - 模拟 BT 远端关闭/重开，观察恢复与状态报告。

## 11. 风险与回退

- **风险**：状态机引入后，`BTConfig` 回调语义改变，需确保所有 SPP 事件都被正确映射。
- **风险**：`Bf` 字段修正后，上位机可能根据真实缓冲调整发送节奏，需与奎享等上位机实测兼容。
- **回退**：如阶段 1 引发新问题，可单独禁用 `?` 节流或恢复旧 `Bf` 计算，保留状态机骨架。

## 12. 后续迭代（阶段 3/4）

- **阶段 3**：在 `bt_state_update()` 中检测 `Connected/Congested` 状态下 N 秒无活动且 `SerialBT.hasClient()` 为 true 时，切到 `Recovering` 并重启 SPP。
- **阶段 4**：包装 `CLIENT_BT` 的 TX 路径，使用 `bt_state_can_tx()` 做拥塞判断，必要时将大报告分包或暂存。
