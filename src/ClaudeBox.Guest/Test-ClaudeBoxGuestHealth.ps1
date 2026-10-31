#Requires -Version 5.1
<#
.SYNOPSIS
  访客内健康检查：假环境字段、Claude 是否在跑、挂载是否可见、宿主 C: 是否误入。

.PARAMETER ExpectedHostname
  期望计算机名（来自 identity.hostname）。

.PARAMETER ExpectedTimezone
  期望时区 ID（如 UTC）。

.PARAMETER ExpectedLocale
  期望 culture（如 en-US）。

.PARAMETER ExpectedMountLetters
  期望已映射的盘符列表，如 @('Z:')。

.PARAMETER MountPlanPath
  可选：对照 mount-plan.json 检查条目。
#>
[CmdletBinding()]
param(
    [string] $ExpectedHostname = '',
    [string] $ExpectedTimezone = '',
    [string] $ExpectedLocale = '',
    [string[]] $ExpectedMountLetters = @(),
    [string] $MountPlanPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$checks = New-Object System.Collections.Generic.List[object]
function Add-Check([string]$Name, [bool]$Ok, [string]$Detail) {
    $script:checks.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
    $flag = if ($Ok) { 'OK  ' } else { 'FAIL' }
    Write-Host ("[{0}] {1}: {2}" -f $flag, $Name, $Detail)
}

Write-Host '=== Guest health (M3) ==='

$hn = $env:COMPUTERNAME
Write-Host ("Hostname: {0}" -f $hn)
if ($ExpectedHostname) {
    Add-Check 'hostname' ($hn -and ($hn.ToLowerInvariant() -eq $ExpectedHostname.ToLowerInvariant())) `
        ("actual={0} expected={1}" -f $hn, $ExpectedHostname)
} else {
    Add-Check 'hostname' ($null -ne $hn -and $hn.Length -gt 0) "actual=$hn (no expected set)"
}

$tzId = ''
try {
    if (Get-Command Get-TimeZone -ErrorAction SilentlyContinue) {
        $tzId = (Get-TimeZone).Id
    } elseif (Get-Command tzutil.exe -ErrorAction SilentlyContinue) {
        $tzId = (& tzutil.exe /g 2>$null | Out-String).Trim()
    }
} catch { $tzId = '' }
Write-Host ("TimeZone: {0}" -f $(if ($tzId) { $tzId } else { '(unavailable)' }))
if ($ExpectedTimezone) {
    Add-Check 'timezone' ($tzId -eq $ExpectedTimezone) ("actual={0} expected={1}" -f $tzId, $ExpectedTimezone)
} else {
    Add-Check 'timezone' (-not [string]::IsNullOrWhiteSpace($tzId)) "actual=$tzId"
}

$culture = [System.Globalization.CultureInfo]::CurrentCulture.Name
Write-Host ("Culture: {0}" -f $culture)
if ($ExpectedLocale) {
    Add-Check 'locale' ($culture -eq $ExpectedLocale) ("actual={0} expected={1}" -f $culture, $ExpectedLocale)
} else {
    Add-Check 'locale' (-not [string]::IsNullOrWhiteSpace($culture)) "actual=$culture"
}

# Claude process
$claudeProcs = @()
try {
    $claudeProcs = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
            $_.ProcessName -match '(?i)^claude' -or $_.Name -match '(?i)claude'
        })
} catch { }
$claudeRunning = $claudeProcs.Count -gt 0
Add-Check 'claude_process' $claudeRunning ("count={0} names={1}" -f $claudeProcs.Count, (($claudeProcs | Select-Object -ExpandProperty ProcessName -Unique) -join ','))
if (-not $claudeRunning) {
    Write-Host '  Hint: after Install-ClaudeBoxWindowsClaude, launch Claude from Start Menu.'
}

# Mounts
Write-Host 'FileSystem drives:'
$roots = @()
try {
    Get-PSDrive -PSProvider FileSystem | ForEach-Object {
        Write-Host ("  {0}: root={1}" -f $_.Name, $_.Root)
        $roots += [string]$_.Root
    }
} catch {
    Write-Host '  (Get-PSDrive unavailable)'
}

$hostCLeak = $false
foreach ($r in $roots) {
    # Heuristic: redirected host C often shows as \\tsclient\C or similar
    if ($r -match '(?i)tsclient[\\/]C' -or $r -match '(?i)\\\\[^\\]+\\C\$') {
        $hostCLeak = $true
    }
}
Add-Check 'no_host_C_redirect_heuristic' (-not $hostCLeak) $(if ($hostCLeak) { 'possible host C redirect detected' } else { 'no tsclient\\C / C$ heuristic hit' })

if ($ExpectedMountLetters -and $ExpectedMountLetters.Count -gt 0) {
    foreach ($letter in $ExpectedMountLetters) {
        $L = $letter.TrimEnd(':')
        $found = $false
        try {
            $found = [bool](Get-PSDrive -Name $L -ErrorAction SilentlyContinue)
        } catch { $found = $false }
        Add-Check "mount_$L" $found "drive ${L}: present=$found"
    }
}

if ($MountPlanPath -and (Test-Path -LiteralPath $MountPlanPath)) {
    try {
        $plan = Get-Content -LiteralPath $MountPlanPath -Raw | ConvertFrom-Json
        foreach ($e in @($plan.entries)) {
            $L = ([string]$e.guest_drive).TrimEnd(':')
            $found = $false
            try { $found = [bool](Get-PSDrive -Name $L -ErrorAction SilentlyContinue) } catch { }
            Add-Check ("plan_mount_" + $e.name) $found ("expect {0}: {1}" -f $e.guest_drive, $e.guest_unc)
        }
    } catch {
        Add-Check 'mount_plan_parse' $false $_.Exception.Message
    }
}

# Proxy glance
try {
    $proxy = (Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -ErrorAction SilentlyContinue)
    if ($proxy) {
        Write-Host ("ProxyEnable={0} ProxyServer={1}" -f $proxy.ProxyEnable, $proxy.ProxyServer)
    }
} catch { }

$failed = @($checks | Where-Object { -not $_.Ok }).Count
Write-Host ("=== Health summary: {0} checks, {1} failed (Verified=false until Win11 soak) ===" -f $checks.Count, $failed)

[pscustomobject]@{
    Checks   = $checks.ToArray()
    Failed   = $failed
    Passed   = ($failed -eq 0)
    Verified = $false
    Note     = 'Do not treat Passed as production verification on non-Hyper-V.'
}
