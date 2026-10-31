#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
  M1 入口：按 config 创建 Gen2 Claude Box 访客（调用 Control\New-ClaudeBoxVM）。

.PARAMETER ConfigPath
  默认仓库 config\config.yaml；不存在则用 config.example.yaml。

.PARAMETER IsoPath
  Windows 安装 ISO（可选）。

.PARAMETER TemplateVhdx
  已准备好的模板系统盘（可选）。

.PARAMETER Force
  删除已存在的同名 VM 后重建。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $ConfigPath = '',
    [string] $IsoPath = '',
    [string] $TemplateVhdx = '',
    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $ConfigPath) {
    $local = Join-Path $repoRoot 'config\config.yaml'
    $example = Join-Path $repoRoot 'config\config.example.yaml'
    $ConfigPath = if (Test-Path -LiteralPath $local) { $local } else { $example }
}

Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Control\ClaudeBox.Control.psd1') -Force

$params = @{
    ConfigPath = $ConfigPath
}
if ($IsoPath) { $params['IsoPath'] = $IsoPath }
if ($TemplateVhdx) { $params['TemplateVhdx'] = $TemplateVhdx }
if ($Force) { $params['Force'] = $true }

New-ClaudeBoxVM @params
Write-Host 'Next: Start-ClaudeBoxVM -Name <vm> -Connect   OR   scripts after OS install for fake-env guest init.'
