#Requires -Version 5.1
<#
.SYNOPSIS
  M0 结构与语法检查：可在 Linux（pwsh）或 Windows 上跑，不依赖 Hyper-V。
#>
[CmdletBinding()]
param(
    [string] $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$failed = 0

function Assert-Path([string] $Rel) {
    $p = Join-Path $RepoRoot $Rel
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Host "FAIL missing: $Rel"
        $script:failed++
    } else {
        Write-Host "OK   $Rel"
    }
}

Write-Host "RepoRoot=$RepoRoot"

$required = @(
    'SCOPE.md',
    'README.md',
    'docs/architecture-v0.md',
    'docs/research-v1-runtime.md',
    'config/config.example.yaml',
    'config/schema/claude-box.schema.json',
    'src/ClaudeBox.Control/ClaudeBox.Control.psm1',
    'src/ClaudeBox.Control/ClaudeBox.Control.psd1',
    'src/ClaudeBox.Gateway/ClaudeBox.Gateway.psm1',
    'src/ClaudeBox.Gateway/ClaudeBox.Gateway.psd1',
    'src/ClaudeBox.Guest/Initialize-ClaudeBoxGuest.ps1',
    'src/ClaudeBox.Guest/Test-ClaudeBoxGuestHealth.ps1',
    'scripts/win11/Test-HyperVPrereqs.ps1',
    'scripts/win11/New-ClaudeBoxSwitch.ps1'
)

foreach ($r in $required) { Assert-Path $r }

# Parse PowerShell scripts/modules
$psFiles = Get-ChildItem -Path $RepoRoot -Recurse -Include *.ps1, *.psm1, *.psd1 |
    Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' }

foreach ($f in $psFiles) {
    $tokens = $null
    $errors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile(
        $f.FullName, [ref]$tokens, [ref]$errors
    )
    if ($errors -and $errors.Count -gt 0) {
        Write-Host ("FAIL parse {0}: {1}" -f $f.FullName, $errors[0].Message)
        $failed++
    } else {
        Write-Host ("OK   parse {0}" -f $f.Name)
    }
}

# Import modules (should work on Linux pwsh for non-Hyper-V functions)
$controlManifest = Join-Path $RepoRoot 'src/ClaudeBox.Control/ClaudeBox.Control.psd1'
$gatewayManifest = Join-Path $RepoRoot 'src/ClaudeBox.Gateway/ClaudeBox.Gateway.psd1'

Import-Module $controlManifest -Force
Import-Module $gatewayManifest -Force

$cfg = Get-ClaudeBoxConfig -Path (Join-Path $RepoRoot 'config/config.example.yaml')
if (-not $cfg.Path) {
    Write-Host 'FAIL Get-ClaudeBoxConfig'
    $failed++
} else {
    Write-Host 'OK   Get-ClaudeBoxConfig'
}

$ready = Test-ClaudeBoxHyperVReady
if ($null -eq $ready) {
    Write-Host 'FAIL Test-ClaudeBoxHyperVReady returned null'
    $failed++
} else {
    Write-Host ("OK   Test-ClaudeBoxHyperVReady Ready={0}" -f $ready.Ready)
}

$gw = Test-ClaudeBoxGatewayConfig -GatewaySpec @{
    http    = 'http://127.0.0.1:7890'
    socks5  = 'socks5://127.0.0.1:7891'
    primary = 'http'
}
if (-not $gw.Ok) {
    Write-Host "FAIL Test-ClaudeBoxGatewayConfig: $($gw.Error)"
    $failed++
} else {
    Write-Host 'OK   Test-ClaudeBoxGatewayConfig'
}

# schema JSON parse
$schemaPath = Join-Path $RepoRoot 'config/schema/claude-box.schema.json'
try {
    $null = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
    Write-Host 'OK   schema JSON'
} catch {
    Write-Host "FAIL schema JSON: $($_.Exception.Message)"
    $failed++
}

if ($failed -gt 0) {
    Write-Host "RESULT: FAILED ($failed)"
    exit 1
}
Write-Host 'RESULT: PASSED'
exit 0
