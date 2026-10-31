#Requires -Version 5.1
<#
.SYNOPSIS
  Claude Box 宿主机控制面（Hyper-V 编排）。

.DESCRIPTION
  M0：占位与配置加载。实际 New-VM / 交换机操作仅在已启用 Hyper-V 的 Win11 上执行。
  本模块在 Linux 上可做语法解析；调用 Hyper-V cmdlet 的函数会明确失败并提示。

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

function Get-ClaudeBoxConfig {
    <#
    .SYNOPSIS
      从 YAML 路径加载配置为 PSCustomObject（M0：手写最小解析或依赖后续 YAML 库）。

    .PARAMETER Path
      config.yaml 路径。

    .NOTES
      TODO(M1): 引入稳定 YAML 解析（如 powershell-yaml），并对照 schema 校验。
      M0 仅检查文件存在并返回原始文本包装，避免在无库环境假装已解析完备。
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
    # TODO(M1): ConvertFrom-Yaml + schema validation
    return [pscustomobject]@{
        Path    = (Resolve-Path -LiteralPath $Path).Path
        Raw     = $raw
        Parsed  = $null
        Warning = 'M0: YAML not fully parsed; use config.example.yaml as reference until M1.'
    }
}

function Test-ClaudeBoxHyperVReady {
    <#
    .SYNOPSIS
      检查 Hyper-V 功能与模块是否可用。

    .NOTES
      非 Windows 直接返回未就绪。Win11 上 TODO：Get-WindowsOptionalFeature / Get-VMHost。
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

    # TODO(M1): Import-Module Hyper-V; Get-VMHost; feature state
    return [pscustomobject]@{
        Ready   = $false
        Reason  = 'TODO(M1): Probe Hyper-V feature and module on Win11.'
        Details = @{}
    }
}

function New-ClaudeBoxInternalSwitch {
    <#
    .SYNOPSIS
      创建或复用 Internal 虚拟交换机。

    .PARAMETER Name
      交换机名，默认 ClaudeBox-Internal。

    .PARAMETER ListenIp
      宿主机 vEthernet 静态 IP（网关地址）。

    .PARAMETER PrefixLength
      子网前缀长度，默认 24。
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

    # TODO(M1):
    # if (-not (Get-VMSwitch -Name $Name -ErrorAction SilentlyContinue)) {
    #   New-VMSwitch -SwitchName $Name -SwitchType Internal
    # }
    # 为 vEthernet ($Name) 设置 $ListenIp/$PrefixLength
    Write-Warning "TODO(M1): Create Internal switch '$Name' and assign $ListenIp/$PrefixLength"
}

function New-ClaudeBoxVM {
    <#
    .SYNOPSIS
      按配置创建 Hyper-V Windows 访客（空壳，不含 Claude）。

    .PARAMETER ConfigPath
      配置文件路径。
    #>
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath
    )

    $cfg = Get-ClaudeBoxConfig -Path $ConfigPath
    if (-not (Test-ClaudeBoxIsWindowsHost)) {
        throw 'New-ClaudeBoxVM requires Windows + Hyper-V.'
    }

    # TODO(M1): New-VM -Name ... -MemoryStartupBytes ... -Generation 2 -SwitchName ...
    # TODO(M1): 附加 VHDX / ISO；禁用其它网卡旁路
    Write-Warning "TODO(M1): Create VM from config at $($cfg.Path)"
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

    # TODO(M1): Start-VM -Name $Name
    # TODO(M1): if ($Connect) { vmconnect.exe localhost $Name }  # 体验待验证
    Write-Warning "TODO(M1): Start-VM '$Name'; Connect=$Connect"
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

    # TODO(M1): Stop-VM -Name $Name -Force:$Force
    Write-Warning "TODO(M1): Stop-VM '$Name' Force=$Force"
}

function Mount-ClaudeBoxVisibleDirs {
    <#
    .SYNOPSIS
      将配置中的 visible_dirs 暴露进访客。

    .NOTES
      机制（Enhanced Session 共享 / SMB / 其它）待验证；M3 实现。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath
    )

    $null = Get-ClaudeBoxConfig -Path $ConfigPath
    Write-Warning 'TODO(M3): Mount visible_dirs into guest (mechanism TBD).'
}

function Set-ClaudeBoxGatewayUpstream {
    <#
    .SYNOPSIS
      把配置中的 gateway.http / gateway.socks5 应用到宿主侧网关进程。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ConfigPath
    )

    $null = Get-ClaudeBoxConfig -Path $ConfigPath
    # 调用 ClaudeBox.Gateway 导出函数（M2）
    Write-Warning 'TODO(M2): Apply upstream to gateway; reload listener on Internal IP.'
}

Export-ModuleMember -Function @(
    'Test-ClaudeBoxIsWindowsHost',
    'Get-ClaudeBoxConfig',
    'Test-ClaudeBoxHyperVReady',
    'New-ClaudeBoxInternalSwitch',
    'New-ClaudeBoxVM',
    'Start-ClaudeBoxVM',
    'Stop-ClaudeBoxVM',
    'Mount-ClaudeBoxVisibleDirs',
    'Set-ClaudeBoxGatewayUpstream'
)
