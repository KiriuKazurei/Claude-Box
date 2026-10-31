# ClaudeBox.Control

宿主机控制面：读/写配置、Hyper-V、Internal 交换机、**New-ClaudeBoxVM（Gen2）**、启停、**可见目录 SMB 挂载（默认拒绝宿主 C:）**、网关上游、假环境清单、Guest bundle、**M4 WinForms 控件**。

- **运行环境**：Windows 11 + Hyper-V（管理员：交换机 / VM / SMB）
- **M3**：`New-ClaudeBoxMountPlan`、`Mount-ClaudeBoxVisibleDirs -ApplyShares`、`Publish-ClaudeBoxGuestBundle`
- **M4**：`Save-ClaudeBoxConfig`、`Connect-ClaudeBoxVM`、[ui/Start-ClaudeBoxControlUi.ps1](./ui/Start-ClaudeBoxControlUi.ps1)
- **非职责**：透明代理实现、访客内装 Claude（见 Guest 脚本）

```powershell
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Test-ClaudeBoxHyperVReady
Get-ClaudeBoxConfig -Path .\config\config.example.yaml
# 控件 UI（Win11）
.\scripts\win11\Start-ClaudeBoxControlUi.ps1
# Linux / CI 逻辑自检
pwsh -File .\src\ClaudeBox.Control\ui\Start-ClaudeBoxControlUi.ps1 -SelfCheck
```

详见 [ui/README.md](./ui/README.md)。
