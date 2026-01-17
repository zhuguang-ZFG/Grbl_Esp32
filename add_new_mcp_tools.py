#!/usr/bin/env python3
"""
Add new MCP tools to Cursor configuration
"""

import os
import json

def add_new_mcp_servers():
    """Add new MCP servers to Cursor configuration"""
    
    cursor_settings_path = "C:/Users/Administrator/AppData/Roaming/Cursor/User/settings.json"
    project_path = "f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32"
    
    # Load existing settings
    settings = {}
    if os.path.exists(cursor_settings_path):
        try:
            with open(cursor_settings_path, 'r', encoding='utf-8') as f:
                content = f.read()
                settings = json.loads(content)
        except Exception as e:
            print(f"Error loading settings: {e}")
            return False
    
    # Ensure MCP structure exists
    if "mcp" not in settings:
        settings["mcp"] = {}
    if "servers" not in settings["mcp"]:
        settings["mcp"]["servers"] = {}
    
    # Add Grbl Command Server
    settings["mcp"]["servers"]["grbl-commands"] = {
        "command": "python",
        "args": [os.path.join(project_path, "grbl_command_mcp_server.py")],
        "disabled": False,
        "env": {
            "PYTHONPATH": project_path,
            "PYTHONIOENCODING": "utf-8"
        },
        "description": "Grbl G-code analysis and optimization tools"
    }
    
    # Add HR4988 Calculator Server
    settings["mcp"]["servers"]["hr4988-calculator"] = {
        "command": "python",
        "args": [os.path.join(project_path, "hr4988_calculator_mcp_server.py")],
        "disabled": False,
        "env": {
            "PYTHONPATH": project_path,
            "PYTHONIOENCODING": "utf-8"
        },
        "description": "HR4988 stepper driver current calculator"
    }
    
    # Add Serial Monitor Server
    settings["mcp"]["servers"]["serial-monitor"] = {
        "command": "python",
        "args": [os.path.join(project_path, "serial_monitor_mcp_server.py")],
        "disabled": False,
        "env": {
            "PYTHONPATH": project_path,
            "PYTHONIOENCODING": "utf-8"
        },
        "description": "Real-time serial communication and monitoring"
    }
    
    # Save updated settings
    try:
        with open(cursor_settings_path, 'w', encoding='utf-8') as f:
            json.dump(settings, f, indent=2, ensure_ascii=False)
        print("Successfully added new MCP tools to Cursor configuration")
        return True
    except Exception as e:
        print(f"Error saving settings: {e}")
        return False

if __name__ == "__main__":
    if add_new_mcp_servers():
        print("New MCP tools added successfully!")
        print("\nAdded tools:")
        print("- @grbl-commands: G-code analysis and optimization")
        print("- @hr4988-calculator: HR4988 current calculation")
        print("- @serial-monitor: Real-time serial monitoring")
        print("\nRestart Cursor to use the new tools!")
    else:
        print("Failed to add new MCP tools")