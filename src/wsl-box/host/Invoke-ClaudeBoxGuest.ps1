<#
.SYNOPSIS
  Run a guest script under /opt/claude-box/guest inside the distro.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Script,

    [string] $ConfigPath = (Join-Path $PSScriptRoot '..\config\wsl-box.yaml'),

    [string[]] $ScriptArgs = @()
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

$name = [IO.Path]::GetFileName($Script)
$raw = if (Test-Path $ConfigPath) { Get-Content $ConfigPath -Raw } else { '' }
$distro = Get-YamlScalar $raw 'distro_name'; if (-not $distro) { $distro = 'ClaudeBox' }

Write-Host "[claude-box/wsl] guest $name on $distro"

if (-not (Test-IsWindows)) {
    Write-Warning "Not Windows — dry-run: wsl -d $distro -- /opt/claude-box/guest/$name"
    exit 0
}

& wsl.exe -d $distro -- bash -lc "/opt/claude-box/guest/$name $($ScriptArgs -join ' ')"
