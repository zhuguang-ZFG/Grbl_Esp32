/*
  HR4988VREF.cpp - HR4988 VREF固定电压控制实现（极简版）
  Part of Grbl_ESP32
  
  极简版HR4988 VREF控制 - 固定输出0.64V
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#ifdef HR4988_VREF_PIN

#include "HR4988VREF_SIMPLE.h"
#include "PaperChangeUtils.h"

// ================================================================================
// 全局状态变量（极简版）
// ================================================================================

static bool vref_initialized = false;

// ================================================================================
// 核心VREF控制函数实现（极简版）
// ================================================================================

bool hr4988_simple_init() {
    // 检查GPIO25是否可用于DAC
    if (HR4988_VREF_PIN != GPIO_NUM_25) {
        LOG_ERROR("HR4988 VREF must be on GPIO25 for DAC functionality");
        return false;
    }
    
    // 直接设置固定电压0.64V（DAC值49）
    dacWrite(HR4988_VREF_PIN, HR4988_FIXED_DAC_VALUE);
    
    vref_initialized = true;
    LOG_MSG("HR4988 VREF initialized: GPIO%d, Fixed %.3fV (DAC=%d/255)", 
            HR4988_VREF_PIN, HR4988_FIXED_VREF_VOLTAGE, HR4988_FIXED_DAC_VALUE);
    
    return true;
}

void hr4988_simple_print_status() {
    if (!vref_initialized) {
        LOG_MSG("HR4988 VREF not initialized");
        return;
    }
    
    LOG_MSG("HR4988 VREF: %.3fV (Fixed, DAC=%d/255, GPIO%d)", 
            HR4988_FIXED_VREF_VOLTAGE, HR4988_FIXED_DAC_VALUE, HR4988_VREF_PIN);
}

#endif // HR4988_VREF_PIN
#endif // AUTO_PAPER_CHANGE_ENABLE
