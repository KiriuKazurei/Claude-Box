#Requires -Version 5.1
<#
.SYNOPSIS
  Claude Box 网关侧配置与宿主进程占位。

.DESCRIPTION
  目标语义：访客出站经 Internal 网到达本模块管理的入口，再转发到用户配置的
  HTTP CONNECT / SOCKS5 上游。M0 仅提供配置校验钩子与文档化命令名。

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

    # M0：若传入已是 hashtable/PSCustomObject 则检查；否则提示用完整解析
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

function Start-ClaudeBoxGateway {
    <#
    .SYNOPSIS
      在 listen_ip 上启动中继（转发到上游）。

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
        [string] $Primary = 'http'
    )

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

    .NOTES
      本函数在宿主上只打印检查清单；真实 curl 须在访客内执行（见 Guest 模块）。
    #>
    [CmdletBinding()]
    param()

    Write-Output @'
Acceptance (run INSIDE guest):
  1. With gateway+upstream up: curl https://api.ipify.org  -> proxy egress IP
  2. Break upstream or misconfigure: curl https://1.1.1.1  -> MUST fail
  3. Restore: Claude login/chat still works
If step 2 fails open, do NOT claim unique-gateway.
'@
}

Export-ModuleMember -Function @(
    'Test-ClaudeBoxGatewayConfig',
    'Start-ClaudeBoxGateway',
    'Stop-ClaudeBoxGateway',
    'Test-ClaudeBoxGatewayDirectBlocked'
)
