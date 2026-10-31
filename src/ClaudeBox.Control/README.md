# ClaudeBox.Control

宿主机控制面：读配置、检查 Hyper-V、创建 Internal 交换机、**New-ClaudeBoxVM（Gen2）**、启停 VM、可见目录（**默认拒绝宿主 C:**）、网关上游与假环境清单。

- **运行环境**：Windows 11 + Hyper-V（管理员权限做交换机/VM）
- **M1**：`New-ClaudeBoxVM`、C: 护栏、最小 YAML 字段抽取
- **非职责**：透明代理实现、访客内装 Claude

```powershell
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Test-ClaudeBoxHyperVReady
Get-ClaudeBoxConfig -Path .\config\config.example.yaml
# Admin on Win11:
# New-ClaudeBoxInternalSwitch
# New-ClaudeBoxVM -ConfigPath .\config\config.yaml -IsoPath D:\ISO\Win11.iso
# Mount-ClaudeBoxVisibleDirs -ConfigPath .\config\config.yaml
```
