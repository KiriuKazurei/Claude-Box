#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
  创建 Claude Box Internal 虚拟交换机并配置宿主机网关 IP（HV-2）。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $SwitchName = 'ClaudeBox-Internal',
    [string] $ListenIp = '192.168.87.1',
    [int] $PrefixLength = 24
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$isWin = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows
)
if (-not $isWin) {
    throw 'This script must run on Windows 11 with Hyper-V.'
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Control\ClaudeBox.Control.psd1') -Force
New-ClaudeBoxInternalSwitch -Name $SwitchName -ListenIp $ListenIp -PrefixLength $PrefixLength
Write-Host 'Next: scripts\win11\New-ClaudeBoxVM.ps1 (HV-3 / M1).'
