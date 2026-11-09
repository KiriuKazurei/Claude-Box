# tests

`Test-Structure.ps1` — 仓库结构、PowerShell 语法、配置抽取、宿主 C: 护栏、网关日志占位。不依赖 Hyper-V；可在 Linux `pwsh` 跑。

```bash
pwsh -File tests/Test-Structure.ps1
```

`test-wsl-box-structure.ps1` — `src/wsl-box` 文件清单（含 `linux/` 云电脑框架入口）。

```bash
pwsh -File tests/test-wsl-box-structure.ps1
```

`wsl-box/test-linux-framework.sh` — 云电脑 / Linux dry-run：同步 `.run/`、apply-spoof、verify、gateway tips、Desktop `--check`。

```bash
bash tests/wsl-box/test-linux-framework.sh
# 或直接：
bash src/wsl-box/linux/Run-Smoke.sh
