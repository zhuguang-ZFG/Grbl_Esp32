/*
  PaperChangeTypes.cpp - 换纸系统数据类型实现
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "PaperChangeTypes.h"

// ================================================================================
// 状态转换矩阵定义
// ================================================================================
const bool state_transition_matrix[PAPER_ERROR + 1][PAPER_ERROR + 1] = {
    // 目标状态: IDLE, PRE_CHECK, EJECTING, FEEDING, UNCLAMP_FEED, CLAMP_FEED, FULL_FEED, REPOSITION, COMPLETE, ERROR
    /* IDLE        */ { true, true,  false,  false,  false,   false,   false,   false,   false,  true },
    /* PRE_CHECK   */ { true, false, true,   true,   false,   false,   false,   false,   false,  true },
    /* EJECTING    */ { true, false, false,  true,   false,   false,   false,   false,   false,  true },
    /* FEEDING     */ { true, false, false,  false,  true,    false,   false,   false,   false,  true },
    /* UNCLAMP_FEED*/ { true, false, false,  false,  false,   true,    false,   false,   false,  true },
    /* CLAMP_FEED  */ { true, false, false,  false,  false,   false,   true,    false,   false,  true },
    /* FULL_FEED   */ { true, false, false,  false,  false,   false,   false,   true,    false,  true },
    /* REPOSITION  */ { true, false, false,  false,  false,   false,   false,   false,   true,    true },
    /* COMPLETE    */ { true, true,  false,  false,  false,   false,   false,   false,   false,  true },
    /* ERROR       */ { true, false, false,  false,  false,   false,   false,   false,   false,  true }
};

// ================================================================================
// 状态名称映射
// ================================================================================
const char* STATE_NAMES[] = {
    "IDLE", "PRE_CHECK", "EJECTING", "FEEDING",
    "UNCLAMP_FEED", "CLAMP_FEED", "FULL_FEED", "REPOSITION", "COMPLETE", "ERROR"
};

#endif // AUTO_PAPER_CHANGE_ENABLE