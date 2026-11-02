<#
.SYNOPSIS
  Bind-mount visible_dirs into the ClaudeBox distro; refuse host C:\ root.
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

# Parse visible_dirs blocks (simple line scanner)
$dirs = @()
$lines = $raw -split "`n"
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\s*-\s*host_path:\s*[""']?([^""'#]+)') {
        $hp = $Matches[1].Trim()
        $gp = $null; $ro = $false
        for ($j = $i + 1; $j -lt [Math]::Min($i + 6, $lines.Count); $j++) {
            if ($lines[$j] -match '^\s*guest_path:\s*[""']?([^""'#]+)') { $gp = $Matches[1].Trim() }
            if ($lines[$j] -match '^\s*read_only:\s*true') { $ro = $true }
            if ($lines[$j] -match '^\s*-\s*host_path:') { break }
        }
        if (-not $gp) { $gp = '/mnt/claude-box/' + [IO.Path]::GetFileName($hp.TrimEnd('\','/')) }
        $dirs += [pscustomobject]@{ Host = $hp; Guest = $gp; RO = $ro }
    }
}

Write-Host "[claude-box/wsl] Mounts for $distro"
foreach ($d in $dirs) {
    $norm = $d.Host -replace '/','\'
    if ($norm -match '^[Cc]:\\?$' -or $norm -match '^[Cc]:\\Users\\?$') {
        throw "Refusing to mount host system path: $($d.Host)"
    }
    Write-Host "  $($d.Host) -> $($d.Guest) (ro=$($d.RO))"
}

if (-not (Test-IsWindows)) {
    Write-Warning "Not Windows — listed mounts only (dry-run)."
    exit 0
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw "wsl.exe not found" }

foreach ($d in $dirs) {
    if (-not (Test-Path -LiteralPath $d.Host)) {
        Write-Warning "Host path missing, skip: $($d.Host)"
        continue
    }
    $wslHost = & wsl.exe wslpath -a $d.Host
    $opts = 'bind'
    if ($d.RO) { $opts = 'bind,ro' }
    & wsl.exe -d $distro -u root -- bash -lc "mkdir -p '$($d.Guest)' && mountpoint -q '$($d.Guest)' || mount --bind '$wslHost' '$($d.Guest)'"
    if ($d.RO) {
        & wsl.exe -d $distro -u root -- bash -lc "mount -o remount,ro,bind '$($d.Guest)'" 2>$null
    }
}

Write-Host "Done. Verify inside distro: ls /mnt/claude-box ; ls /mnt/c  # latter should fail if automount off"
