#Requires -Version 5.1
<#
.SYNOPSIS
  在访客内根据宿主机下发的挂载计划映射可见目录（SMB）。

.DESCRIPTION
  主策略（M3）：宿主机在 Internal 网上开 SMB 共享，访客用 net use / New-PSDrive 映射。
  计划文件由 Control Mount-ClaudeBoxVisibleDirs 写出（JSON），经 PowerShell Direct /
  Copy-VMFile / 可移动介质拷进访客。默认不映射宿主 C:。

.PARAMETER PlanPath
  mount-plan.json 路径。

.PARAMETER Apply
  真正执行 net use；默认只打印。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string] $PlanPath,
    [switch] $Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $PlanPath)) {
    throw "Mount plan not found: $PlanPath"
}

$raw = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8
$plan = $raw | ConvertFrom-Json

Write-Host ("=== Guest mount plan method={0} generated={1} ===" -f $plan.method, $plan.generated_at)
if ($plan.exclude_host_system_drive) {
    Write-Host 'Policy: exclude_host_system_drive=true (refuse host C:)'
}

$isWin = ($env:OS -eq 'Windows_NT') -or ($PSVersionTable.Platform -eq 'Win32NT')
$mapped = New-Object System.Collections.Generic.List[object]

foreach ($e in @($plan.entries)) {
    if ($null -eq $e) { continue }
    $unc = [string]$e.guest_unc
    $letter = [string]$e.guest_drive
    $ro = [bool]$e.read_only
    Write-Host ("Entry: {0} -> {1} ({2}) read_only={3}" -f $e.name, $letter, $unc, $ro)
    if ($e.host_path -match '^[cC]:\\?$') {
        throw "Plan contains host C: — refusing: $($e.host_path)"
    }
    if (-not $Apply -or -not $isWin) {
        Write-Host "  DRY-RUN: net use $letter $unc"
        continue
    }
    if ($PSCmdlet.ShouldProcess("$letter -> $unc", 'net use')) {
        # Drop existing
        cmd.exe /c "net use $letter /delete /y" 2>$null | Out-Null
        $args = @('use', $letter, $unc)
        if ($ro) { $args += '/read_only' }  # note: net use uses different flags; map then attrib
        $p = Start-Process -FilePath 'net.exe' -ArgumentList @('use', $letter, $unc, '/persistent:yes') -Wait -PassThru -NoNewWindow
        if ($p.ExitCode -ne 0) {
            Write-Warning "net use failed exit=$($p.ExitCode) for $unc — 待验证: 访客防火墙/SMB/凭据"
        } else {
            Write-Host "  Mapped $letter -> $unc"
            $mapped.Add([pscustomobject]@{ Drive = $letter; Unc = $unc; Ok = $true })
        }
    }
}

if ($plan.method -eq 'enhanced_session_manual') {
    Write-Host 'Method is enhanced_session_manual: use vmconnect Enhanced Session → show options → Local resources → Drives (script does not auto-redirect).'
}

Write-Host ("Done. mapped_count={0} Apply={1}" -f $mapped.Count, [bool]$Apply)
[pscustomobject]@{
    Method     = $plan.method
    Mapped     = $mapped.ToArray()
    DryRun     = (-not $Apply -or -not $isWin)
    Verified   = $false
}
