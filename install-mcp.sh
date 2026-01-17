#!/bin/bash
# MCP (Model Context Protocol) 安装脚本
# 适用于 Linux/macOS
# 作者: 自动生成
# 日期: 2026-01-15

echo "================================================"
echo "  MCP (Model Context Protocol) 安装脚本"
echo "================================================"
echo ""

# 检查 Node.js 是否安装
echo "[1/5] 检查 Node.js 安装..."
if command -v node &> /dev/null; then
    NODE_VERSION=$(node --version)
    echo "  ✓ Node.js 已安装: $NODE_VERSION"
else
    echo "  ✗ 未找到 Node.js，请先安装 Node.js"
    echo "  下载地址: https://nodejs.org/"
    exit 1
fi

# 查找 Cursor 配置文件位置
echo ""
echo "[2/5] 查找 Cursor 配置文件..."

# macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    CURSOR_USER_DIR="$HOME/Library/Application Support/Cursor/User"
# Linux
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    CURSOR_USER_DIR="$HOME/.config/Cursor/User"
else
    echo "  ✗ 不支持的操作系统: $OSTYPE"
    exit 1
fi

CURSOR_SETTINGS_PATH="$CURSOR_USER_DIR/settings.json"

if [ -d "$CURSOR_USER_DIR" ]; then
    echo "  ✓ 找到 Cursor 用户目录: $CURSOR_USER_DIR"
else
    echo "  ✗ 未找到 Cursor 用户目录"
    echo "  路径: $CURSOR_USER_DIR"
    echo "  请确保已安装并运行过 Cursor"
    exit 1
fi

# 备份现有配置
echo ""
echo "[3/5] 备份现有配置..."
if [ -f "$CURSOR_SETTINGS_PATH" ]; then
    BACKUP_PATH="${CURSOR_SETTINGS_PATH}.backup.$(date +%Y%m%d-%H%M%S)"
    cp "$CURSOR_SETTINGS_PATH" "$BACKUP_PATH"
    echo "  ✓ 配置已备份到: $BACKUP_PATH"
else
    echo "  ℹ 配置文件不存在，将创建新文件"
fi

# 创建配置目录（如果不存在）
mkdir -p "$CURSOR_USER_DIR"

# 读取或创建配置
echo ""
echo "[4/5] 配置 MCP 服务器..."

# 如果配置文件存在，读取它；否则创建新的 JSON 对象
if [ -f "$CURSOR_SETTINGS_PATH" ]; then
    # 使用 jq 合并配置（如果安装了 jq）
    if command -v jq &> /dev/null; then
        jq '.mcpServers = {
            "filesystem": {
                "command": "node",
                "args": ["-e", "console.log(\"filesystem\")"]
            },
            "github": {
                "command": "npx",
                "args": ["-y", "@modelcontextprotocol/server-github"],
                "env": {
                    "GITHUB_PERSONAL_ACCESS_TOKEN": "YOUR_GITHUB_TOKEN_HERE"
                }
            }
        }' "$CURSOR_SETTINGS_PATH" > "${CURSOR_SETTINGS_PATH}.tmp" && mv "${CURSOR_SETTINGS_PATH}.tmp" "$CURSOR_SETTINGS_PATH"
        echo "  ✓ MCP 配置已添加到 Cursor 设置"
    else
        echo "  ⚠ 未安装 jq，请手动编辑配置文件"
        echo "  配置文件位置: $CURSOR_SETTINGS_PATH"
    fi
else
    # 创建新的配置文件
    cat > "$CURSOR_SETTINGS_PATH" << 'EOF'
{
  "mcpServers": {
    "filesystem": {
      "command": "node",
      "args": ["-e", "console.log(\"filesystem\")"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_PERSONAL_ACCESS_TOKEN": "YOUR_GITHUB_TOKEN_HERE"
      }
    }
  }
}
EOF
    echo "  ✓ MCP 配置已创建"
fi

# 提示用户配置 GitHub Token
echo ""
echo "[5/5] GitHub Token 配置..."
echo "  ⚠ 重要：请手动配置 GitHub Personal Access Token"
echo ""
echo "  步骤："
echo "  1. 访问: https://github.com/settings/tokens"
echo "  2. 点击 'Generate new token (classic)'"
echo "  3. 勾选 'repo' 权限"
echo "  4. 复制生成的 Token"
echo "  5. 编辑配置文件，替换 'YOUR_GITHUB_TOKEN_HERE'"
echo ""
echo "  配置文件位置: $CURSOR_SETTINGS_PATH"
echo ""

# 完成
echo "================================================"
echo "  ✓ 安装完成！"
echo "================================================"
echo ""
echo "  下一步："
echo "  1. 重启 Cursor 编辑器"
echo "  2. 配置 GitHub Token（如需要使用 GitHub MCP）"
echo "  3. 测试 MCP 功能"
echo ""
