# Claude Box

隔离运行 Claude Desktop（运行时形态可换）。

**状态（2026-11-11）：主方案是纯 WSL2。**  
在专用发行版里跑 Linux Claude Desktop（`src/wsl-box/`），只挂 `visible_dirs`。效果不好再考虑 WHPX+QEMU 或 `cbox`。Hyper-V 方案已归档 Google Drive。

## 当前怎么走

1. 读 [SCOPE.md](./SCOPE.md)（v0.4.1）  
2. 读 [docs/architecture-v2-wsl.md](./docs/architecture-v2-wsl.md)  
3. 云电脑已按 [docs/framework-build-plan.md](./docs/framework-build-plan.md) 做到接近 beta（P1–P11 dry-run）。清单见 [docs/near-beta-dry-run-acceptance.md](./docs/near-beta-dry-run-acceptance.md)。真机启用 WSL（W1）须和零再批准，步骤仍见 [src/wsl-box/README.md](./src/wsl-box/README.md)  

**硬约束**：隔离、唯一网关、目录可见（切断默认 `/mnt/c`）、进程内 TZ/locale 伪装、拦 Sentry/Datadog/`event_logging`。  
**宿主**：Win11 保持东八区 + 简中；只在 WSL/进程里伪装。  
**官方风险**：Anthropic 建议 WSL 用户装 Windows Desktop；Linux Desktop-in-WSL **不受支持**。

## 文档

- [SCOPE.md](./SCOPE.md) — 范围 v0.4.1  
- [docs/framework-build-plan.md](./docs/framework-build-plan.md) — 框架搭建计划（云电脑做到接近 beta）  
- [docs/near-beta-dry-run-acceptance.md](./docs/near-beta-dry-run-acceptance.md) — P11：已在云电脑证明的项，以及仍待真机 W1 的项  
- [docs/identity-presentation.md](./docs/identity-presentation.md) — 身份展示：采集、设置页、按字段授权  
- [docs/architecture-v2-wsl.md](./docs/architecture-v2-wsl.md) — **架构 v2：主方案 WSL**  
- [docs/spoofing-timezone-locale.md](./docs/spoofing-timezone-locale.md) — 伪装：环境优先 / asar 备用  
- [docs/kill-on-local-ip-change.md](./docs/kill-on-local-ip-change.md) — 可选：本机 IP 波动杀 Claude（默认关）  
- [docs/companion-fingerprint-browser.md](./docs/companion-fingerprint-browser.md) — OA / 授权跳转走伴生指纹浏览器（默认开；P8 已在云电脑接线）  
- [docs/identity-vault.md](./docs/identity-vault.md) — 盒子内身份保险库（默认开；尽量不改宿主；P9 已在云电脑接线）  
- [docs/research-claude-cage.md](./docs/research-claude-cage.md) — 调研：Claude Cage 的低成本固定化环境  
- [docs/compare-desktop-windows-vs-linux-wsl.md](./docs/compare-desktop-windows-vs-linux-wsl.md)  
- [docs/analysis-claude-linux-telemetry.md](./docs/analysis-claude-linux-telemetry.md)  
- [docs/architecture-v1-container.md](./docs/architecture-v1-container.md) — 自建容器（后备，暂不并列）  
- [docs/architecture-v0.md](./docs/architecture-v0.md) — Hyper-V（已弃用）

## 仓库骨架

```
src/wsl-box/             # ★ 当前主轨：host PowerShell + guest bash
src/cbox/                # 后备：Rust 自建容器骨架
src/ClaudeBox.Gateway/   # 网关（可复用到 WSL）
src/ClaudeBox.Control/   # 旧 HV 控件（参考；M4 Start-ClaudeBoxControlUi 仍可 -SelfCheck）
src/ClaudeBox.Guest/     # 旧访客脚本（参考）
src/claudebox/           # C++ 核心（身份 / 挂载 / 网关 / 伪装 / 伴生浏览器 / 保险库 / Desktop 计划）
src/claudebox-ui/        # 当前 C++/Qt 设置页
config/                  # 旧 HV/container 配置示例
docs/
```

## Win11 · WSL 轨（摘要）

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass
Copy-Item .\src\wsl-box\config\wsl-box.example.yaml .\src\wsl-box\config\wsl-box.yaml
# 编辑 gateway / identity / visible_dirs（不要挂 C:\）

# 管理员如需：wsl --install -d Ubuntu-24.04
.\src\wsl-box\host\New-ClaudeBoxWsl.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
.\src\wsl-box\host\Set-ClaudeBoxWslMounts.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
.\src\wsl-box\host\Invoke-ClaudeBoxGuest.ps1 -Script install-claude-desktop.sh
.\src\wsl-box\host\Start-ClaudeBoxWsl.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
.\src\wsl-box\host\Invoke-ClaudeBoxGuest.ps1 -Script verify-spoof.sh
```

## 归档

旧 Hyper-V / 容器方案 7z：Google Drive 文件夹 `legacy-hyperv-container-archive`（文件 `Claude-Box-legacy-hyperv-container-20261102.7z`）。

本地开发机勿 `git push`，除非和零明确要求。
