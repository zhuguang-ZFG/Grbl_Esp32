# Forward agent vibe gate to fz (no sim code in this tree).
# Usage: .\tools\agent_gate.ps1 [-Profile standard]
param(
    [ValidateSet("auto","quick","standard","deep","firmware")]
    [string]$Profile = "auto"
)
$ErrorActionPreference = "Stop"
$Fz = $env:FZ_ROOT
if (-not $Fz) { $Fz = "D:\Users\zhugu\fz" }
if (-not (Test-Path (Join-Path $Fz "scripts\agent_gate.py"))) {
    Write-Error "fz agent_gate not found at $Fz — set FZ_ROOT or clone https://github.com/zhuguang-ZFG/fz"
}
$env:GRBL_ROOT = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$env:FZ_ROOT = $Fz
Write-Host "GRBL_ROOT=$env:GRBL_ROOT FZ_ROOT=$env:FZ_ROOT profile=$Profile"
& python (Join-Path $Fz "scripts\agent_gate.py") --profile $Profile --grbl-root $env:GRBL_ROOT
exit $LASTEXITCODE
