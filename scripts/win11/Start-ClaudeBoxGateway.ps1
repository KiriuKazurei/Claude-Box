#Requires -Version 5.1
<#
.SYNOPSIS
  从配置启动 Claude Box 宿主侧网关（M2 / HV-5）。

.DESCRIPTION
  应用 upstream + 日志；生成 gost 中继计划；若找到 gost.exe 则启动。
  真 Hyper-V 绑定与 Clash 联调：待验证。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $ConfigPath = '',
    [switch] $SkipProcessStart,
    [switch] $WriteStartupLog
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $repoRoot 'config\config.yaml'
    if (-not (Test-Path -LiteralPath $ConfigPath)) {
        $ConfigPath = Join-Path $repoRoot 'config\config.example.yaml'
    }
}

Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Control\ClaudeBox.Control.psd1') -Force
Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1') -Force

$cfg = Get-ClaudeBoxConfig -Path $ConfigPath
$runtime = ConvertTo-ClaudeBoxGatewayRuntimeConfig -Parsed $cfg.Parsed
Write-Host "Config: $($cfg.Path)"
$result = Start-ClaudeBoxGateway -Runtime $runtime -SkipProcessStart:$SkipProcessStart -WriteStartupLog:$WriteStartupLog
Write-Host $result.Message
Write-Host 'Next: guest Set-ClaudeBoxGuestNetwork.ps1; then Test-ClaudeBoxDirectEgressBlocked.ps1'
$result
