# scripts/win11

宿主机一键入口（需在 **Windows 11** 上以适当权限运行）。

| 脚本 | 对应验证 | 说明 |
|---|---|---|
| `Test-HyperVPrereqs.ps1` | HV-1 | 检查 Hyper-V 模块/功能 |
| `New-ClaudeBoxSwitch.ps1` | HV-2 | 创建 Internal 交换机（需管理员） |

完整清单见 `docs/architecture-v0.md` §11。

在仓库根目录：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\win11\Test-HyperVPrereqs.ps1
# Admin:
.\scripts\win11\New-ClaudeBoxSwitch.ps1
```
