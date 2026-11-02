# wsl-box · WSL2 Claude Box（当前主实现轨）

在专用 WSL2 发行版内跑 **Linux Claude Desktop**，宿主保持东八区 + 简中。

设计文档：[docs/architecture-v2-wsl.md](../../docs/architecture-v2-wsl.md)

## 布局

```
host/     PowerShell：建发行版、挂载、启动
guest/    bash：安装 Desktop、伪装、启动、验收探针
config/   wsl-box.example.yaml
```

## 和零机器（Win11）快速路径

1. 启用 WSL2（管理员）：`wsl --install -d Ubuntu-24.04`（已装可跳过）并重启如需。
2. 复制配置：`copy config\wsl-box.example.yaml config\wsl-box.yaml`，改 gateway / identity / visible_dirs。
3. `.\host\New-ClaudeBoxWsl.ps1 -ConfigPath .\config\wsl-box.yaml`
4. `.\host\Set-ClaudeBoxWslMounts.ps1 -ConfigPath .\config\wsl-box.yaml`
5. 发行版内安装 Desktop：`.\host\Invoke-ClaudeBoxGuest.ps1 -Script install-claude-desktop.sh`
6. `.\host\Start-ClaudeBoxWsl.ps1 -ConfigPath .\config\wsl-box.yaml`
7. 验收：`.\host\Invoke-ClaudeBoxGuest.ps1 -Script verify-spoof.sh`

本目录脚本在 **无 WSL 的开发机**上只会做参数/配置校验；真机联调在和零的 Win11。

## 官方说明

Anthropic：WSL 用户应装 Windows Desktop。本轨为不受支持的验证/实现路径，见架构文档风险节。

## 可选：本机 IP 波动杀进程（默认关）

配置 `features.kill_on_local_ip_change: false`。打开后由宿主 `host/Watch-ClaudeBoxLocalIp.ps1` 监视本机 IP；变化则对发行版内 Claude **先礼貌关再强杀**（学 claude-switcher）。专篇：[docs/kill-on-local-ip-change.md](../../docs/kill-on-local-ip-change.md)。

