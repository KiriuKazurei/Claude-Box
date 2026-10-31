# Claude Box

隔离运行 Claude Desktop 的独立产品（运行时形态可换）。

**状态：MVP in progress · M2（网关 + 连接日志）**

## 目标

在隔离环境里跑 Claude Desktop：项目目录可挂入（默认排除宿主 `C:`），其它宿主机文件默认不可见；出入网经唯一网关（可接 Clash，带本地连接日志）；原生 GUI 透出；访客假环境大幅降低环境遥测穿透。

## 当前主路径（已定）

- **平台**：Windows 11 优先
- **盒子**：Hyper-V **Windows 访客**（Claude 主进程+子进程均在访客内）
- **Claude**：Windows Claude（exe/MSIX；保留 Computer use）
- **网络**：Internal vSwitch + 强制唯一网关（HTTP/SOCKS5 → 常接宿主 Clash；中继优先 **gost**）+ **JSONL 连接日志**
- **挂盘**：`visible_dirs`；**排除宿主系统盘 C:**；访客自带假系统环境
- **非主路径**：WSL / Docker / Linux Claude；Sandbox 仅冒烟

## 文档

- [SCOPE.md](./SCOPE.md) — 范围定稿 v0.1
- [docs/architecture-v0.md](./docs/architecture-v0.md) — **架构方案 v0**（MVP 决策 / 里程碑）
- [docs/research-v1-runtime.md](./docs/research-v1-runtime.md) — 隔离运行时选型调研 v1
- [docs/research-v0.md](./docs/research-v0.md) — 初步技术调研 v0
- [src/ClaudeBox.Gateway/README.md](./src/ClaudeBox.Gateway/README.md) — **M2 网关：Win11 怎么跑**
- [scripts/win11/Set-ClaudeBoxInternalGatewayRouting.md](./scripts/win11/Set-ClaudeBoxInternalGatewayRouting.md) — 访客默认路由说明

## 仓库骨架

```
src/ClaudeBox.Control/   # 宿主机：VM / 交换机 / 拒挂 C: / 网关接线
src/ClaudeBox.Gateway/   # 运行时配置、gost 中继计划、JSONL 日志
src/ClaudeBox.Guest/     # 访客假环境 + 网络（默认路由/代理）
config/                  # config.example.yaml + JSON schema
scripts/win11/           # HV 预检、交换机、VM、网关启停、直连验收
tests/                   # 结构与语法检查（可在 Linux pwsh 跑）
```

## 在 Win11 上跑 M2（网关）

> Hyper-V / gost 必须在本机 Win11 执行；Linux 开发机只做结构检查。**真机联调待验证。**

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass

# 0) 若尚未做 M1：
.\scripts\win11\Test-HyperVPrereqs.ps1
.\scripts\win11\New-ClaudeBoxSwitch.ps1

# 1) 安装 gost.exe -> %LOCALAPPDATA%\ClaudeBox\bin\ 或 PATH
# 2) 配置（Clash HTTP/SOCKS 口）
Copy-Item .\config\config.example.yaml .\config\config.yaml

# 3) 启动盒子网关（写 runtime + 日志；有 gost 则拉起中继）
.\scripts\win11\Start-ClaudeBoxGateway.ps1 -ConfigPath .\config\config.yaml -WriteStartupLog

# 4) 访客内（Enhanced Session）：
#    src\ClaudeBox.Guest\Set-ClaudeBoxGuestNetwork.ps1 -GatewayIp 192.168.87.1 -ProxyPort 3080 -Apply

# 5) 验收步骤：
.\scripts\win11\Test-ClaudeBoxDirectEgressBlocked.ps1
```

细节见 [Gateway README](./src/ClaudeBox.Gateway/README.md) 与 [architecture §4 / §11](./docs/architecture-v0.md)。

## 在 Win11 上跑 M1（空访客）

```powershell
.\scripts\win11\New-ClaudeBoxVM.ps1 -IsoPath D:\ISO\Win11.iso
Import-Module .\src\ClaudeBox.Control\ClaudeBox.Control.psd1
Start-ClaudeBoxVM -Name ClaudeBox-Win -Connect
```

## 在 Linux 开发机上检查

```bash
pwsh -File tests/Test-Structure.ps1
```

## 仓库

https://github.com/KiriuKazurei/Claude-Box
