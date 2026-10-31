#Requires -Version 5.1
<#
.SYNOPSIS
  启动 Claude Box 最小控件 UI（M4）。

.DESCRIPTION
  包装 src/ClaudeBox.Control/ui/Start-ClaudeBoxControlUi.ps1。
  Win11：需 Windows PowerShell 5.1 或安装 Desktop 的 pwsh。
  管理员权限：创建 VM / SMB / 网关绑定建议「以管理员运行」。

.EXAMPLE
  .\scripts\win11\Start-ClaudeBoxControlUi.ps1
  .\scripts\win11\Start-ClaudeBoxControlUi.ps1 -ConfigPath .\config\config.yaml
  .\scripts\win11\Start-ClaudeBoxControlUi.ps1 -SelfCheck
#>
[CmdletBinding()]
param(
    [string] $ConfigPath = '',
    [switch] $SelfCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$ui = Join-Path $repoRoot 'src\ClaudeBox.Control\ui\Start-ClaudeBoxControlUi.ps1'
if (-not (Test-Path -LiteralPath $ui)) {
    throw "UI script missing: $ui"
}

& $ui -ConfigPath $ConfigPath -SelfCheck:$SelfCheck
