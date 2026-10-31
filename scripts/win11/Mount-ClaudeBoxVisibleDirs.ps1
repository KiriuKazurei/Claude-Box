#Requires -Version 5.1
<#
.SYNOPSIS
  Win11 入口：生成挂载计划；可选创建 SMB 共享并暂存 Guest bundle（M3）。
#>
[CmdletBinding()]
param(
    [string] $ConfigPath = '',
    [string] $PlanOutPath = '',
    [switch] $ApplyShares,
    [switch] $PublishBundle
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $ConfigPath) {
    $local = Join-Path $repo 'config\config.yaml'
    $ex = Join-Path $repo 'config\config.example.yaml'
    $ConfigPath = if (Test-Path $local) { $local } else { $ex }
}

Import-Module (Join-Path $repo 'src\ClaudeBox.Control\ClaudeBox.Control.psd1') -Force
Mount-ClaudeBoxVisibleDirs -ConfigPath $ConfigPath -PlanOutPath $PlanOutPath -ApplyShares:$ApplyShares -PublishBundle:$PublishBundle
