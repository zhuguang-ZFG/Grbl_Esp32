// This file loads custom code from the Custom/ subdirectory if
// CUSTOM_CODE_FILENAME is defined.
// Dependency: Grbl_Esp32/Custom/paper_system.cpp (clean build if you change it).

#include "Grbl.h"

#ifdef CUSTOM_CODE_FILENAME
#    include CUSTOM_CODE_FILENAME
#endif

#ifdef DISPLAY_CODE_FILENAME
#    include DISPLAY_CODE_FILENAME
#endif
