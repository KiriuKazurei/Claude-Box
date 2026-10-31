#Requires -Version 5.1
<#
.SYNOPSIS
  Hyper-V Windows 访客内初始化：假环境（F1–F6）+ 代理接线 + Claude 安装指引入口。

.DESCRIPTION
  「假 C」= 本访客自有系统盘上的独立 identity，不是映射宿主 C:。
  默认 dry-run（打印将执行的动作）。访客内管理员加 -Apply 才真正改系统。
  非 Windows 上 -Apply 仍只 dry-run（不调用 Win 专用 cmdlet）。

.PARAMETER Apply
  在 Windows 访客内真正设置 hostname / 时区 / locale / workgroup（可能需重启）。
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $IdentityHostname = 'claude-box',
    [string] $Timezone = 'UTC',
    [string] $Locale = 'en-US',
    [string] $Workgroup = 'CLAUDEBOX',
    [string] $GatewayHttp = '',
    [string] $GatewaySocks5 = '',
    [string] $GatewayIp = '',
    [int] $ProxyPort = 3080,
    [switch] $SkipClaudeInstallGuide,
    [switch] $Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-ClaudeBoxGuestIsWindows {
    return ($env:OS -eq 'Windows_NT') -or ($PSVersionTable.PSEdition -eq 'Desktop') -or `
        (($PSVersionTable.Platform -eq 'Win32NT'))
}

function Show-ClaudeBoxFakeEnvChecklist {
    param(
        [string] $Hostname,
        [string] $TimezoneId,
        [string] $Culture,
        [string] $Wg
    )
    Write-Host '=== Fake env checklist (guest-local system disk, NOT host C:) ==='
    Write-Host "[F1] Hostname/computer name -> $Hostname"
    Write-Host "[F2] Timezone -> $TimezoneId  (tzutil /s)"
    Write-Host "[F3] Locale/culture -> $Culture  (Set-WinSystemLocale / Set-Culture)"
    Write-Host "[F4] Workgroup -> $Wg"
    Write-Host '[F5] Confirm host system drive C: is NOT mounted as a redirected share'
    Write-Host '[F6] Optional: minimal desktop / avoid host-identifying apps'
    Write-Host '[F7] Network: Set-ClaudeBoxGuestNetwork.ps1 + gateway force'
}

function Set-ClaudeBoxGuestIdentity {
    param(
        [string] $Hostname,
        [string] $TimezoneId,
        [string] $Culture,
        [string] $Wg,
        [bool] $DoApply
    )

    $isWin = Test-ClaudeBoxGuestIsWindows
    $actions = New-Object System.Collections.Generic.List[object]

    $actions.Add([pscustomobject]@{ Step = 'F1'; Cmd = "Rename-Computer -NewName '$Hostname' -Force"; Target = $Hostname })
    $actions.Add([pscustomobject]@{ Step = 'F2'; Cmd = "tzutil /s `"$TimezoneId`""; Target = $TimezoneId })
    $actions.Add([pscustomobject]@{ Step = 'F3'; Cmd = "Set-WinSystemLocale -SystemLocale $Culture; Set-Culture $Culture; Set-WinUILanguageOverride -Language $Culture"; Target = $Culture })
    $actions.Add([pscustomobject]@{ Step = 'F4'; Cmd = "Add-Computer -WorkgroupName '$Wg' -WarningAction SilentlyContinue (or net.exe)"; Target = $Wg })

    if (-not $DoApply -or -not $isWin) {
        $mode = if (-not $isWin) { 'DRY-RUN (non-Windows host; script complete for guest)' } else { 'DRY-RUN' }
        Write-Host "[Guest] $mode identity hostname=$Hostname tz=$TimezoneId locale=$Culture wg=$Wg"
        foreach ($a in $actions) {
            Write-Host ("  [{0}] would: {1}" -f $a.Step, $a.Cmd)
        }
        return [pscustomobject]@{
            Applied       = $false
            DryRun        = $true
            IsWindows     = $isWin
            RebootNeeded  = $false
            Results       = @()
        }
    }

    Write-Warning 'Apply mode: mutating guest identity (admin + possible reboot).'
    $results = New-Object System.Collections.Generic.List[object]
    $reboot = $false

    # F1 hostname
    try {
        $current = $env:COMPUTERNAME
        if ($current -and ($current.ToLowerInvariant() -eq $Hostname.ToLowerInvariant())) {
            Write-Host "[F1] Hostname already $Hostname"
            $results.Add([pscustomobject]@{ Step = 'F1'; Ok = $true; Detail = 'already-set' })
        } elseif ($PSCmdlet.ShouldProcess($Hostname, 'Rename-Computer')) {
            Rename-Computer -NewName $Hostname -Force -ErrorAction Stop
            $reboot = $true
            Write-Host "[F1] Rename-Computer -> $Hostname (reboot required)"
            $results.Add([pscustomobject]@{ Step = 'F1'; Ok = $true; Detail = 'renamed' })
        }
    } catch {
        Write-Warning "[F1] Rename-Computer: $($_.Exception.Message)"
        $results.Add([pscustomobject]@{ Step = 'F1'; Ok = $false; Detail = $_.Exception.Message })
    }

    # F2 timezone
    try {
        if ($PSCmdlet.ShouldProcess($TimezoneId, 'tzutil /s')) {
            $p = Start-Process -FilePath 'tzutil.exe' -ArgumentList @('/s', $TimezoneId) -Wait -PassThru -NoNewWindow
            if ($p.ExitCode -ne 0) { throw "tzutil exit $($p.ExitCode)" }
            Write-Host "[F2] Timezone set -> $TimezoneId"
            $results.Add([pscustomobject]@{ Step = 'F2'; Ok = $true; Detail = $TimezoneId })
        }
    } catch {
        Write-Warning "[F2] tzutil: $($_.Exception.Message)"
        $results.Add([pscustomobject]@{ Step = 'F2'; Ok = $false; Detail = $_.Exception.Message })
    }

    # F3 locale / culture
    try {
        if ($PSCmdlet.ShouldProcess($Culture, 'Set locale/culture')) {
            if (Get-Command Set-WinSystemLocale -ErrorAction SilentlyContinue) {
                Set-WinSystemLocale -SystemLocale $Culture -ErrorAction Stop
            } else {
                Write-Warning '[F3] Set-WinSystemLocale not available; trying culture only'
            }
            if (Get-Command Set-Culture -ErrorAction SilentlyContinue) {
                Set-Culture -CultureInfo $Culture -ErrorAction Stop
            }
            if (Get-Command Set-WinUILanguageOverride -ErrorAction SilentlyContinue) {
                try { Set-WinUILanguageOverride -Language $Culture -ErrorAction SilentlyContinue } catch { }
            }
            # Also set user locale via intl.cpl alternative: registry Culture (documented fallback)
            Write-Host "[F3] Locale/culture applied -> $Culture (sign-out/reboot may be needed)"
            $reboot = $true
            $results.Add([pscustomobject]@{ Step = 'F3'; Ok = $true; Detail = $Culture })
        }
    } catch {
        Write-Warning "[F3] locale: $($_.Exception.Message)"
        $results.Add([pscustomobject]@{ Step = 'F3'; Ok = $false; Detail = $_.Exception.Message })
    }

    # F4 workgroup
    try {
        if ($PSCmdlet.ShouldProcess($Wg, 'Set workgroup')) {
            $joined = $false
            if (Get-Command Add-Computer -ErrorAction SilentlyContinue) {
                try {
                    Add-Computer -WorkGroupName $Wg -Force -WarningAction SilentlyContinue -ErrorAction Stop
                    $joined = $true
                } catch {
                    # Already in workgroup or not domain-joined path
                    Write-Warning "[F4] Add-Computer: $($_.Exception.Message); trying net.exe"
                }
            }
            if (-not $joined) {
                $p = Start-Process -FilePath 'net.exe' -ArgumentList @('computer', '\\%COMPUTERNAME%', "/workgroup:$Wg") -Wait -PassThru -NoNewWindow -ErrorAction SilentlyContinue
                # net.exe workgroup change historically: wmic / netdom; keep soft-fail
                if ($null -eq $p -or $p.ExitCode -ne 0) {
                    # Fallback documented command for Win11 workgroup rename
                    $null = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
                        '-NoProfile', '-Command',
                        "Add-Computer -WorkGroupName '$Wg' -Force"
                    ) -Wait -PassThru -ErrorAction SilentlyContinue
                }
            }
            Write-Host "[F4] Workgroup target -> $Wg (verify after reboot if domain/workgroup policy blocks)"
            $results.Add([pscustomobject]@{ Step = 'F4'; Ok = $true; Detail = $Wg })
            $reboot = $true
        }
    } catch {
        Write-Warning "[F4] workgroup: $($_.Exception.Message)"
        $results.Add([pscustomobject]@{ Step = 'F4'; Ok = $false; Detail = $_.Exception.Message })
    }

    return [pscustomobject]@{
        Applied      = $true
        DryRun       = $false
        IsWindows    = $true
        RebootNeeded = $reboot
        Results      = $results.ToArray()
    }
}

function Invoke-ClaudeBoxGuestProxyWire {
    param(
        [string] $Http,
        [string] $Socks5,
        [string] $GwIp,
        [int] $Port,
        [bool] $DoApply
    )
    $scriptDir = $PSScriptRoot
    $netScript = Join-Path $scriptDir 'Set-ClaudeBoxGuestNetwork.ps1'
    if (-not $GwIp -and $Http -match 'https?://([^:/]+)') { $GwIp = $Matches[1] }
    if (-not $GwIp) {
        Write-Host "[Guest] skip network wire (no GatewayIp / parseable GatewayHttp); Socks5=$Socks5"
        return
    }
    if (-not (Test-Path -LiteralPath $netScript)) {
        Write-Warning "Set-ClaudeBoxGuestNetwork.ps1 missing at $netScript"
        return
    }
    $args = @{
        GatewayIp = $GwIp
        ProxyPort = $Port
    }
    if ($DoApply -and (Test-ClaudeBoxGuestIsWindows)) {
        Write-Host "[Guest] Invoking Set-ClaudeBoxGuestNetwork -Apply GatewayIp=$GwIp Port=$Port"
        & $netScript @args -Apply
    } else {
        Write-Host "[Guest] DRY-RUN would run Set-ClaudeBoxGuestNetwork GatewayIp=$GwIp Port=$Port"
        & $netScript @args
    }
}

Write-Host '=== Claude Box Guest Init (M3) ==='
Show-ClaudeBoxFakeEnvChecklist -Hostname $IdentityHostname -TimezoneId $Timezone -Culture $Locale -Wg $Workgroup

$identityResult = Set-ClaudeBoxGuestIdentity `
    -Hostname $IdentityHostname `
    -TimezoneId $Timezone `
    -Culture $Locale `
    -Wg $Workgroup `
    -DoApply:([bool]$Apply)

Invoke-ClaudeBoxGuestProxyWire `
    -Http $GatewayHttp `
    -Socks5 $GatewaySocks5 `
    -GwIp $GatewayIp `
    -Port $ProxyPort `
    -DoApply:([bool]$Apply)

$installScript = Join-Path $PSScriptRoot 'Install-ClaudeBoxWindowsClaude.ps1'
if (-not $SkipClaudeInstallGuide) {
    if (Test-Path -LiteralPath $installScript) {
        if ($Apply -and (Test-ClaudeBoxGuestIsWindows)) {
            Write-Host '[Guest] Claude install: running guide/install helper (see Install-ClaudeBoxWindowsClaude.ps1)'
            & $installScript -ShowGuide
        } else {
            Write-Host '[Guest] DRY-RUN Claude install guide:'
            & $installScript -ShowGuide
        }
    } else {
        Write-Host '[Guest] Install-ClaudeBoxWindowsClaude.ps1 not found; see Guest README'
    }
}

Write-Host '=== F5 reminder: host C: must not be among redirected/SMB visible roots ==='
Write-Host 'Next: Mount-ClaudeBoxGuestShares.ps1 (after host Mount-ClaudeBoxVisibleDirs)'
Write-Host 'Health: Test-ClaudeBoxGuestHealth.ps1 -ExpectedHostname ... -ExpectedTimezone ...'

if ($identityResult.RebootNeeded) {
    Write-Warning 'Identity changes may require reboot before hostname/locale fully stick.'
}
if (-not $Apply -or -not $identityResult.Applied) {
    Write-Host '=== Done (checklist / dry-run; pass -Apply inside Windows guest to mutate) ==='
} else {
    Write-Host '=== Apply attempted; verify with Test-ClaudeBoxGuestHealth.ps1 ==='
}

# Emit object for host-side capture when run via PowerShell Direct
$identityResult
