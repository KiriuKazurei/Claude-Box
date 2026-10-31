# Claude Box · 范围 v0.2（主路径改为自建专用容器）

## 一句话

隔离运行时里跑 Claude Desktop；原生 GUI；出入网经唯一网关（可指定 HTTP/SOCKS5）；用独立控件管理；尽量伪装完整独立环境并拦截对环境的遥测。

## 平台

- 优先 Windows 11
- 其次 macOS

## 运行时形态（可替换）

- **Docker-like / 容器**只是思路方向，**不是硬约束**；有更好策略可换。
- 硬约束是：隔离跑 Claude、原生窗口、唯一网关、目录可见性、环境伪装与反遥测。

### 主路径 v0.2（2026-10-31 改）

- 用户机器为 **Win11 家庭版**：无 Hyper-V / Windows Sandbox / Docker Windows 容器。
- **主路径 = 自建专用容器**（宿主上：专用本地用户 + 受限 token + job object + WFP 强制经网关 + hook 环境伪装），以 Rust 实现 Docker 式 CLI `cbox`（run/ps/stop/rm）。见 [docs/architecture-v1-container.md](./docs/architecture-v1-container.md)。
- **第一个里程碑 = C0 容器 MVP**：在容器内启动 Claude（隔离 FS 根 + 经网关 + 进程树受 job 限制）。
- 诚实限制：同内核下硬件/内核信息无法像 VM 那样完全隐藏；Computer use 看到的是宿主真实桌面。
- 下方 Hyper-V 内容**已弃用**，仅作可选参考。

### 旧主路径（已弃用：Hyper-V，2026-10-31）

- **Claude**：Windows Claude（exe/MSIX；主进程+子进程均在访客内）。Linux Claude 无 Computer use，不作主路径。
- **Win11 盒子**：**Hyper-V Windows 访客** + Internal 交换机强制唯一网关。
- **挂盘**：默认排除宿主系统盘 `C:`；只挂 `visible_dirs` / 非系统数据盘；访客自带假系统环境（独立 TZ/locale/hostname）。
- **网关**：强制；上游可接宿主 Clash（HTTP/SOCKS；宿主可开 TUN）；**本地连接日志**（SNI/IP/端口等）可开关，用于事后收紧遥测拦截。
- **验收**：直连失败；大幅降低环境遥测穿透（非绝对零）。
- Sandbox 仅冒烟；WSL/Docker 不作本阶段主路径。
- macOS 次优先，另议。

## 隔离与反遥测（核心）

1. **文件系统**：默认可见目录；排除宿主 `C:`；全盘非默认。
2. **身份伪装**：访客独立假环境（非映射宿主 C 改名）。
3. **网络**：唯一网关 + 可选日志复盘。
4. **重点**：降低 Claude 对真宿主机环境的遥测穿透。

## 控件（MVP）

- 配置网关、上游（含 Clash）、连接日志
- 选择可见目录（拒绝默认挂宿主 C:）
- 启停容器实例（Claude 原生窗口直接在宿主桌面）

## 暂不进 MVP

- 多账号一键切换
- Linux 宿主机一等支持
- Web 远程桌面
- 默认 TLS 解密遥测正文
