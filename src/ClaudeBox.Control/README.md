# ClaudeBox.Control

宿主机控制面：读配置、Hyper-V、Internal 交换机、**New-ClaudeBoxVM（Gen2）**、启停、**可见目录 SMB 挂载计划（默认拒绝宿主 C:）**、网关上游、假环境清单、Guest bundle 发布。

- **运行环境**：Windows 11 + Hyper-V（管理员：交换机 / VM / SMB）
- **M3**：`New-ClaudeBoxMountPlan`、`Mount-ClaudeBoxVisibleDirs -ApplyShares`、`Publish-ClaudeBoxGuestBundle`
- **非职责**：透明代理实现、访客内装 Claude（见 Guest 脚本）

```powershell
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Test-ClaudeBoxHyperVReady
Get-ClaudeBoxConfig -Path .\config\config.example.yaml
Get-ClaudeBoxFakeEnvPlan -ConfigPath .\config\config.example.yaml
# Admin on Win11:
# New-ClaudeBoxInternalSwitch
# New-ClaudeBoxVM -ConfigPath .\config\config.yaml -IsoPath D:\ISO\Win11.iso
# Mount-ClaudeBoxVisibleDirs -ConfigPath .\config\config.yaml -ApplyShares -PublishBundle
```
