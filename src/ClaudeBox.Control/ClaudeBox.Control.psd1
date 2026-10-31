@{
    RootModule        = 'ClaudeBox.Control.psm1'
    ModuleVersion     = '0.0.1'
    GUID              = 'a1b2c3d4-e5f6-7890-abcd-ef1234567890'
    Author            = 'Claude Box'
    Description       = 'Host control plane: Hyper-V VM lifecycle, mounts, gateway wiring.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @(
        'Test-ClaudeBoxIsWindowsHost',
        'Get-ClaudeBoxConfig',
        'Test-ClaudeBoxHyperVReady',
        'New-ClaudeBoxInternalSwitch',
        'New-ClaudeBoxVM',
        'Start-ClaudeBoxVM',
        'Stop-ClaudeBoxVM',
        'Mount-ClaudeBoxVisibleDirs',
        'Set-ClaudeBoxGatewayUpstream'
    )
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
