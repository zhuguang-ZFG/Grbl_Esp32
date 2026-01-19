# Grbl_Esp32 MCP 工具使用说明

## 🎯 概述

已成功安装不依赖 Node.js 的 MCP（Model Context Protocol）工具，为 Grbl_Esp32 项目提供强大的开发辅助功能。

## 🛠 可用的 MCP 工具

### 1. @esp32-tools - ESP32 开发工具
**功能：**
- 列出可用串口
- 构建 Grbl_Esp32 固件
- 上传固件到 ESP32

**使用示例：**
```
用户: @esp32-tools 列出串口
用户: @esp32-tools 构建固件
用户: @esp32-tools 上传固件
```

### 2. @filesystem - 文件系统操作
**功能：**
- 列出项目文件
- 读取文件内容
- 文件浏览和搜索

**使用示例：**
```
用户: @filesystem 列出 Grbl_Esp32/src 目录
用户: @filesystem 读取 platformio.ini
用户: @filesystem 查找所有 .h 文件
```

### 3. @git-tools - Git 操作工具
**功能：**
- 查看Git状态
- 获取提交历史
- 拉取最新代码
- 提交更改

**使用示例：**
```
用户: @git-tools 查看状态
用户: @git-tools 获取最近5次提交
用户: @git-tools 拉取最新代码
用户: @git-tools 添加并提交更改
```

## 🚀 快速开始

1. **重启 Cursor IDE**
   - 完全关闭 Cursor
   - 重新打开 Cursor

2. **打开 Grbl_Esp32 项目**
   - 打开项目文件夹：`f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32`

3. **使用 MCP 工具**
   - 在对话中使用 `@` 符号调用工具
   - 例如：`@esp32-tools 构建固件`

## 📋 典型工作流程

### 工作流程 1：固件开发
```
1. @filesystem 查看源代码文件
2. @esp32-tools 构建固件
3. @esp32-tools 上传固件
4. @git-tools 提交更改
```

### 工作流程 2：代码更新
```
1. @git-tools 拉取最新代码
2. @filesystem 查看变更内容
3. @esp32-tools 重新构建
4. @esp32-tools 上传新固件
```

### 工作流程 3：调试问题
```
1. @esp32-tools 列出串口
2. @filesystem 查看配置文件
3. @git-tools 查看相关提交历史
4. @esp32-tools 重新构建和上传
```

## ⚙️ 配置文件位置

- **Cursor 设置：** `C:\Users\Administrator\AppData\Roaming\Cursor\User\settings.json`
- **MCP 服务器：** `f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32/`
  - `esp32_mcp_server.py` - ESP32 工具服务器
  - `filesystem_mcp_server.py` - 文件系统服务器
  - `git_mcp_server.py` - Git 服务器

## 🔧 故障排除

### 工具无法响应
1. 确保已重启 Cursor
2. 检查 Python 路径是否正确
3. 验证项目路径是否可访问

### 串口相关错误
1. 确保已安装 pyserial：`pip install pyserial`
2. 检查 USB 驱动是否正确安装
3. 确认 ESP32 已正确连接

### Git 操作失败
1. 确保在 Git 仓库目录中
2. 检查网络连接（pull 操作需要）
3. 验证 Git 配置是否正确

## 📝 高级用法

### 自定义工具调用
可以传递参数给工具：
```
用户: @git-tools 获取最近20次提交
用户: @filesystem 列出 .pio 目录，匹配 *.bin
```

### 错误处理
所有工具都会返回详细的错误信息：
```
用户: @esp32-tools 构建固件
返回：{"success": false, "error": "PlatformIO not found"}
```

## 🎉 享受增强的开发体验！

现在你可以使用这些强大的 MCP 工具来：
- 🔍 快速浏览项目文件
- 🚀 自动化固件构建和上传
- 📊 管理Git版本控制
- 🐛 快速调试和诊断问题

祝开发愉快！🎯