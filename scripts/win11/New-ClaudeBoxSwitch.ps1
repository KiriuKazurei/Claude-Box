#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
  创建 Claude Box Internal 虚拟交换机并配置宿主机网关 IP（HV-2）。

.PARAMETER SwitchName
  默认 ClaudeBox-Internal。

.PARAMETER ListenIp
  默认 192.168.87.1。

.PARAMETER PrefixLength
  默认 24。
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

Import-Module Hyper-V -ErrorAction Stop

$existing = Get-VMSwitch -Name $SwitchName -ErrorAction SilentlyContinue
if (-not $existing) {
    if ($PSCmdlet.ShouldProcess($SwitchName, 'New-VMSwitch Internal')) {
        New-VMSwitch -SwitchName $SwitchName -SwitchType Internal | Out-Null
        Write-Host "Created switch: $SwitchName"
    }
} else {
    Write-Host "Switch already exists: $SwitchName"
}

# TODO(M1): 定位 vEthernet ($SwitchName) 适配器并 New-NetIPAddress
Write-Warning "TODO(M1): Assign $ListenIp/$PrefixLength to vEthernet ($SwitchName). Verify with Get-NetIPAddress."
Write-Host 'Next: create empty VM (see docs/architecture-v0.md HV-3 / Control New-ClaudeBoxVM).'
