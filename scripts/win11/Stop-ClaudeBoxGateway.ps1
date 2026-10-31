#Requires -Version 5.1
<#
.SYNOPSIS
  停止 Claude Box 宿主侧网关进程（M2）。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1') -Force
Stop-ClaudeBoxGateway
