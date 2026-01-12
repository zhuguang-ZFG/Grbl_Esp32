; Paper Change Test File - Updated Workflow
; Tests the corrected M0 detection and paper change workflow

; Test 1: M0 with content (should trigger paper change on next command)
G0 X10 Y10 Z20     ; Move to safe position
M0                  ; Triggers M0, returns to origin, sends "ok"
                    ; System waits for next G-code command
G1 X20 Y20 Z0 F1000 ; This command triggers pending paper change
                    ; THEN executes after paper change completes
G0 X0 Y0 Z20       ; Return to origin

; Test 2: M0 without content (should skip paper change)  
G1 X50 Y50 Z0 F1000 ; Move to position
M0                  ; Triggers M0, returns to origin, sends "ok"
                    ; No pending paper change since no content after

; Test 3: Multiple M0s with content
M0                  ; First M0 - will detect content after
G1 X30 Y30 Z0      ; Triggers paper change, then executes
M0                  ; Second M0 - will detect content after  
G1 X40 Y40 Z0      ; Triggers paper change again, then executes

; End of test file