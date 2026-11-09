# linux · 云电脑 dry-run 框架

在无 WSL 的 Linux（如本仓库云电脑）上同步 guest 脚本、注入伪装/网关 env，并跑 smoke。

Win11 真路径仍是 `../host/*.ps1`。本目录**不**创建发行版、**不**改用户机器。

## 命令

```bash
# 从仓库根：
bash src/wsl-box/linux/Install-ClaudeBoxFramework.sh
bash src/wsl-box/linux/Run-Smoke.sh
bash src/wsl-box/linux/Install-ClaudeDesktop.sh --check   # 本地 deb only
bash tests/wsl-box/test-linux-framework.sh
```

环境变量：`CLAUDE_BOX_PREFIX`（默认 `<repo>/.run/claude-box`）、`CLAUDE_BOX_CONFIG`、`CLAUDE_BOX_DEB`。
