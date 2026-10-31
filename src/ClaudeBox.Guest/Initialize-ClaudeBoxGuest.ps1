#Requires -Version 5.1
<#
.SYNOPSIS
  在 Hyper-V Windows 访客内执行的初始化入口（占位）。

.DESCRIPTION
  由宿主机 Control 在适当时机注入并运行（ISO / 启动任务 / 远程会话 —— 机制待验证）。
  本脚本必须在访客 Windows 会话中执行，不要在宿主机当配置工具跑（除非 -WhatIf 文档模式）。

.PARAMETER IdentityHostname
  访客主机名。

.PARAMETER Timezone
  Windows 时区 ID 或友好名（如 UTC）；实际 tzutil 映射 TODO。

.PARAMETER Locale
  如 en-US。

.PARAMETER GatewayHttp
  系统代理 HTTP 地址（可选）。

.PARAMETER GatewaySocks5
  SOCKS5（若系统代理模型支持；否则由本地强制工具使用）——待验证。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $IdentityHostname = 'claude-box',
    [string] $Timezone = 'UTC',
    [string] $Locale = 'en-US',
    [string] $GatewayHttp = '',
    [string] $GatewaySocks5 = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Set-ClaudeBoxGuestIdentity {
    param(
        [string] $Hostname,
        [string] $TimezoneId,
        [string] $Culture
    )
    # TODO(M3): Rename-Computer; tzutil /s; Set-WinSystemLocale / culture
    Write-Host "[Guest] TODO identity hostname=$Hostname tz=$TimezoneId locale=$Culture"
}

function Set-ClaudeBoxGuestProxy {
    param(
        [string] $Http,
        [string] $Socks5
    )
    # TODO(M2/M3): 写系统代理；必要时装强制工具；禁 QUIC —— 待验证
    Write-Host "[Guest] TODO system proxy http=$Http socks5=$Socks5"
}

function Install-ClaudeBoxWindowsClaude {
    # TODO(M3): 引导用户或静默安装官方 Windows Claude；不把安装包放进 git
    Write-Host '[Guest] TODO install Windows Claude (exe/MSIX) from official source'
}

function Test-ClaudeBoxGuestNetwork {
    # TODO(M2): curl 经代理成功；直连失败
    Write-Host '[Guest] TODO network acceptance (proxy ok, direct fail)'
}

Write-Host '=== Claude Box Guest Init (M0 placeholder) ==='
Set-ClaudeBoxGuestIdentity -Hostname $IdentityHostname -TimezoneId $Timezone -Culture $Locale
Set-ClaudeBoxGuestProxy -Http $GatewayHttp -Socks5 $GatewaySocks5
Install-ClaudeBoxWindowsClaude
Test-ClaudeBoxGuestNetwork
Write-Host '=== Done (no real changes in M0) ==='
