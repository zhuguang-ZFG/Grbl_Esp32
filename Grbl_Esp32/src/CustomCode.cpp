// This file loads custom code from the Custom/ subdirectory if
// CUSTOM_CODE_FILENAME is defined.
// Dependency: Grbl_Esp32/Custom/paper_system.cpp (clean build if you change it).

#include "Grbl.h"

#ifdef CUSTOM_CODE_FILENAME
#    include CUSTOM_CODE_FILENAME
#else
// 无自定义机器时提供占位，避免 GCode 里调用 user_m_code 未定义
__attribute__((weak)) Error user_m_code(uint16_t code) {
    (void)code;
    return Error::GcodeUnsupportedCommand;
}
#endif

#ifdef DISPLAY_CODE_FILENAME
#    include DISPLAY_CODE_FILENAME
#endif