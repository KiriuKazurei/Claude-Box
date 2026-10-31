#Requires -Version 5.1
<#
.SYNOPSIS
  在 Hyper-V Windows 访客内：默认网关指向盒子网关 IP，并设置系统代理。

.DESCRIPTION
  M2 网络强制的访客侧半边。默认 Dry-run；加 -Apply 才改系统（需管理员）。
  真机效果：待验证。

.PARAMETER GatewayIp
  宿主网关在 Internal 网上的 IP（config gateway.listen_ip）。

.PARAMETER ProxyPort
  盒子中继监听端口（config gateway.listen_port，默认 3080）。

.PARAMETER GuestIp
  可选：为访客 NIC 设置的静态 IP。

.PARAMETER PrefixLength
  子网前缀，默认 24。

.PARAMETER InterfaceAlias
  网卡名；空则尝试第一块 Up 的以太网适配器。

.PARAMETER Apply
  真正写入 IP/代理。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $GatewayIp = '192.168.87.1',
    [int] $ProxyPort = 3080,
    [string] $GuestIp = '',
    [int] $PrefixLength = 24,
    [string] $InterfaceAlias = '',
    [string] $DnsServer = '',
    [switch] $Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$proxyUrl = "http://{0}:{1}" -f $GatewayIp, $ProxyPort

Write-Host '=== Claude Box guest network (M2) ==='
Write-Host "Default gateway / next-hop: $GatewayIp"
Write-Host "System proxy: $proxyUrl"
Write-Host "DNS: $(if ($DnsServer) { $DnsServer } else { $GatewayIp + ' (or leave DHCP; 待验证)' })"
Write-Host 'Hardening tip: allow outbound TCP to gateway:port; block other egress (待验证).'
Write-Host 'Do NOT use External/Default Switch for this NIC.'

if (-not $Apply) {
    Write-Host '[DRY-RUN] Pass -Apply to change IP/proxy (admin).'
    return [pscustomobject]@{
        Applied   = $false
        GatewayIp = $GatewayIp
        ProxyUrl  = $proxyUrl
        Verified  = $false
    }
}

$isWin = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows
)
if (-not $isWin) {
    throw 'Set-ClaudeBoxGuestNetwork -Apply requires Windows guest.'
}

if ([string]::IsNullOrWhiteSpace($InterfaceAlias)) {
    $nic = Get-NetAdapter -ErrorAction SilentlyContinue |
        Where-Object { $_.Status -eq 'Up' -and $_.InterfaceDescription -notmatch 'Loopback' } |
        Select-Object -First 1
    if (-not $nic) { throw 'No Up network adapter found; pass -InterfaceAlias.' }
    $InterfaceAlias = $nic.Name
}
Write-Host "Interface: $InterfaceAlias"

if ($PSCmdlet.ShouldProcess($InterfaceAlias, "Guest network gateway=$GatewayIp proxy=$proxyUrl")) {
    if (-not [string]::IsNullOrWhiteSpace($GuestIp)) {
        # Remove existing IPv4 then set static
        Get-NetIPAddress -InterfaceAlias $InterfaceAlias -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            ForEach-Object {
                Remove-NetIPAddress -InterfaceAlias $InterfaceAlias -IPAddress $_.IPAddress -Confirm:$false -ErrorAction SilentlyContinue
            }
        Get-NetRoute -InterfaceAlias $InterfaceAlias -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.DestinationPrefix -eq '0.0.0.0/0' } |
            ForEach-Object { Remove-NetRoute -InterfaceAlias $InterfaceAlias -DestinationPrefix '0.0.0.0/0' -Confirm:$false -ErrorAction SilentlyContinue }
        New-NetIPAddress -InterfaceAlias $InterfaceAlias -IPAddress $GuestIp -PrefixLength $PrefixLength -DefaultGateway $GatewayIp -ErrorAction Stop | Out-Null
        Write-Host "Assigned $GuestIp/$PrefixLength gw $GatewayIp"
    } else {
        # Only ensure default route if possible
        try {
            $existing = Get-NetRoute -InterfaceAlias $InterfaceAlias -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue
            if ($existing) {
                Set-NetRoute -InterfaceAlias $InterfaceAlias -DestinationPrefix '0.0.0.0/0' -NextHop $GatewayIp -ErrorAction SilentlyContinue
            } else {
                New-NetRoute -InterfaceAlias $InterfaceAlias -DestinationPrefix '0.0.0.0/0' -NextHop $GatewayIp -ErrorAction SilentlyContinue | Out-Null
            }
            Write-Host "Default route -> $GatewayIp (best-effort)"
        } catch {
            Write-Warning "Default route change failed: $($_.Exception.Message). Set gateway manually in NIC properties."
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($DnsServer)) {
        Set-DnsClientServerAddress -InterfaceAlias $InterfaceAlias -ServerAddresses $DnsServer -ErrorAction SilentlyContinue
    }

    # WinINET user proxy (current user)
    $regPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
    Set-ItemProperty -Path $regPath -Name ProxyEnable -Value 1
    Set-ItemProperty -Path $regPath -Name ProxyServer -Value "$GatewayIp`:$ProxyPort"
    Write-Host "WinINET proxy enabled -> $GatewayIp`:$ProxyPort"

    # WinHTTP (system services) — best-effort
    try {
        & netsh winhttp set proxy "${GatewayIp}:${ProxyPort}" | Out-Null
        Write-Host "WinHTTP proxy set -> ${GatewayIp}:${ProxyPort}"
    } catch {
        Write-Warning "netsh winhttp failed: $($_.Exception.Message)"
    }
}

Write-Warning 'Firewall lockdown not applied automatically (risk of lockout). See Set-ClaudeBoxInternalGatewayRouting.md.'
Write-Host 'Acceptance: Test-ClaudeBoxDirectEgressBlocked (guest curl direct must fail). 待验证.'

return [pscustomobject]@{
    Applied   = $true
    GatewayIp = $GatewayIp
    ProxyUrl  = $proxyUrl
    Interface = $InterfaceAlias
    Verified  = $false
}
