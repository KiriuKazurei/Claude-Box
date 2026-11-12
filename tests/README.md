# tests

`Test-Structure.ps1` — 仓库结构、PowerShell 语法、配置抽取、宿主 C: 护栏、网关日志占位。不依赖 Hyper-V；可在 Linux `pwsh` 跑。

```bash
pwsh -File tests/Test-Structure.ps1
```

`test-wsl-box-structure.ps1` — `src/wsl-box` 文件清单（含 `linux/` 云电脑框架入口）。

```bash
pwsh -File tests/test-wsl-box-structure.ps1
```

`wsl-box/test-linux-framework.sh` — 云电脑 / Linux dry-run：同步 `.run/`、apply-spoof、verify、gateway tips、Desktop `--check`、host identity（默认策略下型号可见、编码不可见）。

`wsl-box/test-host-identity.sh` — 夹具含虚构型号与禁止的硬件编码；apply 后型号进入展示层，编码不得泄漏，时区伪装仍有效。

```bash
bash tests/wsl-box/test-linux-framework.sh
# 或直接：
bash src/wsl-box/linux/Run-Smoke.sh
```

`claudebox/test-p1-gate.sh` — P1 身份策略引擎门禁：编译 C++ 库、跑夹具测试，并用默认策略 apply/verify。默认厂商/型号可展示，硬件编码不得进入展示层。

```bash
bash tests/claudebox/test-p1-gate.sh
```

`claudebox/test-p1-p2-gate.sh` — P1+P2 完整门禁：策略引擎与采集器测试，再跑 collect → apply → verify。原始清单可含编码，默认策略下展示层只有厂商/型号。

```bash
bash tests/claudebox/test-p1-p2-gate.sh
```

`claudebox/test-p3-wiring.sh` — P3 展示层接线：经 `apply-host-identity.sh` 跑「隐藏 / 修改后展示 / 选择展示」，并确认恢复默认策略后编码离开展示层。

`claudebox/test-p1-p2-p3-gate.sh` — P1+P2+P3 完整门禁：复用 P1+P2 门禁、既有 wsl-box host-identity / linux-framework smoke，以及上述接线用例。

```bash
bash tests/claudebox/test-p1-p2-p3-gate.sh
```

`claudebox/test-p4-gate.sh` — P4 设置页门禁：先跑 P1+P2+P3，再编译 Qt 设置页、跑无头用例（夹具清单默认策略、隐藏 / 修改后展示 / 选择展示、整份 `wsl-box.yaml` 往返），并用 `QT_QPA_PLATFORM=offscreen` 启动设置页冒烟。

```bash
bash tests/claudebox/test-p4-gate.sh
```
