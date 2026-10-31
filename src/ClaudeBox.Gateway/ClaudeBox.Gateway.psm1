#Requires -Version 5.1
<#
.SYNOPSIS
  Claude Box 网关侧配置、日志开关与宿主进程占位。

.DESCRIPTION
  访客出站经 Internal 网到达本模块管理的入口，再转发到用户配置的
  HTTP CONNECT / SOCKS5 上游（常为宿主 Clash）。M1：日志配置应用占位；M2：真中继。

.NOTES
  不依赖 Hyper-V cmdlet，便于日后迁到专用网关 VM。
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
    if ($GatewaySpec -is [hashtable] -or $GatewaySpec.PSObject) {
        $http = $GatewaySpec.http
        $socks = $GatewaySpec.socks5
        $primary = $GatewaySpec.primary
    }

    if ([string]::IsNullOrWhiteSpace($http) -and [string]::IsNullOrWhiteSpace($socks)) {
        return [pscustomobject]@{ Ok = $false; Error = 'Need gateway.http and/or gateway.socks5' }
    }

    if ([string]::IsNullOrWhiteSpace($primary)) {
        $primary = if (-not [string]::IsNullOrWhiteSpace($http)) { 'http' } else { 'socks5' }
    }

    return [pscustomobject]@{
        Ok       = $true
        Primary  = $primary
        Http     = $http
        Socks5   = $socks
        Error    = $null
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

function Set-ClaudeBoxGatewayLogging {
    <#
    .SYNOPSIS
      应用连接日志开关与路径（M1 占位：创建目录、写标记文件；M2 接真中继）。

    .PARAMETER Enabled
      是否记录连接（SNI/域名、IP、端口等）。

    .PARAMETER Path
      日志文件路径；空则用默认。

    .PARAMETER MaxMb
      滚动上限占位（M2 实现）。

    .NOTES
      日志仅本地；不解 HTTPS 正文。用于事后优化遥测拦截名单。
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
    if ($Enabled -and $PSCmdlet.ShouldProcess($Path, 'Ensure gateway log directory')) {
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        $marker = Join-Path $dir 'logging.enabled'
        if ($Enabled) {
            Set-Content -LiteralPath $marker -Value ("enabled=true`nmax_mb={0}`ntarget={1}`n" -f $MaxMb, $Path) -Encoding UTF8
        }
    }

    if (-not $Enabled) {
        Write-Host 'Gateway connection logging: DISABLED'
        return [pscustomobject]@{ Enabled = $false; Path = $Path; MaxMb = $MaxMb }
    }

    Write-Host "Gateway connection logging: ENABLED -> $Path (max_mb=$MaxMb)"
    Write-Host 'Fields (M2): ts, src, dst_ip, dst_port, sni_or_host, proto, upstream_ok'
    Write-Warning 'TODO(M2): Wire logger into relay process; rotate by max_mb.'
    return [pscustomobject]@{ Enabled = $true; Path = $Path; MaxMb = $MaxMb }
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
  1. Guest traffic -> Claude Box gateway on Internal IP (e.g. 192.168.87.1)
  2. Box gateway upstream -> host Clash HTTP/SOCKS (e.g. 127.0.0.1:7890 / 7891)
  3. Host may enable Clash TUN for *host* apps; that does NOT replace the box gateway
  4. Never attach guest NIC to External/Default Switch to "reach TUN directly"
  5. Use gateway.logging to capture SNI/IP after a soak run, then tighten blocklists
'@
}

function Start-ClaudeBoxGateway {
    <#
    .SYNOPSIS
      在 listen_ip 上启动中继（转发到上游），并应用日志配置。

    .NOTES
      TODO(M2): 选定二进制（如 gost / 自建 / Win 服务）并监听 Internal NIC。
      验收：访客经此出口；断上游后直连失败。
    #>
    [CmdletBinding()]
    param(
        [string] $ListenIp = '192.168.87.1',
        [string] $UpstreamHttp = '',
        [string] $UpstreamSocks5 = '',
        [ValidateSet('http', 'socks5')]
        [string] $Primary = 'http',
        [bool] $LoggingEnabled = $true,
        [string] $LoggingPath = '',
        [int] $LoggingMaxMb = 64
    )

    $null = Set-ClaudeBoxGatewayLogging -Enabled $LoggingEnabled -Path $LoggingPath -MaxMb $LoggingMaxMb
    Get-ClaudeBoxClashUpstreamNotes | Write-Host
    Write-Warning "TODO(M2): Start gateway on $ListenIp primary=$Primary http=$UpstreamHttp socks5=$UpstreamSocks5"
}

function Stop-ClaudeBoxGateway {
    <#
    .SYNOPSIS
      停止宿主侧网关进程。
    #>
    [CmdletBinding()]
    param()

    Write-Warning 'TODO(M2): Stop gateway process / service.'
}

function Test-ClaudeBoxGatewayDirectBlocked {
    <#
    .SYNOPSIS
      文档化验收：在访客内应证明直连公网失败。
    #>
    [CmdletBinding()]
    param()

    Write-Output @'
Acceptance (run INSIDE guest):
  1. With gateway+upstream up: curl https://api.ipify.org  -> proxy egress IP
  2. Break upstream or misconfigure: curl https://1.1.1.1  -> MUST fail
  3. Restore: Claude login/chat still works
  4. If logging enabled: host log gains SNI/IP lines during (1)
If step 2 fails open, do NOT claim unique-gateway.
'@
}

Export-ModuleMember -Function @(
    'Test-ClaudeBoxGatewayConfig',
    'Get-ClaudeBoxGatewayDefaultLogPath',
    'Set-ClaudeBoxGatewayLogging',
    'Get-ClaudeBoxClashUpstreamNotes',
    'Start-ClaudeBoxGateway',
    'Stop-ClaudeBoxGateway',
    'Test-ClaudeBoxGatewayDirectBlocked'
)
