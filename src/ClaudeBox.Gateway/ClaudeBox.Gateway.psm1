#Requires -Version 5.1
<#
.SYNOPSIS
  Claude Box 网关：配置应用、JSONL 连接日志、中继启停（gost 优先）。

.DESCRIPTION
  访客出站经 Internal 网到达 listen_ip，再转发到用户配置的 HTTP CONNECT / SOCKS5
  上游（常为宿主 Clash）。M2：可应用配置、写 JSONL、生成/启动 gost 中继计划；
  真 Hyper-V 联调标「待验证」。

.NOTES
  不依赖 Hyper-V cmdlet。不解 HTTPS 正文；日志仅本地。
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-ClaudeBoxGatewayConfig {
    <#
    .SYNOPSIS
      检查 gateway 段是否至少有一个上游，并解析 primary。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        $GatewaySpec
    )

    if ($null -eq $GatewaySpec) {
        return [pscustomobject]@{ Ok = $false; Error = 'gateway spec is null' }
    }

    $http = $null
    $socks = $null
    $primary = $null
    $listenIp = $null
    $listenPort = $null
    if ($GatewaySpec -is [hashtable] -or $null -ne $GatewaySpec.PSObject) {
        $http = $GatewaySpec.http
        $socks = $GatewaySpec.socks5
        $primary = $GatewaySpec.primary
        if ($GatewaySpec.PSObject.Properties['listen_ip']) { $listenIp = $GatewaySpec.listen_ip }
        if ($GatewaySpec.PSObject.Properties['ListenIp']) { $listenIp = $GatewaySpec.ListenIp }
        if ($GatewaySpec.PSObject.Properties['listen_port']) { $listenPort = $GatewaySpec.listen_port }
        if ($GatewaySpec.PSObject.Properties['ListenPort']) { $listenPort = $GatewaySpec.ListenPort }
        if ($GatewaySpec -is [hashtable]) {
            if ($GatewaySpec.ContainsKey('listen_ip')) { $listenIp = $GatewaySpec['listen_ip'] }
            if ($GatewaySpec.ContainsKey('ListenIp')) { $listenIp = $GatewaySpec['ListenIp'] }
            if ($GatewaySpec.ContainsKey('listen_port')) { $listenPort = $GatewaySpec['listen_port'] }
            if ($GatewaySpec.ContainsKey('ListenPort')) { $listenPort = $GatewaySpec['ListenPort'] }
        }
    }

    if ([string]::IsNullOrWhiteSpace($http) -and [string]::IsNullOrWhiteSpace($socks)) {
        return [pscustomobject]@{ Ok = $false; Error = 'Need gateway.http and/or gateway.socks5' }
    }

    if ([string]::IsNullOrWhiteSpace($primary)) {
        $primary = if (-not [string]::IsNullOrWhiteSpace($http)) { 'http' } else { 'socks5' }
    }

    if ([string]::IsNullOrWhiteSpace($listenIp)) { $listenIp = '192.168.87.1' }
    $port = 3080
    if ($null -ne $listenPort -and "$listenPort" -ne '') {
        [void][int]::TryParse("$listenPort", [ref]$port)
    }
    if ($port -lt 1 -or $port -gt 65535) { $port = 3080 }

    return [pscustomobject]@{
        Ok         = $true
        Primary    = $primary
        Http       = $http
        Socks5     = $socks
        ListenIp   = $listenIp
        ListenPort = $port
        Error      = $null
    }
}

function Get-ClaudeBoxGatewayDefaultLogPath {
    <#
    .SYNOPSIS
      默认本地日志路径（Windows：LocalAppData；其它：临时目录）。
    #>
    [CmdletBinding()]
    param()

    if ($env:LOCALAPPDATA) {
        return (Join-Path $env:LOCALAPPDATA 'ClaudeBox\logs\gateway.jsonl')
    }
    return (Join-Path ([System.IO.Path]::GetTempPath()) 'ClaudeBox\logs\gateway.jsonl')
}

function Get-ClaudeBoxGatewayStateDir {
    <#
    .SYNOPSIS
      网关运行时状态目录（PID、runtime.json、relay plan）。
    #>
    [CmdletBinding()]
    param()

    if ($env:LOCALAPPDATA) {
        return (Join-Path $env:LOCALAPPDATA 'ClaudeBox\gateway')
    }
    return (Join-Path ([System.IO.Path]::GetTempPath()) 'ClaudeBox\gateway')
}

function ConvertTo-ClaudeBoxGatewayRuntimeConfig {
    <#
    .SYNOPSIS
      把配置（hashtable / Parsed 对象 / 字段）规范成网关运行时对象。

    .PARAMETER Parsed
      Control Get-ClaudeBoxConfig 的 Parsed，或含 Gateway* 字段的对象。

    .PARAMETER GatewaySpec
      原始 gateway 段 hashtable（可选，与 Parsed 二选一或叠加）。
    #>
    [CmdletBinding()]
    param(
        $Parsed = $null,
        $GatewaySpec = $null,
        [string[]] $Blocklist = @()
    )

    $http = ''
    $socks = ''
    $primary = 'http'
    $listenIp = '192.168.87.1'
    $listenPort = 3080
    $subnet = '192.168.87.0/24'
    $loggingEnabled = $true
    $loggingPath = ''
    $maxMb = 64
    $notes = ''
    $blocks = New-Object System.Collections.Generic.List[string]

    if ($null -ne $GatewaySpec) {
        $check = Test-ClaudeBoxGatewayConfig -GatewaySpec $GatewaySpec
        if (-not $check.Ok) { throw $check.Error }
        $http = [string]$check.Http
        $socks = [string]$check.Socks5
        $primary = [string]$check.Primary
        $listenIp = [string]$check.ListenIp
        $listenPort = [int]$check.ListenPort
        if ($GatewaySpec -is [hashtable]) {
            if ($GatewaySpec.ContainsKey('subnet')) { $subnet = [string]$GatewaySpec['subnet'] }
            if ($GatewaySpec.ContainsKey('upstream_notes')) { $notes = [string]$GatewaySpec['upstream_notes'] }
            if ($GatewaySpec.ContainsKey('logging') -and $null -ne $GatewaySpec['logging']) {
                $lg = $GatewaySpec['logging']
                if ($lg -is [hashtable]) {
                    if ($lg.ContainsKey('enabled')) { $loggingEnabled = [bool]$lg['enabled'] }
                    if ($lg.ContainsKey('path')) { $loggingPath = [string]$lg['path'] }
                    if ($lg.ContainsKey('max_mb')) { [void][int]::TryParse("$($lg['max_mb'])", [ref]$maxMb) }
                }
            }
            if ($GatewaySpec.ContainsKey('blocklist') -and $null -ne $GatewaySpec['blocklist']) {
                foreach ($b in @($GatewaySpec['blocklist'])) {
                    if (-not [string]::IsNullOrWhiteSpace("$b")) { $blocks.Add("$b".Trim().ToLowerInvariant()) }
                }
            }
        }
    }

    if ($null -ne $Parsed) {
        if ($Parsed.PSObject.Properties['GatewayHttp'] -and $Parsed.GatewayHttp) { $http = [string]$Parsed.GatewayHttp }
        if ($Parsed.PSObject.Properties['GatewaySocks5'] -and $Parsed.GatewaySocks5) { $socks = [string]$Parsed.GatewaySocks5 }
        if ($Parsed.PSObject.Properties['GatewayPrimary'] -and $Parsed.GatewayPrimary) { $primary = [string]$Parsed.GatewayPrimary }
        if ($Parsed.PSObject.Properties['ListenIp'] -and $Parsed.ListenIp) { $listenIp = [string]$Parsed.ListenIp }
        if ($Parsed.PSObject.Properties['ListenPort'] -and $Parsed.ListenPort) {
            $lp = 0
            if ([int]::TryParse("$($Parsed.ListenPort)", [ref]$lp) -and $lp -ge 1) { $listenPort = $lp }
        }
        if ($Parsed.PSObject.Properties['Subnet'] -and $Parsed.Subnet) { $subnet = [string]$Parsed.Subnet }
        if ($Parsed.PSObject.Properties['LoggingEnabled']) { $loggingEnabled = [bool]$Parsed.LoggingEnabled }
        if ($Parsed.PSObject.Properties['LoggingPath']) { $loggingPath = [string]$Parsed.LoggingPath }
        if ($Parsed.PSObject.Properties['LoggingMaxMb'] -and $Parsed.LoggingMaxMb) {
            $mb = 0
            if ([int]::TryParse("$($Parsed.LoggingMaxMb)", [ref]$mb) -and $mb -ge 1) { $maxMb = $mb }
        }
        if ($Parsed.PSObject.Properties['UpstreamNotes'] -and $Parsed.UpstreamNotes) { $notes = [string]$Parsed.UpstreamNotes }
        if ($Parsed.PSObject.Properties['Blocklist'] -and $null -ne $Parsed.Blocklist) {
            foreach ($b in @($Parsed.Blocklist)) {
                if (-not [string]::IsNullOrWhiteSpace("$b")) { $blocks.Add("$b".Trim().ToLowerInvariant()) }
            }
        }
    }

    foreach ($b in $Blocklist) {
        if (-not [string]::IsNullOrWhiteSpace($b)) { $blocks.Add($b.Trim().ToLowerInvariant()) }
    }

    if ([string]::IsNullOrWhiteSpace($http) -and [string]::IsNullOrWhiteSpace($socks)) {
        throw 'Need gateway.http and/or gateway.socks5'
    }
    if ([string]::IsNullOrWhiteSpace($primary)) {
        $primary = if (-not [string]::IsNullOrWhiteSpace($http)) { 'http' } else { 'socks5' }
    }
    if ([string]::IsNullOrWhiteSpace($loggingPath)) {
        $loggingPath = Get-ClaudeBoxGatewayDefaultLogPath
    }

    # Expand %LOCALAPPDATA% style on Windows; leave as-is elsewhere if env missing
    if ($loggingPath -match '%([^%]+)%') {
        $envName = $Matches[1]
        $envVal = [Environment]::GetEnvironmentVariable($envName)
        if ($envVal) {
            $loggingPath = $loggingPath -replace "%$envName%", $envVal
        }
    }

    $upstreamUrl = if ($primary -eq 'socks5' -and -not [string]::IsNullOrWhiteSpace($socks)) {
        $socks
    } elseif (-not [string]::IsNullOrWhiteSpace($http)) {
        $http
    } else {
        $socks
    }

    $uniqueBlocks = [string[]](@($blocks.ToArray() | Select-Object -Unique))

    return [pscustomobject]@{
        Primary         = $primary
        Http            = $http
        Socks5          = $socks
        UpstreamUrl     = $upstreamUrl
        ListenIp        = $listenIp
        ListenPort      = [int]$listenPort
        Subnet          = $subnet
        LoggingEnabled  = [bool]$loggingEnabled
        LoggingPath     = $loggingPath
        LoggingMaxMb    = [int]$maxMb
        UpstreamNotes   = $notes
        Blocklist       = $uniqueBlocks
        RelayTool       = 'gost'
        # Guest should use this as system proxy / default route next-hop
        GuestProxyUrl   = ("http://{0}:{1}" -f $listenIp, $listenPort)
        GuestGatewayIp  = $listenIp
    }
}

function Set-ClaudeBoxGatewayLogging {
    <#
    .SYNOPSIS
      应用连接日志开关与路径：创建目录、写 marker、可选截断超大日志。

    .NOTES
      日志仅本地；不解 HTTPS 正文。字段见 Write-ClaudeBoxGatewayLogEntry。
    #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [bool] $Enabled = $true,
        [string] $Path = '',
        [int] $MaxMb = 64
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        $Path = Get-ClaudeBoxGatewayDefaultLogPath
    }

    $dir = Split-Path -Parent $Path
    if ($PSCmdlet.ShouldProcess($Path, 'Ensure gateway log directory')) {
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        $marker = Join-Path $dir 'logging.enabled'
        if ($Enabled) {
            Set-Content -LiteralPath $marker -Value ("enabled=true`nmax_mb={0}`ntarget={1}`n" -f $MaxMb, $Path) -Encoding UTF8
            if ((Test-Path -LiteralPath $Path) -and $MaxMb -gt 0) {
                $len = (Get-Item -LiteralPath $Path).Length
                if ($len -gt ([int64]$MaxMb * 1MB)) {
                    $bak = "$Path.1"
                    if (Test-Path -LiteralPath $bak) { Remove-Item -LiteralPath $bak -Force }
                    Move-Item -LiteralPath $Path -Destination $bak -Force
                }
            }
            if (-not (Test-Path -LiteralPath $Path)) {
                New-Item -ItemType File -Path $Path -Force | Out-Null
            }
        } else {
            if (Test-Path -LiteralPath $marker) {
                Set-Content -LiteralPath $marker -Value "enabled=false`n" -Encoding UTF8
            }
        }
    }

    if (-not $Enabled) {
        Write-Host 'Gateway connection logging: DISABLED'
        return [pscustomobject]@{ Enabled = $false; Path = $Path; MaxMb = $MaxMb }
    }

    Write-Host "Gateway connection logging: ENABLED -> $Path (max_mb=$MaxMb)"
    Write-Host 'JSONL fields: ts, src, dst_ip, dst_port, sni_or_host, proto, action, upstream_ok, bytes'
    return [pscustomobject]@{ Enabled = $true; Path = $Path; MaxMb = $MaxMb }
}

function Write-ClaudeBoxGatewayLogEntry {
    <#
    .SYNOPSIS
      追加一条 JSONL 连接日志（action: allow | deny | proxy）。

    .PARAMETER Action
      allow = 放行未代理（一般不应出现于硬强制）；deny = 命中 blocklist；proxy = 经上游转发。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,
        [string] $Src = '',
        [string] $DstIp = '',
        [int] $DstPort = 0,
        [string] $SniOrHost = '',
        [string] $Proto = 'tcp',
        [ValidateSet('allow', 'deny', 'proxy')]
        [string] $Action = 'proxy',
        [Nullable[bool]] $UpstreamOk = $null,
        [Nullable[int64]] $Bytes = $null,
        [string] $Note = ''
    )

    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    $ts = [DateTimeOffset]::UtcNow.ToString('o')
    # Build JSON manually for PS 5.1 compatibility (ordered fields)
    function Esc([string] $s) {
        if ($null -eq $s) { return '' }
        return ($s -replace '\\', '\\' -replace '"', '\"' -replace "`n", '\n' -replace "`r", '\r')
    }

    $parts = New-Object System.Collections.Generic.List[string]
    $parts.Add('"ts":"' + (Esc $ts) + '"')
    $parts.Add('"src":"' + (Esc $Src) + '"')
    $parts.Add('"dst_ip":"' + (Esc $DstIp) + '"')
    $parts.Add('"dst_port":' + [int]$DstPort)
    $parts.Add('"sni_or_host":"' + (Esc $SniOrHost) + '"')
    $parts.Add('"proto":"' + (Esc $Proto) + '"')
    $parts.Add('"action":"' + (Esc $Action) + '"')
    if ($null -ne $UpstreamOk) {
        $parts.Add('"upstream_ok":' + ($(if ($UpstreamOk) { 'true' } else { 'false' })))
    } else {
        $parts.Add('"upstream_ok":null')
    }
    if ($null -ne $Bytes) {
        $parts.Add('"bytes":' + [int64]$Bytes)
    } else {
        $parts.Add('"bytes":null')
    }
    if (-not [string]::IsNullOrWhiteSpace($Note)) {
        $parts.Add('"note":"' + (Esc $Note) + '"')
    }
    $line = '{' + ($parts -join ',') + '}'
    Add-Content -LiteralPath $Path -Value $line -Encoding UTF8
    return $line
}

function Test-ClaudeBoxGatewayHostBlocked {
    <#
    .SYNOPSIS
      主机名是否命中 blocklist（后缀匹配）。
    #>
    [CmdletBinding()]
    param(
        [string] $HostName,
        [string[]] $Blocklist
    )
    if ([string]::IsNullOrWhiteSpace($HostName) -or -not $Blocklist -or $Blocklist.Count -eq 0) {
        return $false
    }
    $h = $HostName.Trim().TrimEnd('.').ToLowerInvariant()
    foreach ($b in $Blocklist) {
        if ([string]::IsNullOrWhiteSpace($b)) { continue }
        $pat = $b.Trim().TrimStart('.').ToLowerInvariant()
        if ($h -eq $pat -or $h.EndsWith('.' + $pat)) { return $true }
    }
    return $false
}

function Get-ClaudeBoxClashUpstreamNotes {
    <#
    .SYNOPSIS
      打印 Clash TUN / HTTP 上游接法说明。
    #>
    [CmdletBinding()]
    param()

    Write-Output @'
Clash upstream (recommended):
  1. Guest traffic -> Claude Box gateway on Internal IP (e.g. 192.168.87.1:3080)
  2. Box gateway upstream -> host Clash HTTP/SOCKS (e.g. 127.0.0.1:7890 / 7891)
  3. Host may enable Clash TUN for *host* apps; that does NOT replace the box gateway
  4. Never attach guest NIC to External/Default Switch to "reach TUN directly"
  5. Use gateway.logging to capture SNI/IP after a soak run, then tighten blocklists
  6. Do NOT enable unrestricted ICS/NAT from Internal switch to public (bypass risk)
'@
}

function Find-ClaudeBoxGostExecutable {
    <#
    .SYNOPSIS
      查找 gost 可执行文件（PATH 或 ClaudeBox\bin）。
    #>
    [CmdletBinding()]
    param()

    $candidates = New-Object System.Collections.Generic.List[string]
    $cmd = Get-Command gost -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) { $candidates.Add($cmd.Source) }
    $cmdExe = Get-Command gost.exe -ErrorAction SilentlyContinue
    if ($cmdExe -and $cmdExe.Source) { $candidates.Add($cmdExe.Source) }

    $binRoots = @()
    if ($env:LOCALAPPDATA) { $binRoots += (Join-Path $env:LOCALAPPDATA 'ClaudeBox\bin') }
    $binRoots += (Join-Path ([System.IO.Path]::GetTempPath()) 'ClaudeBox\bin')
    # Repo-relative optional vendor (not committed binary)
    $moduleRoot = $PSScriptRoot
    if ($moduleRoot) {
        $binRoots += (Join-Path $moduleRoot 'bin')
        $binRoots += (Join-Path (Split-Path (Split-Path $moduleRoot -Parent) -Parent) 'bin')
    }

    foreach ($root in $binRoots) {
        foreach ($name in @('gost.exe', 'gost')) {
            $p = Join-Path $root $name
            if (Test-Path -LiteralPath $p) { $candidates.Add($p) }
        }
    }

    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return $c }
    }
    return $null
}

function New-ClaudeBoxGatewayRelayPlan {
    <#
    .SYNOPSIS
      生成中继计划（gost 命令行 + 配置片段 + 访客路由说明）。不启动进程。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        $Runtime
    )

    $listen = 'http://{0}:{1}' -f $Runtime.ListenIp, $Runtime.ListenPort
    $forward = $Runtime.UpstreamUrl
    # gost -L <local> -F <forward>
    $args = @('-L', $listen, '-F', $forward)
    $cmdline = 'gost ' + ($args -join ' ')

    $yml = @"
# Claude Box M2 relay plan (gost). 待验证 on real Hyper-V.
# Install: place gost.exe in %LOCALAPPDATA%\ClaudeBox\bin\ or PATH.
# Docs: https://gost.run/

services:
  - name: claude-box-in
    addr: "$($Runtime.ListenIp):$($Runtime.ListenPort)"
    handler:
      type: http
    listener:
      type: tcp
    forwarder:
      nodes:
        - name: upstream
          addr: "$forward"

# Logging: prefer Claude Box JSONL via Write-ClaudeBoxGatewayLogEntry /
# Start-ClaudeBoxGateway -WriteStartupLog. gost native log is optional.
"@

    $guestNet = @"
# Guest network (run inside Hyper-V Windows guest) — 待验证
# 1) Default gateway / route toward box listen IP on Internal subnet
#    Example: gateway $($Runtime.GuestGatewayIp) for subnet $($Runtime.Subnet)
# 2) System proxy -> $($Runtime.GuestProxyUrl)
# 3) Prefer firewall: allow TCP to $($Runtime.ListenIp):$($Runtime.ListenPort) only for web egress
# Script: src/ClaudeBox.Guest/Set-ClaudeBoxGuestNetwork.ps1
"@

    return [pscustomobject]@{
        Tool            = 'gost'
        Listen          = $listen
        Forward         = $forward
        Arguments       = $args
        CommandLine     = $cmdline
        GostYaml        = $yml
        GuestNetworkHint = $guestNet
        GostPath        = (Find-ClaudeBoxGostExecutable)
        Status          = 'plan-only'
    }
}

function Save-ClaudeBoxGatewayRuntime {
    <#
    .SYNOPSIS
      把 runtime + relay plan 写到状态目录。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        $Runtime,
        $Plan = $null
    )

    $dir = Get-ClaudeBoxGatewayStateDir
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $runtimePath = Join-Path $dir 'runtime.json'
    $planPath = Join-Path $dir 'relay-plan.txt'
    $ymlPath = Join-Path $dir 'gost.relay.yml'

    $runtimeJson = $Runtime | ConvertTo-Json -Depth 6
    Set-Content -LiteralPath $runtimePath -Value $runtimeJson -Encoding UTF8

    if ($null -eq $Plan) {
        $Plan = New-ClaudeBoxGatewayRelayPlan -Runtime $Runtime
    }
    $planText = @(
        "tool=$($Plan.Tool)",
        "command=$($Plan.CommandLine)",
        "gost_path=$($Plan.GostPath)",
        '',
        $Plan.GuestNetworkHint
    ) -join "`n"
    Set-Content -LiteralPath $planPath -Value $planText -Encoding UTF8
    Set-Content -LiteralPath $ymlPath -Value $Plan.GostYaml -Encoding UTF8

    return [pscustomobject]@{
        StateDir     = $dir
        RuntimePath  = $runtimePath
        PlanPath     = $planPath
        GostYamlPath = $ymlPath
        Plan         = $Plan
    }
}

function Start-ClaudeBoxGateway {
    <#
    .SYNOPSIS
      应用配置、写日志目录、生成中继计划；若找到 gost 则在 Win 上启动中继。

    .PARAMETER Runtime
      ConvertTo-ClaudeBoxGatewayRuntimeConfig 的结果；若空则用参数拼装。

    .PARAMETER SkipProcessStart
      只写配置/计划，不启动进程（Linux 开发机或无 gost 时默认行为亦可）。

    .NOTES
      真机 Hyper-V + gost 联调：待验证。无 gost 时不假装已强制出站。
    #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        $Runtime = $null,
        [string] $ListenIp = '192.168.87.1',
        [int] $ListenPort = 3080,
        [string] $UpstreamHttp = '',
        [string] $UpstreamSocks5 = '',
        [ValidateSet('http', 'socks5')]
        [string] $Primary = 'http',
        [bool] $LoggingEnabled = $true,
        [string] $LoggingPath = '',
        [int] $LoggingMaxMb = 64,
        [string[]] $Blocklist = @(),
        [switch] $SkipProcessStart,
        [switch] $WriteStartupLog
    )

    if ($null -eq $Runtime) {
        $spec = @{
            http        = $UpstreamHttp
            socks5      = $UpstreamSocks5
            primary     = $Primary
            listen_ip   = $ListenIp
            listen_port = $ListenPort
            logging     = @{
                enabled = $LoggingEnabled
                path    = $LoggingPath
                max_mb  = $LoggingMaxMb
            }
            blocklist   = $Blocklist
        }
        $Runtime = ConvertTo-ClaudeBoxGatewayRuntimeConfig -GatewaySpec $spec
    }

    $null = Set-ClaudeBoxGatewayLogging -Enabled $Runtime.LoggingEnabled -Path $Runtime.LoggingPath -MaxMb $Runtime.LoggingMaxMb
    Get-ClaudeBoxClashUpstreamNotes | Write-Host

    $saved = Save-ClaudeBoxGatewayRuntime -Runtime $Runtime
    $plan = $saved.Plan
    Write-Host "Relay plan written: $($saved.PlanPath)"
    Write-Host "  $($plan.CommandLine)"
    Write-Host "Guest proxy URL: $($Runtime.GuestProxyUrl)"

    if ($WriteStartupLog -and $Runtime.LoggingEnabled) {
        Write-ClaudeBoxGatewayLogEntry -Path $Runtime.LoggingPath -Src 'host-gateway' `
            -DstIp $Runtime.ListenIp -DstPort $Runtime.ListenPort `
            -SniOrHost 'startup' -Proto 'control' -Action 'allow' -UpstreamOk $null `
            -Note 'gateway-start' | Out-Null
    }

    $gost = $plan.GostPath
    $started = $false
    $pidVal = $null
    $message = 'plan-only'

    $isWin = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
        [System.Runtime.InteropServices.OSPlatform]::Windows
    )

    if ($SkipProcessStart) {
        $message = 'SkipProcessStart: relay plan saved; process not started.'
        Write-Warning $message
    } elseif (-not $gost) {
        $message = 'gost not found. Install gost.exe to PATH or %LOCALAPPDATA%\ClaudeBox\bin\ then re-run Start-ClaudeBoxGateway. See Gateway README.'
        Write-Warning $message
    } elseif (-not $isWin) {
        $message = 'Non-Windows host: not starting gost (Hyper-V path is Win11). Plan saved for Win11.'
        Write-Warning $message
    } elseif ($PSCmdlet.ShouldProcess($plan.CommandLine, 'Start gost relay')) {
        $stateDir = $saved.StateDir
        $pidFile = Join-Path $stateDir 'gateway.pid'
        $outLog = Join-Path $stateDir 'gost.stdout.log'
        $errLog = Join-Path $stateDir 'gost.stderr.log'
        # Stop previous if pid file exists
        if (Test-Path -LiteralPath $pidFile) {
            $oldPid = 0
            if ([int]::TryParse((Get-Content -LiteralPath $pidFile -Raw).Trim(), [ref]$oldPid)) {
                try {
                    $proc = Get-Process -Id $oldPid -ErrorAction SilentlyContinue
                    if ($proc) {
                        Stop-Process -Id $oldPid -Force -ErrorAction SilentlyContinue
                    }
                } catch { }
            }
        }
        $p = Start-Process -FilePath $gost -ArgumentList $plan.Arguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $outLog -RedirectStandardError $errLog
        $pidVal = $p.Id
        Set-Content -LiteralPath $pidFile -Value "$pidVal" -Encoding UTF8
        $started = $true
        $message = ("Started gost pid={0} (待验证: bind {1} on Internal NIC + Clash upstream)." -f $pidVal, $Runtime.ListenIp)
        Write-Host $message
        if ($Runtime.LoggingEnabled) {
            Write-ClaudeBoxGatewayLogEntry -Path $Runtime.LoggingPath -Src 'host-gateway' `
                -DstIp $Runtime.ListenIp -DstPort $Runtime.ListenPort `
                -SniOrHost 'gost' -Proto 'control' -Action 'proxy' -UpstreamOk $true `
                -Note ("gost-started pid={0}" -f $pidVal) | Out-Null
        }
    }

    return [pscustomobject]@{
        Runtime   = $Runtime
        Plan      = $plan
        Saved     = $saved
        Started   = $started
        Pid       = $pidVal
        Message   = $message
        Verified  = $false  # never claim Hyper-V verification from this box
    }
}

function Stop-ClaudeBoxGateway {
    <#
    .SYNOPSIS
      停止宿主侧网关进程（按 PID 文件）。
    #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param()

    $dir = Get-ClaudeBoxGatewayStateDir
    $pidFile = Join-Path $dir 'gateway.pid'
    if (-not (Test-Path -LiteralPath $pidFile)) {
        Write-Host 'No gateway.pid; nothing to stop.'
        return [pscustomobject]@{ Stopped = $false; Reason = 'no-pid-file' }
    }
    $oldPid = 0
    if (-not [int]::TryParse((Get-Content -LiteralPath $pidFile -Raw).Trim(), [ref]$oldPid)) {
        Write-Warning 'Invalid gateway.pid'
        return [pscustomobject]@{ Stopped = $false; Reason = 'bad-pid' }
    }
    if ($PSCmdlet.ShouldProcess("pid $oldPid", 'Stop-Process')) {
        try {
            $proc = Get-Process -Id $oldPid -ErrorAction SilentlyContinue
            if ($proc) {
                Stop-Process -Id $oldPid -Force
                Write-Host "Stopped gateway pid=$oldPid"
            } else {
                Write-Host "Process $oldPid not running."
            }
        } catch {
            Write-Warning $_.Exception.Message
        }
        Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    }
    return [pscustomobject]@{ Stopped = $true; Pid = $oldPid }
}

function Test-ClaudeBoxDirectEgressBlocked {
    <#
    .SYNOPSIS
      直连出站应失败的验收辅助（文档化步骤 + 可选自检）。

    .PARAMETER Mode
      Document = 打印访客内步骤；SelfCheck = 仅检查配置/日志工具就绪（不测真网）。

    .NOTES
      真访客 curl 验收：待验证。不得用本函数结果宣称已在 Hyper-V 达成唯一网关。
    #>
    [CmdletBinding()]
    param(
        [ValidateSet('Document', 'SelfCheck')]
        [string] $Mode = 'Document',
        $Runtime = $null
    )

    $doc = @'
Acceptance — run INSIDE Hyper-V guest (待验证 on real hardware):
  A. Gateway + upstream (Clash) UP:
     curl.exe -I --proxy http://GATEWAY_IP:PORT https://api.ipify.org
     -> should succeed; egress IP = proxy path
  B. Direct egress MUST fail (unique gateway):
     # With system proxy cleared OR firewall denying non-gateway:
     curl.exe -I --noproxy "*" --connect-timeout 5 https://1.1.1.1
     -> MUST fail / timeout
  C. Optional: stop host gost / break Clash upstream; guest via proxy also fails
  D. Restore upstream; Claude login/chat still works
  E. If logging.enabled: host JSONL gains lines (sni_or_host / dst_ip / action)
If B fails open, do NOT claim unique-gateway.

Host-side helpers:
  Import-Module src/ClaudeBox.Gateway/ClaudeBox.Gateway.psd1
  Test-ClaudeBoxDirectEgressBlocked -Mode Document
  scripts/win11/Test-ClaudeBoxDirectEgressBlocked.ps1
'@

    if ($Mode -eq 'Document') {
        if ($null -ne $Runtime) {
            $doc = $doc -replace 'GATEWAY_IP:PORT', ("{0}:{1}" -f $Runtime.ListenIp, $Runtime.ListenPort)
            $doc = $doc -replace 'GATEWAY_IP', $Runtime.ListenIp
        }
        Write-Output $doc
        return [pscustomobject]@{
            Mode     = 'Document'
            Passed   = $null
            Verified = $false
            Text     = $doc
        }
    }

    # SelfCheck: module can apply config + write log — not live egress
    $ok = $true
    $reasons = New-Object System.Collections.Generic.List[string]
    try {
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('ClaudeBox-egress-selfcheck-{0}.jsonl' -f [guid]::NewGuid().ToString('n'))
        Write-ClaudeBoxGatewayLogEntry -Path $tmp -Src 'selfcheck' -DstIp '1.1.1.1' -DstPort 443 `
            -SniOrHost 'example.telemetry.invalid' -Proto 'tcp' -Action 'deny' -UpstreamOk $false -Note 'selfcheck' | Out-Null
        if (-not (Test-Path -LiteralPath $tmp)) { $ok = $false; $reasons.Add('log-write-failed') }
        else { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    } catch {
        $ok = $false
        $reasons.Add($_.Exception.Message)
    }
    $reasons.Add('Live guest direct-egress probe NOT run (待验证).')
    return [pscustomobject]@{
        Mode     = 'SelfCheck'
        Passed   = $ok
        Verified = $false
        Reasons  = $reasons.ToArray()
        Text     = $doc
    }
}

# Back-compat alias name used in M1 docs
function Test-ClaudeBoxGatewayDirectBlocked {
    [CmdletBinding()]
    param(
        [ValidateSet('Document', 'SelfCheck')]
        [string] $Mode = 'Document',
        $Runtime = $null
    )
    Test-ClaudeBoxDirectEgressBlocked -Mode $Mode -Runtime $Runtime
}

Export-ModuleMember -Function @(
    'Test-ClaudeBoxGatewayConfig',
    'Get-ClaudeBoxGatewayDefaultLogPath',
    'Get-ClaudeBoxGatewayStateDir',
    'ConvertTo-ClaudeBoxGatewayRuntimeConfig',
    'Set-ClaudeBoxGatewayLogging',
    'Write-ClaudeBoxGatewayLogEntry',
    'Test-ClaudeBoxGatewayHostBlocked',
    'Get-ClaudeBoxClashUpstreamNotes',
    'Find-ClaudeBoxGostExecutable',
    'New-ClaudeBoxGatewayRelayPlan',
    'Save-ClaudeBoxGatewayRuntime',
    'Start-ClaudeBoxGateway',
    'Stop-ClaudeBoxGateway',
    'Test-ClaudeBoxDirectEgressBlocked',
    'Test-ClaudeBoxGatewayDirectBlocked'
)
