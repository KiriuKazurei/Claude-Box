#Requires -Version 5.1
<#
.SYNOPSIS
  Windows Claude Desktop 安装指引与可选静默安装（MSIX）。

.DESCRIPTION
  官方 Windows 包为 **MSIX**（非 MSI）。公开文档：
  - 下载页: https://claude.com/download
  - 企业部署: https://support.claude.com/en/articles/12622703-deploy-claude-desktop-for-windows
  - 文档安装说明: https://claude.com/docs/third-party/claude-desktop/installation

  已知固定 redirect（跟随 HTTP 重定向拿到当前构建；**版本随 rollout 变，待验证**）:
  - x64:  https://claude.ai/api/desktop/win32/x64/msix/latest/redirect
  - arm64: https://claude.ai/api/desktop/win32/arm64/msix/latest/redirect
  Offline 变体（更大，含 Cowork 运行时组件）:
  - x64:  https://claude.ai/api/desktop/win32/x64/offline/latest/redirect
  - arm64: https://claude.ai/api/desktop/win32/arm64/offline/latest/redirect

  静默安装（公开可知）:
  - 当前用户: Add-AppxPackage -Path Claude.msix
  - 整机预配: Add-AppxProvisionedPackage -Online -PackagePath Claude.msix -SkipLicense -Regions all
  无厂商专有 /quiet 开关；MSIX 本身无交互。安装包 **不进 git**。

  Computer use / Cowork 在 Hyper-V 访客内是否完整可用：**待验证**（嵌套虚拟化 / Virtual Machine Platform）。

.PARAMETER ShowGuide
  只打印步骤与 URL。

.PARAMETER Download
  尝试下载最新 MSIX 到 -OutDir（需出站经盒子网关；真机待验证）。

.PARAMETER Install
  对已有 LocalMsix 执行 Add-AppxPackage（需 Windows + 适当权限）。

.PARAMETER ProvisionMachine
  使用 Add-AppxProvisionedPackage（管理员；整机）。

.PARAMETER Arch
  x64 或 arm64。

.PARAMETER Offline
  使用 offline redirect URL。

.PARAMETER LocalMsix
  本地已下载的 .msix 路径。

.PARAMETER OutDir
  下载目录。
#>
[CmdletBinding()]
param(
    [switch] $ShowGuide,
    [switch] $Download,
    [switch] $Install,
    [switch] $ProvisionMachine,
    [ValidateSet('x64', 'arm64')]
    [string] $Arch = 'x64',
    [switch] $Offline,
    [string] $LocalMsix = '',
    [string] $OutDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ClaudeBoxClaudeMsixUrl {
    param([string] $Architecture, [bool] $UseOffline)
    $kind = if ($UseOffline) { 'offline' } else { 'msix' }
    return "https://claude.ai/api/desktop/win32/$Architecture/$kind/latest/redirect"
}

function Show-ClaudeBoxClaudeInstallGuide {
    param([string] $Architecture, [bool] $UseOffline)
    $url = Get-ClaudeBoxClaudeMsixUrl -Architecture $Architecture -UseOffline $UseOffline
    Write-Host '=== Windows Claude Desktop install (M3) ==='
    Write-Host 'Official download page : https://claude.com/download'
    Write-Host 'Enterprise deploy doc  : https://support.claude.com/en/articles/12622703-deploy-claude-desktop-for-windows'
    Write-Host 'Installer format       : MSIX (prefer over legacy exe for full features / Cowork)'
    Write-Host ("Redirect URL ({0}{1}): {2}" -f $Architecture, $(if ($UseOffline) { ', offline' } else { '' }), $url)
    Write-Host ''
    Write-Host 'Manual steps (guest, admin recommended for full features):'
    Write-Host '  1. Ensure guest egress goes through Claude Box gateway (M2).'
    Write-Host '  2. Download MSIX via browser or:'
    Write-Host ("     Invoke-WebRequest -Uri '{0}' -OutFile `$env:TEMP\Claude.msix -MaximumRedirection 5" -f $url)
    Write-Host '  3. Per-user silent register:'
    Write-Host '     Add-AppxPackage -Path $env:TEMP\Claude.msix'
    Write-Host '  4. Or machine-wide provision (elevated):'
    Write-Host '     Add-AppxProvisionedPackage -Online -PackagePath $env:TEMP\Claude.msix -SkipLicense -Regions all'
    Write-Host '  5. Optional Cowork: Enable-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -All'
    Write-Host '     (Inside Hyper-V guest this needs nested virtualization — 待验证)'
    Write-Host '  6. Start Claude from Start menu; sign in; smoke Computer use — 待验证'
    Write-Host ''
    Write-Host 'Do NOT commit the installer into the Claude-Box git repo.'
}

function Test-IsWindowsEnv {
    return ($env:OS -eq 'Windows_NT') -or ($PSVersionTable.Platform -eq 'Win32NT')
}

if ($ShowGuide -or (-not $Download -and -not $Install -and -not $ProvisionMachine)) {
    Show-ClaudeBoxClaudeInstallGuide -Architecture $Arch -UseOffline:([bool]$Offline)
}

$url = Get-ClaudeBoxClaudeMsixUrl -Architecture $Arch -UseOffline:([bool]$Offline)

if ($Download) {
    if (-not $OutDir) {
        if ($env:TEMP) { $OutDir = $env:TEMP } else { $OutDir = [System.IO.Path]::GetTempPath() }
    }
    if (-not (Test-Path -LiteralPath $OutDir)) {
        New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    }
    $dest = Join-Path $OutDir ("Claude-Desktop-{0}.msix" -f $Arch)
    Write-Host "Downloading (follows redirect) -> $dest"
    Write-Host "URL: $url"
    if (-not (Test-IsWindowsEnv) -and -not $PSVersionTable.PSEdition) {
        Write-Warning 'Non-Windows: download may work via curl; install will not.'
    }
    try {
        if (Get-Command Invoke-WebRequest -ErrorAction SilentlyContinue) {
            Invoke-WebRequest -Uri $url -OutFile $dest -MaximumRedirection 8 -UseBasicParsing
        } else {
            & curl.exe -L -o $dest $url
        }
        $LocalMsix = $dest
        Write-Host "Downloaded: $LocalMsix"
        Write-Host 'NOTE: 真机经网关下载 待验证; checksum 以官方包为准。'
    } catch {
        Write-Warning "Download failed: $($_.Exception.Message)"
        Write-Host 'Fallback: open https://claude.com/download in guest browser (via gateway).'
    }
}

if ($Install -or $ProvisionMachine) {
    if (-not $LocalMsix -or -not (Test-Path -LiteralPath $LocalMsix)) {
        throw 'LocalMsix required and must exist for -Install / -ProvisionMachine. Download first or pass -LocalMsix.'
    }
    if (-not (Test-IsWindowsEnv)) {
        Write-Warning 'Not Windows: cannot Add-AppxPackage. Guide only.'
        Show-ClaudeBoxClaudeInstallGuide -Architecture $Arch -UseOffline:([bool]$Offline)
        return [pscustomobject]@{ Ok = $false; Reason = 'non-windows'; LocalMsix = $LocalMsix }
    }
    if ($ProvisionMachine) {
        Write-Host "Add-AppxProvisionedPackage -Online -PackagePath $LocalMsix"
        Add-AppxProvisionedPackage -Online -PackagePath $LocalMsix -SkipLicense -Regions 'all'
    } else {
        Write-Host "Add-AppxPackage -Path $LocalMsix"
        Add-AppxPackage -Path $LocalMsix
    }
    Write-Host 'Install command completed. Verify Start Menu → Claude. Computer use 待验证.'
    return [pscustomobject]@{ Ok = $true; LocalMsix = $LocalMsix; Provisioned = [bool]$ProvisionMachine; Verified = $false }
}

return [pscustomobject]@{
    GuideShown = $true
    MsixUrl    = $url
    Arch       = $Arch
    Verified   = $false
    Note       = 'Silent path = Add-AppxPackage / Add-AppxProvisionedPackage; no proprietary /quiet flag.'
}
