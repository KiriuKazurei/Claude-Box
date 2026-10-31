@{
    RootModule        = 'ClaudeBox.Gateway.psm1'
    ModuleVersion     = '0.2.0'
    GUID              = 'b2c3d4e5-f6a7-8901-bcde-f12345678901'
    Author            = 'Claude Box'
    Description       = 'M2: gateway runtime config, JSONL connection logging, gost relay plan/start, direct-egress acceptance helpers.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @(
        'Test-ClaudeBoxGatewayConfig',
        'Get-ClaudeBoxGatewayDefaultLogPath',
        'Get-ClaudeBoxGatewayStateDir',
        'ConvertTo-ClaudeBoxGatewayRuntimeConfig',
        'Set-ClaudeBoxGatewayLogging',
        'Write-ClaudeBoxGatewayLogEntry',
        'Test-ClaudeBoxGatewayHostBlocked',
        'Get-ClaudeBoxClashUpstreamNotes',
        'Find-ClaudeBoxGostExecutable',
        'New-ClaudeBoxGatewayRelayPlan',
        'Save-ClaudeBoxGatewayRuntime',
        'Start-ClaudeBoxGateway',
        'Stop-ClaudeBoxGateway',
        'Test-ClaudeBoxDirectEgressBlocked',
        'Test-ClaudeBoxGatewayDirectBlocked'
    )
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
