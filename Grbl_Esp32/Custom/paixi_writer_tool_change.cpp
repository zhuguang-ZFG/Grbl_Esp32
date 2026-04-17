/*
    paixi_writer_tool_change.cpp
    Manual tool-change helper for the Paixi 3-axis writing machine.

    The goal is to make Tn M6 useful without requiring a fully automatic
    carousel/changer. We keep the behavior conservative:

    - wait until prior motion completes
    - raise the pen with M5
    - optionally move to a configured tool-change point
    - issue M0 so the sender/operator can resume after changing pens
*/

static void paixi_queue_tool_change_line(const char* line) {
    if (line && *line) {
        WebUI::inputBuffer.push(line);
    }
}

void user_tool_change(uint8_t new_tool) {
    char gcode_line[64];

    protocol_buffer_synchronize();

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "Paixi manual tool change requested: T%d", new_tool);

    paixi_queue_tool_change_line("M5\r");
    paixi_queue_tool_change_line("G90\r");

#if PAIXI_M6_USE_TOOL_CHANGE_POINT
    grbl_msg_sendf(
        CLIENT_SERIAL,
        MsgLevel::Info,
        "Paixi M6 moving to pen-change point X%.3f Y%.3f",
        (double)PAIXI_M6_TOOL_CHANGE_X,
        (double)PAIXI_M6_TOOL_CHANGE_Y
    );
    snprintf(
        gcode_line,
        sizeof(gcode_line),
        "G0 X%.3f Y%.3f\r",
        (double)PAIXI_M6_TOOL_CHANGE_X,
        (double)PAIXI_M6_TOOL_CHANGE_Y
    );
    paixi_queue_tool_change_line(gcode_line);
#endif

    paixi_queue_tool_change_line("M0\r");
}
