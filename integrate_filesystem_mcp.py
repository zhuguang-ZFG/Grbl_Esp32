#!/usr/bin/env python3
"""
Integrate Enhanced Filesystem MCP Server into Cursor Configuration
Adds the improved filesystem_mcp_server.py to Cursor's MCP settings
"""

import os
import json
import sys
from pathlib import Path

def get_cursor_settings_path():
    """Get Cursor settings.json path"""
    if sys.platform == "win32":
        appdata = os.environ.get("APPDATA")
        if appdata:
            return os.path.join(appdata, "Cursor", "User", "settings.json")
    elif sys.platform == "darwin":
        home = os.environ.get("HOME")
        if home:
            return os.path.join(home, "Library", "Application Support", "Cursor", "User", "settings.json")
    else:  # Linux
        home = os.environ.get("HOME")
        if home:
            return os.path.join(home, ".config", "Cursor", "User", "settings.json")
    
    raise ValueError("Could not determine Cursor settings path for this platform")


def integrate_filesystem_mcp():
    """Integrate enhanced filesystem MCP server into Cursor configuration"""
    
    # Get paths
    cursor_settings_path = get_cursor_settings_path()
    project_root = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))
    mcp_server_path = os.path.join(project_root, "filesystem_mcp_server.py")
    
    print(f"=== Integrating Enhanced Filesystem MCP Server ===")
    print(f"Project root: {project_root}")
    print(f"MCP server: {mcp_server_path}")
    print(f"Cursor settings: {cursor_settings_path}")
    print()
    
    # Verify MCP server exists
    if not os.path.exists(mcp_server_path):
        print(f"ERROR: MCP server not found: {mcp_server_path}")
        return False
    
    # Create settings directory if needed
    settings_dir = os.path.dirname(cursor_settings_path)
    os.makedirs(settings_dir, exist_ok=True)
    
    # Load existing settings
    settings = {}
    if os.path.exists(cursor_settings_path):
        try:
            with open(cursor_settings_path, 'r', encoding='utf-8') as f:
                content = f.read()
                if content.strip():
                    settings = json.loads(content)
        except json.JSONDecodeError as e:
            print(f"WARNING: Could not parse existing settings: {e}")
            print("Creating new settings file...")
            settings = {}
        except Exception as e:
            print(f"WARNING: Error loading settings: {e}")
            settings = {}
    
    # Ensure MCP structure exists
    if "mcp" not in settings:
        settings["mcp"] = {}
    if "servers" not in settings["mcp"]:
        settings["mcp"]["servers"] = {}
    
    # Configure enhanced filesystem MCP server
    # Use a unique name to avoid conflicts with the official filesystem server
    server_name = "filesystem-enhanced"
    
    settings["mcp"]["servers"][server_name] = {
        "command": "python",
        "args": [mcp_server_path],
        "disabled": False,
        "env": {
            "PROJECT_ROOT": project_root,
            "PYTHONPATH": project_root,
            "PYTHONIOENCODING": "utf-8"
        },
        "description": "Enhanced filesystem MCP server with full protocol support and extended file operations"
    }
    
    # Save updated settings
    try:
        # Backup existing settings
        if os.path.exists(cursor_settings_path):
            from datetime import datetime
            backup_path = f"{cursor_settings_path}.backup.{datetime.now().strftime('%Y%m%d_%H%M%S')}"
            import shutil
            shutil.copy2(cursor_settings_path, backup_path)
            print(f"Backed up existing settings to: {backup_path}")
        
        # Write new settings
        with open(cursor_settings_path, 'w', encoding='utf-8') as f:
            json.dump(settings, f, indent=2, ensure_ascii=False)
        
        print(f"[OK] Successfully integrated MCP server '{server_name}' into Cursor configuration")
        print(f"  Configuration file: {cursor_settings_path}")
        print()
        print("Available tools:")
        print("  - list_files: List files and directories")
        print("  - read_file: Read file content (text or binary)")
        print("  - write_file: Write content to file")
        print("  - delete_file: Delete file or directory")
        print("  - create_directory: Create directory")
        print("  - move_file: Move or rename file/directory")
        print("  - file_stat: Get file/directory statistics")
        print()
        print("Note: You may need to restart Cursor for changes to take effect.")
        return True
        
    except Exception as e:
        print(f"ERROR: Failed to save settings: {e}")
        return False


if __name__ == "__main__":
    try:
        success = integrate_filesystem_mcp()
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"FATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
