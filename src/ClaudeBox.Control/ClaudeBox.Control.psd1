@{
    RootModule        = 'ClaudeBox.Control.psm1'
    ModuleVersion     = '0.1.0'
    GUID              = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890'
    Author            = 'Claude Box'
    Description       = 'Host control plane: Hyper-V VM lifecycle, mounts (no host C:), gateway wiring, fake-env plan.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @(
        'Test-ClaudeBoxIsWindowsHost',
        'Get-ClaudeBoxConfig',
        'Get-ClaudeBoxYamlScalar',
        'Test-ClaudeBoxHostSystemDrivePath',
        'Assert-ClaudeBoxVisibleDirsSafe',
        'Test-ClaudeBoxHyperVReady',
        'New-ClaudeBoxInternalSwitch',
        'New-ClaudeBoxVM',
        'Start-ClaudeBoxVM',
        'Stop-ClaudeBoxVM',
        'Mount-ClaudeBoxVisibleDirs',
        'Set-ClaudeBoxGatewayUpstream',
        'Get-ClaudeBoxFakeEnvPlan'
    )
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
