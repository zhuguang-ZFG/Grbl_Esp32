; R42 / host SIL (grblHAL_sim): lines with glued axes + in-line (comments)
; often error:25 on sim — product dialect radar, not a hard CI fail.
; Policy: D:/Users/zhugu/fz/docs/PRODUCT_SOFT_DIVERGENCE.md (strategy A/C)
G21
G90 (A standard comment)
G1 Z3.810 F228.6 ; a LinuxCNC style comment
G0x0x0 (some lowercase)
G0 X10 (internal comment) Y0
G0X0 (internal comment; with semi colon) Y0Z3
