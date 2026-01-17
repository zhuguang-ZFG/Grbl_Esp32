/*
  HR4988_VREF_TEST.cpp - HR4988 VREF功能测试脚本（简化版）
  Part of Grbl_ESP32
  
  使用方法：
  M1000 - 初始化VREF（固定输出0.64V）
  M1006 - 输出VREF状态
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#ifdef HR4988_VREF_PIN

#include "HR4988VREF_SIMPLE.h"

/**
 * @brief M1000 - 初始化HR4988 VREF测试
 */
void user_define_mcode1000() {
    if (hr4988_simple_init()) {
        grbl_sendf(CLIENT_ALL, "[MSG: HR4988 VREF initialized - Fixed 0.64V output]\r\n");
        hr4988_simple_print_status();
    } else {
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - HR4988 VREF initialization failed]\r\n");
    }
}

/**
 * @brief M1006 - 输出VREF状态
 */
void user_define_mcode1006() {
    hr4988_simple_print_status();
}

// 注意：M1001-M1005和M1007已废弃（VREF固定输出0.64V，不支持模式切换）
// 保留函数声明以避免链接错误，但函数体为空
void user_define_mcode1001() {
    grbl_sendf(CLIENT_ALL, "[MSG: VREF is fixed at 0.64V, mode switching not supported]\r\n");
}

void user_define_mcode1002() {
    grbl_sendf(CLIENT_ALL, "[MSG: VREF is fixed at 0.64V, mode switching not supported]\r\n");
}

void user_define_mcode1003() {
    grbl_sendf(CLIENT_ALL, "[MSG: VREF is fixed at 0.64V, mode switching not supported]\r\n");
}

void user_define_mcode1004() {
    grbl_sendf(CLIENT_ALL, "[MSG: VREF is fixed at 0.64V, mode switching not supported]\r\n");
}

void user_define_mcode1005() {
    grbl_sendf(CLIENT_ALL, "[MSG: VREF is fixed at 0.64V, mode switching not supported]\r\n");
}

void user_define_mcode1007() {
    grbl_sendf(CLIENT_ALL, "[MSG: VREF is fixed at 0.64V, current setting not supported]\r\n");
}

#endif // HR4988_VREF_PIN
#endif // AUTO_PAPER_CHANGE_ENABLE
