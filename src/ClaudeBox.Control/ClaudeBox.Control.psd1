@{
    RootModule        = 'ClaudeBox.Control.psm1'
    ModuleVersion     = '0.4.0'
    GUID              = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890'
    Author            = 'Claude Box'
    Description       = 'Host control plane: Hyper-V VM lifecycle, SMB mounts (no host C:), gateway wiring, Save-ClaudeBoxConfig + WinForms UI helpers.'
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
        'Connect-ClaudeBoxVM',
        'New-ClaudeBoxMountPlan',
        'Save-ClaudeBoxMountPlan',
        'Publish-ClaudeBoxGuestBundle',
        'Mount-ClaudeBoxVisibleDirs',
        'Set-ClaudeBoxGatewayUpstream',
        'Get-ClaudeBoxFakeEnvPlan',
        'ConvertTo-ClaudeBoxYamlDoubleQuoted',
        'Save-ClaudeBoxConfig'
    )
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
