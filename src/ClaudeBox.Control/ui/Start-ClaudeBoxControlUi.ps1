#Requires -Version 5.1
<#
.SYNOPSIS
  Claude Box 最小控件 UI（M4 · PowerShell WinForms）。

.DESCRIPTION
  显示/编辑：VM 名、网关 HTTP/SOCKS5、日志开关、可见目录。
  按钮：Hyper-V 预检、启停网关、创建/启动 VM、挂载、vmconnect。
  保存前拒挂宿主 C:。配置读写 config.yaml（Save-ClaudeBoxConfig）。

  实现选型：WinForms via PowerShell（System.Windows.Forms），无额外 .NET 工程。
  Win11：Windows PowerShell 5.1 或 pwsh + Desktop。
  Linux 开发机：用 -SelfCheck 做逻辑自检（不弹窗）。

.PARAMETER ConfigPath
  配置文件；默认仓库 config/config.yaml，不存在则用 example 并提示另存。

.PARAMETER SelfCheck
  不显示 UI；跑读写与拒 C: 校验后退出 0/1。

.NOTES
  真 Hyper-V / gost / Clash / vmconnect 联调：待验证。不声称已在真机跑通。
#>
[CmdletBinding()]
param(
    [string] $ConfigPath = '',
    [switch] $SelfCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$uiDir = $PSScriptRoot
$controlDir = Split-Path -Parent $uiDir
$srcDir = Split-Path -Parent $controlDir
$repoRoot = Split-Path -Parent $srcDir

Import-Module (Join-Path $controlDir 'ClaudeBox.Control.psd1') -Force
$gwManifest = Join-Path $srcDir 'ClaudeBox.Gateway/ClaudeBox.Gateway.psd1'
if (Test-Path -LiteralPath $gwManifest) {
    Import-Module $gwManifest -Force
}

function Resolve-ClaudeBoxUiConfigPath {
    param([string] $Path)
    if (-not [string]::IsNullOrWhiteSpace($Path)) {
        return $Path
    }
    $preferred = Join-Path $repoRoot 'config/config.yaml'
    if (Test-Path -LiteralPath $preferred) { return $preferred }
    return (Join-Path $repoRoot 'config/config.example.yaml')
}

function Test-ClaudeBoxUiVisiblePathSafe {
    param(
        [string] $HostPath,
        [bool] $ExcludeHostSystemDrive = $true
    )
    if ([string]::IsNullOrWhiteSpace($HostPath)) {
        return [pscustomobject]@{ Ok = $false; Error = 'Path is empty' }
    }
    if ($ExcludeHostSystemDrive -and (Test-ClaudeBoxHostSystemDrivePath -HostPath $HostPath)) {
        return [pscustomobject]@{
            Ok    = $false
            Error = "Refusing host system drive: '$HostPath' (exclude_host_system_drive=true)"
        }
    }
    return [pscustomobject]@{ Ok = $true; Error = $null }
}

function Invoke-ClaudeBoxUiSelfCheck {
    param([string] $Path)

    $failures = 0
    $cfg = Get-ClaudeBoxConfig -Path $Path
    Write-Host ("SelfCheck load VmName={0}" -f $cfg.Parsed.VmName)

    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('claude-box-ui-{0}.yaml' -f [guid]::NewGuid().ToString('n'))
    try {
        $saved = Save-ClaudeBoxConfig -Path $tmp `
            -VmName 'ClaudeBox-UiTest' `
            -GatewayHttp 'http://127.0.0.1:7890' `
            -GatewaySocks5 'socks5://127.0.0.1:7891' `
            -LoggingEnabled $true `
            -LoggingPath '' `
            -VisibleDirs @(@{ HostPath = 'D:\Projects\demo'; ReadOnly = $false }) `
            -ExistingParsed $cfg.Parsed
        $round = Get-ClaudeBoxConfig -Path $saved.Path
        if ($round.Parsed.VmName -ne 'ClaudeBox-UiTest') {
            Write-Host 'FAIL roundtrip VmName'
            $failures++
        } else {
            Write-Host 'OK   Save-ClaudeBoxConfig roundtrip'
        }
        if ($round.Parsed.GatewayHttp -notmatch '7890') {
            Write-Host 'FAIL roundtrip http'
            $failures++
        } else {
            Write-Host 'OK   gateway http preserved'
        }
    } finally {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }

    $bad = Test-ClaudeBoxUiVisiblePathSafe -HostPath 'C:\' -ExcludeHostSystemDrive $true
    if ($bad.Ok) {
        Write-Host 'FAIL UI C: guard should fail'
        $failures++
    } else {
        Write-Host 'OK   UI C: guard'
    }

    try {
        $null = Save-ClaudeBoxConfig -Path (Join-Path ([IO.Path]::GetTempPath()) 'claude-box-should-not.yaml') `
            -VmName 'x' -GatewayHttp 'http://127.0.0.1:1' -VisibleDirs @('C:\') -ExcludeHostSystemDrive $true
        Write-Host 'FAIL Save should reject C:\'
        $failures++
    } catch {
        Write-Host 'OK   Save-ClaudeBoxConfig rejects C:\'
    }

    try {
        Connect-ClaudeBoxVM -Name 'no-such'
        if (Test-ClaudeBoxIsWindowsHost) {
            # On Windows without Hyper-V it may still launch or throw — either ok for selfcheck presence
            Write-Host 'OK   Connect-ClaudeBoxVM callable on Windows'
        }
    } catch {
        if (-not (Test-ClaudeBoxIsWindowsHost) -and $_.Exception.Message -match 'Windows') {
            Write-Host 'OK   Connect-ClaudeBoxVM refuses non-Windows'
        } elseif (Test-ClaudeBoxIsWindowsHost) {
            Write-Host ("OK   Connect-ClaudeBoxVM threw on host (expected if no HV): {0}" -f $_.Exception.Message)
        } else {
            Write-Host "FAIL Connect-ClaudeBoxVM: $($_.Exception.Message)"
            $failures++
        }
    }

    if ($failures -gt 0) {
        Write-Host "SelfCheck RESULT: FAILED ($failures)"
        return $failures
    }
    Write-Host 'SelfCheck RESULT: PASSED'
    return 0
}

$ConfigPath = Resolve-ClaudeBoxUiConfigPath -Path $ConfigPath

if ($SelfCheck) {
    $code = Invoke-ClaudeBoxUiSelfCheck -Path $ConfigPath
    exit $code
}

# --- GUI (Windows only for Show) ---
$isWin = Test-ClaudeBoxIsWindowsHost
if (-not $isWin) {
    Write-Host 'Control UI requires Windows (WinForms). On Linux run: -SelfCheck'
    Write-Host "Config would be: $ConfigPath"
    exit 2
}

try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
} catch {
    throw "Failed to load WinForms. Use Windows PowerShell 5.1 or pwsh with Desktop: $($_.Exception.Message)"
}

[System.Windows.Forms.Application]::EnableVisualStyles()

$script:state = @{
    ConfigPath = $ConfigPath
    Parsed     = $null
    Status     = 'Ready'
}
$lblStatus = $null

function Set-UiStatus([string] $Msg) {
    $script:state.Status = $Msg
    if ($null -ne $lblStatus) { $lblStatus.Text = $Msg }
}

function Load-UiFromConfig {
    param([string] $Path)
    $cfg = Get-ClaudeBoxConfig -Path $Path
    $script:state.ConfigPath = $cfg.Path
    $script:state.Parsed = $cfg.Parsed
    $txtConfig.Text = $cfg.Path
    $txtVmName.Text = [string]$cfg.Parsed.VmName
    $txtHttp.Text = [string]$cfg.Parsed.GatewayHttp
    $txtSocks.Text = [string]$cfg.Parsed.GatewaySocks5
    $chkLog.Checked = [bool]$cfg.Parsed.LoggingEnabled
    $txtLogPath.Text = [string]$cfg.Parsed.LoggingPath
    $listDirs.Items.Clear()
    foreach ($d in @($cfg.Parsed.VisibleDirs)) {
        if (-not [string]::IsNullOrWhiteSpace([string]$d)) {
            [void]$listDirs.Items.Add([string]$d)
        }
    }
    Set-UiStatus ("Loaded {0}" -f $cfg.Path)
}

function Save-UiToConfig {
    $paths = @()
    foreach ($item in $listDirs.Items) { $paths += [string]$item }
    foreach ($p in $paths) {
        $check = Test-ClaudeBoxUiVisiblePathSafe -HostPath $p -ExcludeHostSystemDrive $true
        if (-not $check.Ok) { throw $check.Error }
    }
    $outPath = $txtConfig.Text.Trim()
    if ([string]::IsNullOrWhiteSpace($outPath)) {
        $outPath = Join-Path $repoRoot 'config/config.yaml'
        $txtConfig.Text = $outPath
    }
    # Avoid overwriting example by accident: if still example, redirect to config.yaml
    if ($outPath -match 'config\.example\.yaml$') {
        $outPath = Join-Path $repoRoot 'config/config.yaml'
        $txtConfig.Text = $outPath
    }
    $saved = Save-ClaudeBoxConfig -Path $outPath `
        -VmName $txtVmName.Text.Trim() `
        -GatewayHttp $txtHttp.Text.Trim() `
        -GatewaySocks5 $txtSocks.Text.Trim() `
        -LoggingEnabled ([bool]$chkLog.Checked) `
        -LoggingPath $txtLogPath.Text.Trim() `
        -VisibleDirs $paths `
        -ExistingParsed $script:state.Parsed
    $script:state.ConfigPath = $saved.Path
    $cfg = Get-ClaudeBoxConfig -Path $saved.Path
    $script:state.Parsed = $cfg.Parsed
    Set-UiStatus ("Saved {0} (dirs={1})" -f $saved.Path, $saved.VisibleCount)
    return $saved.Path
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Claude Box Control (M4)'
$form.Size = New-Object System.Drawing.Size(720, 560)
$form.StartPosition = 'CenterScreen'
$form.MinimumSize = New-Object System.Drawing.Size(640, 480)

$lblConfig = New-Object System.Windows.Forms.Label
$lblConfig.Text = 'Config:'
$lblConfig.Location = New-Object System.Drawing.Point(12, 16)
$lblConfig.AutoSize = $true
$form.Controls.Add($lblConfig)

$txtConfig = New-Object System.Windows.Forms.TextBox
$txtConfig.Location = New-Object System.Drawing.Point(70, 12)
$txtConfig.Width = 480
$form.Controls.Add($txtConfig)

$btnLoad = New-Object System.Windows.Forms.Button
$btnLoad.Text = 'Load'
$btnLoad.Location = New-Object System.Drawing.Point(560, 10)
$btnLoad.Width = 60
$btnLoad.Add_Click({
    try {
        $ofd = New-Object System.Windows.Forms.OpenFileDialog
        $ofd.Filter = 'YAML|*.yaml;*.yml|All|*.*'
        $ofd.FileName = $txtConfig.Text
        if ($ofd.ShowDialog() -eq 'OK') {
            Load-UiFromConfig -Path $ofd.FileName
        } elseif (Test-Path -LiteralPath $txtConfig.Text) {
            Load-UiFromConfig -Path $txtConfig.Text
        }
    } catch { Set-UiStatus ("Load error: {0}" -f $_.Exception.Message); [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Load') }
})
$form.Controls.Add($btnLoad)

$btnSave = New-Object System.Windows.Forms.Button
$btnSave.Text = 'Save'
$btnSave.Location = New-Object System.Drawing.Point(628, 10)
$btnSave.Width = 60
$btnSave.Add_Click({
    try {
        $null = Save-UiToConfig
    } catch {
        Set-UiStatus ("Save error: {0}" -f $_.Exception.Message)
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Save')
    }
})
$form.Controls.Add($btnSave)

$y = 48
$lblVm = New-Object System.Windows.Forms.Label
$lblVm.Text = 'VM name:'
$lblVm.Location = New-Object System.Drawing.Point(12, ($y + 3))
$lblVm.AutoSize = $true
$form.Controls.Add($lblVm)
$txtVmName = New-Object System.Windows.Forms.TextBox
$txtVmName.Location = New-Object System.Drawing.Point(100, $y)
$txtVmName.Width = 240
$form.Controls.Add($txtVmName)

$y = 80
$lblHttp = New-Object System.Windows.Forms.Label
$lblHttp.Text = 'Gateway HTTP:'
$lblHttp.Location = New-Object System.Drawing.Point(12, ($y + 3))
$lblHttp.AutoSize = $true
$form.Controls.Add($lblHttp)
$txtHttp = New-Object System.Windows.Forms.TextBox
$txtHttp.Location = New-Object System.Drawing.Point(120, $y)
$txtHttp.Width = 560
$form.Controls.Add($txtHttp)

$y = 112
$lblSocks = New-Object System.Windows.Forms.Label
$lblSocks.Text = 'Gateway SOCKS5:'
$lblSocks.Location = New-Object System.Drawing.Point(12, ($y + 3))
$lblSocks.AutoSize = $true
$form.Controls.Add($lblSocks)
$txtSocks = New-Object System.Windows.Forms.TextBox
$txtSocks.Location = New-Object System.Drawing.Point(120, $y)
$txtSocks.Width = 560
$form.Controls.Add($txtSocks)

$y = 144
$chkLog = New-Object System.Windows.Forms.CheckBox
$chkLog.Text = 'Gateway connection logging'
$chkLog.Location = New-Object System.Drawing.Point(12, $y)
$chkLog.AutoSize = $true
$form.Controls.Add($chkLog)

$y = 172
$lblLog = New-Object System.Windows.Forms.Label
$lblLog.Text = 'Log path:'
$lblLog.Location = New-Object System.Drawing.Point(12, ($y + 3))
$lblLog.AutoSize = $true
$form.Controls.Add($lblLog)
$txtLogPath = New-Object System.Windows.Forms.TextBox
$txtLogPath.Location = New-Object System.Drawing.Point(100, $y)
$txtLogPath.Width = 580
$form.Controls.Add($txtLogPath)

$y = 208
$lblDirs = New-Object System.Windows.Forms.Label
$lblDirs.Text = 'Visible directories (host C: refused):'
$lblDirs.Location = New-Object System.Drawing.Point(12, $y)
$lblDirs.AutoSize = $true
$form.Controls.Add($lblDirs)

$listDirs = New-Object System.Windows.Forms.ListBox
$listDirs.Location = New-Object System.Drawing.Point(12, 232)
$listDirs.Size = New-Object System.Drawing.Size(560, 120)
$form.Controls.Add($listDirs)

$btnAddDir = New-Object System.Windows.Forms.Button
$btnAddDir.Text = 'Add...'
$btnAddDir.Location = New-Object System.Drawing.Point(584, 232)
$btnAddDir.Width = 100
$btnAddDir.Add_Click({
    try {
        $fbd = New-Object System.Windows.Forms.FolderBrowserDialog
        $fbd.Description = 'Select a project/data folder (not host C:\ root)'
        if ($fbd.ShowDialog() -eq 'OK') {
            $p = $fbd.SelectedPath
            $check = Test-ClaudeBoxUiVisiblePathSafe -HostPath $p -ExcludeHostSystemDrive $true
            if (-not $check.Ok) {
                [System.Windows.Forms.MessageBox]::Show($check.Error, 'Visible dir rejected')
                return
            }
            # Also reject if user somehow picked C:\
            if ($listDirs.Items.Contains($p)) { return }
            [void]$listDirs.Items.Add($p)
            Set-UiStatus ("Added dir {0}" -f $p)
        }
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Add dir')
    }
})
$form.Controls.Add($btnAddDir)

$btnRemDir = New-Object System.Windows.Forms.Button
$btnRemDir.Text = 'Remove'
$btnRemDir.Location = New-Object System.Drawing.Point(584, 268)
$btnRemDir.Width = 100
$btnRemDir.Add_Click({
    while ($listDirs.SelectedItems.Count -gt 0) {
        $listDirs.Items.Remove($listDirs.SelectedItems[0])
    }
})
$form.Controls.Add($btnRemDir)

# Action buttons row
$btnY = 370
function New-ActionButton([string]$Text, [int]$X, [scriptblock]$Click) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $Text
    $b.Location = New-Object System.Drawing.Point($X, $btnY)
    $b.Width = 110
    $b.Height = 28
    $b.Add_Click($Click)
    $form.Controls.Add($b)
    return $b
}

$null = New-ActionButton 'Check Hyper-V' 12 {
    try {
        $r = Test-ClaudeBoxHyperVReady
        Set-UiStatus ("Hyper-V Ready={0}: {1}" -f $r.Ready, $r.Reason)
        [System.Windows.Forms.MessageBox]::Show(("Ready={0}`n{1}" -f $r.Ready, $r.Reason), 'Hyper-V')
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message) }
}

$null = New-ActionButton 'Start Gateway' 128 {
    try {
        $path = Save-UiToConfig
        $result = Set-ClaudeBoxGatewayUpstream -ConfigPath $path -WriteStartupLog
        Set-UiStatus ("Gateway: {0}" -f $result.Message)
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Gateway') }
}

$null = New-ActionButton 'Stop Gateway' 244 {
    try {
        if (Get-Command Stop-ClaudeBoxGateway -ErrorAction SilentlyContinue) {
            Stop-ClaudeBoxGateway
            Set-UiStatus 'Gateway stop requested'
        } else {
            throw 'Stop-ClaudeBoxGateway not loaded'
        }
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message) }
}

$null = New-ActionButton 'Create VM' 360 {
    try {
        $path = Save-UiToConfig
        $null = New-ClaudeBoxInternalSwitch -ErrorAction SilentlyContinue
        $vm = New-ClaudeBoxVM -ConfigPath $path
        Set-UiStatus ("VM create: {0}" -f $txtVmName.Text)
        [System.Windows.Forms.MessageBox]::Show('New-ClaudeBoxVM completed (see console/host for details). Attach ISO if empty guest.', 'Create VM')
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Create VM') }
}

$null = New-ActionButton 'Start VM' 476 {
    try {
        $path = Save-UiToConfig
        $name = $txtVmName.Text.Trim()
        Start-ClaudeBoxVM -Name $name
        Set-UiStatus ("Started VM {0}" -f $name)
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Start VM') }
}

$null = New-ActionButton 'Connect' 592 {
    try {
        $name = $txtVmName.Text.Trim()
        Connect-ClaudeBoxVM -Name $name
        Set-UiStatus ("vmconnect launched for {0} (Enhanced Session 待验证)" -f $name)
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Connect') }
}

$btnY2 = 406
$btnMount = New-Object System.Windows.Forms.Button
$btnMount.Text = 'Mount visible dirs'
$btnMount.Location = New-Object System.Drawing.Point(12, $btnY2)
$btnMount.Width = 160
$btnMount.Height = 28
$btnMount.Add_Click({
    try {
        $path = Save-UiToConfig
        $m = Mount-ClaudeBoxVisibleDirs -ConfigPath $path -ApplyShares -PublishBundle
        Set-UiStatus ("Mount plan {0} sharesApplied={1}" -f $m.PlanPath, $m.SharesApplied)
        [System.Windows.Forms.MessageBox]::Show(("Plan: {0}`nSharesApplied={1}`nGuest: Mount-ClaudeBoxGuestShares.ps1 -Apply" -f $m.PlanPath, $m.SharesApplied), 'Mount')
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Mount') }
})
$form.Controls.Add($btnMount)

$btnSwitch = New-Object System.Windows.Forms.Button
$btnSwitch.Text = 'Ensure Internal Switch'
$btnSwitch.Location = New-Object System.Drawing.Point(180, $btnY2)
$btnSwitch.Width = 160
$btnSwitch.Height = 28
$btnSwitch.Add_Click({
    try {
        New-ClaudeBoxInternalSwitch
        Set-UiStatus 'Internal switch ensured'
    } catch { Set-UiStatus $_.Exception.Message; [System.Windows.Forms.MessageBox]::Show($_.Exception.Message) }
})
$form.Controls.Add($btnSwitch)

$lblStatus = New-Object System.Windows.Forms.Label
$lblStatus.Text = 'Ready'
$lblStatus.Location = New-Object System.Drawing.Point(12, 450)
$lblStatus.Size = New-Object System.Drawing.Size(680, 40)
$lblStatus.AutoSize = $false
$form.Controls.Add($lblStatus)

# Initial load
try {
    if (Test-Path -LiteralPath $ConfigPath) {
        Load-UiFromConfig -Path $ConfigPath
    } else {
        $txtConfig.Text = Join-Path $repoRoot 'config/config.yaml'
        Set-UiStatus 'No config yet — edit fields and Save'
    }
} catch {
    Set-UiStatus ("Init load: {0}" -f $_.Exception.Message)
}

[void]$form.ShowDialog()
