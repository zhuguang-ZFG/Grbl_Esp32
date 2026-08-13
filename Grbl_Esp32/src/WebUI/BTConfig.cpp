/*
  BTConfig.cpp -  Bluetooth functions class

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
#    include <BluetoothSerial.h>
#    include <esp_bt.h>
#    include "BTConfig.h"

namespace WebUI {
    BTConfig        bt_config;
    BluetoothSerial SerialBT;
#    ifdef __cplusplus
    extern "C" {
#    endif
    const uint8_t* esp_bt_dev_get_address(void);
#    ifdef __cplusplus
    }
#    endif

    String BTConfig::_btname       = "";
    char   BTConfig::_btclient[18] = "";

    // SPP 回调运行在蓝牙协议栈任务（core 0），只允许记录事件、置标志；
    // 消息发送和 _btclient 更新推迟到 handle()（clientCheckTask，core 1）执行，
    // 避免在回调里写 SPP 造成死锁、以及跨核并发操作 String 导致堆损坏。
    static volatile bool s_bt_connected_evt    = false;
    static volatile bool s_bt_disconnected_evt = false;
    static char          s_bt_client_addr[18]  = "";

    BTConfig::BTConfig() {}

    bool BTConfig::suppress_paper_button_events() {
#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
        return paper_btn_bt_suppress_active();
#else
        return false;
#endif
    }

    static void my_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
        switch (event) {
            case ESP_SPP_SRV_OPEN_EVT: {  //Server connection open
                uint8_t* addr = param->srv_open.rem_bda;
                snprintf(s_bt_client_addr,
                         sizeof(s_bt_client_addr),
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         addr[0],
                         addr[1],
                         addr[2],
                         addr[3],
                         addr[4],
                         addr[5]);
#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
                // 仅置标志位，EMI 抑制窗口需要在连接瞬间立即生效
                paper_btn_arm_bt_suppress();
                paper_bt_on_spp_connected();
#endif
                s_bt_connected_evt = true;
            } break;
            case ESP_SPP_CLOSE_EVT:  //Client connection closed
#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
                paper_bt_on_spp_disconnected();
#endif
                s_bt_disconnected_evt = true;
                break;
            default:
                break;
        }
    }

    const char* BTConfig::info() {
        static String result;
        String        tmp;
        result = "[MSG:";
        if (Is_BT_on()) {
            result += "Mode=BT:Name=";
            result += _btname;
            result += "(";
            result += device_address();
            result += "):Status=";
            if (SerialBT.hasClient()) {
                result += "Connected with ";
                result += _btclient;
            } else {
                result += "Not connected";
            }
        } else {
            result += "No BT";
        }
        result += "]\r\n";
        return result.c_str();
    }
    /**
     * Check if BlueTooth string is valid
     */

    bool BTConfig::isBTnameValid(const char* hostname) {
        //limited size
        if (!hostname) {
            return true;
        }
        char c;
        // length is checked automatically by string setting
        //only letter and digit
        for (int i = 0; i < strlen(hostname); i++) {
            c = hostname[i];
            if (!(isdigit(c) || isalpha(c) || c == '_')) {
                return false;
            }
        }
        return true;
    }

    const char* BTConfig::device_address() {
        const uint8_t* point = esp_bt_dev_get_address();
        static char    str[18];
        str[17] = '\0';
        sprintf(
            str, "%02X:%02X:%02X:%02X:%02X:%02X", (int)point[0], (int)point[1], (int)point[2], (int)point[3], (int)point[4], (int)point[5]);
        return str;
    }

    /**
     * begin WiFi setup
     */
    void BTConfig::begin() {
        //stop active services
        end();
        _btname = bt_name->get();
        if (wifi_radio_mode->get() == ESP_BT) {
            if (!SerialBT.begin(_btname)) {
                report_status_message(Error::BtFailBegin, CLIENT_ALL);
            } else {
                // 默认 BR/EDR 发射功率仅 +3dBm，拉满到 +9dBm 改善信号强度
                esp_err_t pwr_err = esp_bredr_tx_power_set(ESP_PWR_LVL_P9, ESP_PWR_LVL_P9);
                if (pwr_err != ESP_OK) {
                    grbl_sendf(CLIENT_ALL, "[MSG:BT tx power set failed %d]\r\n", (int)pwr_err);
                }
                SerialBT.register_callback(&my_spp_cb);
#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
                paper_btn_arm_bt_suppress();
#endif
                grbl_sendf(CLIENT_ALL, "[MSG:BT Started with %s]\r\n", _btname.c_str());
            }
        } else {
            end();
        }
    }

    /**
     * End WiFi
     */
    void BTConfig::end() { SerialBT.end(); }

    /**
     * Reset ESP
     */
    void BTConfig::reset_settings() {
        bt_name->setDefault();
        wifi_radio_mode->setDefault();
        grbl_send(CLIENT_ALL, "[MSG:BT reset done]\r\n");
    }

    /**
     * Check if BT is on and working
     */
    bool BTConfig::Is_BT_on() { return btStarted(); }

    /**
     * Handle not critical actions that must be done in sync environement
     */
    void BTConfig::handle() {
        // 消费 SPP 回调置的事件标志（见 my_spp_cb 注释）
        if (s_bt_connected_evt) {
            s_bt_connected_evt = false;
            strncpy(_btclient, s_bt_client_addr, sizeof(_btclient) - 1);
            _btclient[sizeof(_btclient) - 1] = '\0';
            grbl_sendf(CLIENT_ALL, "[MSG:BT Connected with %s]\r\n", _btclient);
        }
        if (s_bt_disconnected_evt) {
            s_bt_disconnected_evt = false;
            _btclient[0]          = '\0';
            grbl_send(CLIENT_ALL, "[MSG:BT Disconnected]\r\n");
        }
        COMMANDS::wait(0);
    }

    BTConfig::~BTConfig() { end(); }
}
#endif  // ENABLE_BLUETOOTH
