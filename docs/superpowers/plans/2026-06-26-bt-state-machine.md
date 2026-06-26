# BT 状态机 + 软件流控实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Grbl_Esp32 建立可观测的蓝牙 SPP 状态机，实现断连清理、`Bf` 字段修正、`?` 频率节流，为后续假连接自动恢复奠基。

**Architecture：** 新增 `BTState` 模块统一处理所有 SPP 事件并维护状态；`BTConfig` 仅做事件转发；`Protocol` 主循环周期性调用 `bt_state_update()`；`Report` 使用真实 `client_buffer` 可用空间替代硬编码 `512`。

**Tech Stack：** C++17 / Arduino-ESP32 / PlatformIO / ESP-IDF Bluedroid SPP

---

## 文件结构

| 文件 | 责任 |
|---|---|
| `Grbl_Esp32/src/WebUI/BTState.h` | BT 状态枚举与对外接口声明；无蓝牙时提供空桩 |
| `Grbl_Esp32/src/WebUI/BTState.cpp` | 状态机实现、SPP 事件处理、断连清理、纸张系统通知 |
| `Grbl_Esp32/src/WebUI/BTConfig.cpp` | 将 `my_spp_cb` 事件转发给 `BTState`，移除直接纸张系统调用 |
| `Grbl_Esp32/src/Protocol.cpp` | 主循环调用 `bt_state_update()` |
| `Grbl_Esp32/src/Serial.h` / `.cpp` | 新增 `client_buffer_free()` 辅助函数；`execute_realtime_command` 中对 BT 的 `?` 做节流 |
| `Grbl_Esp32/src/Report.cpp` | BT 客户端的 `Bf` 字段改用 `client_buffer_free(CLIENT_BT)` |
| `Grbl_Esp32/src/Config.h` | 新增 `BT_STATUS_REPORT_MIN_INTERVAL_MS` 宏 |

---

## Task 1: 创建 BTState.h 接口头文件

**Files:**
- Create: `Grbl_Esp32/src/WebUI/BTState.h`

- [ ] **Step 1: 写入 BTState.h**

```cpp
#pragma once

#include <cstdint>

#ifdef ENABLE_BLUETOOTH

#    include <BluetoothSerial.h>

enum class BTState : uint8_t {
    Idle,         // 蓝牙未启动
    Advertising,  // SPP 已启动，等待连接
    Connected,    // SPP 已连接，数据正常
    Congested,    // SPP 拥塞，建议暂停 TX
    Recovering,   // 检测到异常，正在恢复（阶段 3 使用）
};

void    bt_state_init(void);
BTState bt_state_get(void);
void    bt_state_on_event(esp_spp_cb_event_t event, esp_spp_cb_param_t* param);
void    bt_state_update(void);
bool    bt_state_is_connected(void);
bool    bt_state_can_tx(void);
uint32_t bt_state_last_activity_ms(void);

#else

// 无蓝牙构建时提供空桩，避免调用方到处写 #ifdef
enum class BTState : uint8_t {
    Idle,
};

static inline void bt_state_init(void) {}
static inline BTState bt_state_get(void) { return BTState::Idle; }
static inline void bt_state_on_event(uint32_t event, void* param) {}
static inline void bt_state_update(void) {}
static inline bool bt_state_is_connected(void) { return false; }
static inline bool bt_state_can_tx(void) { return false; }
static inline uint32_t bt_state_last_activity_ms(void) { return 0; }

#endif
```

- [ ] **Step 2: 编译验证（默认纸张机器）**

Run:
```bash
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -20
```

Expected: `SUCCESS`（此时 BTState.cpp 尚未创建，但头文件不引用自身，不应影响编译）。

- [ ] **Step 3: Commit**

```bash
git add Grbl_Esp32/src/WebUI/BTState.h
git commit -m "feat(bt): 新增 BTState 状态机接口头文件"
```

---

## Task 2: 创建 BTState.cpp 状态机实现

**Files:**
- Create: `Grbl_Esp32/src/WebUI/BTState.cpp`

- [ ] **Step 1: 写入 BTState.cpp**

```cpp
#include "../Grbl.h"

#ifdef ENABLE_BLUETOOTH

#    include "BTState.h"
#    include "../Serial.h"

static volatile BTState bt_state         = BTState::Idle;
static volatile uint32_t bt_last_event_ms = 0;

void bt_state_init(void) {
    bt_state         = BTState::Idle;
    bt_last_event_ms = 0;
}

BTState bt_state_get(void) {
    return bt_state;
}

static void bt_state_set(BTState new_state) {
    bt_state = new_state;
}

void bt_state_on_event(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    bt_last_event_ms = millis();
    switch (event) {
        case ESP_SPP_INIT_EVT:
        case ESP_SPP_START_EVT:
            bt_state_set(BTState::Advertising);
            break;

        case ESP_SPP_SRV_OPEN_EVT: {
            // 连接建立：清空旧缓冲，避免重连后执行半条旧指令
            client_reset_read_buffer(CLIENT_BT);
            bt_state_set(BTState::Connected);
#    if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
            paper_btn_arm_bt_suppress();
            paper_bt_on_spp_connected();
#    endif
        } break;

        case ESP_SPP_CLOSE_EVT:
#    if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
            paper_bt_on_spp_disconnected();
#    endif
            // 断开：同样清空缓冲，防止重连污染
            client_reset_read_buffer(CLIENT_BT);
            bt_state_set(BTState::Advertising);
            break;

        case ESP_SPP_CONG_EVT:
            bt_state_set(param->cong.cong ? BTState::Congested : BTState::Connected);
            break;

        case ESP_SPP_WRITE_EVT:
            bt_state_set(param->write.cong ? BTState::Congested : BTState::Connected);
            break;

        case ESP_SPP_DATA_IND_EVT:
            if (bt_state == BTState::Advertising) {
                // 在 DATA_IND 之前已收到 SRV_OPEN，保险起见切到 Connected
                bt_state_set(BTState::Connected);
            }
            break;

        default:
            break;
    }
}

void bt_state_update(void) {
    // 阶段 3 将在此加入假连接检测与自动恢复
}

bool bt_state_is_connected(void) {
    return bt_state == BTState::Connected || bt_state == BTState::Congested;
}

bool bt_state_can_tx(void) {
    return bt_state == BTState::Connected;
}

uint32_t bt_state_last_activity_ms(void) {
    return bt_last_event_ms;
}

#endif
```

- [ ] **Step 2: 编译验证（默认纸张机器）**

Run:
```bash
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -20
```

Expected: `SUCCESS`。

- [ ] **Step 3: Commit**

```bash
git add Grbl_Esp32/src/WebUI/BTState.cpp
git commit -m "feat(bt): 实现 BTState 状态机与 SPP 事件处理"
```

---

## Task 3: 将 BTConfig SPP 回调转发给 BTState

**Files:**
- Modify: `Grbl_Esp32/src/WebUI/BTConfig.cpp:21-75`

- [ ] **Step 1: 包含 BTState.h 并简化 my_spp_cb**

在 `BTConfig.cpp` 头部 `#include "BTConfig.h"` 之后添加：

```cpp
#    include "BTState.h"
```

将 `static void my_spp_cb(...)` 替换为：

```cpp
    static void my_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
        switch (event) {
            case ESP_SPP_SRV_OPEN_EVT: {
                char str[18];
                str[17]       = '\0';
                uint8_t* addr = param->srv_open.rem_bda;
                sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
                BTConfig::_btclient = str;
                grbl_sendf(CLIENT_ALL, "[MSG:BT Connected with %s]\r\n", str);
            } break;
            case ESP_SPP_CLOSE_EVT:
                grbl_send(CLIENT_ALL, "[MSG:BT Disconnected]\r\n");
                BTConfig::_btclient = "";
                break;
            default:
                break;
        }
        // 所有事件统一交给状态机处理（含纸张系统通知与缓冲清理）
        bt_state_on_event(event, param);
    }
```

- [ ] **Step 2: 在 BTConfig::begin() 中初始化状态机**

在 `BTConfig::begin()` 内 `end();` 之后添加：

```cpp
    bt_state_init();
```

- [ ] **Step 3: 编译验证（默认纸张机器）**

Run:
```bash
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -20
```

Expected: `SUCCESS`。

- [ ] **Step 4: Commit**

```bash
git add Grbl_Esp32/src/WebUI/BTConfig.cpp
git commit -m "refactor(bt): BTConfig SPP 回调统一转发给 BTState"
```

---

## Task 4: 在 Protocol 主循环中调用 bt_state_update

**Files:**
- Modify: `Grbl_Esp32/src/Protocol.cpp`

- [ ] **Step 1: 包含 BTState.h**

在 `Protocol.cpp` 顶部其他 `#include` 附近添加：

```cpp
#include "WebUI/BTState.h"
```

- [ ] **Step 2: 在主循环中插入 bt_state_update**

找到 `protocol_main_loop()` 中已有的蓝牙轮询块（约 `Grbl_Esp32/src/Protocol.cpp:331`），在其之前添加：

```cpp
        bt_state_update();
```

具体上下文类似：

```cpp
        // 服务蓝牙连接状态机（必须在 BT 轮询之前）
        bt_state_update();

#ifdef ENABLE_BLUETOOTH
        const bool bt_active = WebUI::SerialBT.hasClient();
        if (bt_active) {
            protocol_poll_client(CLIENT_BT);
            ...
        }
#endif
```

- [ ] **Step 3: 编译验证（默认纸张机器）**

Run:
```bash
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -20
```

Expected: `SUCCESS`。

- [ ] **Step 4: Commit**

```bash
git add Grbl_Esp32/src/Protocol.cpp
git commit -m "feat(bt): Protocol 主循环调用 bt_state_update"
```

---

## Task 5: 暴露 client_buffer 真实可用空间

**Files:**
- Modify: `Grbl_Esp32/src/Serial.h`
- Modify: `Grbl_Esp32/src/Serial.cpp`

- [ ] **Step 1: 在 Serial.h 声明新函数**

在 `void client_reset_read_buffer(uint8_t client);` 之后添加：

```cpp
int client_buffer_free(uint8_t client);
```

- [ ] **Step 2: 在 Serial.cpp 实现**

在 `client_reset_read_buffer(...)` 函数之后添加：

```cpp
int client_buffer_free(uint8_t client) {
    if (client >= CLIENT_COUNT) {
        return 0;
    }
    return client_buffer[client].availableforwrite();
}
```

- [ ] **Step 3: 编译验证（默认纸张机器）**

Run:
```bash
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -20
```

Expected: `SUCCESS`。

- [ ] **Step 4: Commit**

```bash
git add Grbl_Esp32/src/Serial.h Grbl_Esp32/src/Serial.cpp
git commit -m "feat(serial): 暴露 client_buffer 真实可用空间 client_buffer_free()"
```

---

## Task 6: 修正 BT 状态报告的 Bf 字段

**Files:**
- Modify: `Grbl_Esp32/src/Report.cpp:609-614`

- [ ] **Step 1: 替换硬编码 Bf 计算**

将：

```cpp
#    if defined(ENABLE_BLUETOOTH)
        if (client == CLIENT_BT) {
            //TODO FIXME
            bufsize = 512 - WebUI::SerialBT.available();
        }
#    endif  //ENABLE_BLUETOOTH
```

替换为：

```cpp
#    if defined(ENABLE_BLUETOOTH)
        if (client == CLIENT_BT) {
            bufsize = client_buffer_free(CLIENT_BT);
        }
#    endif  //ENABLE_BLUETOOTH
```

- [ ] **Step 2: 编译验证（默认纸张机器）**

Run:
```bash
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -20
```

Expected: `SUCCESS`。

- [ ] **Step 3: Commit**

```bash
git add Grbl_Esp32/src/Report.cpp
git commit -m "fix(report): BT 状态报告 Bf 字段使用真实 client_buffer 可用空间"
```

---

## Task 7: 对 BT 的 `?` 实时状态查询做频率节流

**Files:**
- Modify: `Grbl_Esp32/src/Config.h`
- Modify: `Grbl_Esp32/src/Serial.cpp:248-264`

- [ ] **Step 1: 在 Config.h 新增配置宏**

在 `Config.h` 合适位置（如与其他 buffer 宏一起）添加：

```cpp
#ifndef BT_STATUS_REPORT_MIN_INTERVAL_MS
#    define BT_STATUS_REPORT_MIN_INTERVAL_MS 50u  // 蓝牙 ? 最小间隔，避免高频抖动
#endif
```

- [ ] **Step 2: 在 Serial.cpp 的 execute_realtime_command 中节流**

找到 `case Cmd::StatusReport:` 分支，替换为：

```cpp
        case Cmd::StatusReport: {
#ifdef ENABLE_BLUETOOTH
            if (client == CLIENT_BT) {
                static uint32_t last_bt_status_report_ms = 0;
                uint32_t        now_ms                   = millis();
                if (now_ms - last_bt_status_report_ms < BT_STATUS_REPORT_MIN_INTERVAL_MS) {
                    break;
                }
                last_bt_status_report_ms = now_ms;
            }
#endif
            report_realtime_status(client);  // direct call instead of setting flag
            break;
        }
```

- [ ] **Step 3: 编译验证（默认纸张机器）**

Run:
```bash
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -20
```

Expected: `SUCCESS`。

- [ ] **Step 4: Commit**

```bash
git add Grbl_Esp32/src/Config.h Grbl_Esp32/src/Serial.cpp
git commit -m "feat(bt): 对 BT 高频 ? 状态查询做 50ms 节流"
```

---

## Task 8: 非纸张机器编译验证

**Files:**
- N/A（验证任务）

- [ ] **Step 1: 编译 mpcnc_v1p2.h**

Run:
```bash
export PLATFORMIO_BUILD_FLAGS='-DMACHINE_FILENAME=mpcnc_v1p2.h'
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -15
```

Expected: `SUCCESS`。

- [ ] **Step 2: 编译 test_drive.h**

Run:
```bash
export PLATFORMIO_BUILD_FLAGS='-DMACHINE_FILENAME=test_drive.h'
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -15
```

Expected: `SUCCESS`。

- [ ] **Step 3: 恢复默认编译并提交验证结果**

Run:
```bash
unset PLATFORMIO_BUILD_FLAGS
'C:/Users/zhugu/.platformio/penv/Scripts/platformio.exe' run -e release 2>&1 | tail -15
```

Expected: `SUCCESS`。

```bash
git commit --allow-empty -m "ci(bt): 验证 BT 状态机在纸张/非纸张机器均编译通过"
```

---

## Task 9: 实机/模拟测试（可选但强烈建议）

**Files:**
- N/A（验证任务）

- [ ] **Step 1: 高频 `?` 测试**

连接蓝牙后，从上位机以 100Hz 发送 `?`，持续 30 秒：
- 观察面板电机是否出现微动。
- 串口日志中不应出现 `[BT-EOL gap=...]` 异常增大。

- [ ] **Step 2: 断连重连清理测试**

1. 连接蓝牙。
2. 发送半条指令（如 `G0 X10` 但不回车）。
3. 强制断开蓝牙（关闭上位机或走远）。
4. 重新连接。
5. 发送 `$I` 或 `?`。
6. 确认不会执行旧的 `G0 X10`，也不会触发自动换纸。

- [ ] **Step 3: `Bf` 字段验证**

连接后发送 `?`，观察返回的状态行：
- `Bf` 第一个数字应接近 `client_buffer_free(CLIENT_BT)`（即 2048 减去缓冲中已存字节），而不是固定 `512` 附近。

- [ ] **Step 4: 记录测试结果**

将观察到的现象写入 `docs/superpowers/plans/2026-06-26-bt-state-machine-test-notes.md`（如未实机测试则标注“待实机验证”）。

---

## 自检清单

- [x] **Spec coverage：** 状态机、事件转发、断连清理、`Bf` 修正、`?` 节流、非纸张编译均对应到具体 Task。
- [x] **Placeholder scan：** 无 TBD/TODO/"实现 later"；所有代码片段完整。
- [x] **类型一致性：** `BTState` 枚举、`bt_state_on_event` 签名、`client_buffer_free` 返回类型在 Task 间一致。
- [x] **风险回退：** 所有改动均为增量，禁用蓝牙或回滚单个 commit 即可回退。
