# scripts/win11

宿主机一键入口（需在 **Windows 11** 上以适当权限运行）。**当前阶段：M2 网关 + 日志。**

| 脚本 | 对应验证 | 说明 |
|---|---|---|
| `Test-HyperVPrereqs.ps1` | HV-1 | 检查 Hyper-V 模块/功能 |
| `New-ClaudeBoxSwitch.ps1` | HV-2 | Internal 交换机 + 网关 IP（管理员） |
| `New-ClaudeBoxVM.ps1` | HV-3 / M1 | Gen2 VM；ISO/模板可选；不挂宿主 C: |
| `Start-ClaudeBoxGateway.ps1` | HV-5 / M2 | 应用配置、JSONL、gost 中继计划/启停 |
| `Stop-ClaudeBoxGateway.ps1` | M2 | 按 PID 停网关 |
| `Test-ClaudeBoxDirectEgressBlocked.ps1` | HV-6 / M2 | 直连应失败的验收说明 / SelfCheck |
| `Set-ClaudeBoxInternalGatewayRouting.md` | HV-4…6 | 访客默认路由 → 网关 IP |

完整清单见 `docs/architecture-v0.md` §11。网关细节见 `src/ClaudeBox.Gateway/README.md`。

## M2 建议顺序

```powershell
Set-ExecutionPolicy -Scope Process Bypass
cd path\to\Claude-Box

# Admin — Internal 交换机（若未建）
.\scripts\win11\New-ClaudeBoxSwitch.ps1

Copy-Item .\config\config.example.yaml .\config\config.yaml
# 编辑 gateway.http/socks5（Clash）、listen_ip/port、logging、blocklist

# 安装 gost.exe 到 %LOCALAPPDATA%\ClaudeBox\bin\

.\scripts\win11\Start-ClaudeBoxGateway.ps1 -ConfigPath .\config\config.yaml -WriteStartupLog

# 访客内：
#   Set-ClaudeBoxGuestNetwork.ps1 -GatewayIp 192.168.87.1 -ProxyPort 3080 -GuestIp 192.168.87.10 -Apply

.\scripts\win11\Test-ClaudeBoxDirectEgressBlocked.ps1
```

**待验证**：真 Hyper-V 上 gost 绑定 Internal NIC、Clash 上游、访客直连失败、JSONL 中 SNI 采全。

## M1 回顾

```powershell
.\scripts\win11\Test-HyperVPrereqs.ps1
.\scripts\win11\New-ClaudeBoxSwitch.ps1
.\scripts\win11\New-ClaudeBoxVM.ps1 -IsoPath D:\ISO\Win11.iso
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Start-ClaudeBoxVM -Name ClaudeBox-Win -Connect
```
