#!/usr/bin/env python3
"""
Filesystem MCP Server - Enhanced Version
Provides comprehensive file system access for Grbl_Esp32 project
Implements full MCP protocol with extended file operations
"""

import sys
import os
import json
import base64
import shutil
from pathlib import Path
from datetime import datetime
from typing import Dict, Any, Optional, List

# Configuration
PROJECT_ROOT = os.path.abspath(
    os.environ.get("PROJECT_ROOT", os.path.dirname(os.path.abspath(__file__)))
)
ALLOWED_ROOT = PROJECT_ROOT  # Security: restrict access to project root

# MCP Protocol Version
MCP_PROTOCOL_VERSION = "2024-11-05"


def safe_path(path: str) -> Path:
    """
    Convert relative path to absolute path and ensure it's within allowed root.
    Prevents directory traversal attacks.
    """
    # Normalize path separators
    if path.startswith("/"):
        path = path[1:]
    
    # Resolve to absolute path within project root
    full_path = Path(ALLOWED_ROOT) / path
    resolved_path = full_path.resolve()
    
    # Security check: ensure path is within allowed root
    allowed_root_path = Path(ALLOWED_ROOT).resolve()
    try:
        resolved_path.relative_to(allowed_root_path)
    except ValueError:
        raise ValueError(f"Path outside allowed root: {path}")
    
    return resolved_path


def get_file_stat(path: Path) -> Dict[str, Any]:
    """Get file/directory statistics"""
    try:
        stat_info = path.stat()
        return {
            "name": path.name,
            "path": str(path.relative_to(Path(ALLOWED_ROOT))),
            "isFile": path.is_file(),
            "isDirectory": path.is_dir(),
            "size": stat_info.st_size if path.is_file() else 0,
            "mtime": stat_info.st_mtime,
            "ctime": stat_info.st_ctime,
            "mode": stat_info.st_mode
        }
    except Exception as e:
        raise ValueError(f"Failed to stat path: {e}")


def handle_initialize(params: Dict[str, Any], request_id: Optional[Any]) -> Dict[str, Any]:
    """Handle MCP initialize request"""
    return {
        "jsonrpc": "2.0",
        "id": request_id,
        "result": {
            "protocolVersion": MCP_PROTOCOL_VERSION,
            "capabilities": {
                "tools": {},
                "resources": {},
                "prompts": {}
            },
            "serverInfo": {
                "name": "filesystem-mcp-server",
                "version": "1.0.0"
            }
        }
    }


def handle_initialized(params: Dict[str, Any]) -> None:
    """Handle MCP initialized notification (no response needed)"""
    print(f"[INFO] Client initialized", file=sys.stderr)


def handle_tools_list(params: Dict[str, Any], request_id: Optional[Any]) -> Dict[str, Any]:
    """List available tools"""
    tools = [
        {
            "name": "list_files",
            "description": "List files and directories in a directory",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "directory": {
                        "type": "string",
                        "description": "Directory path (relative to project root)"
                    },
                    "pattern": {
                        "type": "string",
                        "description": "File pattern (glob), default: '*'",
                        "default": "*"
                    },
                    "recursive": {
                        "type": "boolean",
                        "description": "List recursively",
                        "default": False
                    }
                },
                "required": ["directory"]
            }
        },
        {
            "name": "read_file",
            "description": "Read file content (text or binary)",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "File path (relative to project root)"
                    },
                    "encoding": {
                        "type": "string",
                        "description": "Encoding for text files, or 'binary' for binary files",
                        "default": "utf-8"
                    }
                },
                "required": ["file_path"]
            }
        },
        {
            "name": "write_file",
            "description": "Write content to a file",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "File path (relative to project root)"
                    },
                    "content": {
                        "type": "string",
                        "description": "File content (text or base64 for binary)"
                    },
                    "encoding": {
                        "type": "string",
                        "description": "Encoding for text files",
                        "default": "utf-8"
                    },
                    "binary": {
                        "type": "boolean",
                        "description": "Whether content is base64-encoded binary",
                        "default": False
                    },
                    "append": {
                        "type": "boolean",
                        "description": "Append to file instead of overwriting",
                        "default": False
                    }
                },
                "required": ["file_path", "content"]
            }
        },
        {
            "name": "delete_file",
            "description": "Delete a file or empty directory",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "File or directory path (relative to project root)"
                    },
                    "recursive": {
                        "type": "boolean",
                        "description": "Recursively delete directory",
                        "default": False
                    }
                },
                "required": ["file_path"]
            }
        },
        {
            "name": "create_directory",
            "description": "Create a directory",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "directory_path": {
                        "type": "string",
                        "description": "Directory path (relative to project root)"
                    },
                    "recursive": {
                        "type": "boolean",
                        "description": "Create parent directories if needed",
                        "default": True
                    }
                },
                "required": ["directory_path"]
            }
        },
        {
            "name": "move_file",
            "description": "Move or rename a file/directory",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "old_path": {
                        "type": "string",
                        "description": "Source file/directory path"
                    },
                    "new_path": {
                        "type": "string",
                        "description": "Destination file/directory path"
                    }
                },
                "required": ["old_path", "new_path"]
            }
        },
        {
            "name": "file_stat",
            "description": "Get file/directory statistics",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "File or directory path"
                    }
                },
                "required": ["file_path"]
            }
        }
    ]
    
    return {
        "jsonrpc": "2.0",
        "id": request_id,
        "result": {
            "tools": tools
        }
    }


def handle_tool_call(tool_name: str, arguments: Dict[str, Any], request_id: Optional[Any]) -> Dict[str, Any]:
    """Handle tool call requests"""
    try:
        if tool_name == "list_files":
            directory = arguments.get("directory", ".")
            pattern = arguments.get("pattern", "*")
            recursive = arguments.get("recursive", False)
            
            target_dir = safe_path(directory)
            if not target_dir.exists():
                raise FileNotFoundError(f"Directory not found: {directory}")
            if not target_dir.is_dir():
                raise ValueError(f"Path is not a directory: {directory}")
            
            files = []
            if recursive:
                for file_path in target_dir.rglob(pattern):
                    files.append(get_file_stat(file_path))
            else:
                for file_path in target_dir.glob(pattern):
                    files.append(get_file_stat(file_path))
            
            result = {"files": files}
            
        elif tool_name == "read_file":
            file_path_str = arguments.get("file_path", "")
            encoding = arguments.get("encoding", "utf-8")
            
            file_path = safe_path(file_path_str)
            if not file_path.exists():
                raise FileNotFoundError(f"File not found: {file_path_str}")
            if not file_path.is_file():
                raise ValueError(f"Path is not a file: {file_path_str}")
            
            if encoding == "binary":
                with open(file_path, "rb") as f:
                    content_bytes = f.read()
                    content_b64 = base64.b64encode(content_bytes).decode("ascii")
                    result = {"content": content_b64, "binary": True}
            else:
                with open(file_path, "r", encoding=encoding) as f:
                    content = f.read()
                    result = {"content": content, "binary": False}
            
        elif tool_name == "write_file":
            file_path_str = arguments.get("file_path", "")
            content = arguments.get("content", "")
            encoding = arguments.get("encoding", "utf-8")
            is_binary = arguments.get("binary", False)
            append = arguments.get("append", False)
            
            file_path = safe_path(file_path_str)
            
            # Create parent directories if needed
            file_path.parent.mkdir(parents=True, exist_ok=True)
            
            mode = "ab" if (is_binary and append) else "wb" if is_binary else "a" if append else "w"
            
            if is_binary:
                content_bytes = base64.b64decode(content)
                with open(file_path, mode) as f:
                    f.write(content_bytes)
            else:
                with open(file_path, mode, encoding=encoding) as f:
                    f.write(content)
            
            result = {"success": True, "path": str(file_path.relative_to(Path(ALLOWED_ROOT)))}
            
        elif tool_name == "delete_file":
            file_path_str = arguments.get("file_path", "")
            recursive = arguments.get("recursive", False)
            
            file_path = safe_path(file_path_str)
            if not file_path.exists():
                raise FileNotFoundError(f"Path not found: {file_path_str}")
            
            if file_path.is_dir():
                if recursive:
                    shutil.rmtree(file_path)
                else:
                    file_path.rmdir()
            else:
                file_path.unlink()
            
            result = {"success": True}
            
        elif tool_name == "create_directory":
            directory_path_str = arguments.get("directory_path", "")
            recursive = arguments.get("recursive", True)
            
            directory_path = safe_path(directory_path_str)
            
            if directory_path.exists():
                if directory_path.is_dir():
                    result = {"success": True, "message": "Directory already exists"}
                else:
                    raise ValueError(f"Path exists but is not a directory: {directory_path_str}")
            else:
                if recursive:
                    directory_path.mkdir(parents=True, exist_ok=True)
                else:
                    directory_path.mkdir(parents=False)
                result = {"success": True, "path": str(directory_path.relative_to(Path(ALLOWED_ROOT)))}
            
        elif tool_name == "move_file":
            old_path_str = arguments.get("old_path", "")
            new_path_str = arguments.get("new_path", "")
            
            old_path = safe_path(old_path_str)
            new_path = safe_path(new_path_str)
            
            if not old_path.exists():
                raise FileNotFoundError(f"Source path not found: {old_path_str}")
            
            # Create parent directory for destination if needed
            new_path.parent.mkdir(parents=True, exist_ok=True)
            
            shutil.move(str(old_path), str(new_path))
            result = {"success": True, "new_path": str(new_path.relative_to(Path(ALLOWED_ROOT)))}
            
        elif tool_name == "file_stat":
            file_path_str = arguments.get("file_path", "")
            
            file_path = safe_path(file_path_str)
            if not file_path.exists():
                raise FileNotFoundError(f"Path not found: {file_path_str}")
            
            result = get_file_stat(file_path)
            
        else:
            raise ValueError(f"Unknown tool: {tool_name}")
        
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "content": [
                    {
                        "type": "text",
                        "text": json.dumps(result, ensure_ascii=False, indent=2)
                    }
                ]
            }
        }
        
    except FileNotFoundError as e:
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {
                "code": -32004,
                "message": str(e)
            }
        }
    except ValueError as e:
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {
                "code": -32002,
                "message": str(e)
            }
        }
    except PermissionError as e:
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {
                "code": -32003,
                "message": f"Permission denied: {e}"
            }
        }
    except Exception as e:
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {
                "code": -32000,
                "message": f"Internal error: {str(e)}"
            }
        }


def handle_request(request: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """Main request handler"""
    try:
        jsonrpc_version = request.get("jsonrpc", "2.0")
        method = request.get("method", "")
        params = request.get("params", {})
        request_id = request.get("id")
        
        # Handle notifications (no response needed)
        if request_id is None:
            if method == "notifications/initialized":
                handle_initialized(params)
                return None
            else:
                # Unknown notification, ignore
                return None
        
        # Handle methods
        if method == "initialize":
            return handle_initialize(params, request_id)
        elif method == "tools/list":
            return handle_tools_list(params, request_id)
        elif method == "tools/call":
            tool_name = params.get("name", "")
            arguments = params.get("arguments", {})
            return handle_tool_call(tool_name, arguments, request_id)
        else:
            return {
                "jsonrpc": jsonrpc_version,
                "id": request_id,
                "error": {
                    "code": -32601,
                    "message": f"Method not found: {method}"
                }
            }
            
    except json.JSONDecodeError:
        return {
            "jsonrpc": "2.0",
            "id": None,
            "error": {
                "code": -32700,
                "message": "Parse error"
            }
        }
    except Exception as e:
        return {
            "jsonrpc": "2.0",
            "id": request.get("id"),
            "error": {
                "code": -32603,
                "message": f"Internal error: {str(e)}"
            }
        }


def main():
    """Main MCP server loop"""
    print(f"[INFO] Filesystem MCP Server started", file=sys.stderr)
    print(f"[INFO] Project root: {PROJECT_ROOT}", file=sys.stderr)
    print(f"[INFO] Protocol version: {MCP_PROTOCOL_VERSION}", file=sys.stderr)
    
    try:
        while True:
            try:
                line = sys.stdin.readline()
                if not line:
                    break
                
                line = line.strip()
                if not line:
                    continue
                
                request = json.loads(line)
                response = handle_request(request)
                
                if response is not None:
                    print(json.dumps(response, ensure_ascii=False), flush=True)
                    
            except EOFError:
                break
            except KeyboardInterrupt:
                print("[INFO] Server shutting down...", file=sys.stderr)
                break
            except Exception as e:
                print(f"[ERROR] Unexpected error: {e}", file=sys.stderr)
                error_response = {
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {
                        "code": -32603,
                        "message": f"Internal error: {str(e)}"
                    }
                }
                print(json.dumps(error_response), flush=True)
                
    except Exception as e:
        print(f"[FATAL] Fatal error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
