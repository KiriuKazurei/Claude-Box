# 架构方案 v2 · WSL2 + Linux Claude Desktop（当前主实现轨）

> 决策日：2026-11-02（Asia/Tokyo）  
> 决策者：和零  
> 取代短期验证优先级：本文件为 **当前主实现轨**；自建 Windows 容器见 [architecture-v1-container.md](./architecture-v1-container.md)（工程后备）；Hyper-V 方案已归档到 Google Drive 7z。  
> 伪装策略：[spoofing-timezone-locale.md](./spoofing-timezone-locale.md)  
> 能力对比：[compare-desktop-windows-vs-linux-wsl.md](./compare-desktop-windows-vs-linux-wsl.md)

## 一句话

在 **WSL2 专用发行版**里跑 **Linux Claude Desktop**；宿主 Win11 家庭版保持东八区 + 简中；时区/语言只在发行版/进程内伪装；出入网经宿主网关；默认切断 `/mnt/c`；拦 Sentry / Datadog RUM / `event_logging`。

## 官方风险（必读）

Anthropic 文档对 WSL 用户的建议是：**装 Windows Claude Desktop，再在 WSL 里用 Claude Code**。  
本方案走的是 **「WSL 内安装 Linux Desktop」**，**不受官方支持**。可能遇到：

- 自动更新 / 安装器假设非 WSL 环境
- WSLg 窗口与 GPU / 输入异常
- Cowork（QEMU/KVM）在 WSL2 嵌套虚拟化下不可用或极慢
- 账户、崩溃上报路径与官方排障不一致

验收与排障以「隔离 + 网关 + 伪装」硬约束为准，不以官方 WSL 支持承诺为准。

## 硬约束映射

| 硬约束 | WSL v2 落点 |
|--------|-------------|
| 隔离跑 Claude | 专用 WSL2 发行版（如 `ClaudeBox`），独立 userData / `ant-did` |
| 唯一网关出网 | 宿主 gost/Clash；发行版内 `HTTP(S)_PROXY`/`ALL_PROXY` + 可选 nftables 强制 |
| 目录可见性 | **切断默认 `/mnt/c`**；只 bind-mount `visible_dirs` |
| 环境伪装 | `TZ`、`LANG`/`LC_*`、`/etc/localtime`、可选 `--lang`（不改宿主） |
| 反遥测 | 网关/hosts/nft 拦 Sentry、Datadog RUM、`/api/event_logging` |
| Computer use | **已降权**；Linux Desktop 官方无 CU |
| 可选止血（默认关） | 本机 IP 波动 → 礼貌关再强杀 Claude（学 claude-switcher；见 [kill-on-local-ip-change.md](./kill-on-local-ip-change.md)） |

## 组件布局

```
宿主 Win11 Home（时区 UTC+8 / 显示 zh-CN 不变）
├─ Clash / 上游代理（可选 TUN）
├─ ClaudeBox.Gateway（gost → 上游；JSONL；blocklist）
├─ src/wsl-box/host/*.ps1
│   ├─ Enable / 创建发行版 ClaudeBox
│   ├─ 配置 .wslconfig / wsl.conf（关 automount）
│   ├─ 注入可见目录挂载
│   └─ 启动：wsl -d ClaudeBox -- … start-claude-desktop.sh
└─ WSL2 发行版 ClaudeBox
    ├─ /etc/wsl.conf          # automount.enabled=false 等
    ├─ /etc/localtime → 假 IANA
    ├─ env: TZ / LANG / HTTP_PROXY …
    ├─ guest scripts: install / apply-spoof / start / verify
    └─ Linux Claude Desktop（Electron；无官方 CU）
```

## 发行版约定

| 项 | 默认 |
|----|------|
| 发行版名 | `ClaudeBox` |
| 基座 | Ubuntu 24.04（可换） |
| 安装根 | `%LOCALAPPDATA%\ClaudeBox\wsl\ClaudeBox`（可配） |
| Desktop 安装 | `.deb` → `/usr` 或用户目录；版本对齐分析笔记（如 4.4212.0） |
| userData | 发行版内独立路径（勿复用宿主 `%APPDATA%\Claude`） |

## 启动与伪装（主路径：不改程序）

宿主 PowerShell 最终等价于：

```powershell
wsl -d ClaudeBox --cd ~ --env TZ=America/Los_Angeles `
  -- env LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 LANGUAGE=en_US `
  HTTP_PROXY=http://$GatewayHost:$Port HTTPS_PROXY=… ALL_PROXY=… `
  /opt/claude-box/guest/start-claude-desktop.sh
```

发行版内 `apply-spoof.sh`：

1. `ln -sfn /usr/share/zoneinfo/$TZ /etc/localtime`（需一次特权或镜像预置）
2. 写入 `/etc/default/locale` 或 session env（与配置一致）
3. 可选：Desktop 启动追加 `--lang=en-US`
4. **备用**（默认关）：asar 定点改写 `lg()` / `bCt()`（见 spoofing 文档）

## 挂载策略

1. `%USERPROFILE%\.wslconfig` / 发行版 `/etc/wsl.conf`：
   - `[automount] enabled=false`（或 `mountFsTab=false` + 自定义）
   - 避免 Claude 默认看见整盘 `C:`
2. 仅对 `visible_dirs`：`wsl.exe` + `mount --bind` 到 `/mnt/claude-box/<name>`，或 `/etc/fstab` 条目由 host 脚本生成。
3. 拒绝把 `C:\` / `C:\Users\<宿主>` 整棵挂进发行版。

## 网关与反遥测

复用 M2 思路（`ClaudeBox.Gateway`），WSL 适配：

- 网关听宿主 `0.0.0.0` 或 WSL 可见的宿主机 IP（`host.docker.internal` 类：`$(ip route | awk '/default/{print $3}')`）。
- 发行版导出 `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`/`NO_PROXY=localhost,127.0.0.1`。
- **blocklist 最低集**（可按 JSONL 增删）：
  - `*.ingest.us.sentry.io` / Sentry 相关
  - Datadog RUM `browser-intake-*.datadoghq.*` / `/api/v2/rum`
  - `api.anthropic.com` 的 `/api/event_logging`（路径级；勿误杀主 API——优先用代理 ACL / SNI 规则精细化）
- 可选：发行版 `nftables` 禁止非代理出网（强化版，W1+）。

### 可选 · 本机 IP 波动杀进程（默认关 · 已知有效）

和零决策（2026-11-02）：增加明确知晓有效的止血方案——**宿主本机 IP 集合变化时，快速杀掉 box 内全部 Claude 进程**。

- 学 [claude-switcher](https://github.com/YOURSJEYO/claude-switcher)：**先礼貌关窗口/发关闭信号，短超时（约 10s）后再强杀**，避免半写坏 userData。
- 配置：`features.kill_on_local_ip_change: false`（**默认 OFF**）。
- **不替代** 唯一网关 / 强制代理；只补「出口身份抖了、进程还挂着」的缺口。
- 专篇与验收：[kill-on-local-ip-change.md](./kill-on-local-ip-change.md)；宿主桩脚本：`src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1`。

## 里程碑（WSL 轨）

| ID | 交付 | 验收 |
|----|------|------|
| **W0** | 文档 + `src/wsl-box` 脚手架 | 本仓库可本地 commit；脚本可在无 WSL 的 CI 上做语法/结构检查 |
| **W1** | 和零机器：启用 WSL2、导入/创建 `ClaudeBox`、关默认 automount | `ls /mnt/c` 失败或为空；`visible_dirs` 可见 |
| **W2** | 安装 Linux Desktop + `apply-spoof` + 经网关启动 | 探针：`Intl` timeZone / locale = 假值；流量进网关 JSONL |
| **W3** | 遥测拦截复盘 | Sentry/Datadog/event_logging 被拦或 culture 已是假值 |
| **W4** | 可选：asar 备用补丁、Cowork/KVM 可行性记录 | 仅主路径不足时启用补丁 |
| **W5** | 可选：`kill_on_local_ip_change` 监视器 | 默认关；打开后 IP 变化 → 礼貌关→强杀；见专篇 |

## 和零机器上的下一步（W1）

本回合 **不在用户 PC 上自动启用 WSL**（需明确批准再动机器）。手工步骤：

1. **管理员 PowerShell**：`wsl --install -d Ubuntu-24.04`（或先 `wsl --status` 看是否已装）→ 重启如提示。
2. 克隆/同步本仓库到 Win11。
3. 复制 `src/wsl-box/config/wsl-box.example.yaml` → 本地 config；改 `identity` / `gateway` / `visible_dirs`。
4. 运行：
   ```powershell
   cd path\to\Claude-Box
   Set-ExecutionPolicy -Scope Process Bypass
   .\src\wsl-box\host\New-ClaudeBoxWsl.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
   .\src\wsl-box\host\Set-ClaudeBoxWslMounts.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
   .\src\wsl-box\host\Start-ClaudeBoxWsl.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
   ```
5. 发行版内首次：`sudo /opt/claude-box/guest/install-claude-desktop.sh`（或 host 一键调用）。
6. 验证：`/opt/claude-box/guest/verify-spoof.sh`。

## 与旧轨关系

| 路径 | 状态 |
|------|------|
| WSL2 + Linux Desktop（本文件） | **当前主实现轨** |
| 自建 Windows 容器 `cbox`（v1） | 工程后备；保留 `src/cbox` |
| Hyper-V / 旧 Control-Guest | 已弃用；源码留作参考；方案包已上 Google Drive |

## 代码入口

- Host：`src/wsl-box/host/`
- Guest：`src/wsl-box/guest/`
- 配置示例：`src/wsl-box/config/wsl-box.example.yaml`
- README：`src/wsl-box/README.md`
- 可选 IP 波动杀进程：[kill-on-local-ip-change.md](./kill-on-local-ip-change.md)
