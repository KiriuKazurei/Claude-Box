# Claude Box

隔离运行 Claude Desktop 的独立产品（运行时形态可换）。

## 目标

在隔离环境里跑 Claude Desktop：项目目录可挂入，其它宿主机文件默认不可见；出入网经唯一网关；原生 GUI 窗口透出；尽量伪装完整独立环境并拦截对环境的遥测。

## 当前主路径（已定）

- **平台**：Windows 11 优先
- **盒子**：Hyper-V **Windows 访客**
- **Claude**：Windows Claude（exe/MSIX；保留 Computer use）
- **网络**：Internal 虚拟交换机 + 强制唯一网关（HTTP CONNECT / SOCKS5）
- **非主路径**：WSL / Docker / Linux Claude；Sandbox 仅冒烟

Docker-like 只是思路，不是硬约束。

## 文档

- [SCOPE.md](./SCOPE.md) — 范围定稿 v0.1
- [docs/architecture-v0.md](./docs/architecture-v0.md) — **架构方案 v0**（控件 / 访客 / 网关 / 里程碑）
- [docs/research-v1-runtime.md](./docs/research-v1-runtime.md) — 隔离运行时选型调研 v1
- [docs/research-v0.md](./docs/research-v0.md) — 初步技术调研 v0

## 仓库骨架（M0）

```
src/ClaudeBox.Control/   # 宿主机：VM / 交换机 / 配置
src/ClaudeBox.Gateway/   # 网关配置与宿主侧占位
src/ClaudeBox.Guest/     # 访客内初始化（在访客 Windows 上跑）
config/                  # config.example.yaml + JSON schema
scripts/win11/           # HV 预检、建 Internal 交换机
tests/                   # 结构与语法检查（可在 Linux pwsh 跑）
```

## 在 Win11 上跑骨架

> 多数 Hyper-V 操作必须在本机 Win11（管理员）执行；本仓库在 Linux CI/开发机上只做结构检查，**不假装已跑通 Hyper-V**。

1. 启用 Windows 功能 **Hyper-V**，重启。
2. 克隆本仓库，管理员打开 PowerShell：

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\win11\Test-HyperVPrereqs.ps1
.\scripts\win11\New-ClaudeBoxSwitch.ps1
Copy-Item .\config\config.example.yaml .\config\config.yaml
# 编辑 config.yaml：gateway.http / gateway.socks5、visible_dirs、vm.*
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Import-Module .\src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1
Test-ClaudeBoxHyperVReady
```

3. 按 [architecture-v0.md §11](./docs/architecture-v0.md) 做 HV-1…HV-10 验证（M1 起实现 `New-ClaudeBoxVM` 等）。

## 在 Linux 开发机上检查

若已安装 PowerShell 7（`pwsh`）：

```bash
pwsh -File tests/Test-Structure.ps1
```

## 仓库

https://github.com/KiriuKazurei/Claude-Box
