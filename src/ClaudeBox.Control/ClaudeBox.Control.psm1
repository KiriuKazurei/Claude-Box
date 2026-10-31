#Requires -Version 5.1
<#
.SYNOPSIS
  Claude Box 宿主机控制面（Hyper-V 编排）。

.DESCRIPTION
  M1：配置字段抽取、拒绝宿主 C:、New-ClaudeBoxVM（Gen2 + Internal）。
  M2：Set-ClaudeBoxGatewayUpstream 接到 Gateway 中继/日志。
  实际 Hyper-V cmdlet 仅在已启用 Hyper-V 的 Win11 上执行；Linux 上可语法解析。

.NOTES
  边界：不实现透明代理协议；不在宿主安装 Claude。
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-ClaudeBoxIsWindowsHost {
    <#
    .SYNOPSIS
      当前进程是否在 Windows 上（Hyper-V 控件前提）。
    #>
    return [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
        [System.Runtime.InteropServices.OSPlatform]::Windows
    )
}

function Get-ClaudeBoxYamlScalar {
    <#
    .SYNOPSIS
      从 YAML 文本中取「缩进块内」简单标量（M1 最小解析，非完整 YAML）。
    #>
    [CmdletBinding()]
    param(
        [AllowEmptyString()]
        [Parameter(Mandatory = $true)]
        [string] $Raw,
        [Parameter(Mandatory = $true)]
        [string] $Key,
        [string] $Default = ''
    )

    if ([string]::IsNullOrEmpty($Raw)) { return $Default }

    # Match lines like: key: value  or key: "value"  (not nested object starters with only key:)
    $pattern = '(?m)^\s*' + [regex]::Escape($Key) + '\s*:\s*(.+?)\s*$'
    $m = [regex]::Match($Raw, $pattern)
    if (-not $m.Success) {
        return $Default
    }
    $v = $m.Groups[1].Value.Trim()
    if ($v -eq '' -or $v -eq '|' -or $v -eq '>') {
        return $Default
    }
    if (($v.StartsWith('"') -and $v.EndsWith('"')) -or ($v.StartsWith("'") -and $v.EndsWith("'"))) {
        $v = $v.Substring(1, $v.Length - 2)
    }
    # Skip if looks like start of nested map accidentally captured empty — already handled
    return $v
}

function Get-ClaudeBoxYamlBool {
    [CmdletBinding()]
    param(
        [string] $Raw,
        [string] $Key,
        [bool] $Default = $false
    )

    if ([string]::IsNullOrEmpty($Raw)) { return $Default }
    $v = Get-ClaudeBoxYamlScalar -Raw $Raw -Key $Key -Default ''
    if ([string]::IsNullOrWhiteSpace($v)) { return $Default }
    switch -Regex ($v.ToLowerInvariant()) {
        '^(true|yes|1)$' { return $true }
        '^(false|no|0)$' { return $false }
        default { return $Default }
    }
}

function Get-ClaudeBoxVisibleDirPaths {
    <#
    .SYNOPSIS
      粗提取 visible_dirs 下的 host_path 列表。
    #>
    [CmdletBinding()]
    param(
        [AllowEmptyString()]
        [Parameter(Mandatory = $true)]
        [string] $Raw
    )

    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($m in [regex]::Matches($Raw, '(?m)^\s*-\s*host_path:\s*(.+?)\s*$')) {
        $v = $m.Groups[1].Value.Trim()
        if (($v.StartsWith('"') -and $v.EndsWith('"')) -or ($v.StartsWith("'") -and $v.EndsWith("'"))) {
            $v = $v.Substring(1, $v.Length - 2)
        }
        # YAML double-escaped backslashes in example: D:\\Projects -> D:\Projects
        $v = $v -replace '\\\\', '\'
        if (-not [string]::IsNullOrWhiteSpace($v)) {
            $paths.Add($v)
        }
    }
    # Flat string[]; avoid unary-comma nesting when caller wraps with @()
    return [string[]]$paths.ToArray()
}


function Get-ClaudeBoxGatewayBlocklist {
    <#
    .SYNOPSIS
      粗提取 gateway.blocklist 下的域名列表。
    #>
    [CmdletBinding()]
    param(
        [AllowEmptyString()]
        [Parameter(Mandatory = $true)]
        [string] $Raw
    )

    $items = New-Object System.Collections.Generic.List[string]
    $inList = $false
    foreach ($line in ($Raw -split '\r?\n')) {
        if ($line -match '^\s*blocklist:\s*$') {
            $inList = $true
            continue
        }
        if ($inList) {
            if ($line -match '^\s*-\s*(.+?)\s*$') {
                $v = $Matches[1].Trim()
                if (($v.StartsWith('"') -and $v.EndsWith('"')) -or ($v.StartsWith("'") -and $v.EndsWith("'"))) {
                    $v = $v.Substring(1, $v.Length - 2)
                }
                if (-not [string]::IsNullOrWhiteSpace($v)) { $items.Add($v) }
                continue
            }
            # left the list
            if ($line -match '^\s*\S') { break }
        }
    }
    return [string[]]$items.ToArray()
}

function Get-ClaudeBoxConfig {
    <#
    .SYNOPSIS
      加载配置：返回 Path/Raw + M1 字段抽取 Parsed。

    .NOTES
      TODO(M2+): 引入 powershell-yaml 并对照 JSON schema 完整校验。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Config not found: $Path"
    }

    $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    $mem = 0
    [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $raw -Key 'memory_mb' -Default '8192'), [ref]$mem)
    if ($mem -lt 2048) { $mem = 8192 }
    $cpus = 0
    [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $raw -Key 'cpus' -Default '2'), [ref]$cpus)
    if ($cpus -lt 1) { $cpus = 2 }
    $gen = 0
    [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $raw -Key 'generation' -Default '2'), [ref]$gen)
    if ($gen -ne 1 -and $gen -ne 2) { $gen = 2 }

    $vmBlock = ''
    $vmMatch = [regex]::Match($raw, '(?ms)^\s*vm:\s*\r?\n(.*?)(?=^\s*gateway:|\z)')
    if ($vmMatch.Success) { $vmBlock = $vmMatch.Groups[1].Value }

    $gwBlock = ''
    $gwMatch = [regex]::Match($raw, '(?ms)^\s*gateway:\s*\r?\n(.*?)(?=^\s*visible_dirs:|^\s*identity:|^\s*guest:|^\s*features:|\z)')
    if ($gwMatch.Success) { $gwBlock = $gwMatch.Groups[1].Value }

    $logBlock = ''
    # logging children are deeper-indented lines until a peer key at gateway indent (4 spaces) or end
    $logMatch = [regex]::Match($gwBlock, '(?ms)^\s*logging:\s*\r?\n((?:^[ \t]{6,}.*\r?\n?)*)')
    if ($logMatch.Success) { $logBlock = $logMatch.Groups[1].Value }

    $idBlock = ''
    $idMatch = [regex]::Match($raw, '(?ms)^\s*identity:\s*\r?\n(.*?)(?=^\s*guest:|^\s*features:|\z)')
    if ($idMatch.Success) { $idBlock = $idMatch.Groups[1].Value }

    $fakeBlock = ''
    $fakeMatch = [regex]::Match($raw, '(?ms)^\s*fake_env:\s*\r?\n((?:^[ \t]{6,}.*\r?\n?)*)')
    if ($fakeMatch.Success) { $fakeBlock = $fakeMatch.Groups[1].Value }

    $featBlock = ''
    $featMatch = [regex]::Match($raw, '(?ms)^\s*features:\s*\r?\n(.*?)\z')
    if ($featMatch.Success) { $featBlock = $featMatch.Groups[1].Value }

    $vmName = Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'name' -Default 'ClaudeBox-Win'
    $mem2 = 0
    [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'memory_mb' -Default "$mem"), [ref]$mem2)
    if ($mem2 -ge 2048) { $mem = $mem2 }
    $cpus2 = 0
    [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'cpus' -Default "$cpus"), [ref]$cpus2)
    if ($cpus2 -ge 1) { $cpus = $cpus2 }
    $gen2 = 0
    [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'generation' -Default "$gen"), [ref]$gen2)
    if ($gen2 -eq 1 -or $gen2 -eq 2) { $gen = $gen2 }

    $parsed = [pscustomobject]@{
        ApiVersion = Get-ClaudeBoxYamlScalar -Raw $raw -Key 'apiVersion' -Default ''
        VmName     = $vmName
        MemoryMb   = $mem
        Cpus       = $cpus
        Generation = $gen
        VhdPath    = Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'vhd_path' -Default ''
        IsoPath    = Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'iso_path' -Default ''
        TemplateVhdx = Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'template_vhdx' -Default ''
        SwitchName = Get-ClaudeBoxYamlScalar -Raw $vmBlock -Key 'switch_name' -Default 'ClaudeBox-Internal'
        GatewayHttp = Get-ClaudeBoxYamlScalar -Raw $gwBlock -Key 'http' -Default ''
        GatewaySocks5 = Get-ClaudeBoxYamlScalar -Raw $gwBlock -Key 'socks5' -Default ''
        GatewayPrimary = Get-ClaudeBoxYamlScalar -Raw $gwBlock -Key 'primary' -Default 'http'
        ListenIp   = Get-ClaudeBoxYamlScalar -Raw $gwBlock -Key 'listen_ip' -Default '192.168.87.1'
        Subnet     = Get-ClaudeBoxYamlScalar -Raw $gwBlock -Key 'subnet' -Default '192.168.87.0/24'
        LoggingEnabled = Get-ClaudeBoxYamlBool -Raw $logBlock -Key 'enabled' -Default $true
        LoggingPath = Get-ClaudeBoxYamlScalar -Raw $logBlock -Key 'path' -Default ''
        LoggingMaxMb = $(
            $mb = 64
            [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $logBlock -Key 'max_mb' -Default '64'), [ref]$mb)
            if ($mb -lt 1) { 64 } else { $mb }
        )
        ListenPort = $(
            $lp = 3080
            [void][int]::TryParse((Get-ClaudeBoxYamlScalar -Raw $gwBlock -Key 'listen_port' -Default '3080'), [ref]$lp)
            if ($lp -lt 1 -or $lp -gt 65535) { 3080 } else { $lp }
        )
        UpstreamNotes = Get-ClaudeBoxYamlScalar -Raw $gwBlock -Key 'upstream_notes' -Default ''
        Blocklist = @(Get-ClaudeBoxGatewayBlocklist -Raw $gwBlock)
        Hostname   = Get-ClaudeBoxYamlScalar -Raw $idBlock -Key 'hostname' -Default 'claude-box'
        Timezone   = Get-ClaudeBoxYamlScalar -Raw $idBlock -Key 'timezone' -Default 'UTC'
        Locale     = Get-ClaudeBoxYamlScalar -Raw $idBlock -Key 'locale' -Default 'en-US'
        ExcludeHostSystemDrive = Get-ClaudeBoxYamlBool -Raw $featBlock -Key 'exclude_host_system_drive' -Default $true
        FullDisk   = Get-ClaudeBoxYamlBool -Raw $featBlock -Key 'full_disk' -Default $false
        FakeEnvEnabled = Get-ClaudeBoxYamlBool -Raw $fakeBlock -Key 'enabled' -Default $true
        Workgroup = Get-ClaudeBoxYamlScalar -Raw $fakeBlock -Key 'workgroup' -Default 'CLAUDEBOX'
        MinimalDesktop = Get-ClaudeBoxYamlBool -Raw $fakeBlock -Key 'minimal_desktop' -Default $true
        VisibleDirs = @(Get-ClaudeBoxVisibleDirPaths -Raw $raw)
        MountMethod = $(
            $mm = ''
            $mountBlock = ''
            $mountMatch = [regex]::Match($raw, '(?ms)^\s*mount:\s*\r?\n((?:^[ \t]{2,}.*\r?\n?)*)')
            if ($mountMatch.Success) { $mountBlock = $mountMatch.Groups[1].Value }
            $mm = Get-ClaudeBoxYamlScalar -Raw $mountBlock -Key 'method' -Default 'smb_internal'
            if ([string]::IsNullOrWhiteSpace($mm)) { 'smb_internal' } else { $mm }
        )
        GuestMountRoot = $(
            $mountBlock2 = ''
            $mountMatch2 = [regex]::Match($raw, '(?ms)^\s*mount:\s*\r?\n((?:^[ \t]{2,}.*\r?\n?)*)')
            if ($mountMatch2.Success) { $mountBlock2 = $mountMatch2.Groups[1].Value }
            Get-ClaudeBoxYamlScalar -Raw $mountBlock2 -Key 'guest_root' -Default 'Z:'
        )
        HostSharePrefix = $(
            $mountBlock3 = ''
            $mountMatch3 = [regex]::Match($raw, '(?ms)^\s*mount:\s*\r?\n((?:^[ \t]{2,}.*\r?\n?)*)')
            if ($mountMatch3.Success) { $mountBlock3 = $mountMatch3.Groups[1].Value }
            Get-ClaudeBoxYamlScalar -Raw $mountBlock3 -Key 'share_prefix' -Default 'ClaudeBox'
        )
    }

    return [pscustomobject]@{
        Path    = (Resolve-Path -LiteralPath $Path).Path
        Raw     = $raw
        Parsed  = $parsed
        Warning = 'Minimal YAML field extract; replace with full parser + schema later.'
    }
}

function Test-ClaudeBoxHostSystemDrivePath {
    <#
    .SYNOPSIS
      判断路径是否表示宿主系统盘根 / 整盘 C:（应被拒绝挂载）。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $HostPath
    )

    $p = $HostPath.Trim().TrimEnd('\', '/')
    if ($p -match '^[cC]:$') { return $true }
    if ($p -match '^[cC]:[/\\]$') { return $true }
    # Entire drive share forms
    if ($p -match '^[cC]:[/\\]\*$') { return $true }
    return $false
}

function Assert-ClaudeBoxVisibleDirsSafe {
    <#
    .SYNOPSIS
      在 exclude_host_system_drive 时拒绝宿主 C: 整盘挂入。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        $Parsed
    )

    $exclude = $true
    if ($null -ne $Parsed.ExcludeHostSystemDrive) {
        $exclude = [bool]$Parsed.ExcludeHostSystemDrive
    }

    if (-not $exclude) {
        Write-Warning 'exclude_host_system_drive=false: host C: may be mountable; fake-env guarantees weaken.'
        return
    }

    foreach ($d in @($Parsed.VisibleDirs)) {
        if ($null -eq $d) { continue }
        foreach ($one in @($d)) {
            $pathStr = [string]$one
            if ([string]::IsNullOrWhiteSpace($pathStr)) { continue }
            if (Test-ClaudeBoxHostSystemDrivePath -HostPath $pathStr) {
                throw "Refusing to mount host system drive into guest: '$pathStr' (exclude_host_system_drive=true). Use a project dir or non-C: data drive."
            }
        }
    }
}

function Test-ClaudeBoxHyperVReady {
    <#
    .SYNOPSIS
      检查 Hyper-V 功能与模块是否可用。
    #>
    [CmdletBinding()]
    param()

    if (-not (Test-ClaudeBoxIsWindowsHost)) {
        return [pscustomobject]@{
            Ready   = $false
            Reason  = 'Not running on Windows; Hyper-V checks skipped.'
            Details = @{}
        }
    }

    $details = @{}
    try {
        $mod = Get-Module -ListAvailable -Name Hyper-V | Select-Object -First 1
        $details['HyperVModule'] = if ($mod) { "$($mod.Version)" } else { 'missing' }
        if (-not $mod) {
            return [pscustomobject]@{
                Ready   = $false
                Reason  = 'Hyper-V PowerShell module not found.'
                Details = $details
            }
        }
        Import-Module Hyper-V -ErrorAction Stop
        $null = Get-VMHost -ErrorAction Stop
        $details['VMHost'] = 'ok'
        return [pscustomobject]@{
            Ready   = $true
            Reason  = 'Hyper-V module loaded; Get-VMHost succeeded.'
            Details = $details
        }
    } catch {
        $details['Error'] = $_.Exception.Message
        return [pscustomobject]@{
            Ready   = $false
            Reason  = "Hyper-V probe failed: $($_.Exception.Message)"
            Details = $details
        }
    }
}

function New-ClaudeBoxInternalSwitch {
    <#
    .SYNOPSIS
      创建或复用 Internal 虚拟交换机，并尝试绑定 listen IP。
    #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [string] $Name = 'ClaudeBox-Internal',
        [string] $ListenIp = '192.168.87.1',
        [int] $PrefixLength = 24
    )

    if (-not (Test-ClaudeBoxIsWindowsHost)) {
        throw 'New-ClaudeBoxInternalSwitch requires Windows + Hyper-V.'
    }

    Import-Module Hyper-V -ErrorAction Stop

    $existing = Get-VMSwitch -Name $Name -ErrorAction SilentlyContinue
    if (-not $existing) {
        if ($PSCmdlet.ShouldProcess($Name, 'New-VMSwitch Internal')) {
            New-VMSwitch -SwitchName $Name -SwitchType Internal | Out-Null
            Write-Host "Created switch: $Name"
        }
    } else {
        Write-Host "Switch already exists: $Name"
    }

    # Assign IP to vEthernet adapter
    $adapterName = "vEthernet ($Name)"
    $adapter = Get-NetAdapter -Name $adapterName -ErrorAction SilentlyContinue
    if (-not $adapter) {
        Write-Warning "Adapter '$adapterName' not found yet; assign $ListenIp/$PrefixLength manually."
        return
    }

    $hasIp = Get-NetIPAddress -InterfaceAlias $adapterName -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -eq $ListenIp }
    if ($hasIp) {
        Write-Host "IP $ListenIp already on $adapterName"
        return
    }

    if ($PSCmdlet.ShouldProcess($adapterName, "New-NetIPAddress $ListenIp/$PrefixLength")) {
        try {
            New-NetIPAddress -InterfaceAlias $adapterName -IPAddress $ListenIp -PrefixLength $PrefixLength -ErrorAction Stop | Out-Null
            Write-Host "Assigned $ListenIp/$PrefixLength to $adapterName"
        } catch {
            Write-Warning "Failed to assign IP: $($_.Exception.Message)"
        }
    }
}

function New-ClaudeBoxVM {
    <#
    .SYNOPSIS
      按配置创建 Hyper-V Gen2 Windows 访客（空壳；ISO/模板占位）。

    .PARAMETER ConfigPath
      配置文件路径。

    .PARAMETER IsoPath
      覆盖配置中的 iso_path（Windows 安装 ISO）。

    .PARAMETER TemplateVhdx
      覆盖配置中的 template_vhdx（已有系统盘模板）。

    .PARAMETER Force
      若同名 VM 已存在则先报错；Force 时删除（危险，需确认）。

    .NOTES
      - 只挂 Internal 交换机（来自配置 switch_name）。
      - 不在此函数挂宿主 C:；挂载走 Mount-ClaudeBoxVisibleDirs。
      - 无 ISO/模板时仍创建 VM + 空白 VHDX，供手工装系统（M1）。
    #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath,
        [string] $IsoPath = '',
        [string] $TemplateVhdx = '',
        [switch] $Force
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    $p = $cfg.Parsed

    Assert-ClaudeBoxVisibleDirsSafe -Parsed $p

    if (-not (Test-ClaudeBoxIsWindowsHost)) {
        throw 'New-ClaudeBoxVM requires Windows + Hyper-V. Config parse and C: guard OK on this host.'
    }

    $ready = Test-ClaudeBoxHyperVReady
    if (-not $ready.Ready) {
        throw "Hyper-V not ready: $($ready.Reason)"
    }

    Import-Module Hyper-V -ErrorAction Stop

    $vmName = $p.VmName
    $memoryBytes = [uint64]$p.MemoryMb * 1MB
    $switchName = if ($p.SwitchName) { $p.SwitchName } else { 'ClaudeBox-Internal' }
    $generation = [int]$p.Generation
    if ($generation -ne 2) {
        Write-Warning "MVP recommends Generation 2; config has $generation — proceeding as specified."
    }

    $iso = if ($IsoPath) { $IsoPath } else { $p.IsoPath }
    $template = if ($TemplateVhdx) { $TemplateVhdx } else { $p.TemplateVhdx }

    $existing = Get-VM -Name $vmName -ErrorAction SilentlyContinue
    if ($existing) {
        if (-not $Force) {
            throw "VM '$vmName' already exists. Pass -Force to remove and recreate (destructive)."
        }
        if ($PSCmdlet.ShouldProcess($vmName, 'Remove-VM -Force')) {
            if ($existing.State -ne 'Off') {
                Stop-VM -Name $vmName -Force -TurnOff -ErrorAction SilentlyContinue
            }
            Remove-VM -Name $vmName -Force
        }
    }

    # Ensure switch
    if (-not (Get-VMSwitch -Name $switchName -ErrorAction SilentlyContinue)) {
        New-ClaudeBoxInternalSwitch -Name $switchName -ListenIp $p.ListenIp
    }

    # Disk paths
    $diskRoot = Join-Path $env:LOCALAPPDATA 'ClaudeBox\disks'
    if (-not (Test-Path -LiteralPath $diskRoot)) {
        New-Item -ItemType Directory -Path $diskRoot -Force | Out-Null
    }

    $vhdPath = $p.VhdPath
    if ([string]::IsNullOrWhiteSpace($vhdPath)) {
        $vhdPath = Join-Path $diskRoot ($vmName + '.vhdx')
    }

    if ($PSCmdlet.ShouldProcess($vmName, "New-VM Gen$generation memory=$($p.MemoryMb)MB switch=$switchName")) {
        if ($template -and (Test-Path -LiteralPath $template)) {
            Write-Host "Using template VHDX: $template"
            Copy-Item -LiteralPath $template -Destination $vhdPath -Force
        } elseif (-not (Test-Path -LiteralPath $vhdPath)) {
            # 40GB dynamic blank disk for manual OS install
            New-VHD -Path $vhdPath -SizeBytes 40GB -Dynamic | Out-Null
            Write-Host "Created blank VHDX: $vhdPath"
        }

        $vm = New-VM -Name $vmName -MemoryStartupBytes $memoryBytes -Generation $generation -VHDPath $vhdPath -SwitchName $switchName
        Set-VM -Name $vmName -ProcessorCount ([int]$p.Cpus) -CheckpointType Disabled -ErrorAction SilentlyContinue

        # Strip any extra NICs that might land on Default Switch — keep only our switch
        Get-VMNetworkAdapter -VMName $vmName | ForEach-Object {
            if ($_.SwitchName -ne $switchName) {
                Write-Warning "Removing NIC on unexpected switch '$($_.SwitchName)'"
                Remove-VMNetworkAdapter -VMNetworkAdapter $_
            }
        }
        if (-not (Get-VMNetworkAdapter -VMName $vmName -ErrorAction SilentlyContinue)) {
            Add-VMNetworkAdapter -VMName $vmName -SwitchName $switchName | Out-Null
        }

        if ($iso -and (Test-Path -LiteralPath $iso)) {
            Add-VMDvdDrive -VMName $vmName -Path $iso -ErrorAction SilentlyContinue
            $dvd = Get-VMDvdDrive -VMName $vmName | Select-Object -First 1
            if ($dvd) {
                Set-VMFirmware -VMName $vmName -FirstBootDevice $dvd -ErrorAction SilentlyContinue
            }
            Write-Host "Attached ISO: $iso"
        } else {
            Write-Host 'No ISO/template usable — VM disk ready for manual attach/install (M1 empty guest).'
        }

        Write-Host "Created VM '$vmName' (Gen $generation) on switch '$switchName'."
        Write-Host 'Mounts: use Mount-ClaudeBoxVisibleDirs (host C: excluded by default).'
        Write-Host 'Fake env: apply guest scripts after OS is up (Initialize-ClaudeBoxGuest.ps1).'
        return $vm
    }
}

function Start-ClaudeBoxVM {
    <#
    .SYNOPSIS
      启动命名 Claude Box VM，并可选打开会话窗口。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Name,
        [switch] $Connect
    )

    if (-not (Test-ClaudeBoxIsWindowsHost)) {
        throw 'Start-ClaudeBoxVM requires Windows + Hyper-V.'
    }

    Import-Module Hyper-V -ErrorAction Stop
    Start-VM -Name $Name
    if ($Connect) {
        $vmconnect = Join-Path $env:windir 'System32\vmconnect.exe'
        if (Test-Path -LiteralPath $vmconnect) {
            Start-Process -FilePath $vmconnect -ArgumentList @('localhost', $Name)
        } else {
            Write-Warning 'vmconnect.exe not found; open Hyper-V Manager Connect manually (Enhanced Session).'
        }
    }
}

function Stop-ClaudeBoxVM {
    <#
    .SYNOPSIS
      停止命名 Claude Box VM。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Name,
        [switch] $Force
    )

    if (-not (Test-ClaudeBoxIsWindowsHost)) {
        throw 'Stop-ClaudeBoxVM requires Windows + Hyper-V.'
    }

    Import-Module Hyper-V -ErrorAction Stop
    if ($Force) {
        Stop-VM -Name $Name -Force -TurnOff
    } else {
        Stop-VM -Name $Name
    }
}

function Get-ClaudeBoxVisibleDirEntries {
    <#
    .SYNOPSIS
      提取 visible_dirs 条目（host_path + read_only）。
    #>
    [CmdletBinding()]
    param(
        [AllowEmptyString()]
        [Parameter(Mandatory = $true)]
        [string] $Raw
    )

    $entries = New-Object System.Collections.Generic.List[object]
    $lines = $Raw -split '\r?\n'
    $i = 0
    while ($i -lt $lines.Count) {
        $line = $lines[$i]
        if ($line -match '^\s*-\s*host_path:\s*(.+?)\s*$') {
            $v = $Matches[1].Trim()
            if (($v.StartsWith('"') -and $v.EndsWith('"')) -or ($v.StartsWith("'") -and $v.EndsWith("'"))) {
                $v = $v.Substring(1, $v.Length - 2)
            }
            $v = $v -replace '\\\\', '\'
            $ro = $false
            if (($i + 1) -lt $lines.Count -and $lines[$i + 1] -match '^\s*read_only:\s*(.+?)\s*$') {
                $rv = $Matches[1].Trim().ToLowerInvariant()
                $ro = ($rv -eq 'true' -or $rv -eq 'yes' -or $rv -eq '1')
            }
            if (-not [string]::IsNullOrWhiteSpace($v)) {
                $entries.Add([pscustomobject]@{ HostPath = $v; ReadOnly = $ro })
            }
        }
        $i++
    }
    return @($entries.ToArray())
}

function New-ClaudeBoxMountPlan {
    <#
    .SYNOPSIS
      根据配置生成可见目录挂载计划（默认 smb_internal；拒绝宿主 C:）。

    .DESCRIPTION
      主策略 **smb_internal**：宿主机在 ClaudeBox-Internal 网上为每个可见目录建 SMB 共享，
      访客用 net use 映射盘符。备选：
      - copy_vmfile：Guest Services Copy-VMFile（适合推脚本，不适合大目录持续同步）
      - enhanced_session_manual：vmconnect 勾选驱动器（控件只出说明，不自动配置）
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    Assert-ClaudeBoxVisibleDirsSafe -Parsed $cfg.Parsed

    $method = [string]$cfg.Parsed.MountMethod
    if ([string]::IsNullOrWhiteSpace($method)) { $method = 'smb_internal' }
    $method = $method.ToLowerInvariant()
    $prefix = [string]$cfg.Parsed.HostSharePrefix
    if ([string]::IsNullOrWhiteSpace($prefix)) { $prefix = 'ClaudeBox' }
    $guestRoot = [string]$cfg.Parsed.GuestMountRoot
    if ([string]::IsNullOrWhiteSpace($guestRoot)) { $guestRoot = 'Z:' }
    $startLetter = $guestRoot.Substring(0, 1).ToUpperInvariant()
    if ($startLetter -notmatch '^[D-Z]$') { $startLetter = 'Z' }

    $listenIp = [string]$cfg.Parsed.ListenIp
    if ([string]::IsNullOrWhiteSpace($listenIp)) { $listenIp = '192.168.87.1' }

    $entriesIn = @(Get-ClaudeBoxVisibleDirEntries -Raw $cfg.Raw)
    if ($entriesIn.Count -eq 0 -and $cfg.Parsed.VisibleDirs) {
        foreach ($p in @($cfg.Parsed.VisibleDirs)) {
            $entriesIn += [pscustomobject]@{ HostPath = [string]$p; ReadOnly = $false }
        }
    }

    $letterCode = [int][char]$startLetter
    $planEntries = New-Object System.Collections.Generic.List[object]
    $idx = 0
    foreach ($e in $entriesIn) {
        $hp = [string]$e.HostPath
        if ([string]::IsNullOrWhiteSpace($hp)) { continue }
        if ($cfg.Parsed.ExcludeHostSystemDrive -and (Test-ClaudeBoxHostSystemDrivePath -HostPath $hp)) {
            throw "Refusing host system drive in mount plan: $hp"
        }
        $norm = $hp.TrimEnd('\', '/').Replace('/', '\')
        $leaf = ($norm -split '[\\/]')[-1]
        if ([string]::IsNullOrWhiteSpace($leaf) -or $leaf -match '^[A-Za-z]:$') { $leaf = "dir$idx" }
        $safe = ($leaf -replace '[^A-Za-z0-9_\-]', '_')
        if ($safe.Length -gt 40) { $safe = $safe.Substring(0, 40) }
        $shareName = "{0}_{1}" -f $prefix, $safe
        $drive = '{0}:' -f [char]$letterCode
        # Prefer high letters descending if starting at Z
        if ($startLetter -eq 'Z') {
            $drive = '{0}:' -f [char]([int][char]'Z' - $idx)
        } else {
            $drive = '{0}:' -f [char]($letterCode + $idx)
        }
        $unc = '\\{0}\{1}' -f $listenIp, $shareName
        $planEntries.Add([pscustomobject]@{
            name       = $safe
            host_path  = $hp
            read_only  = [bool]$e.ReadOnly
            share_name = $shareName
            guest_unc  = $unc
            guest_drive = $drive
        })
        $idx++
    }

    return [pscustomobject]@{
        version                    = 1
        method                     = $method
        generated_at               = (Get-Date).ToString('o')
        exclude_host_system_drive  = [bool]$cfg.Parsed.ExcludeHostSystemDrive
        listen_ip                  = $listenIp
        vm_name                    = $cfg.Parsed.VmName
        guest_script               = 'src/ClaudeBox.Guest/Mount-ClaudeBoxGuestShares.ps1'
        notes                      = @(
            'Primary: SMB shares on host Internal IP; guest maps drive letters.',
            'Enhanced Session drive redirect is manual fallback (vmconnect).',
            'Copy-VMFile is for pushing this plan/scripts, not large live trees.'
        )
        entries                    = $planEntries.ToArray()
        verified                   = $false
    }
}

function Save-ClaudeBoxMountPlan {
    <#
    .SYNOPSIS
      将挂载计划写成 JSON（默认 %LOCALAPPDATA%\ClaudeBox\mounts\mount-plan.json）。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        $Plan,
        [string] $Path = ''
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        $base = $env:LOCALAPPDATA
        if ([string]::IsNullOrWhiteSpace($base)) {
            $base = Join-Path ([System.IO.Path]::GetTempPath()) 'ClaudeBox'
        } else {
            $base = Join-Path $base 'ClaudeBox'
        }
        $dir = Join-Path $base 'mounts'
        $Path = Join-Path $dir 'mount-plan.json'
    }
    $dirName = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dirName)) {
        New-Item -ItemType Directory -Path $dirName -Force | Out-Null
    }
    ($Plan | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $Path -Encoding UTF8
    return $Path
}

function Publish-ClaudeBoxGuestBundle {
    <#
    .SYNOPSIS
      把 Guest 脚本 + mount-plan 准备成可拷进访客的目录；可选 Copy-VMFile（Win11）。

    .NOTES
      Copy-VMFile 需启用 Guest Service Interface；真机待验证。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath,
        [string] $PlanPath = '',
        [string] $StagingDir = '',
        [switch] $CopyToVm
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    $guestSrc = Join-Path (Split-Path -Parent $PSScriptRoot) 'ClaudeBox.Guest'
    if (-not $StagingDir) {
        $base = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'ClaudeBox' } else { Join-Path ([IO.Path]::GetTempPath()) 'ClaudeBox' }
        $StagingDir = Join-Path $base 'guest-bundle'
    }
    if (Test-Path -LiteralPath $StagingDir) {
        Remove-Item -LiteralPath $StagingDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
    Copy-Item -Path (Join-Path $guestSrc '*') -Destination $StagingDir -Recurse -Force
    if (-not $PlanPath) {
        $plan = New-ClaudeBoxMountPlan -ConfigPath $ConfigPath
        $PlanPath = Save-ClaudeBoxMountPlan -Plan $plan
    }
    Copy-Item -LiteralPath $PlanPath -Destination (Join-Path $StagingDir 'mount-plan.json') -Force

    $result = [pscustomobject]@{
        StagingDir = $StagingDir
        PlanPath   = $PlanPath
        VmName     = $cfg.Parsed.VmName
        CopiedToVm = $false
        Verified   = $false
        Note       = 'Copy folder into guest via Enhanced Session / SMB / Copy-VMFile'
    }

    if ($CopyToVm) {
        if (-not (Test-ClaudeBoxIsWindowsHost)) {
            Write-Warning 'CopyToVm ignored: not Windows host'
            return $result
        }
        try {
            Enable-VMIntegrationService -VMName $cfg.Parsed.VmName -Name 'Guest Service Interface' -ErrorAction SilentlyContinue
            $dest = 'C:\ClaudeBox'
            # Copy-VMFile copies a file; create a zip for one-shot
            $zip = Join-Path (Split-Path $StagingDir) 'guest-bundle.zip'
            if (Test-Path $zip) { Remove-Item $zip -Force }
            Compress-Archive -Path (Join-Path $StagingDir '*') -DestinationPath $zip -Force
            Copy-VMFile -Name $cfg.Parsed.VmName -SourcePath $zip -DestinationPath ($dest + '\guest-bundle.zip') -FileSource Host -CreateFullPath -ErrorAction Stop
            $result.CopiedToVm = $true
            $result.Note = "Copied zip to guest $dest\guest-bundle.zip — expand inside guest (待验证)"
        } catch {
            Write-Warning "Copy-VMFile failed: $($_.Exception.Message)"
            $result.Note = $_.Exception.Message
        }
    }
    return $result
}

function Mount-ClaudeBoxVisibleDirs {
    <#
    .SYNOPSIS
      将配置中的 visible_dirs 暴露进访客；默认拒绝宿主 C:。

    .DESCRIPTION
      M3 主路径：生成 SMB 挂载计划并在宿主机创建共享（-ApplyShares）。
      访客侧执行 Mount-ClaudeBoxGuestShares.ps1 -PlanPath mount-plan.json -Apply。

    .PARAMETER ApplyShares
      在 Windows 宿主机上 New-SmbShare（需管理员）；Linux / 无权限则只写计划。
    #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath,
        [string] $PlanOutPath = '',
        [switch] $ApplyShares,
        [switch] $PublishBundle
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    Assert-ClaudeBoxVisibleDirsSafe -Parsed $cfg.Parsed

    if ($cfg.Parsed.FullDisk) {
        Write-Warning 'features.full_disk=true: environment spoofing weakened; host C: still blocked if exclude_host_system_drive=true.'
    }

    $plan = New-ClaudeBoxMountPlan -ConfigPath $ConfigPath
    $saved = Save-ClaudeBoxMountPlan -Plan $plan -Path $PlanOutPath
    Write-Host ("Mount plan method={0} entries={1} -> {2}" -f $plan.method, @($plan.entries).Count, $saved)

    if (@($plan.entries).Count -eq 0) {
        Write-Host 'No visible_dirs configured; guest will not see host project files.'
        return [pscustomobject]@{ PlanPath = $saved; Plan = $plan; SharesApplied = $false; Verified = $false }
    }

    foreach ($e in @($plan.entries)) {
        Write-Host ("  - {0} => {1} ({2}) ro={3}" -f $e.host_path, $e.guest_drive, $e.guest_unc, $e.read_only)
    }

    $sharesApplied = $false
    if ($ApplyShares) {
        if (-not (Test-ClaudeBoxIsWindowsHost)) {
            Write-Warning 'ApplyShares: not Windows — plan only (dry-run skeleton).'
        } elseif ($plan.method -ne 'smb_internal') {
            Write-Warning "ApplyShares only implements smb_internal; method=$($plan.method)"
            if ($plan.method -eq 'enhanced_session_manual') {
                Write-Host 'Open vmconnect → Enhanced Session → Local resources → Drives; do not redirect host C:.'
            }
        } else {
            foreach ($e in @($plan.entries)) {
                $hp = $e.host_path
                if (-not (Test-Path -LiteralPath $hp)) {
                    Write-Warning "Host path missing (share skipped): $hp"
                    continue
                }
                if ($PSCmdlet.ShouldProcess($e.share_name, "New-SmbShare $hp")) {
                    try {
                        if (Get-Command Get-SmbShare -ErrorAction SilentlyContinue) {
                            $existing = Get-SmbShare -Name $e.share_name -ErrorAction SilentlyContinue
                            if ($existing) {
                                Write-Host ("Share exists: {0}" -f $e.share_name)
                            } else {
                                New-SmbShare -Name $e.share_name -Path $hp -FullAccess 'Everyone' -ErrorAction Stop | Out-Null
                                Write-Host ("Created SMB share {0} -> {1}" -f $e.share_name, $hp)
                            }
                            if ($e.read_only -and (Get-Command Grant-SmbShareAccess -ErrorAction SilentlyContinue)) {
                                # Best-effort: revoke change rights later; MVP FullAccess + guest discipline
                                Write-Host 'read_only=true: prefer guest mapping discipline; tighten SMB ACL in M4.'
                            }
                            $sharesApplied = $true
                        } else {
                            Write-Warning 'SMB cmdlets unavailable; install FS-SMBShare or create share manually.'
                        }
                    } catch {
                        Write-Warning "New-SmbShare $($e.share_name): $($_.Exception.Message)"
                    }
                }
            }
        }
    } else {
        Write-Host 'Shares not applied (pass -ApplyShares on Win11 admin). Guest: Mount-ClaudeBoxGuestShares.ps1 -Apply'
    }

    $bundle = $null
    if ($PublishBundle) {
        $bundle = Publish-ClaudeBoxGuestBundle -ConfigPath $ConfigPath -PlanPath $saved
        Write-Host ("Guest bundle staged: {0}" -f $bundle.StagingDir)
    }

    return [pscustomobject]@{
        PlanPath      = $saved
        Plan          = $plan
        SharesApplied = $sharesApplied
        Bundle        = $bundle
        Verified      = $false
        Note          = 'Hyper-V SMB/Copy-VMFile 待验证'
    }
}


function Set-ClaudeBoxGatewayUpstream {
    <#
    .SYNOPSIS
      把配置中的 gateway 上游与日志选项应用到宿主侧网关（调用 ClaudeBox.Gateway）。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath,
        [switch] $SkipProcessStart,
        [switch] $WriteStartupLog
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    $p = $cfg.Parsed
    Write-Host "Gateway upstream http=$($p.GatewayHttp) socks5=$($p.GatewaySocks5) primary=$($p.GatewayPrimary)"
    Write-Host "ListenIp=$($p.ListenIp) ListenPort=$($p.ListenPort) logging.enabled=$($p.LoggingEnabled) path=$($p.LoggingPath)"
    Write-Host 'Clash TUN note: point http/socks5 at host Clash ports; do NOT give guest External switch.'

    $gwMod = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) 'ClaudeBox.Gateway') 'ClaudeBox.Gateway.psd1'
    if (-not (Test-Path -LiteralPath $gwMod)) {
        throw "ClaudeBox.Gateway module not found near Control: $gwMod"
    }
    Import-Module $gwMod -Force
    $runtime = ConvertTo-ClaudeBoxGatewayRuntimeConfig -Parsed $p
    return Start-ClaudeBoxGateway -Runtime $runtime -SkipProcessStart:$SkipProcessStart -WriteStartupLog:$WriteStartupLog
}

function Get-ClaudeBoxFakeEnvPlan {
    <#
    .SYNOPSIS
      从配置生成访客假环境步骤清单（供注入 Guest 初始化）。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    $p = $cfg.Parsed
    return [pscustomobject]@{
        Enabled        = [bool]$p.FakeEnvEnabled
        Hostname       = $p.Hostname
        Timezone       = $p.Timezone
        Locale         = $p.Locale
        Workgroup      = $p.Workgroup
        MinimalDesktop = [bool]$p.MinimalDesktop
        Steps          = @(
            'F1 Rename-Computer / hostname',
            'F2 tzutil set timezone',
            'F3 Set-WinSystemLocale / culture',
            'F4 Workgroup align (guest.fake_env.workgroup)',
            'F5 Confirm host C: not mounted',
            'F6 Optional minimal desktop',
            'F7 Gateway network (Set-ClaudeBoxGuestNetwork)'
        )
        GuestScript    = 'src/ClaudeBox.Guest/Initialize-ClaudeBoxGuest.ps1'
        ApplyExample   = ".\Initialize-ClaudeBoxGuest.ps1 -IdentityHostname $($p.Hostname) -Timezone $($p.Timezone) -Locale $($p.Locale) -Workgroup $($p.Workgroup) -Apply"
    }
}

Export-ModuleMember -Function @(
    'Test-ClaudeBoxIsWindowsHost',
    'Get-ClaudeBoxConfig',
    'Get-ClaudeBoxGatewayBlocklist',
    'Get-ClaudeBoxYamlScalar',
    'Get-ClaudeBoxVisibleDirEntries',
    'Test-ClaudeBoxHostSystemDrivePath',
    'Assert-ClaudeBoxVisibleDirsSafe',
    'Test-ClaudeBoxHyperVReady',
    'New-ClaudeBoxInternalSwitch',
    'New-ClaudeBoxVM',
    'Start-ClaudeBoxVM',
    'Stop-ClaudeBoxVM',
    'New-ClaudeBoxMountPlan',
    'Save-ClaudeBoxMountPlan',
    'Publish-ClaudeBoxGuestBundle',
    'Mount-ClaudeBoxVisibleDirs',
    'Set-ClaudeBoxGatewayUpstream',
    'Get-ClaudeBoxFakeEnvPlan'
)
