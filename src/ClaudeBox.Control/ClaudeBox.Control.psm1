#Requires -Version 5.1
<#
.SYNOPSIS
  Claude Box 宿主机控制面（Hyper-V 编排）。

.DESCRIPTION
  M1：配置字段抽取、拒绝宿主 C:、New-ClaudeBoxVM（Gen2 + Internal）骨架。
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
        [Parameter(Mandatory = $true)]
        [string] $Raw,
        [Parameter(Mandatory = $true)]
        [string] $Key,
        [string] $Default = ''
    )

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
        Hostname   = Get-ClaudeBoxYamlScalar -Raw $idBlock -Key 'hostname' -Default 'claude-box'
        Timezone   = Get-ClaudeBoxYamlScalar -Raw $idBlock -Key 'timezone' -Default 'UTC'
        Locale     = Get-ClaudeBoxYamlScalar -Raw $idBlock -Key 'locale' -Default 'en-US'
        ExcludeHostSystemDrive = Get-ClaudeBoxYamlBool -Raw $featBlock -Key 'exclude_host_system_drive' -Default $true
        FullDisk   = Get-ClaudeBoxYamlBool -Raw $featBlock -Key 'full_disk' -Default $false
        FakeEnvEnabled = Get-ClaudeBoxYamlBool -Raw $fakeBlock -Key 'enabled' -Default $true
        VisibleDirs = @(Get-ClaudeBoxVisibleDirPaths -Raw $raw)
    }

    return [pscustomobject]@{
        Path    = (Resolve-Path -LiteralPath $Path).Path
        Raw     = $raw
        Parsed  = $parsed
        Warning = 'M1: minimal YAML field extract; replace with full parser + schema in M2+.'
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

function Mount-ClaudeBoxVisibleDirs {
    <#
    .SYNOPSIS
      将配置中的 visible_dirs 暴露进访客；默认拒绝宿主 C:。

    .NOTES
      机制（Enhanced Session 共享 / SMB / 其它）待验证；M3 实现真正挂载。
      M1：校验路径安全并打印将要挂载的列表。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    Assert-ClaudeBoxVisibleDirsSafe -Parsed $cfg.Parsed

    if ($cfg.Parsed.FullDisk) {
        Write-Warning 'features.full_disk=true: environment spoofing weakened; host C: still blocked if exclude_host_system_drive=true.'
    }

    $dirs = @($cfg.Parsed.VisibleDirs)
    if ($dirs.Count -eq 0) {
        Write-Host 'No visible_dirs configured; guest will not see host project files.'
        return
    }

    Write-Host 'Visible dirs (mount mechanism TODO M3):'
    foreach ($d in $dirs) {
        Write-Host "  - $d"
    }
    Write-Warning 'TODO(M3): Actually mount into guest (Enhanced Session share / SMB / other).'
}

function Set-ClaudeBoxGatewayUpstream {
    <#
    .SYNOPSIS
      把配置中的 gateway 上游与日志选项应用到宿主侧网关。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    $p = $cfg.Parsed
    Write-Host "Gateway upstream http=$($p.GatewayHttp) socks5=$($p.GatewaySocks5) primary=$($p.GatewayPrimary)"
    Write-Host "ListenIp=$($p.ListenIp) logging.enabled=$($p.LoggingEnabled) path=$($p.LoggingPath)"
    Write-Host 'Clash TUN note: point http/socks5 at host Clash ports; do NOT give guest External switch.'
    Write-Warning 'TODO(M2): Apply upstream + logging to gateway process; reload listener on Internal IP.'
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
        Enabled   = $true
        Hostname  = $p.Hostname
        Timezone  = $p.Timezone
        Locale    = $p.Locale
        Steps     = @(
            'F1 Rename-Computer / hostname',
            'F2 tzutil set timezone',
            'F3 Set-WinSystemLocale / culture',
            'F4 Workgroup align (guest.fake_env.workgroup)',
            'F5 Confirm host C: not mounted',
            'F6 Optional minimal desktop'
        )
        GuestScript = 'src/ClaudeBox.Guest/Initialize-ClaudeBoxGuest.ps1'
    }
}

Export-ModuleMember -Function @(
    'Test-ClaudeBoxIsWindowsHost',
    'Get-ClaudeBoxConfig',
    'Get-ClaudeBoxYamlScalar',
    'Test-ClaudeBoxHostSystemDrivePath',
    'Assert-ClaudeBoxVisibleDirsSafe',
    'Test-ClaudeBoxHyperVReady',
    'New-ClaudeBoxInternalSwitch',
    'New-ClaudeBoxVM',
    'Start-ClaudeBoxVM',
    'Stop-ClaudeBoxVM',
    'Mount-ClaudeBoxVisibleDirs',
    'Set-ClaudeBoxGatewayUpstream',
    'Get-ClaudeBoxFakeEnvPlan'
)
