# Grbl_Esp32 MCP Installation Script for PowerShell
# This script installs MCP servers for Grbl_Esp32 development

param(
    [switch]$Force,
    [switch]$Verbose
)

Write-Host "=== Grbl_Esp32 MCP Installation Script ===" -ForegroundColor Cyan
Write-Host "Installing MCP servers for Grbl_Esp32 development..." -ForegroundColor Green

# Check if Node.js is installed
try {
    $nodeVersion = node --version 2>$null
    if ($nodeVersion) {
        Write-Host "Node.js found: $nodeVersion" -ForegroundColor Green
    } else {
        Write-Host "Node.js not found. Please install Node.js first." -ForegroundColor Red
        Write-Host "Download from: https://nodejs.org/" -ForegroundColor Yellow
        exit 1
    }
} catch {
    Write-Host "Error checking Node.js installation." -ForegroundColor Red
    exit 1
}

# Check npm
try {
    $npmVersion = npm --version 2>$null
    Write-Host "npm found: $npmVersion" -ForegroundColor Green
} catch {
    Write-Host "npm not found. Please install npm first." -ForegroundColor Red
    exit 1
}

# Create MCP directory
$mcpDir = "$env:USERPROFILE\.mcp"
if (!(Test-Path $mcpDir)) {
    New-Item -ItemType Directory -Path $mcpDir -Force | Out-Null
    Write-Host "Created MCP directory: $mcpDir" -ForegroundColor Green
}

# Install required MCP servers
Write-Host "Installing MCP servers..." -ForegroundColor Yellow

# Filesystem MCP
Write-Host "Installing @modelcontextprotocol/server-filesystem..." -ForegroundColor White
npm install -g @modelcontextprotocol/server-filesystem

# GitHub MCP  
Write-Host "Installing @modelcontextprotocol/server-github..." -ForegroundColor White
npm install -g @modelcontextprotocol/server-github

# Memory MCP
Write-Host "Installing @modelcontextprotocol/server-memory..." -ForegroundColor White
npm install -g @modelcontextprotocol/server-memory

# Braille MCP
Write-Host "Installing @modelcontextprotocol/server-braille-search..." -ForegroundColor White
npm install -g @modelcontextprotocol/server-braille-search

# Fetch MCP
Write-Host "Installing @modelcontextprotocol/server-fetch..." -ForegroundColor White
npm install -g @modelcontextprotocol/server-fetch

# Git MCP
Write-Host "Installing @modelcontextprotocol/server-git..." -ForegroundColor White
npm install -g @modelcontextprotocol/server-git

# Create Cursor settings directory
$cursorDir = "$env:APPDATA\Cursor"
if (!(Test-Path $cursorDir)) {
    New-Item -ItemType Directory -Path $cursorDir -Force | Out-Null
    Write-Host "Created Cursor directory: $cursorDir" -ForegroundColor Green
}

# Backup existing settings
$cursorSettingsPath = "$cursorDir\User\settings.json"
if (Test-Path $cursorSettingsPath) {
    $backupPath = "$cursorSettingsPath.backup.$(Get-Date -Format 'yyyyMMdd_HHmmss')"
    Copy-Item $cursorSettingsPath $backupPath
    Write-Host "Backed up existing settings to: $backupPath" -ForegroundColor Yellow
}

# Read existing settings or create new
$settings = @{}
if (Test-Path $cursorSettingsPath) {
    try {
        $existingContent = Get-Content $cursorSettingsPath -Raw -Encoding UTF8
        if ($existingContent) {
            $settings = $existingContent | ConvertFrom-Json -AsHashtable
        }
    } catch {
        Write-Host "Warning: Could not parse existing settings, creating new ones." -ForegroundColor Yellow
        $settings = @{}
    }
} else {
    # Create User directory if it doesn't exist
    $userDir = "$cursorDir\User"
    if (!(Test-Path $userDir)) {
        New-Item -ItemType Directory -Path $userDir -Force | Out-Null
    }
}

# Add MCP configuration
if (-not $settings.ContainsKey("mcp")) {
    $settings["mcp"] = @{}
}

if (-not $settings.mcp.ContainsKey("servers")) {
    $settings.mcp["servers"] = @{}
}

# Add MCP servers configuration
$projectPath = "f:/BaiduNetdiskDownload/code/GRBL/Grbl_Esp32"

# Filesystem server for project access
$settings.mcp.servers["filesystem"] = @{
    "command" = "node"
    "args" = @("$env:USERPROFILE\.mcp\node_modules\@modelcontextprotocol\server-filesystem\dist\index.js", $projectPath)
    "disabled" = $false
    "autoApprove" = @(
        "read_file",
        "write_to_file", 
        "list_files",
        "create_directory",
        "move_file",
        "delete_file"
    )
}

# GitHub server for repository access
$settings.mcp.servers["github"] = @{
    "command" = "node"
    "args" = @("$env:USERPROFILE\.mcp\node_modules\@modelcontextprotocol\server-github\dist\index.js")
    "disabled" = $false
}

# Memory server
$settings.mcp.servers["memory"] = @{
    "command" = "node"
    "args" = @("$env:USERPROFILE\.mcp\node_modules\@modelcontextprotocol\server-memory\dist\index.js")
    "disabled" = $false
}

# Fetch server
$settings.mcp.servers["fetch"] = @{
    "command" = "node"
    "args" = @("$env:USERPROFILE\.mcp\node_modules\@modelcontextprotocol\server-fetch\dist\index.js")
    "disabled" = $false
}

# Git server
$settings.mcp.servers["git"] = @{
    "command" = "node"
    "args" = @("$env:USERPROFILE\.mcp\node_modules\@modelcontextprotocol\server-git\dist\index.js", $projectPath)
    "disabled" = $false
}

# ESP32 Python server
$settings.mcp.servers["esp32-tools"] = @{
    "command" = "python"
    "args" = @("$projectPath/esp32_mcp_server.py")
    "disabled" = $false
    "env" = @{
        "PYTHONPATH" = $projectPath
    }
}

# Save new settings
$settingsJson = $settings | ConvertTo-Json -Depth 10
$settingsJson | Out-File -FilePath $cursorSettingsPath -Encoding UTF8

Write-Host "MCP configuration saved to Cursor settings." -ForegroundColor Green

# Test installation
Write-Host "Testing MCP installation..." -ForegroundColor Yellow

# Test filesystem server
try {
    $testResult = & node "$env:USERPROFILE\.mcp\node_modules\@modelcontextprotocol\server-filesystem\dist\index.js" --help 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Filesystem MCP server working" -ForegroundColor Green
    } else {
        Write-Host "✗ Filesystem MCP server test failed" -ForegroundColor Red
    }
} catch {
    Write-Host "✗ Could not test filesystem MCP server" -ForegroundColor Red
}

# Test GitHub server
try {
    $testResult = & node "$env:USERPROFILE\.mcp\node_modules\@modelcontextprotocol\server-github\dist\index.js" --help 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ GitHub MCP server working" -ForegroundColor Green
    } else {
        Write-Host "✗ GitHub MCP server test failed" -ForegroundColor Red
    }
} catch {
    Write-Host "✗ Could not test GitHub MCP server" -ForegroundColor Red
}

# Test ESP32 Python server
try {
    $testResult = & python "$projectPath/esp32_mcp_server.py" --help 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ ESP32 Tools MCP server working" -ForegroundColor Green
    } else {
        Write-Host "✗ ESP32 Tools MCP server test failed" -ForegroundColor Red
    }
} catch {
    Write-Host "✗ Could not test ESP32 Tools MCP server" -ForegroundColor Red
}

Write-Host "`n=== Installation Complete ===" -ForegroundColor Cyan
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Restart Cursor IDE" -ForegroundColor White
Write-Host "2. Open Grbl_Esp32 project" -ForegroundColor White
Write-Host "3. Use @ to access MCP tools:" -ForegroundColor White
Write-Host "   - @filesystem - File operations" -ForegroundColor Gray
Write-Host "   - @github - GitHub integration" -ForegroundColor Gray
Write-Host "   - @memory - Memory management" -ForegroundColor Gray
Write-Host "   - @fetch - Web fetching" -ForegroundColor Gray
Write-Host "   - @git - Git operations" -ForegroundColor Gray
Write-Host "   - @esp32-tools - ESP32 development tools" -ForegroundColor Gray

Write-Host "`nEnjoy enhanced Grbl_Esp32 development!" -ForegroundColor Green