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

环境变量：`CLAUDE_BOX_PREFIX`（默认 `<repo>/.run/claude-box`）、`CLAUDE_BOX_CONFIG`、`CLAUDE_BOX_DEB`、`CLAUDE_BOX_HOST_INVENTORY`（可选，宿主侧原始清单；apply 只保留厂商/型号）。

`Run-Smoke.sh` 会跑 TZ/locale 伪装和 host identity 展示层（型号可发布，编码必须被丢掉）。单独验收：`bash tests/wsl-box/test-host-identity.sh`。
不改写活的 `/sys/class/dmi`，也不隐藏 WSL 内核标识。
