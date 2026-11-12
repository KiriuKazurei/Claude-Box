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

环境变量：`CLAUDE_BOX_PREFIX`（默认 `<repo>/.run/claude-box`）、`CLAUDE_BOX_CONFIG`、`CLAUDE_BOX_DEB`、`CLAUDE_BOX_HOST_INVENTORY`（可选原始清单）、`CLAUDE_BOX_IDENTITY_POLICY`（可选每字段策略）、`CLAUDE_BOX_IDENTITY_BIN`（可选，覆盖引擎路径）。`Install-ClaudeBoxFramework.sh` 会编译并安装 `claudebox-identity`。无策略文件时 apply 按默认策略：厂商/型号可展示，编码不进入展示层。

`Run-Smoke.sh` 会跑 TZ/locale 伪装和 host identity 展示层（默认策略下型号可发布，编码必须被丢掉）。单独验收：`bash tests/wsl-box/test-host-identity.sh`。接线与「隐藏 / 修改后展示 / 选择展示」：`bash tests/claudebox/test-p1-p2-p3-gate.sh`。
不改写活的 `/sys/class/dmi`，也不隐藏 WSL 内核标识。
