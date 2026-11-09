# wsl-box · WSL2 Claude Box（当前主实现轨）

在专用 WSL2 发行版内跑 **Linux Claude Desktop**，宿主保持东八区 + 简中。

设计文档：[docs/architecture-v2-wsl.md](../../docs/architecture-v2-wsl.md)

## 布局

```
host/     PowerShell：建发行版、挂载、启动（Win11 真路径）
guest/    bash：安装 Desktop、伪装、启动、验收探针
linux/    云电脑 / Linux dry-run 框架（无 WSL 也可跑 spoof/verify/gateway）
config/   wsl-box.example.yaml
```

## 云电脑（Linux）快速路径

在本仓库云电脑上验证 guest 行为（**不**启用 Win11 WSL、**不**强装 Desktop）：

```bash
cd /path/to/Claude-Box
bash src/wsl-box/linux/Install-ClaudeBoxFramework.sh
bash src/wsl-box/linux/Run-Smoke.sh
# 可选：仅检查本地 deb 是否存在（不下载、不 apt）
bash src/wsl-box/linux/Install-ClaudeDesktop.sh --check
# 结构 + smoke
bash tests/wsl-box/test-linux-framework.sh
```

- 前缀默认：`<repo>/.run/claude-box`（已 gitignore）；可用 `CLAUDE_BOX_PREFIX` / `CLAUDE_BOX_CONFIG` 覆盖。
- 读 `config/wsl-box.yaml`（若有）否则 `wsl-box.example.yaml`。
- `apply-spoof` 无 root 时只做 session env；写 `/etc/localtime` 需 `sudo`。
- Desktop 安装**只认** `installers/linux/*.deb` 或 `CLAUDE_BOX_DEB`；缺包则明确失败，不自动下载。

## 和零机器（Win11）快速路径

1. 启用 WSL2（管理员）：`wsl --install -d Ubuntu-24.04`（已装可跳过）并重启如需。
2. 复制配置：`copy config\wsl-box.example.yaml config\wsl-box.yaml`，改 gateway / identity / visible_dirs。
3. `.\host\New-ClaudeBoxWsl.ps1 -ConfigPath .\config\wsl-box.yaml`
4. `.\host\Set-ClaudeBoxWslMounts.ps1 -ConfigPath .\config\wsl-box.yaml`
5. 发行版内安装 Desktop：`.\host\Invoke-ClaudeBoxGuest.ps1 -Script install-claude-desktop.sh`
6. `.\host\Start-ClaudeBoxWsl.ps1 -ConfigPath .\config\wsl-box.yaml`
7. 验收：`.\host\Invoke-ClaudeBoxGuest.ps1 -Script verify-spoof.sh`

`host/*.ps1` 在 **无 WSL 的开发机**上只会做参数/配置校验；真机联调在和零的 Win11（需明确批准再动机器）。

## 官方说明

Anthropic：WSL 用户应装 Windows Desktop。本轨为不受支持的验证/实现路径，见架构文档风险节。

## 可选：本机 IP 波动杀进程（默认关）

配置 `features.kill_on_local_ip_change: false`。打开后由宿主 `host/Watch-ClaudeBoxLocalIp.ps1` 监视本机 IP；变化则对发行版内 Claude **先礼貌关再强杀**（学 claude-switcher）。专篇：[docs/kill-on-local-ip-change.md](../../docs/kill-on-local-ip-change.md)。
