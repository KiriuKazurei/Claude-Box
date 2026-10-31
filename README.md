# Claude Box

隔离运行 Claude Desktop 的独立产品（运行时形态可换）。

**状态：MVP in progress · M1（空访客骨架）**

## 目标

在隔离环境里跑 Claude Desktop：项目目录可挂入（默认排除宿主 `C:`），其它宿主机文件默认不可见；出入网经唯一网关（可接 Clash，带本地连接日志）；原生 GUI 透出；访客假环境大幅降低环境遥测穿透。

## 当前主路径（已定）

- **平台**：Windows 11 优先
- **盒子**：Hyper-V **Windows 访客**（Claude 主进程+子进程均在访客内）
- **Claude**：Windows Claude（exe/MSIX；保留 Computer use）
- **网络**：Internal vSwitch + 强制唯一网关（HTTP/SOCKS5 → 常接宿主 Clash）+ **连接日志**
- **挂盘**：`visible_dirs`；**排除宿主系统盘 C:**；访客自带假系统环境
- **非主路径**：WSL / Docker / Linux Claude；Sandbox 仅冒烟

## 文档

- [SCOPE.md](./SCOPE.md) — 范围定稿 v0.1
- [docs/architecture-v0.md](./docs/architecture-v0.md) — **架构方案 v0**（MVP 决策 / 里程碑）
- [docs/research-v1-runtime.md](./docs/research-v1-runtime.md) — 隔离运行时选型调研 v1
- [docs/research-v0.md](./docs/research-v0.md) — 初步技术调研 v0

## 仓库骨架

```
src/ClaudeBox.Control/   # 宿主机：VM / 交换机 / 拒挂 C: / 假环境清单
src/ClaudeBox.Gateway/   # 网关、Clash 说明、连接日志占位
src/ClaudeBox.Guest/     # 访客内假环境步骤 + 健康检查
config/                  # config.example.yaml + JSON schema
scripts/win11/           # HV 预检、交换机、New-ClaudeBoxVM
tests/                   # 结构与语法检查（可在 Linux pwsh 跑）
```

## 在 Win11 上跑 M1

> Hyper-V 操作必须在本机 Win11（管理员）执行；Linux 开发机只做结构检查。

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\win11\Test-HyperVPrereqs.ps1
.\scripts\win11\New-ClaudeBoxSwitch.ps1
Copy-Item .\config\config.example.yaml .\config\config.yaml
# 编辑 config.yaml 后：
.\scripts\win11\New-ClaudeBoxVM.ps1 -IsoPath D:\ISO\Win11.iso
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Start-ClaudeBoxVM -Name ClaudeBox-Win -Connect
```

细节见 [scripts/win11/README.md](./scripts/win11/README.md) 与 [architecture §11](./docs/architecture-v0.md)。

## 在 Linux 开发机上检查

```bash
pwsh -File tests/Test-Structure.ps1
```

## 仓库

https://github.com/KiriuKazurei/Claude-Box
