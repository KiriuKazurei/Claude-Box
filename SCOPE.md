# Claude Box · 范围 v0.4.1（主方案：纯 WSL）

## 一句话

隔离运行时里跑 Claude Desktop；出入网经唯一网关（可指定 HTTP/SOCKS5）；目录可见可控；在盒子/进程内伪装时区与语言并拦截环境遥测。

## 平台

- 优先 Windows 11（家庭版可用；不依赖 Hyper-V）
- 其次 macOS（另议）

## 运行时形态（可替换）

- **Docker-like / 容器 / WSL / VM** 都只是手段，**不是硬约束**。
- 硬约束：隔离跑 Claude、唯一网关、目录可见性、环境伪装与反遥测。
- Computer use **已降权**（Linux Desktop 官方无 CU）。

### 现阶段主方案（2026-11-11 · v0.4.0）

和零选定：**纯 WSL2 + 可见目录** 作为 Claude Box 的主方案（不装 Docker Desktop）。目标是完整隔离：专用发行版跑 Linux Claude Desktop、唯一网关、只开放指定目录、盒子/进程内时区语言伪装、OA 走伴生指纹浏览器、保险库尽量不改宿主。不做「伪装成非虚拟机以绕开检测」。

**主方案 · WSL2 + Linux Claude Desktop**

- 专用发行版（如 `ClaudeBox`）；宿主 Win11 保持东八区 + 简中，只在发行版 / 进程内伪装。
- 切断默认 `/mnt/c`，只挂 `visible_dirs`；网关拦 Sentry / Datadog RUM / `event_logging`（默认名单写入 gost YAML；HTTPS CONNECT 看不到路径，默认不做 TLS MITM）。
- **官方风险**：Anthropic 建议 WSL 用户装 **Windows** Desktop；本路径不受官方支持。见 [docs/architecture-v2-wsl.md](./docs/architecture-v2-wsl.md)。
- 代码：现有框架在 `src/wsl-box/`；C++/Qt 主控件按 [docs/framework-build-plan.md](./docs/framework-build-plan.md) 在云电脑推进到接近 beta。真机启用 WSL 仍需和零明确批准。

**后备 · 主方案效果不好再换**

- **WHPX + QEMU** 跑 Debian/Ubuntu 客机，再装 Linux Desktop（家庭版可用，不依赖完整 Hyper-V / WSL / Docker）。隔离更硬，磁盘约需预留 20 GB 量级。对照见 [docs/research-claude-cage.md](./docs/research-claude-cage.md)。
- **不依赖 WSL 的 Windows 盒子**（`cbox` + 专属目录跑 Windows Desktop）：见 [docs/architecture-v1-container.md](./docs/architecture-v1-container.md)、`src/cbox/`。暂不并列开发。

### 已归档 / 弃用

- **Hyper-V Windows 访客**方案：源码仍在仓库作参考；完整方案包已上传 Google Drive（`Claude-Box-legacy-hyperv-container-*.7z`）。
- Sandbox 仅历史冒烟思路。

## 隔离与反遥测（核心）

1. **文件系统**：默认可见目录；排除宿主系统盘整挂；WSL 关 automount。
2. **身份伪装**：发行版/容器独立假环境（非改宿主）。
   - **身份展示由用户按字段授权**（2026-11-11 · v0.4.0）：部署后采集电脑有关信息，在设置页列出。用户选择哪些原样提供给 Claude、哪些隐藏、哪些修改后再展示。默认：厂商 / 型号可原样展示；序列号、UUID、MAC、磁盘序列号、CPU ID、MachineGuid、设备实例路径等硬件编码默认隐藏。详见 [docs/identity-presentation.md](./docs/identity-presentation.md)。
   - **时区 / 语言仍只在盒子内伪装**，不跟宿主；设置页展示的是盒子伪装值，不把宿主真实时区作为默认可展示字段。
3. **网络**：唯一网关 + 可选 JSONL 复盘。
4. **重点**：降低 Claude 对真宿主机环境的遥测穿透。
5. **时区 / 系统语言伪装**  
   - 作用域：仅 box / Claude 进程（含 WSL）。  
   - 优先不改程序；备用 patch asar / hook。  
   - 详见 [docs/spoofing-timezone-locale.md](./docs/spoofing-timezone-locale.md)。

6. **可选 · 本机 IP 波动时断开网关并结束进程（默认关 · 唯一网关模块）**  
   - 归属唯一网关模块；设置在网关页。  
   - 触发：宿主本机 IP 集合变化时，**并行启动**：断开唯一网关，并礼貌关闭 box 内 Claude 进程。两条线程同时开始，互不等待。  
   - 进程学 [claude-switcher](https://github.com/YOURSJEYO/claude-switcher)：礼貌关 → grace（默认 10s）→ 仍存活则强杀。grace 只约束进程，不推迟网关断开。  
   - 配置：`features.kill_on_local_ip_change: false`（默认 OFF）。  
   - 已知有效。详见 [docs/kill-on-local-ip-change.md](./docs/kill-on-local-ip-change.md)。

7. **OAuth（OA）跳转走伴生指纹浏览器**  
   - Claude 打开登录 / 授权页时，用独立指纹浏览器浏览，**不走宿主默认浏览器**，避免 Cookie、扩展、真机指纹从浏览器侧泄漏。  
   - 参考 [CCBaga](https://github.com/liseami/CCBaga) 的伴生浏览器；出网仍经唯一网关。  
   - 配置 `features.oauth_open_in_fingerprint_browser: true`（意图默认开；云电脑 P8 已以 dry-run / mock 引擎接线，真机活体 OA 尚未核验，不得把真机项标成成功）。  
   - 详见 [docs/companion-fingerprint-browser.md](./docs/companion-fingerprint-browser.md)。

8. **本地身份保险库（学 CCBaga · 尽量不改宿主）**  
   - 盒子内归档 / 切换 Claude Desktop（及 Code）本地身份，失败可回滚；登录态随身份走。  
   - 保险库放在发行版或 `%LOCALAPPDATA%\ClaudeBox\` 专属目录，**不**改宿主时区/语言/默认浏览器，也**不**把宿主日常 Claude 目录当作工作副本。  
   - 配置 `features.identity_vault: true`（意图默认开；云电脑 P9 已可对盒子前缀做归档 / 切换 / 回滚，真机活体登录态尚未核验）。  
   - 详见 [docs/identity-vault.md](./docs/identity-vault.md)。


## 控件（MVP · WSL 轨）

- 主控件：**C++20 + Qt 6**（见 [docs/framework-build-plan.md](./docs/framework-build-plan.md)）
- 身份清单设置页：列出采集结果；按字段选择原样展示 / 隐藏 / 修改后展示（见上）
- 配置网关、上游（含 Clash）、连接日志
- 选择可见目录（拒绝默认挂宿主 `C:`）
- 启停 WSL 发行版内 Desktop（WSLg 窗口）
- 可选开关（网关页）：本机 IP 波动时断开网关并结束盒子内 Claude（默认关；见上）
- OA / 授权页用伴生指纹浏览器打开（默认开；见上；P8 已在云电脑接线）
- 盒子内身份保险库：归档 / 切换 / 回滚（默认开；见上；P9 已在云电脑接线）

开发顺序：先在云电脑把架构与代码做到接近 beta，再放到真机；真机启用 WSL 仍须和零明确批准。云电脑 dry-run 验收见 [docs/near-beta-dry-run-acceptance.md](./docs/near-beta-dry-run-acceptance.md)。

## 暂不进 MVP

- Computer use 一等支持
- 改写宿主默认 Claude 目录 / 宿主凭据库作为保险库工作副本（身份切换只在盒子内做）
- Linux 宿主机一等支持
- Web 远程桌面
- 默认 TLS 解密遥测正文
- 默认启用 asar 补丁
