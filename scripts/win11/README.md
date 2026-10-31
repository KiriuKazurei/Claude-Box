# scripts/win11

宿主机一键入口（需在 **Windows 11** 上以适当权限运行）。**当前阶段：M1 空访客。**

| 脚本 | 对应验证 | 说明 |
|---|---|---|
| `Test-HyperVPrereqs.ps1` | HV-1 | 检查 Hyper-V 模块/功能 |
| `New-ClaudeBoxSwitch.ps1` | HV-2 | Internal 交换机 + 网关 IP（管理员） |
| `New-ClaudeBoxVM.ps1` | HV-3 / M1 | Gen2 VM；ISO/模板可选；不挂宿主 C: |

完整清单见 `docs/architecture-v0.md` §11。

## M1 建议顺序

```powershell
Set-ExecutionPolicy -Scope Process Bypass
cd path\to\Claude-Box

.\scripts\win11\Test-HyperVPrereqs.ps1

# Admin:
.\scripts\win11\New-ClaudeBoxSwitch.ps1

Copy-Item .\config\config.example.yaml .\config\config.yaml
# 编辑：vm.name / memory / iso_path 或 template_vhdx、gateway.*、visible_dirs（勿写 C:\）

# Admin — 有 ISO 时：
.\scripts\win11\New-ClaudeBoxVM.ps1 -IsoPath D:\ISO\Win11.iso

Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Start-ClaudeBoxVM -Name ClaudeBox-Win -Connect

# 装好系统后，在访客内跑 Guest 假环境清单：
#   Initialize-ClaudeBoxGuest.ps1
# 挂载校验（宿主侧，应拒绝 C:\）：
Mount-ClaudeBoxVisibleDirs -ConfigPath .\config\config.yaml
Get-ClaudeBoxFakeEnvPlan -ConfigPath .\config\config.yaml
```

网关真中继与连接日志写入属 **M2**；M1 可先 `Set-ClaudeBoxGatewayLogging` 建日志目录。
