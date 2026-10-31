# scripts/win11

宿主机一键入口（需在 **Windows 11** 上以适当权限运行）。**当前阶段：M3 Claude + 挂载。**

| 脚本 | 对应验证 | 说明 |
|---|---|---|
| `Test-HyperVPrereqs.ps1` | HV-1 | 检查 Hyper-V 模块/功能 |
| `New-ClaudeBoxSwitch.ps1` | HV-2 | Internal 交换机 + 网关 IP（管理员） |
| `New-ClaudeBoxVM.ps1` | HV-3 / M1 | Gen2 VM；ISO/模板可选；不挂宿主 C: |
| `Start-ClaudeBoxGateway.ps1` | HV-5 / M2 | 应用配置、JSONL、gost 中继计划/启停 |
| `Stop-ClaudeBoxGateway.ps1` | M2 | 按 PID 停网关 |
| `Test-ClaudeBoxDirectEgressBlocked.ps1` | HV-6 / M2 | 直连应失败的验收说明 / SelfCheck |
| `Mount-ClaudeBoxVisibleDirs.ps1` | HV-9 / M3 | 挂载计划 JSON；`-ApplyShares` 建 SMB；`-PublishBundle` |
| `Set-ClaudeBoxInternalGatewayRouting.md` | HV-4…6 | 访客默认路由 → 网关 IP |

完整清单见 `docs/architecture-v0.md` §11。Guest 侧见 `src/ClaudeBox.Guest/README.md`。

## M3 建议顺序

```powershell
Set-ExecutionPolicy -Scope Process Bypass
cd path\to\Claude-Box
Copy-Item .\config\config.example.yaml .\config\config.yaml

# Admin — 若未建交换机/VM/网关，先完成 M1/M2
.\scripts\win11\Mount-ClaudeBoxVisibleDirs.ps1 -ConfigPath .\config\config.yaml -ApplyShares -PublishBundle

# 将 %LOCALAPPDATA%\ClaudeBox\guest-bundle 拷进访客后：
# Initialize-ClaudeBoxGuest.ps1 -Apply
# Install-ClaudeBoxWindowsClaude.ps1 -ShowGuide
# Mount-ClaudeBoxGuestShares.ps1 -PlanPath .\mount-plan.json -Apply
# Test-ClaudeBoxGuestHealth.ps1 ...
```

**待验证**：真 SMB 跨 Internal、Copy-VMFile、Claude MSIX 经网关下载、Computer use。

## M4 控件 UI

```powershell
.\scripts\win11\Start-ClaudeBoxControlUi.ps1
.\scripts\win11\Start-ClaudeBoxControlUi.ps1 -SelfCheck
```

见 `src/ClaudeBox.Control/ui/README.md`。

