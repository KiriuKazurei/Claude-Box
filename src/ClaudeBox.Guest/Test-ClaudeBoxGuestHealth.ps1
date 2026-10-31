#Requires -Version 5.1
<#
.SYNOPSIS
  访客内健康检查：打印伪装字段与网络基线（占位）。
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest

Write-Host '=== Guest health (M0) ==='
Write-Host ("Hostname: {0}" -f $env:COMPUTERNAME)
try {
    $tz = Get-TimeZone
    Write-Host ("TimeZone: {0}" -f $tz.Id)
} catch {
    Write-Host 'TimeZone: (unavailable)'
}
Write-Host ("Culture: {0}" -f [System.Globalization.CultureInfo]::CurrentCulture.Name)
Write-Host 'TODO(M2): Probe default route, proxy settings, curl egress IP, direct-fail.'
