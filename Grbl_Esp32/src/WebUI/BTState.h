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
