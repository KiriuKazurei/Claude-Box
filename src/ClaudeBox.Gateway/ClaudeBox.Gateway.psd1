@{
    RootModule        = 'ClaudeBox.Gateway.psm1'
    ModuleVersion     = '0.1.0'
    GUID              = 'b2c3d4e5-f6a7-8901-bcde-f12345678901'
    Author            = 'Claude Box'
    Description       = 'Gateway config, Clash upstream notes, connection-logging placeholders, host relay stubs.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @(
        'Test-ClaudeBoxGatewayConfig',
        'Get-ClaudeBoxGatewayDefaultLogPath',
        'Set-ClaudeBoxGatewayLogging',
        'Get-ClaudeBoxClashUpstreamNotes',
        'Start-ClaudeBoxGateway',
        'Stop-ClaudeBoxGateway',
        'Test-ClaudeBoxGatewayDirectBlocked'
    )
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
