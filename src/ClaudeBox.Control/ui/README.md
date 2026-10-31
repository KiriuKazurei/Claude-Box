# Claude Box Control UI（M4）

最小宿主机控件：**PowerShell + WinForms**（`System.Windows.Forms`），无独立 .csproj / Avalonia 工程，便于在仓库内维护。

## Win11 启动

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass

# 可选：从示例复制配置
Copy-Item .\config\config.example.yaml .\config\config.yaml

# 图形界面（建议管理员）
.\scripts\win11\Start-ClaudeBoxControlUi.ps1

# 或直接
.\src\ClaudeBox.Control\ui\Start-ClaudeBoxControlUi.ps1 -ConfigPath .\config\config.yaml
```

**要求**

- Windows 11（或能加载 WinForms 的 Windows）
- **Windows PowerShell 5.1**（自带 WinForms），或 **PowerShell 7 + Windows Desktop**（`pwsh` 需能 `Add-Type System.Windows.Forms`）
- Hyper-V / 网关 / SMB 等动作需相应功能与常管理员权限

不需要单独安装 .NET SDK 来「编译」本 UI；若以后改成 C# WPF/WinForms 工程，再用 `dotnet run`。

## Linux / CI

无 GUI。跑逻辑自检：

```bash
pwsh -File src/ClaudeBox.Control/ui/Start-ClaudeBoxControlUi.ps1 -SelfCheck
```

## 能力

| 控件 | 行为 |
|---|---|
| Load / Save | 读写 `config.yaml`（`Save-ClaudeBoxConfig`）；保存时避免覆盖 `config.example.yaml` |
| VM name / HTTP / SOCKS5 / 日志 | 编辑后 Save 或随动作自动 Save |
| Visible dirs | 文件夹选择；**拒宿主 `C:`** |
| Check Hyper-V | `Test-ClaudeBoxHyperVReady` |
| Start/Stop Gateway | `Set-ClaudeBoxGatewayUpstream` / `Stop-ClaudeBoxGateway` |
| Create / Start VM | `New-ClaudeBoxVM` / `Start-ClaudeBoxVM` |
| Connect | `Connect-ClaudeBoxVM` → `vmconnect`（Enhanced Session **待验证**） |
| Mount | `Mount-ClaudeBoxVisibleDirs -ApplyShares -PublishBundle` |

## 待验证

真机 Hyper-V、gost、Clash、SMB、vmconnect 透窗均未在本仓库变更中宣称已跑通。
