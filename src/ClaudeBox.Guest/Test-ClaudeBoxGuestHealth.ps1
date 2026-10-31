#Requires -Version 5.1
<#
.SYNOPSIS
  访客内健康检查：伪装字段、挂盘是否误含宿主 C:、网络基线（占位）。
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest

Write-Host '=== Guest health ==='
Write-Host ("Hostname: {0}" -f $env:COMPUTERNAME)
try {
    $tz = Get-TimeZone
    Write-Host ("TimeZone: {0}" -f $tz.Id)
} catch {
    Write-Host 'TimeZone: (unavailable)'
}
Write-Host ("Culture: {0}" -f [System.Globalization.CultureInfo]::CurrentCulture.Name)

Write-Host 'Mounted volumes:'
try {
    Get-PSDrive -PSProvider FileSystem | ForEach-Object {
        Write-Host ("  {0}: {1}" -f $_.Name, $_.Root)
    }
} catch {
    Write-Host '  (Get-PSDrive unavailable)'
}

Write-Host 'Check: host C: should NOT appear as a redirected share of the physical host system drive.'
Write-Host 'TODO(M2): Probe default route, proxy settings, curl egress IP, direct-fail.'
Write-Host 'TODO(M3): Assert hostname/tz/locale match fake_env plan from host config.'
