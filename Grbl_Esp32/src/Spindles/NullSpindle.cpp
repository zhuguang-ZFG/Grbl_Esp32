/*
    NullSpindle.cpp

    This is used when you don't want to use a spindle No I/O will be used
    and most methods don't do anything

    Part of Grbl_ESP32
    2020 -	Bart Dring

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "NullSpindle.h"
#include "../MotionControl.h"

#ifdef PAIXI_PEN_M3_M5_CONTROL
namespace {
    void move_pen_to_z(float target_z) {
        float* current = system_get_mpos();
        float  target[MAX_N_AXIS] = { 0 };
        for (int axis = 0; axis < MAX_N_AXIS; ++axis) {
            target[axis] = current[axis];
        }
        if (target[Z_AXIS] == target_z) {
            return;
        }

        plan_line_data_t pl_data = {};
        pl_data.feed_rate        = PAIXI_PEN_MOTION_FEED;
        pl_data.spindle_speed    = sys.spindle_speed;
        pl_data.spindle          = SpindleState::Disable;
        pl_data.coolant          = {};
        pl_data.is_jog           = false;
        target[Z_AXIS]           = target_z;

        mc_line(target, &pl_data);
        protocol_buffer_synchronize();
        plan_sync_position();
        gc_sync_position();
    }
}
#endif

namespace Spindles {
    // ======================= Null ==============================
    // Null is just bunch of do nothing (ignore) methods to be used when you don't want a spindle

    void Null::init() {
        is_reversable = false;
        use_delays    = false;
        config_message();
    }
    uint32_t Null::set_rpm(uint32_t rpm) {
        sys.spindle_speed = rpm;
        return rpm;
    }
    void Null::set_state(SpindleState state, uint32_t rpm) {
#ifdef PAIXI_PEN_M3_M5_CONTROL
        if (state == SpindleState::Disable) {
            move_pen_to_z(PAIXI_PEN_UP_Z);
        } else {
            move_pen_to_z(PAIXI_PEN_DOWN_Z);
        }
#endif
        _current_state    = state;
        sys.spindle_speed = rpm;
    }
    SpindleState Null::get_state() { return _current_state; }
    void         Null::stop() {}
    void Null::config_message() {
#ifdef PAIXI_PEN_M3_M5_CONTROL
        grbl_msg_sendf(
            CLIENT_ALL,
            MsgLevel::Info,
            "Pen control via M3/M5 enabled (M3->Z%.3f, M5->Z%.3f)",
            (double)PAIXI_PEN_DOWN_Z,
            (double)PAIXI_PEN_UP_Z
        );
#else
        grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "No spindle");
#endif
    }
}
