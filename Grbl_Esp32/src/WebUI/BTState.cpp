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
