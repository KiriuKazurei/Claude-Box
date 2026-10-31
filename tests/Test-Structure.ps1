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
    'src/ClaudeBox.Gateway/README.md',
    'src/ClaudeBox.Gateway/samples/gateway.log.example.jsonl',
    'src/ClaudeBox.Gateway/templates/gost.relay.example.yml',
    'src/ClaudeBox.Gateway/gateway.upstream.example.json',
    'src/ClaudeBox.Guest/Initialize-ClaudeBoxGuest.ps1',
    'src/ClaudeBox.Guest/Test-ClaudeBoxGuestHealth.ps1',
    'src/ClaudeBox.Guest/Set-ClaudeBoxGuestNetwork.ps1',
    'src/ClaudeBox.Guest/Install-ClaudeBoxWindowsClaude.ps1',
    'src/ClaudeBox.Guest/Mount-ClaudeBoxGuestShares.ps1',
    'scripts/win11/Test-HyperVPrereqs.ps1',
    'scripts/win11/Mount-ClaudeBoxVisibleDirs.ps1',
    'scripts/win11/New-ClaudeBoxSwitch.ps1',
    'scripts/win11/New-ClaudeBoxVM.ps1',
    'scripts/win11/Start-ClaudeBoxGateway.ps1',
    'scripts/win11/Stop-ClaudeBoxGateway.ps1',
    'scripts/win11/Test-ClaudeBoxDirectEgressBlocked.ps1',
    'scripts/win11/Set-ClaudeBoxInternalGatewayRouting.md',
    'scripts/win11/Start-ClaudeBoxControlUi.ps1',
    'src/ClaudeBox.Control/ui/Start-ClaudeBoxControlUi.ps1',
    'src/ClaudeBox.Control/ui/README.md'
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
    if ([int]$cfg.Parsed.ListenPort -ne 3080) {
        Write-Host ("FAIL ListenPort expected 3080 got {0}" -f $cfg.Parsed.ListenPort)
        $failed++
    } else {
        Write-Host 'OK   ListenPort=3080'
    }
    if (-not $cfg.Parsed.LoggingEnabled) {
        Write-Host 'FAIL LoggingEnabled should be true in example'
        $failed++
    }
    $bl = @($cfg.Parsed.Blocklist)
    if ($bl.Count -lt 1) {
        Write-Host 'FAIL Blocklist should parse from example'
        $failed++
    } else {
        Write-Host ("OK   Blocklist count={0}" -f $bl.Count)
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
    http        = 'http://127.0.0.1:7890'
    socks5      = 'socks5://127.0.0.1:7891'
    primary     = 'http'
    listen_ip   = '192.168.87.1'
    listen_port = 3080
}
if (-not $gw.Ok) {
    Write-Host "FAIL Test-ClaudeBoxGatewayConfig: $($gw.Error)"
    $failed++
} else {
    Write-Host 'OK   Test-ClaudeBoxGatewayConfig'
}

$runtime = ConvertTo-ClaudeBoxGatewayRuntimeConfig -Parsed $cfg.Parsed
if ($runtime.GuestProxyUrl -ne 'http://192.168.87.1:3080') {
    Write-Host "FAIL GuestProxyUrl=$($runtime.GuestProxyUrl)"
    $failed++
} else {
    Write-Host 'OK   ConvertTo-ClaudeBoxGatewayRuntimeConfig'
}

if (-not (Test-ClaudeBoxGatewayHostBlocked -HostName 'www.google-analytics.com' -Blocklist @('google-analytics.com'))) {
    Write-Host 'FAIL blocklist suffix match'
    $failed++
} else {
    Write-Host 'OK   Test-ClaudeBoxGatewayHostBlocked'
}

$logPath = Join-Path ([System.IO.Path]::GetTempPath()) ('ClaudeBox-test-{0}\gateway.jsonl' -f [guid]::NewGuid().ToString('n'))
$log = Set-ClaudeBoxGatewayLogging -Enabled $true -Path $logPath
if (-not $log.Enabled) {
    Write-Host 'FAIL Set-ClaudeBoxGatewayLogging'
    $failed++
} else {
    Write-Host 'OK   Set-ClaudeBoxGatewayLogging'
}

$line = Write-ClaudeBoxGatewayLogEntry -Path $logPath -Src '192.168.87.10:1' -DstIp '1.2.3.4' -DstPort 443 `
    -SniOrHost 'api.example.com' -Proto 'tcp' -Action 'proxy' -UpstreamOk $true -Bytes 100
if ($line -notmatch '"action":"proxy"' -or $line -notmatch 'sni_or_host') {
    Write-Host "FAIL Write-ClaudeBoxGatewayLogEntry: $line"
    $failed++
} else {
    Write-Host 'OK   Write-ClaudeBoxGatewayLogEntry'
}

$deny = Write-ClaudeBoxGatewayLogEntry -Path $logPath -SniOrHost 'evil.telemetry.invalid' -DstPort 443 -Action 'deny' -UpstreamOk $false
if ($deny -notmatch '"action":"deny"') {
    Write-Host 'FAIL deny log action'
    $failed++
} else {
    Write-Host 'OK   log action=deny'
}

$plan = New-ClaudeBoxGatewayRelayPlan -Runtime $runtime
if ($plan.CommandLine -notmatch 'gost' -or $plan.CommandLine -notmatch '192.168.87.1') {
    Write-Host "FAIL relay plan: $($plan.CommandLine)"
    $failed++
} else {
    Write-Host ("OK   New-ClaudeBoxGatewayRelayPlan {0}" -f $plan.CommandLine)
}

$start = Start-ClaudeBoxGateway -Runtime $runtime -SkipProcessStart -WriteStartupLog
if ($start.Verified -eq $true) {
    Write-Host 'FAIL Start must not claim Verified=true without Hyper-V'
    $failed++
}
if (-not $start.Saved -or -not (Test-Path -LiteralPath $start.Saved.PlanPath)) {
    Write-Host 'FAIL Start did not save relay plan'
    $failed++
} else {
    Write-Host 'OK   Start-ClaudeBoxGateway SkipProcessStart saves plan'
}

$egress = Test-ClaudeBoxDirectEgressBlocked -Mode SelfCheck -Runtime $runtime
if ($egress.Verified -eq $true) {
    Write-Host 'FAIL egress helper must not claim Verified'
    $failed++
}
if ($egress.Passed -ne $true) {
    Write-Host 'FAIL Test-ClaudeBoxDirectEgressBlocked SelfCheck'
    $failed++
} else {
    Write-Host 'OK   Test-ClaudeBoxDirectEgressBlocked SelfCheck'
}

# Control wiring (SkipProcessStart)
$wired = Set-ClaudeBoxGatewayUpstream -ConfigPath (Join-Path $RepoRoot 'config/config.example.yaml') -SkipProcessStart
if (-not $wired.Runtime) {
    Write-Host 'FAIL Set-ClaudeBoxGatewayUpstream wiring'
    $failed++
} else {
    Write-Host 'OK   Set-ClaudeBoxGatewayUpstream -> Gateway'
}

$planGuest = Get-ClaudeBoxFakeEnvPlan -ConfigPath (Join-Path $RepoRoot 'config/config.example.yaml')
if (-not $planGuest.Steps -or $planGuest.Steps.Count -lt 5) {
    Write-Host 'FAIL Get-ClaudeBoxFakeEnvPlan'
    $failed++
} else {
    Write-Host 'OK   Get-ClaudeBoxFakeEnvPlan'
}

# M3 mount plan (Linux dry-run)
try {
    $m = Mount-ClaudeBoxVisibleDirs -ConfigPath (Join-Path $RepoRoot 'config/config.example.yaml')
    if (-not $m.PlanPath -or -not (Test-Path -LiteralPath $m.PlanPath)) {
        Write-Host 'FAIL Mount-ClaudeBoxVisibleDirs did not save plan'
        $failed++
    } else {
        $planJson = Get-Content -LiteralPath $m.PlanPath -Raw | ConvertFrom-Json
        if ($planJson.method -ne 'smb_internal') {
            Write-Host ("FAIL mount method={0}" -f $planJson.method)
            $failed++
        } elseif (@($planJson.entries).Count -lt 1) {
            Write-Host 'FAIL mount plan entries empty'
            $failed++
        } else {
            $bad = @($planJson.entries | Where-Object { $_.host_path -match '^[cC]:\?$' })
            if ($bad.Count -gt 0) {
                Write-Host 'FAIL mount plan contains host C:'
                $failed++
            } else {
                Write-Host ("OK   Mount-ClaudeBoxVisibleDirs plan entries={0} method={1}" -f @($planJson.entries).Count, $planJson.method)
            }
        }
    }
} catch {
    Write-Host "FAIL Mount-ClaudeBoxVisibleDirs: $($_.Exception.Message)"
    $failed++
}

# Reject C: via mount plan builder
try {
    $tmpCfg = Join-Path ([IO.Path]::GetTempPath()) ('claude-box-bad-{0}.yaml' -f [guid]::NewGuid().ToString('n'))
    $badYaml = @"
apiVersion: claude-box/v1alpha1
kind: Box
metadata:
  name: bad
spec:
  vm:
    name: ClaudeBox-Win
    memory_mb: 8192
  gateway:
    http: "http://127.0.0.1:7890"
  visible_dirs:
    - host_path: "C:\\"
      read_only: false
  features:
    exclude_host_system_drive: true
"@
    Set-Content -LiteralPath $tmpCfg -Value $badYaml -Encoding UTF8
    try {
        $null = New-ClaudeBoxMountPlan -ConfigPath $tmpCfg
        Write-Host 'FAIL New-ClaudeBoxMountPlan should reject C:\'
        $failed++
    } catch {
        if ($_.Exception.Message -match 'system drive|C:') {
            Write-Host 'OK   New-ClaudeBoxMountPlan rejects C:\'
        } else {
            Write-Host "FAIL unexpected throw: $($_.Exception.Message)"
            $failed++
        }
    } finally {
        Remove-Item -LiteralPath $tmpCfg -Force -ErrorAction SilentlyContinue
    }
} catch {
    Write-Host "FAIL C: mount plan reject harness: $($_.Exception.Message)"
    $failed++
}

# Fake env plan includes workgroup
if (-not $planGuest.Workgroup) {
    Write-Host 'FAIL FakeEnvPlan missing Workgroup'
    $failed++
} else {
    Write-Host ("OK   FakeEnvPlan Workgroup={0}" -f $planGuest.Workgroup)
}

# Guest init dry-run on this host (Write-Host -> information stream)
$guestInit = Join-Path $RepoRoot 'src/ClaudeBox.Guest/Initialize-ClaudeBoxGuest.ps1'
$initOut = & $guestInit -IdentityHostname 'claude-box' -Timezone UTC -Locale en-US *>&1 | Out-String
if ($initOut -notmatch 'DRY-RUN') {
    Write-Host 'FAIL Initialize-ClaudeBoxGuest dry-run output unexpected'
    $failed++
} else {
    Write-Host 'OK   Initialize-ClaudeBoxGuest dry-run'
}

$guide = & (Join-Path $RepoRoot 'src/ClaudeBox.Guest/Install-ClaudeBoxWindowsClaude.ps1') -ShowGuide *>&1 | Out-String
if ($guide -notmatch 'Add-AppxPackage' -or $guide -notmatch 'msix') {
    Write-Host 'FAIL Install-ClaudeBoxWindowsClaude guide'
    $failed++
} else {
    Write-Host 'OK   Install-ClaudeBoxWindowsClaude guide'
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
    $schemaObj = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
    if (-not $schemaObj.properties.spec.properties.gateway.properties.listen_port) {
        Write-Host 'FAIL schema missing listen_port'
        $failed++
    } elseif (-not $schemaObj.properties.spec.properties.gateway.properties.blocklist) {
        Write-Host 'FAIL schema missing blocklist'
        $failed++
    } elseif (-not $schemaObj.properties.spec.properties.mount) {
        Write-Host 'FAIL schema missing mount'
        $failed++
    } else {
        Write-Host 'OK   schema JSON (+ listen_port, blocklist, mount)'
    }
} catch {
    Write-Host "FAIL schema JSON: $($_.Exception.Message)"
    $failed++
}

# MVP keywords in architecture
$arch = Get-Content -LiteralPath (Join-Path $RepoRoot 'docs/architecture-v0.md') -Raw
foreach ($kw in @('exclude_host_system_drive', 'gateway.logging', 'Clash', '假', 'M3', 'M4', 'smb_internal', 'gost', 'Save-ClaudeBoxConfig', 'WinForms', '待验证')) {
    if ($arch -notmatch [regex]::Escape($kw)) {
        Write-Host "FAIL architecture missing keyword: $kw"
        $failed++
    }
}
Write-Host 'OK   architecture MVP/M4 keywords'


# --- M4: Save-ClaudeBoxConfig + UI SelfCheck ---
$uiScript = Join-Path $RepoRoot 'src/ClaudeBox.Control/ui/Start-ClaudeBoxControlUi.ps1'
try {
    $tmpOut = Join-Path ([IO.Path]::GetTempPath()) ('claude-box-m4-{0}.yaml' -f [guid]::NewGuid().ToString('n'))
    $saved = Save-ClaudeBoxConfig -Path $tmpOut `
        -VmName 'ClaudeBox-M4' `
        -GatewayHttp 'http://127.0.0.1:7890' `
        -GatewaySocks5 'socks5://127.0.0.1:7891' `
        -LoggingEnabled $true `
        -VisibleDirs @(@{ HostPath = 'D:\Work\proj'; ReadOnly = $true }) `
        -ExistingParsed $cfg.Parsed
    $again = Get-ClaudeBoxConfig -Path $saved.Path
    if ($again.Parsed.VmName -ne 'ClaudeBox-M4') {
        Write-Host ("FAIL Save VmName={0}" -f $again.Parsed.VmName)
        $failed++
    } elseif (-not $again.Parsed.LoggingEnabled) {
        Write-Host 'FAIL Save LoggingEnabled'
        $failed++
    } elseif (@($again.Parsed.VisibleDirs) -notcontains 'D:\Work\proj' -and (@($again.Parsed.VisibleDirs) | Where-Object { $_ -match 'Work' }).Count -lt 1) {
        Write-Host ("FAIL Save VisibleDirs={0}" -f (@($again.Parsed.VisibleDirs) -join ','))
        $failed++
    } else {
        Write-Host 'OK   Save-ClaudeBoxConfig roundtrip'
    }
    Remove-Item -LiteralPath $tmpOut -Force -ErrorAction SilentlyContinue
} catch {
    Write-Host "FAIL Save-ClaudeBoxConfig: $($_.Exception.Message)"
    $failed++
}

try {
    $null = Save-ClaudeBoxConfig -Path (Join-Path ([IO.Path]::GetTempPath()) 'claude-box-m4-bad.yaml') `
        -VmName 'bad' -GatewayHttp 'http://127.0.0.1:1' -VisibleDirs @('C:\') -ExcludeHostSystemDrive $true
    Write-Host 'FAIL Save-ClaudeBoxConfig should reject C:\'
    $failed++
} catch {
    if ($_.Exception.Message -match 'system drive|C:') {
        Write-Host 'OK   Save-ClaudeBoxConfig rejects C:\'
    } else {
        Write-Host "FAIL Save C: reject message: $($_.Exception.Message)"
        $failed++
    }
}

try {
    Connect-ClaudeBoxVM -Name 'ClaudeBox-Win'
    if (-not (Test-ClaudeBoxIsWindowsHost)) {
        Write-Host 'FAIL Connect should throw on non-Windows'
        $failed++
    } else {
        Write-Host 'OK   Connect-ClaudeBoxVM on Windows (launched or no-op)'
    }
} catch {
    if (-not (Test-ClaudeBoxIsWindowsHost) -and $_.Exception.Message -match 'Windows') {
        Write-Host 'OK   Connect-ClaudeBoxVM refuses non-Windows'
    } elseif (Test-ClaudeBoxIsWindowsHost) {
        Write-Host ("OK   Connect-ClaudeBoxVM host error acceptable: {0}" -f $_.Exception.Message)
    } else {
        Write-Host "FAIL Connect-ClaudeBoxVM: $($_.Exception.Message)"
        $failed++
    }
}

$uiOut = & $uiScript -SelfCheck *>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $uiOut -notmatch 'SelfCheck RESULT: PASSED') {
    Write-Host "FAIL UI SelfCheck exit=$LASTEXITCODE"
    Write-Host $uiOut
    $failed++
} else {
    Write-Host 'OK   UI -SelfCheck PASSED'
}

$readmeUi = Get-Content -LiteralPath (Join-Path $RepoRoot 'README.md') -Raw
if ($readmeUi -notmatch 'M4' -or $readmeUi -notmatch 'Start-ClaudeBoxControlUi') {
    Write-Host 'FAIL README missing M4 / Start-ClaudeBoxControlUi'
    $failed++
} else {
    Write-Host 'OK   README mentions M4 UI'
}

# sample jsonl readable
$sample = Join-Path $RepoRoot 'src/ClaudeBox.Gateway/samples/gateway.log.example.jsonl'
$sampleLines = Get-Content -LiteralPath $sample
if ($sampleLines.Count -lt 2) {
    Write-Host 'FAIL sample jsonl too short'
    $failed++
} else {
    $null = $sampleLines[0] | ConvertFrom-Json
    Write-Host 'OK   sample gateway.log.example.jsonl'
}

if ($failed -gt 0) {
    Write-Host "RESULT: FAILED ($failed)"
    exit 1
}
Write-Host 'RESULT: PASSED'
exit 0
