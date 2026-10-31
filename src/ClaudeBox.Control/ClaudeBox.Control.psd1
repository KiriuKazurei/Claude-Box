@{
    RootModule        = 'ClaudeBox.Control.psm1'
    ModuleVersion     = '0.3.0'
    GUID              = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890'
    Author            = 'Claude Box'
    Description       = 'Host control plane: Hyper-V VM lifecycle, SMB visible-dir mounts (no host C:), gateway wiring, fake-env plan, guest bundle publish.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @(
        'Test-ClaudeBoxIsWindowsHost',
        'Get-ClaudeBoxConfig',
        'Get-ClaudeBoxGatewayBlocklist',
        'Get-ClaudeBoxYamlScalar',
        'Get-ClaudeBoxVisibleDirEntries',
        'Test-ClaudeBoxHostSystemDrivePath',
        'Assert-ClaudeBoxVisibleDirsSafe',
        'Test-ClaudeBoxHyperVReady',
        'New-ClaudeBoxInternalSwitch',
        'New-ClaudeBoxVM',
        'Start-ClaudeBoxVM',
        'Stop-ClaudeBoxVM',
        'New-ClaudeBoxMountPlan',
        'Save-ClaudeBoxMountPlan',
        'Publish-ClaudeBoxGuestBundle',
        'Mount-ClaudeBoxVisibleDirs',
        'Set-ClaudeBoxGatewayUpstream',
        'Get-ClaudeBoxFakeEnvPlan'
    )
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
