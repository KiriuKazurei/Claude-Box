# Internal 交换机：访客默认路由 → 网关 IP（M2）

## 拓扑

```
[Guest NIC] — ClaudeBox-Internal — [Host vEthernet (ClaudeBox-Internal)]
   IP e.g. 192.168.87.10                IP = gateway.listen_ip (e.g. 192.168.87.1)
   Default gateway = 192.168.87.1         gost listens :listen_port (e.g. 3080)
                                         upstream → Clash 127.0.0.1:7890
```

## 宿主

1. `New-ClaudeBoxSwitch.ps1` 已把 `listen_ip` 配到 `vEthernet (ClaudeBox-Internal)`。
2. `Start-ClaudeBoxGateway.ps1` 让 gost（若已安装）绑在 `listen_ip:listen_port`。
3. **不要**对 Internal 网开「无限制 ICS/NAT 直出公网」——否则访客可绕过盒子网关（**待验证**加固）。
4. 宿主防火墙需允许从 Internal 子网入站到 `listen_port`（**待验证**）。

## 访客

运行（管理员）：

```powershell
# 拷贝进访客后：
.\Set-ClaudeBoxGuestNetwork.ps1 -GatewayIp 192.168.87.1 -ProxyPort 3080 -GuestIp 192.168.87.10 -PrefixLength 24 -Apply
```

脚本会：

- 给访客 NIC 设静态 IP（可选）/ 默认网关 = `GatewayIp`
- 设系统代理 = `http://GatewayIp:ProxyPort`
- 打印防火墙加固建议（默认不自动改防火墙，避免锁死会话）

## 验收

见 `Test-ClaudeBoxDirectEgressBlocked.ps1`：经代理成功；`--noproxy "*"` 直连必须失败。

**状态**：文档 + 脚本已齐；真机 Hyper-V 联调 **待验证**。
