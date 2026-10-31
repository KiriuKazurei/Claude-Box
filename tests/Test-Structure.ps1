#Requires -Version 5.1
<#
.SYNOPSIS
  结构与语法检查：可在 Linux（pwsh）或 Windows 上跑，不依赖 Hyper-V。
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
    'scripts/win11/New-ClaudeBoxSwitch.ps1',
    'scripts/win11/New-ClaudeBoxVM.ps1'
)

foreach ($r in $required) { Assert-Path $r }

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

$controlManifest = Join-Path $RepoRoot 'src/ClaudeBox.Control/ClaudeBox.Control.psd1'
$gatewayManifest = Join-Path $RepoRoot 'src/ClaudeBox.Gateway/ClaudeBox.Gateway.psd1'

Import-Module $controlManifest -Force
Import-Module $gatewayManifest -Force

$cfg = Get-ClaudeBoxConfig -Path (Join-Path $RepoRoot 'config/config.example.yaml')
if (-not $cfg.Path -or -not $cfg.Parsed) {
    Write-Host 'FAIL Get-ClaudeBoxConfig'
    $failed++
} else {
    Write-Host ("OK   Get-ClaudeBoxConfig VmName={0} Gen={1}" -f $cfg.Parsed.VmName, $cfg.Parsed.Generation)
    if ($cfg.Parsed.VmName -ne 'ClaudeBox-Win') {
        Write-Host ("FAIL VmName expected ClaudeBox-Win got {0}" -f $cfg.Parsed.VmName)
        $failed++
    }
    if (-not $cfg.Parsed.ExcludeHostSystemDrive) {
        Write-Host 'FAIL ExcludeHostSystemDrive should default true'
        $failed++
    }
}

# C: guard
if (-not (Test-ClaudeBoxHostSystemDrivePath -HostPath 'C:\')) {
    Write-Host 'FAIL Test-ClaudeBoxHostSystemDrivePath C:\'
    $failed++
} else {
    Write-Host 'OK   Test-ClaudeBoxHostSystemDrivePath C:\'
}

try {
    Assert-ClaudeBoxVisibleDirsSafe -Parsed ([pscustomobject]@{
        ExcludeHostSystemDrive = $true
        VisibleDirs = @('C:\')
    })
    Write-Host 'FAIL Assert-ClaudeBoxVisibleDirsSafe should throw on C:\'
    $failed++
} catch {
    Write-Host 'OK   Assert-ClaudeBoxVisibleDirsSafe rejects C:\'
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

$log = Set-ClaudeBoxGatewayLogging -Enabled $true -Path (Join-Path ([System.IO.Path]::GetTempPath()) 'ClaudeBox-test\gateway.jsonl')
if (-not $log.Enabled) {
    Write-Host 'FAIL Set-ClaudeBoxGatewayLogging'
    $failed++
} else {
    Write-Host 'OK   Set-ClaudeBoxGatewayLogging'
}

$plan = Get-ClaudeBoxFakeEnvPlan -ConfigPath (Join-Path $RepoRoot 'config/config.example.yaml')
if (-not $plan.Steps -or $plan.Steps.Count -lt 5) {
    Write-Host 'FAIL Get-ClaudeBoxFakeEnvPlan'
    $failed++
} else {
    Write-Host 'OK   Get-ClaudeBoxFakeEnvPlan'
}

# New-ClaudeBoxVM on Linux should throw Windows requirement after config+guard
try {
    New-ClaudeBoxVM -ConfigPath (Join-Path $RepoRoot 'config/config.example.yaml')
    Write-Host 'FAIL New-ClaudeBoxVM should throw on non-Windows'
    $failed++
} catch {
    if ($_.Exception.Message -match 'Windows') {
        Write-Host 'OK   New-ClaudeBoxVM refuses non-Windows'
    } else {
        Write-Host "FAIL New-ClaudeBoxVM unexpected: $($_.Exception.Message)"
        $failed++
    }
}

$schemaPath = Join-Path $RepoRoot 'config/schema/claude-box.schema.json'
try {
    $null = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
    Write-Host 'OK   schema JSON'
} catch {
    Write-Host "FAIL schema JSON: $($_.Exception.Message)"
    $failed++
}

# MVP keywords in architecture
$arch = Get-Content -LiteralPath (Join-Path $RepoRoot 'docs/architecture-v0.md') -Raw
foreach ($kw in @('exclude_host_system_drive', 'gateway.logging', 'Clash', '假', 'M1')) {
    if ($arch -notmatch [regex]::Escape($kw)) {
        Write-Host "FAIL architecture missing keyword: $kw"
        $failed++
    }
}
Write-Host 'OK   architecture MVP keywords'

if ($failed -gt 0) {
    Write-Host "RESULT: FAILED ($failed)"
    exit 1
}
Write-Host 'RESULT: PASSED'
exit 0
