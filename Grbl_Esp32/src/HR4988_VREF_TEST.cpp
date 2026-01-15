/*
  HR4988_VREF_TEST.cpp - HR4988 VREF功能测试脚本
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
  
  使用方法：
  1. 在G-code中发送 M1000 - 初始化VREF测试
  2. 在G-code中发送 M1001 - 设置待机模式
  3. 在G-code中发送 M1002 - 设置精密模式  
  4. 在G-code中发送 M1003 - 设置正常模式
  5. 在G-code中发送 M1004 - 设置高扭矩模式
  6. 在G-code中发送 M1005 - 设置最大模式
  7. 在G-code中发送 M1006 - 输出VREF状态
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
        grbl_sendf(CLIENT_ALL, "[MSG: HR4988 VREF test initialized]\r\n");
        
        // 测试所有模式
        motor_current_mode_t modes[] = {
            MOTOR_MODE_STANDBY, MOTOR_MODE_PRECISION, MOTOR_MODE_NORMAL,
            MOTOR_MODE_HIGH_TORQUE, MOTOR_MODE_MAXIMUM
        };
        
        for (int i = 0; i < MOTOR_MODE_COUNT; i++) {
            grbl_sendf(CLIENT_ALL, "[MSG: Testing %s mode]\r\n", 
                         hr4988_simple_get_mode_name(modes[i]));
            
            if (hr4988_simple_set_mode(modes[i])) {
                grbl_sendf(CLIENT_ALL, "[MSG: %s mode OK - Current: %.2fA]\r\n", 
                             hr4988_simple_get_mode_name(modes[i]), 
                             hr4988_simple_get_current());
                delay(1000); // 等待1秒
            } else {
                grbl_sendf(CLIENT_ALL, "[MSG: %s mode FAILED]\r\n", 
                             hr4988_simple_get_mode_name(modes[i]));
            }
        }
        
        // 恢复到正常模式
        hr4988_simple_set_mode(MOTOR_MODE_NORMAL);
        grbl_sendf(CLIENT_ALL, "[MSG: VREF test complete - Current: %.2fA]\r\n", 
                     hr4988_simple_get_current());
    } else {
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - HR4988 VREF initialization failed]\r\n");
    }
}

/**
 * @brief M1001 - 设置待机模式
 */
void user_define_mcode1001() {
    if (hr4988_simple_set_mode(MOTOR_MODE_STANDBY)) {
        grbl_sendf(CLIENT_ALL, "[MSG: HR4988 set to STANDBY mode (%.2fA)]\r\n", 
                     hr4988_simple_get_current());
    } else {
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - Failed to set STANDBY mode]\r\n");
    }
}

/**
 * @brief M1002 - 设置精密模式
 */
void user_define_mcode1002() {
    if (hr4988_simple_set_mode(MOTOR_MODE_PRECISION)) {
        grbl_sendf(CLIENT_ALL, "[MSG: HR4988 set to PRECISION mode (%.2fA)]\r\n", 
                     hr4988_simple_get_current());
    } else {
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - Failed to set PRECISION mode]\r\n");
    }
}

/**
 * @brief M1003 - 设置正常模式
 */
void user_define_mcode1003() {
    if (hr4988_simple_set_mode(MOTOR_MODE_NORMAL)) {
        grbl_sendf(CLIENT_ALL, "[MSG: HR4988 set to NORMAL mode (%.2fA)]\r\n", 
                     hr4988_simple_get_current());
    } else {
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - Failed to set NORMAL mode]\r\n");
    }
}

/**
 * @brief M1004 - 设置高扭矩模式
 */
void user_define_mcode1004() {
    if (hr4988_simple_set_mode(MOTOR_MODE_HIGH_TORQUE)) {
        grbl_sendf(CLIENT_ALL, "[MSG: HR4988 set to HIGH_TORQUE mode (%.2fA)]\r\n", 
                     hr4988_simple_get_current());
    } else {
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - Failed to set HIGH_TORQUE mode]\r\n");
    }
}

/**
 * @brief M1005 - 设置最大模式
 */
void user_define_mcode1005() {
    if (hr4988_simple_set_mode(MOTOR_MODE_MAXIMUM)) {
        grbl_sendf(CLIENT_ALL, "[MSG: HR4988 set to MAXIMUM mode (%.2fA)]\r\n", 
                     hr4988_simple_get_current());
    } else {
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - Failed to set MAXIMUM mode]\r\n");
    }
}

/**
 * @brief M1006 - 输出VREF状态
 */
void user_define_mcode1006() {
    hr4988_simple_print_status();
}

/**
 * @brief M1007 - 测试特定电流值
 */
void user_define_mcode1007() {
    float test_currents[] = {0.3f, 0.5f, 0.8f, 1.0f, 1.2f, 1.5f, 1.8f, 2.0f};
    int count = sizeof(test_currents) / sizeof(test_currents[0]);
    
    grbl_sendf(CLIENT_ALL, "[MSG: Testing HR4988 current values]\r\n");
    
    for (int i = 0; i < count; i++) {
        grbl_sendf(CLIENT_ALL, "[MSG: Testing %.2fA]\r\n", test_currents[i]);
        
        if (hr4988_simple_set_current(test_currents[i])) {
            float voltage = test_currents[i] * HR4988_CURRENT_FACTOR * HR4988_SENSE_RESISTOR;
            grbl_sendf(CLIENT_ALL, "[MSG: OK - Voltage: %.3fV]\r\n", voltage);
        } else {
            grbl_sendf(CLIENT_ALL, "[MSG: FAILED - %.2fA out of range]\r\n", test_currents[i]);
        }
        
        delay(500); // 等待0.5秒
    }
    
    // 恢复到正常模式
    hr4988_simple_set_mode(MOTOR_MODE_NORMAL);
    grbl_sendf(CLIENT_ALL, "[MSG: Current test complete - Current: %.2fA]\r\n", 
                 hr4988_simple_get_current());
}

// 在M代码表中添加注册（需要添加到相关文件中）
/*
// 在用户自定义M代码处理中添加：
case 1000: user_define_mcode1000(); break;
case 1001: user_define_mcode1001(); break;
case 1002: user_define_mcode1002(); break;
case 1003: user_define_mcode1003(); break;
case 1004: user_define_mcode1004(); break;
case 1005: user_define_mcode1005(); break;
case 1006: user_define_mcode1006(); break;
case 1007: user_define_mcode1007(); break;
*/

#endif // HR4988_VREF_PIN
#endif // AUTO_PAPER_CHANGE_ENABLE