<#
.SYNOPSIS
  Export only the host manufacturer and product/model for the Claude Box guest.

.DESCRIPTION
  Allowed fields (and the only WMI/CIM properties this script will query):
    Win32_ComputerSystem.Manufacturer
    Win32_ComputerSystem.Model

  This script does not query or write:
    BIOS/chassis/board serial numbers, product/board/chassis UUIDs,
    MAC addresses, disk/volume serials, CPU ProcessorId, Windows MachineGuid,
    device instance paths (PNPDeviceID), or similar unique encodings.

  On a non-Windows machine, pass -FixturePath (a raw inventory YAML that may
  include forbidden keys). The output is always sanitized to manufacturer + product.

  This is not a tool to hide that the guest is WSL or a VM.

.PARAMETER FixturePath
  Optional raw inventory YAML used by tests (and as a dry-run stand-in).
  Forbidden keys in the fixture are dropped, never copied to the output.

.PARAMETER OutputPath
  Destination YAML (manufacturer + product only).
#>
[CmdletBinding()]
param(
    [string] $FixturePath,

    [string] $OutputPath = '',

    [string] $ConfigPath = (Join-Path $PSScriptRoot '..\config\wsl-box.yaml'),

    [switch] $WhatIfDry
)

$ErrorActionPreference = 'Stop'

function Test-IsWindows {
    return ($env:OS -eq 'Windows_NT') -or
        ($null -ne $PSVersionTable.Platform -and $PSVersionTable.Platform -eq 'Win32NT') -or
        [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
            [System.Runtime.InteropServices.OSPlatform]::Windows)
}

$script:AllowedAliases = @{
    manufacturer = @(
        'manufacturer', 'sys_vendor', 'vendor',
        'system_manufacturer', 'win32_computersystem.manufacturer'
    )
    product = @(
        'product', 'product_name', 'productname', 'model',
        'system_product_name', 'win32_computersystem.model'
    )
}

$script:HiddenKeys = @(
    'serial', 'serial_number', 'serialnumber', 'product_serial',
    'board_serial', 'chassis_serial', 'bios_serial', 'disk_serial', 'volume_serial',
    'uuid', 'product_uuid', 'board_uuid', 'chassis_uuid', 'machine_uuid', 'system_uuid',
    'mac', 'mac_address', 'macaddress',
    'cpu_id', 'processor_id', 'processorid',
    'machine_guid', 'machineguid',
    'device_instance_path', 'pnp_device_id', 'pnpdeviceid', 'instancename',
    'asset_tag', 'board_asset_tag', 'chassis_asset_tag'
)

function Normalize-Key([string] $Key) {
    return (($Key.ToLowerInvariant()) -replace '[^a-z0-9]+', '_').Trim('_')
}

function Classify-Key([string] $Key) {
    $n = Normalize-Key $Key
    foreach ($field in @('manufacturer', 'product')) {
        foreach ($alias in $script:AllowedAliases[$field]) {
            if ($n -eq (Normalize-Key $alias)) { return $field }
        }
    }
    if ($script:HiddenKeys -contains $n) { return 'forbidden' }
    foreach ($tok in @('serial', 'uuid', 'guid', 'mac')) {
        if ($n -eq $tok -or $n.StartsWith($tok + '_') -or $n.EndsWith('_' + $tok)) {
            return 'forbidden'
        }
    }
    return 'unknown'
}

function Get-FlatYamlMap([string] $Path) {
    $map = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*#' -or $line.Trim() -eq '') { continue }
        $m = [regex]::Match($line, '^\s*([A-Za-z0-9_.]+)\s*:\s*(.*?)\s*$')
        if (-not $m.Success) { continue }
        $k = $m.Groups[1].Value
        $v = $m.Groups[2].Value.Trim()
        if ($v.StartsWith('#') -or $v -in @('', '~', 'null', '[]', '|', '>')) { continue }
        if ($v.StartsWith('"') -and $v.EndsWith('"') -and $v.Length -ge 2) {
            $v = $v.Substring(1, $v.Length - 2)
        } elseif ($v.StartsWith("'") -and $v.EndsWith("'") -and $v.Length -ge 2) {
            $v = $v.Substring(1, $v.Length - 2)
        } else {
            $hash = $v.IndexOf(' #')
            if ($hash -ge 0) { $v = $v.Substring(0, $hash).Trim() }
        }
        $map[$k] = $v
    }
    return $map
}

function Sanitize-Inventory($Map) {
    $allowed = [ordered]@{ manufacturer = ''; product = '' }
    foreach ($k in $Map.Keys) {
        $kind = Classify-Key $k
        if ($kind -in @('manufacturer', 'product') -and -not [string]::IsNullOrWhiteSpace($Map[$k])) {
            $allowed[$kind] = [string]$Map[$k]
        }
    }
    return $allowed
}

function Assert-LooksLikeModel([string] $Field, [string] $Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return }
    if ($Value -match '(?i)[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}') {
        throw "host identity ${Field} looks like a UUID/GUID; refusing to write it"
    }
    if ($Value -match '(?i)(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}') {
        throw "host identity ${Field} looks like a MAC address; refusing to write it"
    }
}

function Write-AllowedYaml([string] $Path, $Allowed) {
    Assert-LooksLikeModel 'manufacturer' $Allowed.manufacturer
    Assert-LooksLikeModel 'product' $Allowed.product
    $man = $Allowed.manufacturer.Replace('\', '\\').Replace('"', '\"')
    $prod = $Allowed.product.Replace('\', '\\').Replace('"', '\"')
    $body = @"
# Sanitized by Export-ClaudeBoxHostIdentity.ps1 — manufacturer + product only.
# Hardware identifiers are never queried or written.
manufacturer: "$man"
product: "$prod"
"@
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    Set-Content -LiteralPath $Path -Value $body -Encoding utf8
}

Write-Host "[claude-box/wsl] Export-ClaudeBoxHostIdentity"

if (-not $OutputPath) {
    if ($env:CLAUDE_BOX_PREFIX) {
        $OutputPath = Join-Path $env:CLAUDE_BOX_PREFIX 'var/host-identity.allowed.yaml'
    } else {
        $repoGuess = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
        $OutputPath = Join-Path $repoGuess '.run/claude-box/var/host-identity.allowed.yaml'
    }
}

$allowed = $null

if ($FixturePath) {
    if (-not (Test-Path -LiteralPath $FixturePath)) {
        throw "Fixture not found: $FixturePath"
    }
    Write-Host "  Sanitizing fixture: $FixturePath"
    $raw = Get-FlatYamlMap $FixturePath
    $allowed = Sanitize-Inventory $raw
    $dropped = @($raw.Keys | Where-Object { (Classify-Key $_) -eq 'forbidden' })
    Write-Host "  Dropped $($dropped.Count) forbidden key(s); kept manufacturer/product only."
} elseif (Test-IsWindows) {
    # Query ONLY these two properties. Do not call Win32_BIOS, Win32_ComputerSystemProduct,
    # Win32_NetworkAdapter, Win32_DiskDrive, Win32_Processor, or the MachineGuid registry key.
    Write-Host "  Reading Win32_ComputerSystem Manufacturer, Model (no serials/UUIDs/MACs)"
    $cs = Get-CimInstance -ClassName Win32_ComputerSystem -Property Manufacturer, Model
    $allowed = [ordered]@{
        manufacturer = [string]$cs.Manufacturer
        product      = [string]$cs.Model
    }
} else {
    Write-Warning "Not Windows — pass -FixturePath to sanitize a raw inventory (dry-run)."
    Write-Host "OK (dry-run): would query only Win32_ComputerSystem.Manufacturer and Model."
    exit 0
}

Write-Host "  manufacturer=$($allowed.manufacturer)"
Write-Host "  product=$($allowed.product)"

if ($WhatIfDry) {
    Write-Host "WhatIf: would write $OutputPath"
    exit 0
}

Write-AllowedYaml $OutputPath $allowed
Write-Host "  Wrote $OutputPath"
