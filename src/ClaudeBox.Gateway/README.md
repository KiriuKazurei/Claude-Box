# ClaudeBox.Gateway

唯一网关：把 Internal 网上的访客流量转到控件配置的 HTTP/SOCKS5 上游。

- **部署**：优先宿主机进程绑在 `listen_ip`；不稳再评专用网关 VM
- **M0**：配置检查 + Start/Stop 占位 + 直连失败验收说明
- **硬要求**：不能只靠环境变量；验收见 `Test-ClaudeBoxGatewayDirectBlocked`

详见 `docs/architecture-v0.md` §4。
