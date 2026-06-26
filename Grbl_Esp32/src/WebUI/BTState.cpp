/*
  BTState.cpp - Bluetooth SPP connection state machine

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

#include "../Grbl.h"

#ifdef ENABLE_BLUETOOTH

#    include "BTState.h"
#    include "../Serial.h"

static volatile BTState  bt_state         = BTState::Idle;
static volatile uint32_t bt_last_event_ms = 0;

void bt_state_init(void) {
    bt_state         = BTState::Idle;
    bt_last_event_ms = 0;
}

BTState bt_state_get(void) {
    BTState s = bt_state;
    return s;
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

#    ifdef ESP_SPP_UNINIT_EVT
        case ESP_SPP_UNINIT_EVT:
#    endif
#    ifdef ESP_SPP_SRV_STOP_EVT
        case ESP_SPP_SRV_STOP_EVT:
#    endif
#    if defined(ESP_SPP_UNINIT_EVT) || defined(ESP_SPP_SRV_STOP_EVT)
            bt_state_set(BTState::Idle);
            break;
#    endif

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

        case ESP_SPP_DATA_IND_EVT: {
            BTState s = bt_state;
            if (s == BTState::Advertising) {
                // 在 DATA_IND 之前已收到 SRV_OPEN，保险起见切到 Connected
                bt_state_set(BTState::Connected);
            }
        } break;

        default:
            break;
    }
}

void bt_state_update(void) {
    // 阶段 3 将在此加入假连接检测与自动恢复
}

bool bt_state_is_connected(void) {
    BTState s = bt_state;
    return s == BTState::Connected || s == BTState::Congested;
}

bool bt_state_can_tx(void) {
    BTState s = bt_state;
    return s == BTState::Connected;
}

uint32_t bt_state_last_activity_ms(void) {
    return bt_last_event_ms;
}

#endif
