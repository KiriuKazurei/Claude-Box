# Claude Box

隔离运行 Claude Desktop 的独立产品（运行时形态可换）。

**状态：MVP in progress · M4（最小控件 UI）**

## 目标

在隔离环境里跑 Claude Desktop：项目目录可挂入（默认排除宿主 `C:`），其它宿主机文件默认不可见；出入网经唯一网关（可接 Clash，带本地连接日志）；原生 GUI 透出；访客假环境大幅降低环境遥测穿透。

## 当前主路径（已定）

- **平台**：Windows 11 优先
- **盒子**：Hyper-V **Windows 访客**（Claude 主进程+子进程均在访客内）
- **Claude**：Windows Claude（**MSIX**；保留 Computer use）
- **网络**：Internal vSwitch + 强制唯一网关（HTTP/SOCKS5 → 常接宿主 Clash；中继优先 **gost**）+ **JSONL 连接日志**
- **挂盘**：`visible_dirs` via **`smb_internal`**；**排除宿主系统盘 C:**；访客自带假系统环境
- **非主路径**：WSL / Docker / Linux Claude；Sandbox 仅冒烟

## 文档

- [SCOPE.md](./SCOPE.md) — 范围定稿 v0.1
- [docs/architecture-v0.md](./docs/architecture-v0.md) — **架构方案 v0**（MVP 决策 / 里程碑；含 M4）
- [docs/research-v1-runtime.md](./docs/research-v1-runtime.md) — 隔离运行时选型调研 v1
- [docs/research-v0.md](./docs/research-v0.md) — 初步技术调研 v0
- [src/ClaudeBox.Gateway/README.md](./src/ClaudeBox.Gateway/README.md) — M2 网关
- [src/ClaudeBox.Guest/README.md](./src/ClaudeBox.Guest/README.md) — **M3 访客：假环境 / Claude / 挂载 / 健康检查**
- [scripts/win11/Set-ClaudeBoxInternalGatewayRouting.md](./scripts/win11/Set-ClaudeBoxInternalGatewayRouting.md) — 访客默认路由说明

## 仓库骨架

```
src/ClaudeBox.Control/   # 宿主机：VM / 挂载 / 网关接线 / Save-Config / ui（WinForms）
src/ClaudeBox.Gateway/   # 运行时配置、gost 中继计划、JSONL 日志
src/ClaudeBox.Guest/     # 假环境 Apply、Claude MSIX、访客挂载、健康检查
config/                  # config.example.yaml + JSON schema
scripts/win11/           # HV / 网关 / 挂载入口
tests/                   # 结构与语法检查（可在 Linux pwsh 跑）
```

## 在 Win11 上跑 M3（Claude + 挂载）

> Hyper-V / SMB / Claude 必须在本机 Win11 + 访客内执行；Linux 开发机只做结构检查。**真机联调待验证。**

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass
Copy-Item .\config\config.example.yaml .\config\config.yaml
# 编辑 visible_dirs（不要写 C:\）、gateway、identity

# M1/M2 若未做：预检 → 交换机 → VM → 网关
.\scripts\win11\Test-HyperVPrereqs.ps1
.\scripts\win11\New-ClaudeBoxSwitch.ps1
.\scripts\win11\New-ClaudeBoxVM.ps1 -IsoPath D:\ISO\Win11.iso
.\scripts\win11\Start-ClaudeBoxGateway.ps1 -ConfigPath .\config\config.yaml -WriteStartupLog

# M3 挂载计划 + SMB（管理员）
.\scripts\win11\Mount-ClaudeBoxVisibleDirs.ps1 -ConfigPath .\config\config.yaml -ApplyShares -PublishBundle

# 访客内（Enhanced Session / 已拷入 guest-bundle）：
#   .\Initialize-ClaudeBoxGuest.ps1 -IdentityHostname claude-box -Timezone UTC -Locale en-US -Workgroup CLAUDEBOX -GatewayIp 192.168.87.1 -Apply
#   .\Install-ClaudeBoxWindowsClaude.ps1 -ShowGuide
#   .\Mount-ClaudeBoxGuestShares.ps1 -PlanPath .\mount-plan.json -Apply
#   .\Test-ClaudeBoxGuestHealth.ps1 -ExpectedHostname claude-box -ExpectedTimezone UTC -ExpectedLocale en-US -ExpectedMountLetters Z:
```

## 在 Win11 上跑 M4（控件 UI）

> PowerShell **WinForms** 最小控件；无独立 .NET 工程。真机 Hyper-V 联调 **待验证**。

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass
Copy-Item .\config\config.example.yaml .\config\config.yaml   # 若尚无

# 图形界面（建议管理员 PowerShell）
.\scripts\win11\Start-ClaudeBoxControlUi.ps1

# 逻辑自检（Linux / CI 也可用）
pwsh -File .\src\ClaudeBox.Control\ui\Start-ClaudeBoxControlUi.ps1 -SelfCheck
```

说明：[src/ClaudeBox.Control/ui/README.md](./src/ClaudeBox.Control/ui/README.md)。

## 在 Win11 上跑 M2（网关）

```powershell
.\scripts\win11\Start-ClaudeBoxGateway.ps1 -ConfigPath .\config\config.yaml -WriteStartupLog
.\scripts\win11\Test-ClaudeBoxDirectEgressBlocked.ps1
```

细节见 [Gateway README](./src/ClaudeBox.Gateway/README.md)。

## 在 Linux 开发机上检查

```bash
pwsh -File tests/Test-Structure.ps1
```

## 仓库

https://github.com/KiriuKazurei/Claude-Box
