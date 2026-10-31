# ClaudeBox.Control

宿主机控制面：读配置、检查 Hyper-V、创建 Internal 交换机、启停 VM、挂载可见目录、把上游交给 Gateway。

- **运行环境**：Windows 11 + Hyper-V（需管理员权限做交换机/VM）
- **M0**：函数占位 + `Get-ClaudeBoxConfig` 读文件
- **非职责**：透明代理实现、访客内装 Claude

```powershell
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Test-ClaudeBoxHyperVReady
Get-ClaudeBoxConfig -Path .\config\config.example.yaml
```
