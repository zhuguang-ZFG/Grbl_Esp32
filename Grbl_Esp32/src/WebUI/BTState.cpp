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

#    include "BTConfig.h"
#    include "BTState.h"
#    include "../Serial.h"

static volatile BTState  bt_state         = BTState::Idle;
static volatile uint32_t bt_last_event_ms = 0;

// Recovery state
static uint8_t  bt_recovery_step      = 0;
static uint32_t bt_recovery_timestamp = 0;
static uint8_t  bt_recovery_attempts  = 0;

// TX ring buffer for congested-but-critical messages
static uint8_t bt_tx_ring[BT_TX_RING_SIZE];
static size_t  bt_tx_ring_head = 0;
static size_t  bt_tx_ring_tail = 0;
static size_t  bt_tx_ring_used = 0;

void bt_state_init(void) {
    bt_state              = BTState::Idle;
    bt_last_event_ms      = 0;
    bt_recovery_step      = 0;
    bt_recovery_timestamp = 0;
    bt_recovery_attempts  = 0;
    bt_tx_ring_head       = 0;
    bt_tx_ring_tail       = 0;
    bt_tx_ring_used       = 0;
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
            bt_recovery_attempts = 0;  // 成功连接后重置恢复计数
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

static size_t bt_tx_ring_free(void) {
    return BT_TX_RING_SIZE - bt_tx_ring_used;
}

static bool bt_tx_ring_push(const char* data, size_t len) {
    if (len > bt_tx_ring_free()) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        bt_tx_ring[bt_tx_ring_head] = data[i];
        bt_tx_ring_head             = (bt_tx_ring_head + 1) % BT_TX_RING_SIZE;
    }
    bt_tx_ring_used += len;
    return true;
}

static size_t bt_tx_ring_pop(char* out, size_t max_len) {
    size_t n = (bt_tx_ring_used < max_len) ? bt_tx_ring_used : max_len;
    for (size_t i = 0; i < n; i++) {
        out[i]          = bt_tx_ring[bt_tx_ring_tail];
        bt_tx_ring_tail = (bt_tx_ring_tail + 1) % BT_TX_RING_SIZE;
    }
    bt_tx_ring_used -= n;
    return n;
}

static void bt_tx_ring_reset(void) {
    bt_tx_ring_head = 0;
    bt_tx_ring_tail = 0;
    bt_tx_ring_used = 0;
}

bool bt_tx_send(const char* text, size_t len, bool critical) {
    if (text == nullptr || len == 0) {
        return true;
    }

    if (!WebUI::SerialBT.hasClient()) {
        return false;
    }

    BTState s = bt_state_get();
    if (s == BTState::Idle || s == BTState::Advertising || s == BTState::Recovering) {
        return false;
    }

    if (s == BTState::Connected) {
        size_t written = WebUI::SerialBT.write((const uint8_t*)text, len);
        if (written == len) {
            return true;
        }
        // 底层队列已满：关键消息入环，非关键消息丢弃
        if (critical && written < len && bt_tx_ring_push(text + written, len - written)) {
            return true;
        }
        return false;
    }

    // Congested：只缓存关键消息
    if (critical && bt_tx_ring_push(text, len)) {
        return true;
    }
    return false;
}

void bt_tx_flush(void) {
    if (!WebUI::SerialBT.hasClient() || bt_tx_ring_used == 0) {
        return;
    }

    char   chunk[64];
    size_t total = 0;
    while (bt_tx_ring_used > 0 && total < BT_TX_RING_SIZE) {
        size_t n       = bt_tx_ring_pop(chunk, sizeof(chunk));
        size_t written = WebUI::SerialBT.write((const uint8_t*)chunk, n);
        if (written < n) {
            // 仍拥塞：把未发送部分塞回环首（保持顺序）
            size_t remain = n - written;
            if (remain > bt_tx_ring_free()) {
                // 空间不足，只能丢弃尾部（不应发生，因刚弹出）
                remain = bt_tx_ring_free();
            }
            // 临时回退 tail，把剩余字节写回（保持原有顺序）
            bt_tx_ring_tail = (bt_tx_ring_tail + BT_TX_RING_SIZE - remain) % BT_TX_RING_SIZE;
            for (size_t i = 0; i < remain; i++) {
                bt_tx_ring[bt_tx_ring_tail] = chunk[written + i];
                bt_tx_ring_tail             = (bt_tx_ring_tail + 1) % BT_TX_RING_SIZE;
            }
            bt_tx_ring_used += remain;
            break;
        }
        total += n;
    }
}

static void bt_execute_recovery(uint32_t now_ms) {
    switch (bt_recovery_step) {
        case 0:  // 结束 SPP
            WebUI::SerialBT.end();
            bt_recovery_step      = 1;
            bt_recovery_timestamp = now_ms;
            bt_tx_ring_reset();
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[BTState] SPP ended for recovery");
            break;

        case 1:  // 冷却
            if (now_ms - bt_recovery_timestamp < BT_RECOVERY_COOLDOWN_MS) {
                return;
            }
            bt_recovery_step      = 2;
            bt_recovery_timestamp = now_ms;
            break;

        case 2: {  // 重新启动 SPP
            String bt_name = WebUI::bt_config.BTname();
            if (WebUI::SerialBT.begin(bt_name.c_str())) {
                bt_recovery_attempts = 0;
                bt_state_set(BTState::Advertising);
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[BTState] SPP restarted, advertising as %s", bt_name.c_str());
            } else {
                bt_recovery_attempts++;  // begin 失败，计入尝试次数
                if (bt_recovery_attempts > BT_RECOVERY_MAX_RETRIES) {
                    bt_state_set(BTState::Idle);
                    grbl_msg_sendf(CLIENT_SERIAL,
                                   MsgLevel::Info,
                                   "[BTState] SPP restart failed %u times, giving up",
                                   bt_recovery_attempts);
                } else {
                    // 重试：回到 step 0 重新 end/begin
                    bt_recovery_step = 0;
                    grbl_msg_sendf(CLIENT_SERIAL,
                                   MsgLevel::Info,
                                   "[BTState] SPP restart failed (attempt %u/%u), retrying",
                                   bt_recovery_attempts,
                                   BT_RECOVERY_MAX_RETRIES);
                }
            }
        } break;

        default:
            bt_recovery_step = 0;
            break;
    }
}

void bt_state_update(void) {
    uint32_t now_ms = millis();
    BTState  s      = bt_state_get();

    // 1. 假连接检测
    if (s == BTState::Connected || s == BTState::Congested) {
        if (now_ms - bt_last_event_ms > BT_LINK_SILENCE_TIMEOUT_MS) {
            if (bt_recovery_attempts >= BT_RECOVERY_MAX_RETRIES) {
                bt_state_set(BTState::Idle);
                grbl_msg_sendf(CLIENT_SERIAL,
                               MsgLevel::Info,
                               "[BTState] Link silent, max recovery retries (%u) exceeded",
                               BT_RECOVERY_MAX_RETRIES);
                return;
            }
            bt_state_set(BTState::Recovering);
            bt_recovery_step      = 0;
            bt_recovery_timestamp = now_ms;
            bt_recovery_attempts  = 1;  // 本次恢复周期的第一次尝试
            grbl_msg_sendf(CLIENT_SERIAL,
                           MsgLevel::Info,
                           "[BTState] Link silent for %u ms, entering recovery",
                           BT_LINK_SILENCE_TIMEOUT_MS);
        }
        return;
    }

    // 2. 恢复执行
    if (s == BTState::Recovering) {
        bt_execute_recovery(now_ms);
        return;
    }

    // 3. 拥塞解除后刷出缓存的关键消息
    if (s == BTState::Connected && bt_tx_ring_used > 0) {
        bt_tx_flush();
    }
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
