#!/usr/bin/env python3
"""
Grbl_Esp32 MCP Setup without Node.js
This script creates a basic MCP configuration that works without Node.js servers
"""

import os
import json
import sys
from pathlib import Path

def create_mcp_config():
    """Create MCP configuration for Cursor without Node.js dependencies"""
    
    project_path = "f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32"
    
    # Cursor settings path
    cursor_dir = os.path.expandvars(r"%APPDATA%\Cursor\User")
    settings_path = os.path.join(cursor_dir, "settings.json")
    
    print("=== Grbl_Esp32 MCP Setup (No Node.js Required) ===")
    print(f"Project path: {project_path}")
    print(f"Cursor settings: {settings_path}")
    
    # Create directories
    os.makedirs(cursor_dir, exist_ok=True)
    
    # Load existing settings
    settings = {}
    if os.path.exists(settings_path):
        try:
            with open(settings_path, 'r', encoding='utf-8') as f:
                settings = json.load(f)
        except Exception as e:
            print(f"Warning: Could not load existing settings: {e}")
            settings = {}
    
    # Initialize MCP structure
    if "mcp" not in settings:
        settings["mcp"] = {}
    if "servers" not in settings["mcp"]:
        settings["mcp"]["servers"] = {}
    
    # Add Python-based ESP32 tools server
    settings["mcp"]["servers"]["esp32-tools"] = {
        "command": "python",
        "args": [os.path.join(project_path, "esp32_mcp_server.py")],
        "disabled": False,
        "env": {
            "PYTHONPATH": project_path,
            "PYTHONIOENCODING": "utf-8"
        },
        "description": "ESP32 development tools for Grbl_Esp32 project"
    }
    
    # Add Git-based server (if git is available)
    settings["mcp"]["servers"]["git-tools"] = {
        "command": "python",
        "args": [os.path.join(project_path, "git_mcp_server.py")],
        "disabled": False,
        "env": {
            "PYTHONPATH": project_path,
            "PYTHONIOENCODING": "utf-8"
        },
        "description": "Git operations for Grbl_Esp32 project"
    }
    
    # Add filesystem access (built-in Python)
    settings["mcp"]["servers"]["filesystem"] = {
        "command": "python",
        "args": [os.path.join(project_path, "filesystem_mcp_server.py")],
        "disabled": False,
        "env": {
            "PROJECT_ROOT": project_path,
            "PYTHONIOENCODING": "utf-8"
        },
        "description": "File system access for Grbl_Esp32 project"
    }
    
    # Save settings
    try:
        with open(settings_path, 'w', encoding='utf-8') as f:
            json.dump(settings, f, indent=2, ensure_ascii=False)
        print(f"[OK] MCP configuration saved to {settings_path}")
    except Exception as e:
        print(f"[ERROR] Failed to save settings: {e}")
        return False
    
    return True

def create_simple_mcp_servers():
    """Create simplified Python MCP servers"""
    
    project_path = "f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32"
    
    # ESP32 Tools Server
    esp32_server = '''#!/usr/bin/env python3
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
'''

    # Filesystem Server
    fs_server = '''#!/usr/bin/env python3
"""
Filesystem MCP Server - Simplified Version
Provides file system access for Grbl_Esp32 project
"""

import sys
import os
import json
import glob
from pathlib import Path

def list_files(directory=".", pattern="*"):
    """List files in directory"""
    try:
        project_root = os.environ.get("PROJECT_ROOT", ".")
        target_dir = os.path.join(project_root, directory)
        
        if not os.path.exists(target_dir):
            return {"error": f"Directory not found: {target_dir}"}
            
        files = []
        for file_path in glob.glob(os.path.join(target_dir, pattern)):
            rel_path = os.path.relpath(file_path, project_root)
            files.append({
                "path": rel_path,
                "is_dir": os.path.isdir(file_path),
                "size": os.path.getsize(file_path) if os.path.isfile(file_path) else 0
            })
        
        return {"files": files}
    except Exception as e:
        return {"error": str(e)}

def read_file(file_path):
    """Read file content"""
    try:
        project_root = os.environ.get("PROJECT_ROOT", ".")
        full_path = os.path.join(project_root, file_path)
        
        if not os.path.exists(full_path):
            return {"error": f"File not found: {file_path}"}
            
        with open(full_path, 'r', encoding='utf-8') as f:
            content = f.read()
            
        return {"content": content}
    except Exception as e:
        return {"error": str(e)}

def main():
    """Main MCP server loop"""
    print("Filesystem MCP Server started", file=sys.stderr)
    
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
                                "name": "list_files",
                                "description": "List files in directory"
                            },
                            {
                                "name": "read_file",
                                "description": "Read file content"
                            }
                        ]
                    }
                }
            elif method == "tools/call":
                tool_name = request["params"]["name"]
                arguments = request["params"].get("arguments", {})
                
                if tool_name == "list_files":
                    directory = arguments.get("directory", ".")
                    pattern = arguments.get("pattern", "*")
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(list_files(directory, pattern))}]}}
                elif tool_name == "read_file":
                    file_path = arguments.get("file_path", "")
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(read_file(file_path))}]}}
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
'''

    # Write servers to files
    servers = {
        "esp32_mcp_server.py": esp32_server,
        "filesystem_mcp_server.py": fs_server
    }
    
    for filename, content in servers.items():
        filepath = os.path.join(project_path, filename)
        try:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"[OK] Created {filename}")
        except Exception as e:
            print(f"[ERROR] Failed to create {filename}: {e}")
            return False
    
    return True

def main():
    """Main setup function"""
    print("Setting up MCP for Grbl_Esp32 without Node.js...")
    
    # Create MCP configuration
    if not create_mcp_config():
        print("[ERROR] Failed to create MCP configuration")
        return False
    
    # Create simplified MCP servers
    if not create_simple_mcp_servers():
        print("[ERROR] Failed to create MCP servers")
        return False
    
    print("\n[OK] MCP setup completed successfully!")
    print("\nNext steps:")
    print("1. Restart Cursor IDE")
    print("2. Open Grbl_Esp32 project")
    print("3. Use @ to access MCP tools:")
    print("   - @esp32-tools - ESP32 development tools")
    print("   - @filesystem - File operations")
    print("   - @git-tools - Git operations")
    print("\nNote: Some features require pyserial:")
    print("  pip install pyserial")
    
    return True

if __name__ == "__main__":
    main()