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
- 身份展示（P3）：`apply-host-identity.sh` 调用 C++ `claudebox-identity`，按策略把字段写到 `$CLAUDE_BOX_PREFIX/env/host-model.env`、`var/host-identity.presented.yaml` 和 `presentation/dmi/id/`。默认仍是厂商 / 型号可展示、硬件编码隐藏。`hide_identifiers: true` 是过渡约束（设为 false 仍会失败，避免整份原始清单被放开）；要展示或修改某个编码，请用每字段策略文件（`CLAUDE_BOX_IDENTITY_POLICY` 或 `var/host-identity.policy.yaml`）。二进制由 `Install-ClaudeBoxFramework.sh` 编到 `$CLAUDE_BOX_PREFIX/bin/claudebox-identity`，也可用 `CLAUDE_BOX_IDENTITY_BIN` 指定。见 [docs/identity-presentation.md](../../docs/identity-presentation.md) 与 [src/claudebox/README.md](../claudebox/README.md)。
- 本机 `/sys/class/dmi` 在 dry-run 上**不会**改写（通常需特权且由内核提供）。verify 读的是展示层。内核名 `microsoft-standard-WSL2` 等痕迹会保持可见，这不是缺陷。
- 真机导出：`host/Export-ClaudeBoxHostIdentity.ps1` 只读 `Win32_ComputerSystem` 的 Manufacturer / Model。测试夹具：`tests/wsl-box/fixtures/host-inventory.raw.yaml`（虚构值）。
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

## 可选：本机 IP 波动时断开网关并结束进程（默认关 · 唯一网关模块）

配置 `features.kill_on_local_ip_change: false`。打开后由宿主 `host/Watch-ClaudeBoxLocalIp.ps1` 监视本机 IP；变化则**并行启动** `Stop-ClaudeBoxGateway` 与发行版内 Claude 的礼貌关闭（互不等待），进程在 grace 后若仍存活再强杀。专篇：[docs/kill-on-local-ip-change.md](../../docs/kill-on-local-ip-change.md)。

## OAuth（OA）跳转：伴生指纹浏览器（默认开 · W6 接线）

配置 `features.oauth_open_in_fingerprint_browser: true`。Claude 打开登录 / 授权页时，用独立指纹浏览器浏览，**不走宿主默认浏览器**。不改宿主时区语言。专篇：[docs/companion-fingerprint-browser.md](../../docs/companion-fingerprint-browser.md)；入口：`companion-browser/open-oauth-url.sh`（P8 已接到 C++ 核心与设置页；引擎未就绪时拒绝打开，不回退宿主浏览器）。

## 身份保险库（默认开 · 尽量不改宿主 · P9）

配置 `features.identity_vault: true`。在盒子专属目录归档 / 切换 Claude 本地身份，**不**改写宿主默认 Claude 目录或宿主时区语言。专篇：[docs/identity-vault.md](../../docs/identity-vault.md)；实现：C++ `claudebox::vault` 与 `identity-vault/` 说明。
