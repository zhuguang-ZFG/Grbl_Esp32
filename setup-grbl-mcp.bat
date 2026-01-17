@echo off
REM Grbl_ESP32 专用 MCP 安装脚本
REM 配置 ESP32 开发专用的 Model Context Protocol 服务器
REM 日期: 2026-01-16

echo =================================================
echo   Grbl_ESP32 MCP 开发工具安装
echo =================================================
echo.

REM 检查 Python 是否安装
echo [1/4] 检查 Python 环境...
python --version >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo   ✗ 未找到 Python，请先安装 Python 3.8+
    echo   下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
) else (
    for /f "tokens=*" %%i in ('python --version') do set "PYTHON_VERSION=%%i"
    echo   ✓ Python 已安装: %PYTHON_VERSION%
)

REM 检查 Cursor 配置位置
echo.
echo [2/4] 查找 Cursor 配置...
set "CURSOR_SETTINGS_PATH=%APPDATA%\Cursor\User\settings.json"

if exist "%APPDATA%\Cursor\User" (
    echo   ✓ 找到 Cursor 目录: %APPDATA%\Cursor\User
) else (
    echo   ✗ 未找到 Cursor 用户目录
    echo   请确保已安装 Cursor
    pause
    exit /b 1
)

REM 备份现有配置
echo.
echo [3/4] 备份现有配置...
if exist "%CURSOR_SETTINGS_PATH%" (
    for /f "tokens=2 delims==" %%i in ('wmic OS Get localdatetime /value') do set "DATETIME=%%i"
    set "DATETIME=%DATETIME:~0,8%-%DATETIME:~8,6%"
    set "BACKUP_PATH=%CURSOR_SETTINGS_PATH%.backup.%DATETIME%"
    copy "%CURSOR_SETTINGS_PATH%" "%BACKUP_PATH%" >nul
    echo   ✓ 配置已备份到: %BACKUP_PATH%
) else (
    echo   ℹ 配置文件不存在，将创建新配置
)

REM 安装 MCP 包
echo.
echo [4/4] 安装必需的 MCP 包...

REM 安装基础 MCP 服务器
echo   安装基础 MCP 服务器...
call npm install -g @modelcontextprotocol/server-filesystem >nul 2>&1
call npm install -g @modelcontextprotocol/server-github >nul 2>&1
call npm install -g @modelcontextprotocol/server-memory >nul 2>&1
call npm install -g @modelcontextprotocol/server-puppeteer >nul 2>&1

REM 安装 Python MCP 依赖
echo   安装 Python MCP 依赖...
pip install mcp >nul 2>&1
pip install pyserial >nul 2>&1

echo   ✓ MCP 包安装完成

REM 配置 Grbl_Esp32 专用 MCP 服务器
echo.
echo [5/4] 配置 Grbl_Esp32 MCP 服务器...

REM 使用 PowerShell 配置专用 MCP 服务器
powershell -Command "& {
    $settingsPath = '%CURSOR_SETTINGS_PATH%'
    
    $grblMcpConfig = @{
        mcpServers = @{
            filesystem = @{
                command = 'npx'
                args = @('-y', '@modelcontextprotocol/server-filesystem')
            }
            github = @{
                command = 'npx'
                args = @('-y', '@modelcontextprotocol/server-github')
                env = @{
                    GITHUB_PERSONAL_ACCESS_TOKEN = 'YOUR_GITHUB_TOKEN_HERE'
                }
            }
            memory = @{
                command = 'npx'
                args = @('-y', '@modelcontextprotocol/server-memory')
            }
            'esp32-tools' = @{
                command = 'python'
                args = @('f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32/esp32_mcp_server.py')
                cwd = 'f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32'
            }
            'grbl-analyzer' = @{
                command = 'python'
                args = @('-c', 'import grbl_analyzer; print(\"Grbl analyzer ready\")')
                cwd = 'f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32'
            }
            'hr4988-calculator' = @{
                command = 'python'
                args = @('-c', 'import hr4988_calculator; print(\"HR4988 calculator ready\")')
                cwd = 'f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32'
            }
        }
    }
    
    # 保存配置
    $grblMcpConfig | ConvertTo-Json -Depth 10 | Out-File -FilePath $settingsPath -Encoding UTF8
    Write-Host '   ✓ Grbl_Esp32 MCP 配置已保存'
}"

echo.
echo =================================================
echo   ✓ Grbl_Esp32 MCP 安装完成！
echo =================================================
echo.
echo   已安装的专用 MCP 服务器：
echo   - esp32-tools: ESP32 串口调试、固件构建、硬件诊断
echo   - grbl-analyzer: Grbl G-code 分析和优化
echo   - hr4988-calculator: HR4988 电流计算和配置
echo   - filesystem: 文件系统访问
echo   - github: GitHub 仓库集成
echo   - memory: 记忆管理
echo.
echo   使用方法：
echo   1. 重启 Cursor
echo   2. 在 Cursor 中输入 @esp32-tools 调用 ESP32 工具
echo   3. 在 Cursor 中输入 @grbl-analyzer 分析 G-code
echo   4. 在 Cursor 中输入 @hr4988-calculator 计算电流
echo.

REM 询问是否立即打开配置文件设置 GitHub Token
set /p "CONFIG_TOKEN=是否立即配置 GitHub Token？(y/n): "
if /i "%CONFIG_TOKEN%"=="y" (
    echo.
    echo 正在打开 GitHub Token 页面...
    start https://github.com/settings/tokens
    timeout /t 3 >nul
    echo.
    echo 正在打开配置文件...
    start notepad "%CURSOR_SETTINGS_PATH%"
)

echo.
pause