<#
.SYNOPSIS
  Start Linux Claude Desktop inside ClaudeBox with spoofed TZ/locale and proxy env.
#>
[CmdletBinding()]
param(
    [string] $ConfigPath = (Join-Path $PSScriptRoot '..\config\wsl-box.yaml')
)

$ErrorActionPreference = 'Stop'

function Test-IsWindows {
    return ($env:OS -eq 'Windows_NT') -or [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)
}

function Get-YamlScalar {
    param([string]$Text, [string]$Key)
    $m = [regex]::Match($Text, "(?m)^\s*${Key}:\s*[""']?([^""'#\r\n]+)")
    if ($m.Success) { return $m.Groups[1].Value.Trim() }
    return $null
}

if (-not (Test-Path -LiteralPath $ConfigPath)) { throw "Config not found: $ConfigPath" }
$raw = Get-Content -LiteralPath $ConfigPath -Raw
$distro = Get-YamlScalar $raw 'distro_name'; if (-not $distro) { $distro = 'ClaudeBox' }
$tz = Get-YamlScalar $raw 'timezone'; if (-not $tz) { $tz = 'America/Los_Angeles' }
$locale = Get-YamlScalar $raw 'locale'; if (-not $locale) { $locale = 'en_US.UTF-8' }
$lang = Get-YamlScalar $raw 'language'; if (-not $lang) { $lang = 'en_US' }
$elang = Get-YamlScalar $raw 'electron_lang'
$ghost = Get-YamlScalar $raw 'host'
$gport = Get-YamlScalar $raw 'http_port'; if (-not $gport) { $gport = '3080' }
$cmd = Get-YamlScalar $raw 'command'; if (-not $cmd) { $cmd = 'claude-desktop' }
$man = Get-YamlScalar $raw 'manufacturer'; if (-not $man) { $man = '' }
$prod = Get-YamlScalar $raw 'product'; if (-not $prod) { $prod = '' }
$hideIds = Get-YamlScalar $raw 'hide_identifiers'; if (-not $hideIds) { $hideIds = 'true' }

Write-Host "[claude-box/wsl] Start $distro TZ=$tz locale=$locale gateway=${ghost}:${gport}"
Write-Host "  host identity: manufacturer=$man product=$prod hide_identifiers=$hideIds"

if (-not (Test-IsWindows)) {
    Write-Warning "Not Windows — would invoke guest start-claude-desktop.sh with spoof env."
    exit 0
}

# Resolve gateway host for WSL if auto
if (-not $ghost -or $ghost -eq 'auto') {
    $ghost = (& wsl.exe -d $distro -- bash -lc "ip route show default 2>/dev/null | awk '{print `$3; exit}'").Trim()
    if (-not $ghost) { $ghost = '127.0.0.1' }
}

$proxy = "http://${ghost}:${gport}"
$args = @(
    '-d', $distro,
    '--cd', '~',
    'env',
    "TZ=$tz",
    "LANG=$locale",
    "LC_ALL=$locale",
    "LANGUAGE=$lang",
    "HTTP_PROXY=$proxy",
    "HTTPS_PROXY=$proxy",
    "ALL_PROXY=$proxy",
    'NO_PROXY=localhost,127.0.0.1',
    "CLAUDE_BOX_ELECTRON_LANG=$elang",
    "CLAUDE_BOX_DESKTOP_CMD=$cmd",
    "CLAUDE_BOX_HOST_MANUFACTURER=$man",
    "CLAUDE_BOX_HOST_PRODUCT=$prod",
    "CLAUDE_BOX_HIDE_IDENTIFIERS=$hideIds",
    "CLAUDE_BOX_PREFIX=/opt/claude-box",
    '/opt/claude-box/guest/start-claude-desktop.sh'
)
Write-Host "wsl $($args -join ' ')"
& wsl.exe @args
