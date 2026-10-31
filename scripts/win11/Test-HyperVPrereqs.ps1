#Requires -Version 5.1
<#
.SYNOPSIS
  Win11 上检查 Claude Box Hyper-V 前置条件（HV-1）。
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

Write-Host '=== Claude Box · Hyper-V prereq check (M1) ==='

$module = Get-Module -ListAvailable -Name Hyper-V | Select-Object -First 1
if ($module) {
    Write-Host ("Hyper-V module: {0}" -f $module.Version)
} else {
    Write-Host 'Hyper-V module: NOT FOUND'
}

try {
    $feat = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -ErrorAction Stop
    Write-Host ("Feature Microsoft-Hyper-V-All: {0}" -f $feat.State)
} catch {
    Write-Host ("Feature probe failed (need admin?): {0}" -f $_.Exception.Message)
}

Write-Host 'Next: scripts/win11/New-ClaudeBoxSwitch.ps1 then New-ClaudeBoxVM.ps1'
