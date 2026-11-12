#Requires -Version 5.1
<#
.SYNOPSIS
  Optional watcher in the unique-gateway module: when the host local IP set
  changes, start gateway disconnect and polite Claude close in parallel.

.DESCRIPTION
  Controlled by features.kill_on_local_ip_change in wsl-box.yaml — DEFAULT OFF.
  This script refuses to arm itself unless -ForceEnable or the config flag is true.

  On an IP set change (when armed):
    - Thread A starts immediately: Stop-ClaudeBoxGateway / scripts/win11/Stop-ClaudeBoxGateway.ps1
    - Thread B starts immediately: polite close of box Claude processes
    Neither thread waits for the other to finish.
    After kill_grace_seconds (default 10), force-kill processes still alive.
    The process grace never delays the gateway disconnect.

  Does NOT change host timezone/locale. Does NOT install a Windows service.
  Live wsl.exe / gost stop only happens on Win11 when not in -DryRun.

.NOTES
  Docs: docs/kill-on-local-ip-change.md
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string] $ConfigPath = "",

    [Parameter(Mandatory = $false)]
    [string] $DistroName = "ClaudeBox",

    [Parameter(Mandatory = $false)]
    [int] $GraceSeconds = 10,

    [Parameter(Mandatory = $false)]
    [int] $IntervalSeconds = 5,

    [Parameter(Mandatory = $false)]
    [switch] $ForceEnable,

    [Parameter(Mandatory = $false)]
    [switch] $Once,  # single cycle (for tests when armed)

    [Parameter(Mandatory = $false)]
    [switch] $DryRun,  # plan only: no wsl.exe, no live gost stop, no process signals

    [Parameter(Mandatory = $false)]
    [string] $PreviousFingerprint = "",

    [Parameter(Mandatory = $false)]
    [string] $CurrentFingerprint = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')

function Get-LocalIpFingerprint {
    $addrs = @()
    try {
        Get-NetIPAddress -AddressFamily IPv4, IPv6 -ErrorAction SilentlyContinue |
            Where-Object {
                $_.IPAddress -and
                $_.IPAddress -notmatch '^127\.' -and
                $_.IPAddress -ne '::1' -and
                $_.PrefixOrigin -ne 'WellKnown'
            } |
            ForEach-Object { $addrs += "$($_.InterfaceAlias)|$($_.IPAddress)/$($_.PrefixLength)" }
    } catch {
        # Fallback: ipconfig scrape is worse; leave empty fingerprint
    }
    ($addrs | Sort-Object) -join "`n"
}

function Read-KillFlag([string]$path) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return $false }
    $raw = Get-Content -LiteralPath $path -Raw
    # Minimal YAML peek: kill_on_local_ip_change: true|false
    if ($raw -match '(?m)^\s*kill_on_local_ip_change:\s*true\s*$') { return $true }
    return $false
}

function Read-IntFlag([string]$path, [string]$key, [int]$default) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return $default }
    $raw = Get-Content -LiteralPath $path -Raw
    $m = [regex]::Match($raw, "(?m)^\s*$([regex]::Escape($key)):\s*(\d+)\s*$")
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return $default
}

function Stop-ClaudeBoxGatewayNow {
    param([switch]$PlanOnly)

    $stopScript = Join-Path $script:RepoRoot 'scripts\win11\Stop-ClaudeBoxGateway.ps1'
    $module = Join-Path $script:RepoRoot 'src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1'
    if ($PlanOnly) {
        Write-Host "[claude-box] DRY-RUN thread=gateway start now: Stop-ClaudeBoxGateway ($stopScript) — not waiting for process close"
        return
    }
    if (Test-Path -LiteralPath $stopScript) {
        & $stopScript
        return
    }
    if (Test-Path -LiteralPath $module) {
        Import-Module $module -Force
        Stop-ClaudeBoxGateway
        return
    }
    Write-Warning "Stop-ClaudeBoxGateway helper not found under $script:RepoRoot"
}

function Stop-ClaudeInDistro {
    param([string]$Distro, [int]$Grace, [switch]$PlanOnly)

    if ($PlanOnly) {
        Write-Host "[claude-box] DRY-RUN thread=process start now: polite close claude-desktop / claude / Claude — not waiting for gateway disconnect"
        Write-Host "[claude-box] DRY-RUN thread=process after ${Grace}s: force kill remaining box Claude processes (grace applies to processes only)"
        return
    }

    Write-Host "[claude-box] polite close: wsl -d $Distro (claude-desktop / claude / Claude)"
    $null = & wsl.exe -d $Distro -- bash -lc "pkill -TERM -x claude-desktop 2>/dev/null; pkill -TERM -x claude 2>/dev/null; pkill -TERM -x Claude 2>/dev/null; true" 2>$null

    $deadline = (Get-Date).AddSeconds($Grace)
    while ((Get-Date) -lt $deadline) {
        $still = & wsl.exe -d $Distro -- bash -lc "pgrep -x claude-desktop; pgrep -x claude; pgrep -x Claude; true" 2>$null
        if (-not $still -or ($still -as [string]).Trim().Length -eq 0) {
            Write-Host "[claude-box] Claude exited within grace (${Grace}s)"
            return
        }
        Start-Sleep -Seconds 1
    }

    Write-Host "[claude-box] force kill after ${Grace}s"
    $null = & wsl.exe -d $Distro -- bash -lc "pkill -KILL -x claude-desktop 2>/dev/null; pkill -KILL -x claude 2>/dev/null; pkill -KILL -x Claude 2>/dev/null; true" 2>$null
}

function Start-ParallelIpChangeResponse {
    param([string]$Distro, [int]$Grace, [switch]$PlanOnly)

    Write-Host "[claude-box] local IP set changed — start gateway disconnect and polite close in parallel (neither waits for the other)"

    if ($PlanOnly) {
        Stop-ClaudeBoxGatewayNow -PlanOnly
        Stop-ClaudeInDistro -Distro $Distro -Grace $Grace -PlanOnly
        return
    }

    # Start gateway disconnect without waiting, then immediately start polite close.
    $gwJob = Start-Job -ScriptBlock {
        param($RepoRoot)
        $ErrorActionPreference = 'Stop'
        $stopScript = Join-Path $RepoRoot 'scripts\win11\Stop-ClaudeBoxGateway.ps1'
        $module = Join-Path $RepoRoot 'src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1'
        if (Test-Path -LiteralPath $stopScript) {
            & $stopScript
        } elseif (Test-Path -LiteralPath $module) {
            Import-Module $module -Force
            Stop-ClaudeBoxGateway
        } else {
            Write-Warning "Stop-ClaudeBoxGateway helper not found under $RepoRoot"
        }
    } -ArgumentList $script:RepoRoot.Path

    try {
        Stop-ClaudeInDistro -Distro $Distro -Grace $Grace
    } finally {
        if ($gwJob) {
            Wait-Job $gwJob | Out-Null
            Receive-Job $gwJob
            Remove-Job $gwJob -Force -ErrorAction SilentlyContinue
        }
    }
}

if ($ConfigPath -and (Test-Path -LiteralPath $ConfigPath)) {
    $GraceSeconds = Read-IntFlag $ConfigPath 'kill_grace_seconds' $GraceSeconds
    $IntervalSeconds = Read-IntFlag $ConfigPath 'watch_interval_seconds' $IntervalSeconds
}

$armed = $ForceEnable.IsPresent -or (Read-KillFlag $ConfigPath)
if (-not $armed) {
    Write-Host "[claude-box] kill_on_local_ip_change is OFF (default). Pass -ForceEnable or set features.kill_on_local_ip_change: true. Exiting."
    exit 0
}

Write-Host "[claude-box] watcher ARMED distro=$DistroName grace=${GraceSeconds}s interval=${IntervalSeconds}s dryRun=$($DryRun.IsPresent)"

if ($PreviousFingerprint -ne "") {
    $prev = $PreviousFingerprint
} else {
    $prev = Get-LocalIpFingerprint
}
Write-Host "[claude-box] initial IP fingerprint:`n$prev"

function Invoke-OneCycle([string]$From, [string]$To) {
    if ($To -eq $From) {
        Write-Host "[claude-box] local IP set unchanged — no gateway stop, no process kill"
        return $To
    }
    Write-Host "[claude-box] was:`n$From"
    Write-Host "[claude-box] now:`n$To"
    Start-ParallelIpChangeResponse -Distro $DistroName -Grace $GraceSeconds -PlanOnly:$DryRun
    return $To
}

if ($Once) {
    if ($CurrentFingerprint -ne "") {
        $null = Invoke-OneCycle -From $prev -To $CurrentFingerprint
    } elseif ($DryRun) {
        Write-Host "[claude-box] DRY-RUN -Once without a current fingerprint: no address change to apply"
    } else {
        $cur = Get-LocalIpFingerprint
        $null = Invoke-OneCycle -From $prev -To $cur
    }
    exit 0
}

if ($DryRun) {
    Write-Host "[claude-box] DRY-RUN armed; refusing to enter a live watch loop on this host."
    exit 0
}

while ($true) {
    Start-Sleep -Seconds $IntervalSeconds
    $cur = Get-LocalIpFingerprint
    if ($cur -ne $prev) {
        $prev = Invoke-OneCycle -From $prev -To $cur
    }
}
