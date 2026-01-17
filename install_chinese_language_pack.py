#!/usr/bin/env python3
"""
安装 Cursor IDE 中文语言包
Cursor 基于 VS Code，可以使用 VS Code 的中文语言包扩展
"""

import os
import sys
import subprocess
import json
from pathlib import Path

# VS Code 中文语言包扩展 ID
CHINESE_LANGUAGE_PACK_EXTENSION_ID = "MS-CEINTL.vscode-language-pack-zh-hans"

def find_cursor_executable():
    """查找 Cursor 可执行文件路径"""
    if sys.platform == "win32":
        possible_paths = [
            r"C:\Users\{}\AppData\Local\Programs\cursor\Cursor.exe".format(os.environ.get("USERNAME", "")),
            r"C:\Program Files\Cursor\Cursor.exe",
            r"C:\Program Files (x86)\Cursor\Cursor.exe",
        ]
        # 从环境变量中查找
        if "LOCALAPPDATA" in os.environ:
            local_appdata = os.environ["LOCALAPPDATA"]
            possible_paths.insert(0, os.path.join(local_appdata, "Programs", "cursor", "Cursor.exe"))
    elif sys.platform == "darwin":  # macOS
        possible_paths = [
            "/Applications/Cursor.app/Contents/Resources/app/bin/cursor",
            "/usr/local/bin/cursor",
        ]
    else:  # Linux
        possible_paths = [
            "/usr/bin/cursor",
            "/usr/local/bin/cursor",
            os.path.expanduser("~/.local/bin/cursor"),
        ]
    
    for path in possible_paths:
        if os.path.exists(path):
            return path
    
    return None

def install_extension_via_cli(executable_path, extension_id):
    """通过命令行安装扩展"""
    try:
        if sys.platform == "win32":
            # Windows: 使用 cursor.cmd 或直接调用 cursor
            cmd = [executable_path, "--install-extension", extension_id]
        else:
            # macOS/Linux
            cmd = [executable_path, "--install-extension", extension_id]
        
        print(f"正在安装扩展: {extension_id}")
        print(f"执行命令: {' '.join(cmd)}")
        
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60
        )
        
        if result.returncode == 0:
            print("[OK] 扩展安装成功！")
            return True
        else:
            print(f"[ERROR] 扩展安装失败:")
            print(f"  标准输出: {result.stdout}")
            print(f"  错误输出: {result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print("[ERROR] 安装超时")
        return False
    except Exception as e:
        print(f"[ERROR] 安装出错: {e}")
        return False

def install_extension_manually():
    """手动安装说明"""
    print("\n" + "="*60)
    print("手动安装中文语言包步骤：")
    print("="*60)
    print()
    print("方法 1: 通过 Cursor 扩展市场安装")
    print("  1. 打开 Cursor")
    print("  2. 按 Ctrl+Shift+X (Windows/Linux) 或 Cmd+Shift+X (macOS) 打开扩展面板")
    print("  3. 搜索 'Chinese' 或 '中文'")
    print(f"  4. 找到 'Chinese (Simplified) Language Pack for Visual Studio Code'")
    print("  5. 点击 'Install' 安装")
    print("  6. 安装后重启 Cursor")
    print()
    print("方法 2: 通过命令行安装")
    cursor_path = find_cursor_executable()
    if cursor_path:
        print(f"  找到 Cursor: {cursor_path}")
        print(f"  执行命令:")
        if sys.platform == "win32":
            print(f'    "{cursor_path}" --install-extension {CHINESE_LANGUAGE_PACK_EXTENSION_ID}')
        else:
            print(f'    "{cursor_path}" --install-extension {CHINESE_LANGUAGE_PACK_EXTENSION_ID}')
    else:
        print("  未找到 Cursor 可执行文件，请手动安装")
    print()
    print("方法 3: 直接下载安装")
    print(f"  扩展 ID: {CHINESE_LANGUAGE_PACK_EXTENSION_ID}")
    print("  访问: https://marketplace.visualstudio.com/items?itemName=MS-CEINTL.vscode-language-pack-zh-hans")
    print()

def main():
    """主函数"""
    print("="*60)
    print("Cursor IDE 中文语言包安装")
    print("="*60)
    print()
    
    # 查找 Cursor 可执行文件
    cursor_path = find_cursor_executable()
    
    if cursor_path:
        print(f"[OK] 找到 Cursor: {cursor_path}")
        print()
        
        # 尝试通过命令行安装
        success = install_extension_via_cli(cursor_path, CHINESE_LANGUAGE_PACK_EXTENSION_ID)
        
        if success:
            print()
            print("="*60)
            print("安装完成！")
            print("="*60)
            print()
            print("下一步：")
            print("  1. 重启 Cursor")
            print("  2. 如果界面没有自动切换为中文，请按以下步骤操作：")
            print("     - 按 Ctrl+Shift+P (Windows/Linux) 或 Cmd+Shift+P (macOS)")
            print("     - 输入 'Configure Display Language'")
            print("     - 选择 'zh-cn' (中文简体)")
            print("     - 重启 Cursor")
        else:
            print()
            install_extension_manually()
    else:
        print("[ERROR] 未找到 Cursor 可执行文件")
        print()
        install_extension_manually()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n安装已取消")
        sys.exit(1)
    except Exception as e:
        print(f"\n错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
