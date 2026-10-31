# ClaudeBox.Guest

在 **Hyper-V Windows 访客内** 运行：假环境 Apply（F1–F4）、网络强制、可见目录映射、Windows Claude（MSIX）安装指引、健康检查。

- 「假 C」= 访客自有系统盘 + identity，**不是**宿主 C: 映射
- 默认 dry-run；访客内加 `-Apply` 才改系统（**M3**）
- 非 Windows 上 `-Apply` 仍只打印将执行的命令（脚本完整，便于 CI）
- 安装包不进仓库

## 假环境

```powershell
# Inside guest (admin):
.\Initialize-ClaudeBoxGuest.ps1 -IdentityHostname claude-box -Timezone UTC -Locale en-US -Workgroup CLAUDEBOX -GatewayIp 192.168.87.1 -ProxyPort 3080
.\Initialize-ClaudeBoxGuest.ps1 ... -Apply
```

## Windows Claude（MSIX）

```powershell
.\Install-ClaudeBoxWindowsClaude.ps1 -ShowGuide
# Optional (egress via gateway 待验证):
# .\Install-ClaudeBoxWindowsClaude.ps1 -Download -Arch x64
# .\Install-ClaudeBoxWindowsClaude.ps1 -Install -LocalMsix $env:TEMP\Claude-Desktop-x64.msix
```

官方：https://claude.com/download ；企业 MSIX：`Add-AppxPackage` / `Add-AppxProvisionedPackage`（见脚本头注释）。Computer use 在访客内 **待验证**。

## 挂载（访客侧）

宿主机先：`Mount-ClaudeBoxVisibleDirs -ApplyShares` → 得到 `mount-plan.json`，拷进访客后：

```powershell
.\Mount-ClaudeBoxGuestShares.ps1 -PlanPath .\mount-plan.json
.\Mount-ClaudeBoxGuestShares.ps1 -PlanPath .\mount-plan.json -Apply
```

## 健康检查

```powershell
.\Test-ClaudeBoxGuestHealth.ps1 -ExpectedHostname claude-box -ExpectedTimezone UTC -ExpectedLocale en-US -ExpectedMountLetters Z: -MountPlanPath .\mount-plan.json
```

## 网络（M2）

```powershell
.\Set-ClaudeBoxGuestNetwork.ps1 -GatewayIp 192.168.87.1 -ProxyPort 3080 -GuestIp 192.168.87.10 -Apply
```
