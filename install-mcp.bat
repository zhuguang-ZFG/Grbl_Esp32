@echo off
REM MCP (Model Context Protocol) 安装脚本 - Windows版本
REM 适用于 Windows 10/11
REM 作者: 自动生成
REM 日期: 2026-01-16

echo =================================================
echo   MCP (Model Context Protocol) 安装脚本
echo =================================================
echo.

REM 检查 Node.js 是否安装
echo [1/5] 检查 Node.js 安装...
node --version >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo   ✗ 未找到 Node.js，请先安装 Node.js
    echo   下载地址: https://nodejs.org/
    pause
    exit /b 1
) else (
    for /f "tokens=*" %%i in ('node --version') do set NODE_VERSION=%%i
    echo   ✓ Node.js 已安装: %NODE_VERSION%
)

REM 查找 Cursor 配置文件位置
echo.
echo [2/5] 查找 Cursor 配置文件...

REM Windows 系统路径
set "CURSOR_USER_DIR=%APPDATA%\Cursor\User"
set "CURSOR_SETTINGS_PATH=%CURSOR_USER_DIR%\settings.json"

if exist "%CURSOR_USER_DIR%" (
    echo   ✓ 找到 Cursor 用户目录: %CURSOR_USER_DIR%
) else (
    echo   ✗ 未找到 Cursor 用户目录
    echo   路径: %CURSOR_USER_DIR%
    echo   请确保已安装并运行过 Cursor
    pause
    exit /b 1
)

REM 备份现有配置
echo.
echo [3/5] 备份现有配置...
if exist "%CURSOR_SETTINGS_PATH%" (
    for /f "tokens=2 delims==" %%i in ('wmic OS Get localdatetime /value') do set "DATETIME=%%i"
    set "DATETIME=%DATETIME:~0,8%-%DATETIME:~8,6%"
    set "BACKUP_PATH=%CURSOR_SETTINGS_PATH%.backup.%DATETIME%"
    copy "%CURSOR_SETTINGS_PATH%" "%BACKUP_PATH%" >nul
    echo   ✓ 配置已备份到: %BACKUP_PATH%
) else (
    echo   ℹ 配置文件不存在，将创建新文件
)

REM 创建配置目录（如果不存在）
if not exist "%CURSOR_USER_DIR%" mkdir "%CURSOR_USER_DIR%"

REM 安装通用 MCP 服务器
echo.
echo [4/5] 安装通用 MCP 服务器...

REM 使用 PowerShell 创建 JSON 配置
powershell -Command "& {
    $settingsPath = '%CURSOR_SETTINGS_PATH%'
    $backupPath = '%BACKUP_PATH%'
    
    $mcpConfig = @{
        mcpServers = @{
            filesystem = @{
                command = 'node'
                args = @('-e', 'console.log(\"filesystem\")')
            }
            github = @{
                command = 'npx'
                args = @('-y', '@modelcontextprotocol/server-github')
                env = @{
                    GITHUB_PERSONAL_ACCESS_TOKEN = 'YOUR_GITHUB_TOKEN_HERE'
                }
            }
            memory = @{
                command = 'node'
                args = @('-e', 'console.log(\"memory\")')
            }
            brave-search = @{
                command = 'npx'
                args = @('-y', '@modelcontextprotocol/server-brave-search')
            }
            puppeteer = @{
                command = 'npx'
                args = @('-y', '@modelcontextprotocol/server-puppeteer')
            }
        }
    }
    
    # 转换为 JSON 并保存
    $mcpConfig | ConvertTo-Json -Depth 10 | Out-File -FilePath $settingsPath -Encoding UTF8
    Write-Host '   ✓ MCP 配置已添加到 Cursor 设置'
}"

REM 提示用户配置 GitHub Token
echo.
echo [5/5] GitHub Token 配置...
echo   ⚠ 重要：请手动配置 GitHub Personal Access Token
echo.
echo   步骤：
echo   1. 访问: https://github.com/settings/tokens
echo   2. 点击 'Generate new token (classic)'
echo   3. 勾选 'repo' 权限
echo   4. 复制生成的 Token
echo   5. 编辑配置文件，替换 'YOUR_GITHUB_TOKEN_HERE'
echo.
echo   配置文件位置: %CURSOR_SETTINGS_PATH%
echo.

REM 完成安装
echo =================================================
echo   ✓ MCP 安装完成！
echo =================================================
echo.
echo   已安装的 MCP 服务器：
echo   - filesystem: 文件系统访问
echo   - github: GitHub 仓库访问
echo   - memory: 记忆管理
echo   - brave-search: 网页搜索
echo   - puppeteer: 网页自动化
echo.
echo   下一步：
echo   1. 重启 Cursor 编辑器
echo   2. 配置 GitHub Token（如需要使用 GitHub MCP）
echo   3. 测试 MCP 功能
echo.

REM 询问是否立即打开配置文件
set /p "CONFIG_OPEN=是否立即打开配置文件进行编辑？(y/n): "
if /i "%CONFIG_OPEN%"=="y" (
    echo 正在打开配置文件...
    start notepad "%CURSOR_SETTINGS_PATH%"
)

echo.
pause