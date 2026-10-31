#Requires -Version 5.1
<#
.SYNOPSIS
  在 Hyper-V Windows 访客内执行的初始化入口（假环境步骤清单 + 占位动作）。

.DESCRIPTION
  由宿主机 Control 在适当时机注入并运行（ISO / 启动任务 / 远程会话 —— 机制待验证）。
  「假 C 环境」= 本访客自有系统盘上的独立 identity，不是映射宿主 C:。

.PARAMETER IdentityHostname
  访客主机名。

.PARAMETER Timezone
  Windows 时区 ID（如 UTC）。

.PARAMETER Locale
  如 en-US。

.PARAMETER Workgroup
  工作组名占位。

.PARAMETER GatewayHttp
  系统代理 HTTP 地址。

.PARAMETER GatewaySocks5
  SOCKS5（待验证是否进系统代理模型）。

.PARAMETER Apply
  真正执行改系统动作（默认 false：只打印清单，安全）。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $IdentityHostname = 'claude-box',
    [string] $Timezone = 'UTC',
    [string] $Locale = 'en-US',
    [string] $Workgroup = 'CLAUDEBOX',
    [string] $GatewayHttp = '',
    [string] $GatewaySocks5 = '',
    [switch] $Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Show-ClaudeBoxFakeEnvChecklist {
    param(
        [string] $Hostname,
        [string] $TimezoneId,
        [string] $Culture,
        [string] $Wg
    )
    Write-Host '=== Fake env checklist (guest-local system disk, NOT host C:) ==='
    Write-Host "[F1] Hostname/computer name -> $Hostname"
    Write-Host "[F2] Timezone -> $TimezoneId  (tzutil /s ...)"
    Write-Host "[F3] Locale/culture -> $Culture"
    Write-Host "[F4] Workgroup -> $Wg"
    Write-Host '[F5] Confirm host system drive C: is NOT mounted as a share'
    Write-Host '[F6] Optional: minimal desktop / avoid host-identifying apps'
    Write-Host '[F7] Network: system proxy + hard force via gateway (see Set-ClaudeBoxGuestProxy)'
}

function Set-ClaudeBoxGuestIdentity {
    param(
        [string] $Hostname,
        [string] $TimezoneId,
        [string] $Culture,
        [string] $Wg,
        [bool] $DoApply
    )
    if (-not $DoApply) {
        Write-Host "[Guest] DRY-RUN identity hostname=$Hostname tz=$TimezoneId locale=$Culture wg=$Wg"
        return
    }
    # Real mutations only inside guest when -Apply is set (M3 hardening).
    Write-Warning 'Apply mode: attempting identity changes (may require reboot / admin).'
    try {
        if ($PSCmdlet.ShouldProcess($Hostname, 'Rename-Computer')) {
            Rename-Computer -NewName $Hostname -Force -ErrorAction SilentlyContinue
        }
    } catch {
        Write-Warning "Rename-Computer: $($_.Exception.Message)"
    }
    try {
        if ($PSCmdlet.ShouldProcess($TimezoneId, 'tzutil')) {
            & tzutil.exe /s $TimezoneId
        }
    } catch {
        Write-Warning "tzutil: $($_.Exception.Message)"
    }
    Write-Host "[Guest] TODO(M3): Set-WinSystemLocale $Culture; workgroup $Wg"
}

function Set-ClaudeBoxGuestProxy {
    param(
        [string] $Http,
        [string] $Socks5,
        [bool] $DoApply
    )
    if (-not $DoApply) {
        Write-Host "[Guest] DRY-RUN system proxy http=$Http socks5=$Socks5"
        return
    }
    Write-Host "[Guest] TODO(M2/M3): Write system proxy; force tool; disable QUIC — http=$Http socks5=$Socks5"
}

function Install-ClaudeBoxWindowsClaude {
    param([bool] $DoApply)
    if (-not $DoApply) {
        Write-Host '[Guest] DRY-RUN install Windows Claude (exe/MSIX) from official source'
        return
    }
    Write-Host '[Guest] TODO(M3): Guide/silent install official Windows Claude; do not vendor installer in git'
}

function Test-ClaudeBoxGuestNetwork {
    Write-Host '[Guest] TODO(M2): curl via proxy success; direct fail; optional compare with gateway log'
}

Write-Host '=== Claude Box Guest Init ==='
Show-ClaudeBoxFakeEnvChecklist -Hostname $IdentityHostname -TimezoneId $Timezone -Culture $Locale -Wg $Workgroup
Set-ClaudeBoxGuestIdentity -Hostname $IdentityHostname -TimezoneId $Timezone -Culture $Locale -Wg $Workgroup -DoApply:([bool]$Apply)
Set-ClaudeBoxGuestProxy -Http $GatewayHttp -Socks5 $GatewaySocks5 -DoApply:([bool]$Apply)
Install-ClaudeBoxWindowsClaude -DoApply:([bool]$Apply)
Test-ClaudeBoxGuestNetwork
if (-not $Apply) {
    Write-Host '=== Done (checklist only; pass -Apply inside guest to mutate) ==='
} else {
    Write-Host '=== Apply attempted; reboot may be required; verify with Test-ClaudeBoxGuestHealth.ps1 ==='
}
