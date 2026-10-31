@{
    RootModule        = 'ClaudeBox.Gateway.psm1'
    ModuleVersion     = '0.0.1'
    GUID              = 'b2c3d4e5-f6a7-8901-bcde-f12345678901'
    Author            = 'Claude Box'
    Description       = 'Gateway config helpers and host-side proxy process placeholders.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @(
        'Test-ClaudeBoxGatewayConfig',
        'Start-ClaudeBoxGateway',
        'Stop-ClaudeBoxGateway',
        'Test-ClaudeBoxGatewayDirectBlocked'
    )
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
