#pragma once

/*
  StatusBeacon.h - read-only UDP status beacon

  Part of the hutuji tree (see D:/Users/hutuji/docs/protocol.md §9-H′).
  Broadcasts a one-line machine status to the LAN so an observer (S3 小派)
  can learn about jobs driven by ANY client (奎享 Telnet/serial included)
  without occupying the single Telnet slot. Diagnostic observation face
  only: never sends commands, never touches motion/paper/auth logic.
*/

namespace WebUI {
    class StatusBeacon {
    public:
        static void begin();
        static void end();
        static void handle();
    };
}
