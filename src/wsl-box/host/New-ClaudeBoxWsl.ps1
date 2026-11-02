<#
.SYNOPSIS
  Create or configure the ClaudeBox WSL2 distro (stub-capable on non-Windows).
.NOTES
  Does NOT enable the Windows optional feature automatically without -AllowEnableWsl.
  Official Anthropic guidance prefers Windows Desktop for WSL users; this path is unsupported.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string] $ConfigPath = (Join-Path $PSScriptRoot '..\config\wsl-box.yaml'),

    [switch] $AllowEnableWsl,

    [switch] $WhatIfDry
)

$ErrorActionPreference = 'Stop'

function Test-IsWindows {
    return ($env:OS -eq 'Windows_NT') -or ($PSVersionTable.Platform -eq 'Win32NT') -or [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)
}

function Get-YamlScalar {
    param([string]$Text, [string]$Key)
    $m = [regex]::Match($Text, "(?m)^\s*${Key}:\s*[""']?([^""'#\r\n]+)")
    if ($m.Success) { return $m.Groups[1].Value.Trim() }
    return $null
}

Write-Host "[claude-box/wsl] New-ClaudeBoxWsl"
Write-Host "  Config: $ConfigPath"

if (-not (Test-Path -LiteralPath $ConfigPath)) {
    $example = Join-Path $PSScriptRoot '..\config\wsl-box.example.yaml'
    throw "Config not found: $ConfigPath — copy from $example"
}

$raw = Get-Content -LiteralPath $ConfigPath -Raw
$distro = Get-YamlScalar $raw 'distro_name'
if (-not $distro) { $distro = 'ClaudeBox' }
$tz = Get-YamlScalar $raw 'timezone'
$locale = Get-YamlScalar $raw 'locale'
$disableAuto = $raw -match 'disable_automount:\s*true'

Write-Host "  Distro: $distro"
Write-Host "  Identity TZ/locale (guest): $tz / $locale"
Write-Host "  Disable automount: $disableAuto"

if (-not (Test-IsWindows)) {
    Write-Warning "Not Windows — structural dry-run only. On 和零's Win11, re-run this script."
    Write-Host "OK (dry-run): would ensure distro '$distro', write wsl.conf automount=$(-not $disableAuto), sync guest scripts."
    exit 0
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    if (-not $AllowEnableWsl) {
        throw "wsl.exe not found. Install WSL manually (wsl --install) or re-run with -AllowEnableWsl after explicit approval."
    }
    if ($WhatIfDry) {
        Write-Host "WhatIf: would run wsl --install"
        exit 0
    }
    Write-Host "Running: wsl --install -d Ubuntu-24.04"
    & wsl.exe --install -d Ubuntu-24.04
    Write-Host "If prompted, reboot, then re-run this script."
    exit 0
}

$existing = & wsl.exe -l -q 2>$null | ForEach-Object { $_.Trim() -replace '\x00','' } | Where-Object { $_ -eq $distro }
if (-not $existing) {
    Write-Host "Distro '$distro' missing."
    Write-Host "Next: export a base Ubuntu and import, e.g.:"
    Write-Host "  wsl --export Ubuntu-24.04 `$env:TEMP\ubuntu.tar"
    Write-Host "  mkdir `$env:LOCALAPPDATA\ClaudeBox\wsl\$distro"
    Write-Host "  wsl --import $distro `$env:LOCALAPPDATA\ClaudeBox\wsl\$distro `$env:TEMP\ubuntu.tar --version 2"
    Write-Host "Or rename/register manually, then re-run."
    if ($WhatIfDry) { exit 0 }
    # Do not auto-import without a local tar — leave actionable message
    exit 2
}

# Push guest scripts + wsl.conf snippet
$guestSrc = (Resolve-Path (Join-Path $PSScriptRoot '..\guest')).Path
$guestSrcWsl = & wsl.exe wslpath -a $guestSrc
$wslConf = @"
[boot]
systemd=true

[automount]
enabled=$($disableAuto.ToString().ToLower() -eq 'false')
mountFsTab=true

[network]
generateHosts=true
generateResolvConf=true

[user]
default=claudebox
"@
# When disable_automount true → enabled=false
if ($disableAuto) {
    $wslConf = @"
[boot]
systemd=true

[automount]
enabled=false
mountFsTab=true

[network]
generateHosts=true
generateResolvConf=true
"@
}

if ($WhatIfDry) {
    Write-Host "WhatIf: would write /etc/wsl.conf and sync $guestSrcWsl -> /opt/claude-box/guest"
    exit 0
}

$b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($wslConf))
& wsl.exe -d $distro -u root -- bash -lc "mkdir -p /opt/claude-box/guest /etc && echo $b64 | base64 -d > /etc/wsl.conf && mkdir -p /mnt/claude-box"
& wsl.exe -d $distro -u root -- bash -lc "rm -rf /opt/claude-box/guest/*; cp -a '$guestSrcWsl'/. /opt/claude-box/guest/; chmod +x /opt/claude-box/guest/*.sh"
& wsl.exe -d $distro -u root -- bash -lc "id claudebox 2>/dev/null || useradd -m -s /bin/bash claudebox; chown -R claudebox:claudebox /opt/claude-box"

Write-Host "Wrote /etc/wsl.conf and synced guest scripts. Run: wsl --shutdown  then Set-ClaudeBoxWslMounts / Start-ClaudeBoxWsl."
