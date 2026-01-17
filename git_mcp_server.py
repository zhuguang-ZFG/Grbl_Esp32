#!/usr/bin/env python3
"""
Git MCP Server - Simplified Version
Provides Git operations for Grbl_Esp32 project
"""

import sys
import os
import json
import subprocess
from pathlib import Path

def get_git_status():
    """Get git status"""
    try:
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=os.environ.get("PROJECT_ROOT", "."),
            capture_output=True,
            text=True
        )
        return {
            "success": result.returncode == 0,
            "output": result.stdout.strip(),
            "error": result.stderr.strip() if result.stderr else None
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

def get_git_log(limit=10):
    """Get recent git commits"""
    try:
        result = subprocess.run(
            ["git", "log", "--oneline", f"-{limit}"],
            cwd=os.environ.get("PROJECT_ROOT", "."),
            capture_output=True,
            text=True
        )
        return {
            "success": result.returncode == 0,
            "commits": result.stdout.strip().split('\n') if result.stdout.strip() else [],
            "error": result.stderr.strip() if result.stderr else None
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

def git_pull():
    """Pull latest changes"""
    try:
        result = subprocess.run(
            ["git", "pull"],
            cwd=os.environ.get("PROJECT_ROOT", "."),
            capture_output=True,
            text=True
        )
        return {
            "success": result.returncode == 0,
            "output": result.stdout.strip(),
            "error": result.stderr.strip() if result.stderr else None
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

def git_add(filespec="."):
    """Add files to staging"""
    try:
        result = subprocess.run(
            ["git", "add", filespec],
            cwd=os.environ.get("PROJECT_ROOT", "."),
            capture_output=True,
            text=True
        )
        return {
            "success": result.returncode == 0,
            "output": result.stdout.strip(),
            "error": result.stderr.strip() if result.stderr else None
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

def git_commit(message):
    """Commit changes"""
    try:
        result = subprocess.run(
            ["git", "commit", "-m", message],
            cwd=os.environ.get("PROJECT_ROOT", "."),
            capture_output=True,
            text=True
        )
        return {
            "success": result.returncode == 0,
            "output": result.stdout.strip(),
            "error": result.stderr.strip() if result.stderr else None
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

def main():
    """Main MCP server loop"""
    print("Git MCP Server started", file=sys.stderr)
    
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
                                "name": "get_git_status",
                                "description": "Get git working tree status"
                            },
                            {
                                "name": "get_git_log",
                                "description": "Get recent git commits"
                            },
                            {
                                "name": "git_pull",
                                "description": "Pull latest changes from remote"
                            },
                            {
                                "name": "git_add",
                                "description": "Add files to staging area"
                            },
                            {
                                "name": "git_commit",
                                "description": "Commit staged changes"
                            }
                        ]
                    }
                }
            elif method == "tools/call":
                tool_name = request["params"]["name"]
                arguments = request["params"].get("arguments", {})
                
                if tool_name == "get_git_status":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(get_git_status())}]}}
                elif tool_name == "get_git_log":
                    limit = arguments.get("limit", 10)
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(get_git_log(limit))}]}}
                elif tool_name == "git_pull":
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(git_pull())}]}}
                elif tool_name == "git_add":
                    filespec = arguments.get("filespec", ".")
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(git_add(filespec))}]}}
                elif tool_name == "git_commit":
                    message = arguments.get("message", "Auto commit")
                    response = {"result": {"content": [{"type": "text", "text": json.dumps(git_commit(message))}]}}
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