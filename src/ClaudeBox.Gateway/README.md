# ClaudeBox.Gateway

唯一网关：Internal 网上访客流量 → HTTP/SOCKS5 上游（常为宿主 **Clash**）+ **本地连接日志**。

- **部署**：优先宿主机进程绑在 `listen_ip`
- **M1**：`Set-ClaudeBoxGatewayLogging`、Clash 上游说明
- **M2**：真中继进程 + 写 JSONL（SNI/IP/端口）
- **硬要求**：不能只靠环境变量；验收见 `Test-ClaudeBoxGatewayDirectBlocked`

```powershell
Import-Module .\src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1
Set-ClaudeBoxGatewayLogging -Enabled $true
Get-ClaudeBoxClashUpstreamNotes
```
