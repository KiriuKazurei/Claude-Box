# Claude Box · 范围 v0.3（主实现轨：WSL2 + Linux Desktop）

## 一句话

隔离运行时里跑 Claude Desktop；出入网经唯一网关（可指定 HTTP/SOCKS5）；目录可见可控；在盒子/进程内伪装时区与语言并拦截环境遥测。

## 平台

- 优先 Windows 11（家庭版可用；不依赖 Hyper-V）
- 其次 macOS（另议）

## 运行时形态（可替换）

- **Docker-like / 容器 / WSL / VM** 都只是手段，**不是硬约束**。
- 硬约束：隔离跑 Claude、唯一网关、目录可见性、环境伪装与反遥测。
- Computer use **已降权**（Linux Desktop 官方无 CU）。

### 当前主实现轨（2026-11-02 · v0.3）

- **WSL2 + Linux Claude Desktop**（专用发行版，如 `ClaudeBox`）。
- **宿主 Win11 照旧**：UTC+8 / 东八区显示，界面简中；**只在 WSL/进程内**伪装。
- 伪装主路径：`TZ`、`LANG`/`LC_*`、`/etc/localtime`、可选 `--lang`；asar/hook 备用。
- 切断默认 `/mnt/c`；只挂 `visible_dirs`。
- 网关拦 Sentry / Datadog RUM / `event_logging`。
- **官方风险**：Anthropic 建议 WSL 用户装 **Windows** Desktop；本路径不受官方支持。见 [docs/architecture-v2-wsl.md](./docs/architecture-v2-wsl.md)。
- 代码：`src/wsl-box/`。

### 工程后备（原 v0.2 主路径）

- **自建专用 Windows 容器**（`cbox`：专用用户 + job + WFP + 进程环境伪装）。见 [docs/architecture-v1-container.md](./docs/architecture-v1-container.md)、`src/cbox/`。
- 适用：以后要回 Windows Desktop / 更强同机隔离时再启。

### 已归档 / 弃用

- **Hyper-V Windows 访客**方案：源码仍在仓库作参考；完整方案包已上传 Google Drive（`Claude-Box-legacy-hyperv-container-*.7z`）。
- Sandbox 仅历史冒烟思路。

## 隔离与反遥测（核心）

1. **文件系统**：默认可见目录；排除宿主系统盘整挂；WSL 关 automount。
2. **身份伪装**：发行版/容器独立假环境（非改宿主）。
3. **网络**：唯一网关 + 可选 JSONL 复盘。
4. **重点**：降低 Claude 对真宿主机环境的遥测穿透。
5. **时区 / 系统语言伪装**  
   - 作用域：仅 box / Claude 进程（含 WSL）。  
   - 优先不改程序；备用 patch asar / hook。  
   - 详见 [docs/spoofing-timezone-locale.md](./docs/spoofing-timezone-locale.md)。

## 控件（MVP · WSL 轨）

- 配置网关、上游（含 Clash）、连接日志
- 选择可见目录（拒绝默认挂宿主 `C:`）
- 启停 WSL 发行版内 Desktop（WSLg 窗口）

## 暂不进 MVP

- Computer use 一等支持
- 多账号一键切换
- Linux 宿主机一等支持
- Web 远程桌面
- 默认 TLS 解密遥测正文
- 默认启用 asar 补丁
