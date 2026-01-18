/*
  HR4988VREF.h - HR4988 VREF固定电压控制（极简版）
  Part of Grbl_ESP32
  
  极简版HR4988 VREF控制 - 固定输出0.64V
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE
#ifdef HR4988_VREF_PIN

#include "Grbl.h"

// ================================================================================
// VREF固定电压配置
// ================================================================================

#define HR4988_FIXED_VREF_VOLTAGE   0.64f    // 固定VREF电压 (V)
#define VREF_MAX_VOLTAGE            3.3f     // ESP32 DAC最大电压 (V)
#define HR4988_FIXED_DAC_VALUE      ((uint8_t)(HR4988_FIXED_VREF_VOLTAGE / VREF_MAX_VOLTAGE * 255.0f))  // 固定DAC值 (自动计算)

// ================================================================================
// VREF控制接口（极简版）
// ================================================================================

/**
 * @brief 初始化HR4988 VREF控制（固定输出0.64V）
 * @return true=成功, false=失败
 */
bool hr4988_simple_init();

/**
 * @brief 打印VREF状态
 */
void hr4988_simple_print_status();

#endif // HR4988_VREF_PIN
#endif // AUTO_PAPER_CHANGE_ENABLE
