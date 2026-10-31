#Requires -Version 5.1
<#
.SYNOPSIS
  打印「直连出站应失败」验收步骤；可选 SelfCheck（不测真 Hyper-V）。

.PARAMETER Mode
  Document（默认）或 SelfCheck。
#>
[CmdletBinding()]
param(
    [ValidateSet('Document', 'SelfCheck')]
    [string] $Mode = 'Document',
    [string] $ConfigPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1') -Force

$runtime = $null
if (-not [string]::IsNullOrWhiteSpace($ConfigPath) -and (Test-Path -LiteralPath $ConfigPath)) {
    Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Control\ClaudeBox.Control.psd1') -Force
    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    $runtime = ConvertTo-ClaudeBoxGatewayRuntimeConfig -Parsed $cfg.Parsed
} elseif (Test-Path -LiteralPath (Join-Path $repoRoot 'config\config.example.yaml')) {
    Import-Module (Join-Path $repoRoot 'src\ClaudeBox.Control\ClaudeBox.Control.psd1') -Force
    $cfg = Get-ClaudeBoxConfig -Path (Join-Path $repoRoot 'config\config.example.yaml')
    $runtime = ConvertTo-ClaudeBoxGatewayRuntimeConfig -Parsed $cfg.Parsed
}

$result = Test-ClaudeBoxDirectEgressBlocked -Mode $Mode -Runtime $runtime
if ($Mode -eq 'SelfCheck' -and $result.Passed -eq $false) {
    exit 1
}
$result
