# Structure check for wsl-box (runs on Linux pwsh or Windows)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not (Test-Path (Join-Path $root 'src/wsl-box'))) {
  $root = Resolve-Path (Join-Path $PSScriptRoot '..')
}
$base = Join-Path $root 'src/wsl-box'
$need = @(
  'README.md',
  'config/wsl-box.example.yaml',
  'host/New-ClaudeBoxWsl.ps1',
  'host/Set-ClaudeBoxWslMounts.ps1',
  'host/Start-ClaudeBoxWsl.ps1',
  'host/Invoke-ClaudeBoxGuest.ps1',
  'host/Watch-ClaudeBoxLocalIp.ps1',
  'guest/apply-spoof.sh',
  'guest/install-claude-desktop.sh',
  'guest/start-claude-desktop.sh',
  'guest/verify-spoof.sh',
  'guest/gateway-tips.sh',
  'guest/apply-host-identity.sh',
  'guest/verify-host-identity.sh',
  'guest/lib/host-identity.py',
  'guest/lib/identity-engine.sh',
  'host/Export-ClaudeBoxHostIdentity.ps1',
  'linux/Install-ClaudeBoxFramework.sh',
  'linux/Run-Smoke.sh',
  'linux/Install-ClaudeDesktop.sh',
  'linux/lib/common.sh',
  'linux/lib/load-config.sh',
  'companion-browser/open-oauth-url.sh',
  'identity-vault/README.md'
)
foreach ($r in $need) {
  $p = Join-Path $base $r
  if (-not (Test-Path -LiteralPath $p)) { throw "missing $p" }
}
$doc = Join-Path $root 'docs/architecture-v2-wsl.md'
if (-not (Test-Path $doc)) { throw "missing $doc" }
Write-Host "OK wsl-box structure"
