# 架构方案 v2 · WSL2 + Linux Claude Desktop（当前主实现轨）

> 决策日：2026-11-02（Asia/Tokyo）  
> 决策者：和零  
> 2026-11-11：和零选定 **纯 WSL2 为主方案**（本文件）。效果不好再换 WHPX+QEMU 或 [architecture-v1-container.md](./architecture-v1-container.md) 的 `cbox`。Hyper-V 方案已归档到 Google Drive 7z。  
> 同日补充：身份展示改为设置页按字段授权；框架在云电脑做到接近 beta 再上真机。计划见 [framework-build-plan.md](./framework-build-plan.md)，口径见 [identity-presentation.md](./identity-presentation.md)。控件主栈为 C++20 + Qt 6。  
> 伪装策略：[spoofing-timezone-locale.md](./spoofing-timezone-locale.md)  
> 能力对比：[compare-desktop-windows-vs-linux-wsl.md](./compare-desktop-windows-vs-linux-wsl.md)

## 一句话

在 **WSL2 专用发行版**里跑 **Linux Claude Desktop**；宿主 Win11 家庭版保持东八区 + 简中；时区/语言只在发行版/进程内伪装；出入网经宿主网关；默认切断 `/mnt/c`；拦 Sentry / Datadog RUM / `event_logging`。

## 官方风险（必读）

Anthropic 文档对 WSL 用户的建议是：**装 Windows Claude Desktop，再在 WSL 里用 Claude Code**。  
本方案走的是 **「WSL 内安装 Linux Desktop」**，**不受官方支持**。可能遇到：

- 自动更新 / 安装器假设非 WSL 环境
- WSLg 窗口与 GPU / 输入异常
- Cowork（QEMU/KVM）在 WSL2 嵌套虚拟化下不可用或极慢
- 账户、崩溃上报路径与官方排障不一致

验收与排障以「隔离 + 网关 + 伪装」硬约束为准，不以官方 WSL 支持承诺为准。

## 硬约束映射

| 硬约束 | WSL v2 落点 |
|--------|-------------|
| 隔离跑 Claude | 专用 WSL2 发行版（如 `ClaudeBox`），独立 userData / `ant-did` |
| 唯一网关出网 | 宿主 gost/Clash；发行版内 `HTTP(S)_PROXY`/`ALL_PROXY` + 可选 nftables 强制 |
| 目录可见性 | **切断默认 `/mnt/c`**；只 bind-mount `visible_dirs` |
| 环境伪装 | `TZ`、`LANG`/`LC_*`、`/etc/localtime`、可选 `--lang`（不改宿主） |
| 身份展示按字段授权 | 部署后采集电脑信息，设置页决定原样展示 / 隐藏 / 修改后展示；默认厂商与型号可原样展示，硬件编码默认隐藏。见 [identity-presentation.md](./identity-presentation.md) |
| 反遥测 | 网关/hosts/nft 拦 Sentry、Datadog RUM、`/api/event_logging` |
| Computer use | **已降权**；Linux Desktop 官方无 CU |
| 可选止血（默认关 · 唯一网关模块） | 本机 IP 波动 → **并行启动**断开唯一网关与礼貌关闭 Claude；进程 grace 后再强杀（学 claude-switcher；见 [kill-on-local-ip-change.md](./kill-on-local-ip-change.md)） |
| OA 不走宿主浏览器 | Claude 跳转 OAuth / 授权页时，用独立伴生指纹浏览器打开（见 [companion-fingerprint-browser.md](./companion-fingerprint-browser.md)） |
| 身份保险库（尽量不改宿主） | 盒子内归档/切换 Claude 身份（见 [identity-vault.md](./identity-vault.md)）；不改宿主时区语言或默认 Claude 目录 |

## 组件布局

```
宿主 Win11 Home（时区 UTC+8 / 显示 zh-CN 不变）
├─ Clash / 上游代理（可选 TUN）
├─ ClaudeBox.Gateway（gost → 上游；JSONL；blocklist）
├─ src/wsl-box/host/*.ps1
│   ├─ Enable / 创建发行版 ClaudeBox
│   ├─ 配置 .wslconfig / wsl.conf（关 automount）
│   ├─ 注入可见目录挂载
│   └─ 启动：wsl -d ClaudeBox -- … start-claude-desktop.sh
└─ WSL2 发行版 ClaudeBox
    ├─ /etc/wsl.conf          # automount.enabled=false 等
    ├─ /etc/localtime → 假 IANA
    ├─ env: TZ / LANG / HTTP_PROXY …
    ├─ guest scripts: install / apply-spoof / start / verify
    └─ Linux Claude Desktop（Electron；无官方 CU）
        └─ OAuth / 授权跳转 → 伴生指纹浏览器（独立 profile；不走宿主默认浏览器）
```

## 发行版约定

| 项 | 默认 |
|----|------|
| 发行版名 | `ClaudeBox` |
| 基座 | Ubuntu 24.04（可换） |
| 安装根 | `%LOCALAPPDATA%\ClaudeBox\wsl\ClaudeBox`（可配） |
| Desktop 安装 | `.deb` → `/usr` 或用户目录；版本对齐分析笔记（如 4.4212.0） |
| userData | 发行版内独立路径（勿复用宿主 `%APPDATA%\Claude`） |

## 启动与伪装（主路径：不改程序）

宿主 PowerShell 最终等价于：

```powershell
wsl -d ClaudeBox --cd ~ --env TZ=America/Los_Angeles `
  -- env LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 LANGUAGE=en_US `
  HTTP_PROXY=http://$GatewayHost:$Port HTTPS_PROXY=… ALL_PROXY=… `
  /opt/claude-box/guest/start-claude-desktop.sh
```

发行版内 `apply-spoof.sh`（会接着跑 `apply-host-identity.sh`）：

1. `ln -sfn /usr/share/zoneinfo/$TZ /etc/localtime`（需一次特权或镜像预置）
2. 写入 `/etc/default/locale` 或 session env（与配置一致）
3. 可选：Desktop 启动追加 `--lang=en-US`
4. 按用户策略将字段写入展示层（见下节）；默认写入厂商 / 型号，硬件编码默认不写入
5. **备用**（默认关）：asar 定点改写 `lg()` / `bCt()`（见 spoofing 文档）

## 挂载策略

1. `%USERPROFILE%\.wslconfig` / 发行版 `/etc/wsl.conf`：
   - `[automount] enabled=false`（或 `mountFsTab=false` + 自定义）
   - 避免 Claude 默认看见整盘 `C:`
2. 仅对 `visible_dirs`：`wsl.exe` + `mount --bind` 到 `/mnt/claude-box/<name>`，或 `/etc/fstab` 条目由 host 脚本生成。
3. 拒绝把 `C:\` / `C:\Users\<宿主>` 整棵挂进发行版。 云电脑上的 C++ 挂载计划（`claudebox::mount::plan_mounts`）与设置页共用同一套校验，只做 dry-run，不调用 `wsl.exe`。

## 网关与反遥测

复用 M2 思路（`ClaudeBox.Gateway`），WSL 适配：

- 网关听宿主 `0.0.0.0` 或 WSL 可见的宿主机 IP（`host.docker.internal` 类：`$(ip route | awk '/default/{print $3}')`）。
- 发行版导出 `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`/`NO_PROXY=localhost,127.0.0.1`。
- **blocklist 最低集**（可按 JSONL 增删）：
  - `*.ingest.us.sentry.io` / Sentry 相关
  - Datadog RUM `browser-intake-*.datadoghq.*` / `/api/v2/rum`
  - `api.anthropic.com` 的 `/api/event_logging`（路径级；勿误杀主 API——优先用代理 ACL / SNI 规则精细化）
- 可选：发行版 `nftables` 禁止非代理出网（强化版，W1+）。
- 云电脑上的 C++ 核心（`claudebox::gateway`）与设置页共用同一套校验 / 中继计划 / JSONL 状态：空日志路径落在盒子前缀 `var/logs/gateway.jsonl`，不使用宿主 `%LOCALAPPDATA%`。这是 dry-run，不启动 gost，也不绑定 Win11 网卡。Win11 上的实际中继启动仍走 `ClaudeBox.Gateway`。

### 可选 · 本机 IP 波动时断开网关并结束进程（默认关 · 已知有效 · 唯一网关模块）

和零决策（2026-11-02）：增加明确知晓有效的止血方案。2026-11-12 补充：本策略**放在唯一网关模块**；宿主本机 IP 集合变化时，**断开唯一网关**和**礼貌关闭 box 内 Claude 进程**作为两条线程同时启动，互不等待，让丢链接和结束进程尽快一起生效。

- 学 [claude-switcher](https://github.com/YOURSJEYO/claude-switcher) 的进程节奏：**先礼貌关窗口/发关闭信号，短超时（约 10s）后再强杀**，避免半写坏 userData。这段 grace 只约束进程，**不**推迟网关断开。
- 配置：`features.kill_on_local_ip_change: false`（**默认 OFF**）。
- 不能取代「出网必须经唯一网关」这条硬约束；补的是「出口身份抖了、连接和进程还挂着」的缺口。
- 专篇与验收：[kill-on-local-ip-change.md](./kill-on-local-ip-change.md)；C++ 计划：`claudebox/gateway_ip.hpp`；宿主桩脚本：`src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1`。设置页开关在「网关」栏。

### OAuth（OA）跳转 · 伴生指纹浏览器（规划 · 默认开）

和零决策（2026-11-11）：参考 [CCBaga](https://github.com/liseami/CCBaga)，学 **伴生指纹浏览器** 与 **身份保险库**，并尽可能不改宿主环境。Claude 跳出登录 / 授权页时，用独立 profile 的指纹浏览器打开，避免宿主 Chrome/Edge 侧泄漏；出网仍经唯一网关。配置 `features.oauth_open_in_fingerprint_browser: true`。专篇：[companion-fingerprint-browser.md](./companion-fingerprint-browser.md)；入口：`src/wsl-box/companion-browser/open-oauth-url.sh` 与 C++ `claudebox::companion`（P8 已在云电脑以 dry-run / mock 引擎落地）。

### 身份保险库（规划 · 默认开 · 尽量不改宿主）

和零补充（同日）：保险库也学。归档 / 切换只发生在发行版或 `%LOCALAPPDATA%\ClaudeBox\` 专属目录，**不**改宿主时区语言，也**不**把宿主日常 Claude 目录当作工作副本。配置 `features.identity_vault: true`。专篇：[identity-vault.md](./identity-vault.md)；实现：`claudebox::vault` 与 `src/wsl-box/identity-vault/`（P9 已在云电脑以 dry-run / 夹具落地）。

## 框架搭建模块

在云电脑按 [framework-build-plan.md](./framework-build-plan.md) 从 P0 做到 P11（接近 beta），再考虑真机 W1。控件主栈为 C++20 + Qt 6。

## 里程碑（WSL 轨）

| ID | 交付 | 验收 |
|----|------|------|
| **W0** | 文档 + `src/wsl-box` 脚手架 | 本仓库可本地 commit；脚本可在无 WSL 的 CI 上做语法/结构检查 |
| **W0.5** | **云电脑 Linux dry-run 框架**（`src/wsl-box/linux/`） | 无 Win11/WSL：同步 guest → `.run/claude-box`；`Run-Smoke` 过 apply-spoof / verify / gateway tips / host identity；Desktop 仅本地 deb `--check`；见下节 |
| **W1** | 和零机器：启用 WSL2、导入/创建 `ClaudeBox`、关默认 automount | `ls /mnt/c` 失败或为空；`visible_dirs` 可见 |
| **W2** | 安装 Linux Desktop + `apply-spoof` + 经网关启动 | 探针：`Intl` timeZone / locale = 假值；流量进网关 JSONL |
| **W3** | 遥测拦截复盘 | Sentry/Datadog/event_logging 被拦或 culture 已是假值 |
| **W4** | 可选：asar 备用补丁、Cowork/KVM 可行性记录 | 仅主路径不足时启用补丁 |
| **W5** | 可选：`kill_on_local_ip_change` 监视器（唯一网关模块） | 默认关；打开后 IP 变化 → 并行启动断开网关与礼貌关，进程 grace 后再强杀；见专篇 |
| **W6** | OA 跳转走伴生指纹浏览器 | 授权 URL 不落入宿主默认浏览器；独立 profile；时区/语言与盒子 identity 一致；流量进网关；引擎未就绪则拒绝打开（不回退宿主浏览器） |
| **W7** | 盒子内身份保险库 | 归档/切换/回滚只动盒子目录；宿主时区语言与默认 Claude 目录不变；可选与伴生浏览器 profile 绑定 |

## 身份展示（采集 + 设置页按字段授权）

和零（2026-11-11 · SCOPE v0.4.0）：Claude Box 部署后采集电脑有关信息，在设置页列出。用户选择哪些原样提供给 Claude、哪些隐藏、哪些修改后再展示。默认：厂商 / 型号可原样展示；序列号、主板/机箱 UUID、MAC、磁盘序列号、CPU ID、MachineGuid、设备实例路径等硬件编码默认隐藏。时区 / 语言仍只在盒子内伪装，不把宿主真实时区作为默认可展示字段。专篇：[identity-presentation.md](./identity-presentation.md)。

这不是「把环境装成普通真机」或隐藏 WSL。内核名 `microsoft-standard-WSL2`、virtio、hypervisor 标志等痕迹会继续存在，本方案不把它们当成要修掉的缺陷。

### 配置（过渡期与目标）

`spec.host_identity`（见 `src/wsl-box/config/wsl-box.example.yaml`）：

**现行实现（P3 已接线）**

- `expose_real_model: true`：允许发布厂商 / 型号（默认策略为 expose）
- `manufacturer` / `product`：可选预填；真机上通常由导出脚本写入，不必把真实型号提交进仓库
- `hide_identifiers: true`：过渡约束，设为 false 时 apply / verify 必须失败。不是「公开全部编码」的开关；按字段授权走策略文件
- `hidden_identifier_keys`：文档 + 校验清单（serial、UUID、MAC、disk_serial、cpu_id、machine_guid、device_instance_path 等）
- 每字段策略：`CLAUDE_BOX_IDENTITY_POLICY` 或 `$CLAUDE_BOX_PREFIX/var/host-identity.policy.yaml`（`expose` / `hide` / `override`）

**目标形态（P4 设置页仍待做）**

- 部署时把完整采集结果写入盒子内**原始清单**（不自动交给 Claude，不进 git）
- 每字段模式为 `expose` / `hide` / `override`；设置页编辑并保存
- 默认策略与上表一致；用户授权后，指定编码可以进入展示层（原样或修改后）
- 硬开关让位于按字段授权，但默认隐藏编码的测试仍须保留

### 真机采集（尚未做）

当前 `src/wsl-box/host/Export-ClaudeBoxHostIdentity.ps1` **只**读取 `Win32_ComputerSystem` 的 `Manufacturer` 与 `Model`。P2 采集器会扩展为读取完整原始清单（含编码），但只写入盒子内原始清单，默认不发布。云电脑测试用 `-FixturePath` 或等价夹具注入虚构清单，不采集真机编码。

### 展示层（dry-run 合同）

本机 `/sys/class/dmi/id` 通常是内核提供的，无特权无法安全改写，本框架**不**在 dry-run 上改写活的 sysfs。Desktop 与 verify 读的是盒子前缀下的展示层：

| 路径 | 内容 |
|------|------|
| `$CLAUDE_BOX_PREFIX/env/host-model.env` | 按策略发布的字段（默认含 `CLAUDE_BOX_HOST_MANUFACTURER` / `CLAUDE_BOX_HOST_PRODUCT`） |
| `$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor` | 厂商（若策略为展示或修改后展示） |
| `$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name` | 产品型号（若策略为展示或修改后展示） |
| `$CLAUDE_BOX_PREFIX/var/host-identity.presented.yaml` | 按策略生成的完整展示副本 |
| `$CLAUDE_BOX_PREFIX/var/host-identity.allowed.yaml` | 向后兼容的厂商 / 型号摘要 |
| `$CLAUDE_BOX_PREFIX/var/host-inventory.raw.yaml` | 原始清单；不得被当作展示层读取 |

`guest/apply-host-identity.sh` 调用 C++ `claudebox-identity` 按策略写展示层，并拒绝把看起来像 UUID / MAC 的值当作型号发布。`guest/verify-host-identity.sh` 对照同一份策略：隐藏字段（及 override 前的原始值）不得出现在展示层；`expose` / `override` 的字段必须与策略一致。既有 TZ / locale 探针仍须通过。

真机 WSL 若以后要做 DMI overlay，也必须遵守同一合同：只发布策略允许的字段；仍不以「隐藏 WSL」为目标。

### 尚未完成

- 未实现设置页（见 [framework-build-plan.md](./framework-build-plan.md) P4）。P1–P3 策略引擎、采集器与展示层接线已落地
- 未在和零的 Win11 上启用 WSL，也未把展示层接到真实 Claude Desktop 进程
- 未改写活的 `/sys/class/dmi`（dry-run 明确不做）
- 未屏蔽 WSL 内核标识（不会当作缺陷来「修」）

## 云电脑 Linux dry-run（W0.5）

在 Grok Bot / 云电脑等 **纯 Linux** 环境验证 guest 编排（**不**启用用户 Win11 WSL）：

```bash
bash src/wsl-box/linux/Install-ClaudeBoxFramework.sh
bash src/wsl-box/linux/Run-Smoke.sh
bash tests/wsl-box/test-linux-framework.sh   # 含 test-host-identity.sh
```

- 前缀：`<repo>/.run/claude-box`（gitignore）；配置驱动 `TZ`/`LANG`/代理/`blocklist`。
- 宿主 `host/*.ps1` 仍为 Win11 真路径；此处只 dry-run guest。
- Desktop：`Install-ClaudeDesktop.sh --check` 只认 `installers/linux/*.deb`，不自动下载。

## 和零机器上的下一步（W1）


本回合 **不在用户 PC 上自动启用 WSL**（需明确批准再动机器）。手工步骤：

1. **管理员 PowerShell**：`wsl --install -d Ubuntu-24.04`（或先 `wsl --status` 看是否已装）→ 重启如提示。
2. 克隆/同步本仓库到 Win11。
3. 复制 `src/wsl-box/config/wsl-box.example.yaml` → 本地 config；改 `identity` / `gateway` / `visible_dirs`。
   厂商/型号可用 `host/Export-ClaudeBoxHostIdentity.ps1` 写入（只读 Manufacturer 与 Model，不导出序列号等编码）。
4. 运行：
   ```powershell
   cd path\to\Claude-Box
   Set-ExecutionPolicy -Scope Process Bypass
   .\src\wsl-box\host\New-ClaudeBoxWsl.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
   .\src\wsl-box\host\Set-ClaudeBoxWslMounts.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
   .\src\wsl-box\host\Start-ClaudeBoxWsl.ps1 -ConfigPath .\src\wsl-box\config\wsl-box.yaml
   ```
5. 发行版内首次：`sudo /opt/claude-box/guest/install-claude-desktop.sh`（或 host 一键调用）。
6. 验证：`/opt/claude-box/guest/verify-spoof.sh`。

## 与旧轨关系

| 路径 | 状态 |
|------|------|
| WSL2 + Linux Desktop（本文件） | **主方案**；真机启用需和零批准 |
| WHPX + QEMU Linux 客机 | **后备**（家庭版可用，不依赖完整 Hyper-V）；见 [research-claude-cage.md](./research-claude-cage.md) |
| 自建 Windows 容器 / 专属目录 `cbox`（v1） | **后备**，暂不并列开发；见 [architecture-v1-container.md](./architecture-v1-container.md) |
| Hyper-V / 旧 Control-Guest | 已弃用；源码留作参考；方案包已上 Google Drive |

## 代码入口

- Host：`src/wsl-box/host/`（含 `Export-ClaudeBoxHostIdentity.ps1`，只导出厂商/型号）
- Guest：`src/wsl-box/guest/`（`apply-host-identity.sh` / `verify-host-identity.sh`）
- **Cloud Linux dry-run**：`src/wsl-box/linux/`（`Install-ClaudeBoxFramework.sh` / `Run-Smoke.sh`）
- 配置示例：`src/wsl-box/config/wsl-box.example.yaml`
- README：`src/wsl-box/README.md`
- 可选 IP 波动杀进程：[kill-on-local-ip-change.md](./kill-on-local-ip-change.md)
- 伴生指纹浏览器（OA）：[companion-fingerprint-browser.md](./companion-fingerprint-browser.md)；`src/wsl-box/companion-browser/` 与 `claudebox/companion.hpp`（P8）
- 身份保险库：[identity-vault.md](./identity-vault.md)；`claudebox/vault.hpp` 与 `src/wsl-box/identity-vault/`（P9）
- 接近 beta dry-run 验收：[near-beta-dry-run-acceptance.md](./near-beta-dry-run-acceptance.md)（P11；缺真机项不标成功）
