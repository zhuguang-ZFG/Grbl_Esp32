/*
  BTState.h - Bluetooth SPP connection state machine

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#pragma once

#include <cstdint>

// BT connection state machine states
enum class BTState : uint8_t {
    Idle,         // Bluetooth not started
    Advertising,  // SPP started, waiting for connection
    Connected,    // SPP connected, data flowing normally
    Congested,    // SPP congested, TX should pause
    Recovering,   // Recovering from link failure (phase 3)
};

#ifdef ENABLE_BLUETOOTH

#    include <BluetoothSerial.h>

void    bt_state_init(void);
BTState bt_state_get(void);
void    bt_state_on_event(esp_spp_cb_event_t event, esp_spp_cb_param_t* param);
void    bt_state_update(void);
bool    bt_state_is_connected(void);
bool    bt_state_can_tx(void);
uint32_t bt_state_last_activity_ms(void);

// TX flow control
bool bt_tx_send(const char* text, size_t len, bool critical);
void bt_tx_flush(void);
bool bt_tx_message_is_critical(const char* text);

#else

// Forward declarations so stubs can match the real signatures without
// pulling in the full Bluetooth stack headers.
struct esp_spp_cb_param_t;
typedef uint32_t esp_spp_cb_event_t;

// Stubs for non-Bluetooth builds; keeps call sites free of #ifdef.
static inline void bt_state_init(void) {}
static inline BTState bt_state_get(void) { return BTState::Idle; }
static inline void bt_state_on_event(esp_spp_cb_event_t /*event*/, esp_spp_cb_param_t* /*param*/) {}
static inline void bt_state_update(void) {}
static inline bool bt_state_is_connected(void) { return false; }
static inline bool bt_state_can_tx(void) { return false; }
static inline uint32_t bt_state_last_activity_ms(void) { return 0; }
static inline bool bt_tx_send(const char* /*text*/, size_t /*len*/, bool /*critical*/) { return false; }
static inline void bt_tx_flush(void) {}
static inline bool bt_tx_message_is_critical(const char* /*text*/) { return true; }

#endif
