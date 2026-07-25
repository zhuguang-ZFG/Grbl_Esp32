#pragma once

#include <cstdint>

enum class PaperBtAckEvent : uint8_t {
    SppConnected,
    SppDisconnected,
    HostAck,
    PollIdle,
    PollBusy,
    ChangeCompleted,
    ChangeFailed,
    RealtimeCommand,
};

struct PaperBtAckState {
    bool armed = false;
    bool pending = false;
    bool running = false;
};

inline PaperBtAckState paper_bt_ack_reduce(PaperBtAckState state, PaperBtAckEvent event) {
    switch (event) {
        case PaperBtAckEvent::SppConnected:
            state.armed = !state.running;
            state.pending = false;
            return state;
        case PaperBtAckEvent::SppDisconnected:
            state.armed = false;
            state.pending = false;
            return state;
        case PaperBtAckEvent::HostAck:
            if (state.armed && !state.running) {
                state.armed = false;
                state.pending = true;
            }
            return state;
        case PaperBtAckEvent::PollIdle:
            if (state.pending && !state.running) {
                state.pending = false;
                state.running = true;
            }
            return state;
        case PaperBtAckEvent::PollBusy:
        case PaperBtAckEvent::RealtimeCommand:
            return state;
        case PaperBtAckEvent::ChangeCompleted:
        case PaperBtAckEvent::ChangeFailed:
            state.pending = false;
            state.running = false;
            return state;
        default:
            return state;
    }
}
