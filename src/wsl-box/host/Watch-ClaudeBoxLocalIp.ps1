#Requires -Version 5.1
<#
.SYNOPSIS
  Optional watcher: when the host local IP set changes, politely close then force-kill Claude in the WSL box.

.DESCRIPTION
  Known-good pattern learned from claude-switcher (polite close → grace → force kill).
  Controlled by features.kill_on_local_ip_change in wsl-box.yaml — DEFAULT OFF.
  This script refuses to arm itself unless -ForceEnable or the config flag is true.
  Does NOT change host timezone/locale. Does NOT replace the gateway.

.NOTES
  Stub / W5: implements the kill rhythm and IP snapshot helpers; full service install TBD.
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
    [switch] $Once  # single check+kill for testing when armed
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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

function Stop-ClaudeInDistro {
    param([string]$Distro, [int]$Grace)

    Write-Host "[claude-box] polite close: wsl -d $Distro (claude / electron)"
    # Polite: SIGTERM-ish via killall/pkill without -9
    $null = & wsl.exe -d $Distro -- bash -lc "pkill -TERM -x claude-desktop 2>/dev/null; pkill -TERM -f '[C]laude' 2>/dev/null; true" 2>$null

    $deadline = (Get-Date).AddSeconds($Grace)
    while ((Get-Date) -lt $deadline) {
        $still = & wsl.exe -d $Distro -- bash -lc "pgrep -af 'claude|Claude' || true" 2>$null
        if (-not $still -or ($still -as [string]).Trim().Length -eq 0) {
            Write-Host "[claude-box] Claude exited within grace (${Grace}s)"
            return
        }
        Start-Sleep -Seconds 1
    }

    Write-Host "[claude-box] force kill after ${Grace}s"
    $null = & wsl.exe -d $Distro -- bash -lc "pkill -KILL -x claude-desktop 2>/dev/null; pkill -KILL -f '[C]laude' 2>/dev/null; true" 2>$null
}

$armed = $ForceEnable.IsPresent -or (Read-KillFlag $ConfigPath)
if (-not $armed) {
    Write-Host "[claude-box] kill_on_local_ip_change is OFF (default). Pass -ForceEnable or set features.kill_on_local_ip_change: true. Exiting."
    exit 0
}

Write-Host "[claude-box] watcher ARMED distro=$DistroName grace=${GraceSeconds}s interval=${IntervalSeconds}s"
$prev = Get-LocalIpFingerprint
Write-Host "[claude-box] initial IP fingerprint:`n$prev"

if ($Once) {
    Stop-ClaudeInDistro -Distro $DistroName -Grace $GraceSeconds
    exit 0
}

while ($true) {
    Start-Sleep -Seconds $IntervalSeconds
    $cur = Get-LocalIpFingerprint
    if ($cur -ne $prev) {
        Write-Host "[claude-box] local IP set changed — killing Claude"
        Write-Host "[claude-box] was:`n$prev"
        Write-Host "[claude-box] now:`n$cur"
        Stop-ClaudeInDistro -Distro $DistroName -Grace $GraceSeconds
        $prev = $cur
    }
}
