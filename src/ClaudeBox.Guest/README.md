# ClaudeBox.Guest

在 **Hyper-V Windows 访客内** 运行：假环境清单（F1–F7）、系统代理/强制出站、引导安装 Windows Claude、健康检查。

- 「假 C」= 访客自有系统盘 + identity，**不是**宿主 C: 映射
- 由 Control 注入；除可见目录外不假设与宿主共享盘
- 默认 dry-run；访客内加 `-Apply` 才改系统（M3）
- 安装包不进仓库

```powershell
# Inside guest:
.\Initialize-ClaudeBoxGuest.ps1 -IdentityHostname claude-box -Timezone UTC -GatewayHttp http://192.168.87.1:8080
.\Initialize-ClaudeBoxGuest.ps1 -Apply   # after review
.\Test-ClaudeBoxGuestHealth.ps1
```

## M2 网络（访客内）

`Set-ClaudeBoxGuestNetwork.ps1`：默认网关 → 盒子 `listen_ip`；系统代理 → `http://listen_ip:listen_port`。

```powershell
.\Set-ClaudeBoxGuestNetwork.ps1 -GatewayIp 192.168.87.1 -ProxyPort 3080 -GuestIp 192.168.87.10 -Apply
```

真机效果 **待验证**。说明见 `scripts/win11/Set-ClaudeBoxInternalGatewayRouting.md`。
