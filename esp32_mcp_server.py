#!/usr/bin/env python3
"""
ESP32 Tools MCP Server - Simplified Version
Provides ESP32 development tools for Grbl_Esp32 project
"""

import sys
import os
import json
import subprocess
from pathlib import Path

def list_serial_ports():
    """List available serial ports"""
    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        return [{"port": p.device, "desc": p.description} for p in ports]
    except ImportError:
        return [{"error": "pyserial not installed. Run: pip install pyserial"}]

def build_firmware():
    """Build firmware using PlatformIO"""
    try:
        result = subprocess.run(
            ["pio", "run", "-e", "release"],
            cwd=os.environ.get("PROJECT_ROOT", "."),
            capture_output=True,
            text=True,
            timeout=300
        )
        return {
            "success": result.returncode == 0,
            "stdout": result.stdout,
            "stderr": result.stderr
        }
    except Exception as e:
        return {"error": str(e)}

def upload_firmware():
    """Upload firmware to ESP32"""
    try:
        result = subprocess.run(
            ["pio", "run", "-t", "upload", "-e", "release"],
            cwd=os.environ.get("PROJECT_ROOT", "."),
            capture_output=True,
            text=True,
            timeout=120
        )
        return {
            "success": result.returncode == 0,
            "stdout": result.stdout,
            "stderr": result.stderr
        }
    except Exception as e:
        return {"error": str(e)}

def main():
    """Main MCP server loop"""
    print("ESP32 Tools MCP Server started", file=sys.stderr)
    
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
                                "name": "list_serial_ports",
                                "description": "List available serial ports"
                            },
                            {
                                "name": "build_firmware",
                                "description": "Build Grbl_Esp32 firmware"
                            },
                            {
                                "name": "upload_firmware", 
                                "description": "Upload firmware to ESP32"
                            }
                        ]
                    }
                }
            elif method == "tools/call":
                tool_name = request["params"]["name"]
                if tool_name == "list_serial_ports":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(list_serial_ports())}]}}
                elif tool_name == "build_firmware":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(build_firmware())}]}}
                elif tool_name == "upload_firmware":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(upload_firmware())}]}}
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
