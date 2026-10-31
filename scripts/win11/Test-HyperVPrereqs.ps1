#Requires -Version 5.1
<#
.SYNOPSIS
  Win11 上检查 Claude Box Hyper-V 前置条件（HV-1）。

.DESCRIPTION
  可在管理员 PowerShell 中运行。非 Windows 会打印跳过说明并以非零退出。
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$isWin = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows
)

if (-not $isWin) {
    Write-Host 'SKIP: Not Windows. Run this script on Win11 with Hyper-V.'
    exit 2
}

Write-Host '=== Claude Box · Hyper-V prereq check ==='

$module = Get-Module -ListAvailable -Name Hyper-V | Select-Object -First 1
if ($module) {
    Write-Host ("Hyper-V module: {0}" -f $module.Version)
} else {
    Write-Host 'Hyper-V module: NOT FOUND'
}

# Feature probe (may need admin)
try {
    $feat = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -ErrorAction Stop
    Write-Host ("Feature Microsoft-Hyper-V-All: {0}" -f $feat.State)
} catch {
    Write-Host ("Feature probe failed (need admin?): {0}" -f $_.Exception.Message)
}

Write-Host 'Next: scripts/win11/New-ClaudeBoxSwitch.ps1 (HV-2)'
