#!/usr/bin/env python3
"""
Grbl Command MCP Server
Provides Grbl-specific command analysis and G-code optimization tools
"""

import sys
import os
import json
import re
from pathlib import Path

def analyze_gcode(gcode_content):
    """Analyze G-code content and provide insights"""
    lines = gcode_content.strip().split('\n')
    analysis = {
        "total_lines": len(lines),
        "commands": {},
        "movement_commands": [],
        "spindle_commands": [],
        "feed_rates": [],
        "estimated_time": 0,
        "warnings": []
    }
    
    for line_num, line in enumerate(lines, 1):
        line = line.strip().upper()
        if not line or line.startswith(';'):
            continue
            
        # Extract commands
        if re.match(r'^[GM]\d+', line):
            cmd = re.match(r'^([GM]\d+)', line).group(1)
            analysis["commands"][cmd] = analysis["commands"].get(cmd, 0) + 1
            
            # Movement commands
            if cmd in ['G00', 'G01', 'G02', 'G03']:
                analysis["movement_commands"].append({"line": line_num, "content": line})
                # Extract feed rate
                if 'F' in line:
                    feed_match = re.search(r'F([\d.]+)', line)
                    if feed_match:
                        analysis["feed_rates"].append(float(feed_match.group(1)))
            
            # Spindle commands
            elif cmd in ['M03', 'M04', 'M05']:
                analysis["spindle_commands"].append({"line": line_num, "content": line})
        
        # Look for potential issues
        if 'Z' in line and 'X' in line and 'Y' in line:
            analysis["warnings"].append(f"Line {line_num}: 3D movement detected in 2D mode")
    
    return analysis

def optimize_gcode(gcode_content, target_speed=None):
    """Optimize G-code for better performance"""
    lines = gcode_content.strip().split('\n')
    optimized = []
    current_feed = None
    
    for line in lines:
        line = line.strip()
        if not line or line.startswith(';'):
            optimized.append(line)
            continue
            
        # Remove redundant commands
        if 'G90' in line or 'G91' in line:
            if not any('G90' in prev or 'G91' in prev for prev in optimized[-5:]):
                optimized.append(line)
        else:
            # Optimize feed rates
            if target_speed and 'F' in line:
                line = re.sub(r'F[\d.]+', f'F{target_speed}', line)
            optimized.append(line)
    
    return '\n'.join(optimized)

def generate_test_pattern(pattern_type="square"):
    """Generate test G-code patterns"""
    patterns = {
        "square": """
; Square test pattern
G21 ; Set units to millimeters
G90 ; Absolute positioning
G00 X0 Y0 Z5 ; Move to start position
G01 Z0 F100 ; Lower pen
G01 X100 F1000 ; Draw square
G01 Y100
G01 X0
G01 Y0
G00 Z5 ; Lift pen
G00 X0 Y0 ; Return to origin
""",
        "circle": """
; Circle test pattern
G21 ; Set units to millimeters
G90 ; Absolute positioning
G00 X50 Y0 Z5 ; Move to center
G01 Z0 F100 ; Lower pen
G02 I50 J0 F1000 ; Draw circle
G00 Z5 ; Lift pen
G00 X0 Y0 ; Return to origin
""",
        "paper_change_test": """
; Paper change system test
G21 ; Set units to millimeters
G90 ; Absolute positioning

; Test paper movement
M1000 ; Enable paper change system
M1001 S100 ; Move paper 100 steps forward
G04 P1000 ; Wait 1 second
M1001 S-50 ; Move paper 50 steps backward
G04 P1000 ; Wait 1 second
M1002 ; Disable paper change system

; Test pen movement
G00 X0 Y0 Z5
G01 Z0 F100 ; Lower pen
G01 X10 Y10 F500
G01 X20 Y0
G01 X10 Y-10
G01 X0 Y0
G00 Z5 ; Lift pen
"""
    }
    
    return patterns.get(pattern_type, patterns["square"]).strip()

def validate_grbl_syntax(gcode_content):
    """Validate Grbl G-code syntax"""
    lines = gcode_content.strip().split('\n')
    errors = []
    warnings = []
    
    valid_commands = ['G0', 'G1', 'G2', 'G3', 'G4', 'G17', 'G18', 'G19', 'G20', 'G21', 
                     'G28', 'G90', 'G91', 'G92', 'M0', 'M2', 'M3', 'M4', 'M5', 'M8', 'M9']
    
    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        if not line or line.startswith(';'):
            continue
            
        # Extract command
        cmd_match = re.match(r'^([GM]\d+)', line.upper())
        if cmd_match:
            cmd = cmd_match.group(1)
            if cmd not in valid_commands:
                errors.append(f"Line {line_num}: Invalid command '{cmd}'")
        
        # Check for syntax issues
        if line.count('(') != line.count(')'):
            warnings.append(f"Line {line_num}: Unmatched parentheses")
            
        # Check for deprecated commands
        if 'G61' in line.upper():
            warnings.append(f"Line {line_num}: G61 path control mode may not be supported")
    
    return {
        "valid": len(errors) == 0,
        "errors": errors,
        "warnings": warnings
    }

def main():
    """Main MCP server loop"""
    print("Grbl Command MCP Server started", file=sys.stderr)
    
    while True:
        try:
            line = input()
            if not line:
                continue
                
            request = json.loads(line)
            method = request.get("method", "")
            
            if method == "tools/list":
                response = {
                    "result": {
                        "tools": [
                            {
                                "name": "analyze_gcode",
                                "description": "Analyze G-code content and provide insights"
                            },
                            {
                                "name": "optimize_gcode",
                                "description": "Optimize G-code for better performance"
                            },
                            {
                                "name": "generate_test_pattern",
                                "description": "Generate test G-code patterns"
                            },
                            {
                                "name": "validate_grbl_syntax",
                                "description": "Validate Grbl G-code syntax"
                            }
                        ]
                    }
                }
            elif method == "tools/call":
                tool_name = request["params"]["name"]
                arguments = request["params"].get("arguments", {})
                
                if tool_name == "analyze_gcode":
                    gcode = arguments.get("gcode_content", "")
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(analyze_gcode(gcode), indent=2)}]}}
                elif tool_name == "optimize_gcode":
                    gcode = arguments.get("gcode_content", "")
                    speed = arguments.get("target_speed")
                    result = optimize_gcode(gcode, speed)
                    response = {"result": {"content": [{"type": "text", "text": result}]}}
                elif tool_name == "generate_test_pattern":
                    pattern_type = arguments.get("pattern_type", "square")
                    result = generate_test_pattern(pattern_type)
                    response = {"result": {"content": [{"type": "text", "text": result}]}}
                elif tool_name == "validate_grbl_syntax":
                    gcode = arguments.get("gcode_content", "")
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(validate_grbl_syntax(gcode), indent=2)}]}}
                else:
                    response = {"error": {"code": -32601, "message": f"Unknown tool: {tool_name}"}}
            else:
                response = {"error": {"code": -32601, "message": f"Unknown method: {method}"}}
                
            print(json.dumps(response), flush=True)
            
        except EOFError:
            break
        except Exception as e:
            error_response = {"error": {"code": -32603, "message": str(e)}}
            print(json.dumps(error_response), flush=True)

if __name__ == "__main__":
    main()