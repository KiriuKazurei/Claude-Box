# 架构方案 v2 · WSL2 + Linux Claude Desktop（当前主实现轨）

> 决策日：2026-11-02（Asia/Tokyo）  
> 决策者：和零  
> 2026-11-11：和零选定 **纯 WSL2 为主方案**（本文件）。效果不好再换 WHPX+QEMU 或 [architecture-v1-container.md](./architecture-v1-container.md) 的 `cbox`。Hyper-V 方案已归档到 Google Drive 7z。  
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
| 机型可真、编码不可见 | 可从宿主注入真实厂商 / 产品型号供 Desktop 读取；**禁止**传入或泄漏序列号、主板/机箱 UUID、MAC、磁盘序列号、CPU ID、MachineGuid 等硬件编码 |
| 反遥测 | 网关/hosts/nft 拦 Sentry、Datadog RUM、`/api/event_logging` |
| Computer use | **已降权**；Linux Desktop 官方无 CU |
| 可选止血（默认关） | 本机 IP 波动 → 礼貌关再强杀 Claude（学 claude-switcher；见 [kill-on-local-ip-change.md](./kill-on-local-ip-change.md)） |
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
4. 将允许的厂商 / 型号写入展示层（见下节）；**不**写入硬件编码
5. **备用**（默认关）：asar 定点改写 `lg()` / `bCt()`（见 spoofing 文档）

## 挂载策略

1. `%USERPROFILE%\.wslconfig` / 发行版 `/etc/wsl.conf`：
   - `[automount] enabled=false`（或 `mountFsTab=false` + 自定义）
   - 避免 Claude 默认看见整盘 `C:`
2. 仅对 `visible_dirs`：`wsl.exe` + `mount --bind` 到 `/mnt/claude-box/<name>`，或 `/etc/fstab` 条目由 host 脚本生成。
3. 拒绝把 `C:\` / `C:\Users\<宿主>` 整棵挂进发行版。

## 网关与反遥测

复用 M2 思路（`ClaudeBox.Gateway`），WSL 适配：

- 网关听宿主 `0.0.0.0` 或 WSL 可见的宿主机 IP（`host.docker.internal` 类：`$(ip route | awk '/default/{print $3}')`）。
- 发行版导出 `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`/`NO_PROXY=localhost,127.0.0.1`。
- **blocklist 最低集**（可按 JSONL 增删）：
  - `*.ingest.us.sentry.io` / Sentry 相关
  - Datadog RUM `browser-intake-*.datadoghq.*` / `/api/v2/rum`
  - `api.anthropic.com` 的 `/api/event_logging`（路径级；勿误杀主 API——优先用代理 ACL / SNI 规则精细化）
- 可选：发行版 `nftables` 禁止非代理出网（强化版，W1+）。

### 可选 · 本机 IP 波动杀进程（默认关 · 已知有效）

和零决策（2026-11-02）：增加明确知晓有效的止血方案——**宿主本机 IP 集合变化时，快速杀掉 box 内全部 Claude 进程**。

- 学 [claude-switcher](https://github.com/YOURSJEYO/claude-switcher)：**先礼貌关窗口/发关闭信号，短超时（约 10s）后再强杀**，避免半写坏 userData。
- 配置：`features.kill_on_local_ip_change: false`（**默认 OFF**）。
- **不替代** 唯一网关 / 强制代理；只补「出口身份抖了、进程还挂着」的缺口。
- 专篇与验收：[kill-on-local-ip-change.md](./kill-on-local-ip-change.md)；宿主桩脚本：`src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1`。

### OAuth（OA）跳转 · 伴生指纹浏览器（规划 · 默认开）

和零决策（2026-11-11）：参考 [CCBaga](https://github.com/liseami/CCBaga)，学 **伴生指纹浏览器** 与 **身份保险库**，并尽可能不改宿主环境。Claude 跳出登录 / 授权页时，用独立 profile 的指纹浏览器打开，避免宿主 Chrome/Edge 侧泄漏；出网仍经唯一网关。配置 `features.oauth_open_in_fingerprint_browser: true`。专篇：[companion-fingerprint-browser.md](./companion-fingerprint-browser.md)；桩：`src/wsl-box/companion-browser/open-oauth-url.sh`。

### 身份保险库（规划 · 默认开 · 尽量不改宿主）

和零补充（同日）：保险库也学。归档 / 切换只发生在发行版或 `%LOCALAPPDATA%\ClaudeBox\` 专属目录，**不**改宿主时区语言，也**不**把宿主日常 Claude 目录当作工作副本。配置 `features.identity_vault: true`。专篇：[identity-vault.md](./identity-vault.md)；桩：`src/wsl-box/identity-vault/`。

## 里程碑（WSL 轨）

| ID | 交付 | 验收 |
|----|------|------|
| **W0** | 文档 + `src/wsl-box` 脚手架 | 本仓库可本地 commit；脚本可在无 WSL 的 CI 上做语法/结构检查 |
| **W0.5** | **云电脑 Linux dry-run 框架**（`src/wsl-box/linux/`） | 无 Win11/WSL：同步 guest → `.run/claude-box`；`Run-Smoke` 过 apply-spoof / verify / gateway tips / host identity；Desktop 仅本地 deb `--check`；见下节 |
| **W1** | 和零机器：启用 WSL2、导入/创建 `ClaudeBox`、关默认 automount | `ls /mnt/c` 失败或为空；`visible_dirs` 可见 |
| **W2** | 安装 Linux Desktop + `apply-spoof` + 经网关启动 | 探针：`Intl` timeZone / locale = 假值；流量进网关 JSONL |
| **W3** | 遥测拦截复盘 | Sentry/Datadog/event_logging 被拦或 culture 已是假值 |
| **W4** | 可选：asar 备用补丁、Cowork/KVM 可行性记录 | 仅主路径不足时启用补丁 |
| **W5** | 可选：`kill_on_local_ip_change` 监视器 | 默认关；打开后 IP 变化 → 礼貌关→强杀；见专篇 |
| **W6** | OA 跳转走伴生指纹浏览器 | 授权 URL 不落入宿主默认浏览器；独立 profile；时区/语言与盒子 identity 一致；流量进网关；引擎未就绪则拒绝打开（不回退宿主浏览器） |
| **W7** | 盒子内身份保险库 | 归档/切换/回滚只动盒子目录；宿主时区语言与默认 Claude 目录不变；可选与伴生浏览器 profile 绑定 |

## 机型注入与硬件编码屏蔽

和零（2026-11-11 · SCOPE v0.3.3）：Desktop **可以**看到宿主真实的厂商与产品型号；**不得**看到序列号、主板/机箱 UUID、MAC、磁盘序列号、CPU 处理器 ID、Windows MachineGuid、设备实例路径等硬件编码。时区 / 语言伪装不因此放宽。

这不是「把环境装成普通真机」或隐藏 WSL。内核名 `microsoft-standard-WSL2`、virtio、hypervisor 标志等痕迹会继续存在，本方案不把它们当成要修掉的缺陷。

### 配置

`spec.host_identity`（见 `src/wsl-box/config/wsl-box.example.yaml`）：

- `expose_real_model: true`：允许发布厂商 / 型号
- `manufacturer` / `product`：可选预填；真机上通常由导出脚本写入，不必把真实型号提交进仓库
- `hide_identifiers: true`：**硬约束**。设为 false 时 apply / verify 必须失败
- `hidden_identifier_keys`：文档 + 校验清单（serial、UUID、MAC、disk_serial、cpu_id、machine_guid、device_instance_path 等）。代码里还有一份内置黑名单，配置删掉某项也不会放开

### 真机（尚未做）

`src/wsl-box/host/Export-ClaudeBoxHostIdentity.ps1` **只**读取 `Win32_ComputerSystem` 的 `Manufacturer` 与 `Model`，写成 guest 侧允许清单。不查询 `Win32_BIOS` 序列号、`Win32_ComputerSystemProduct` UUID、网卡 MAC、磁盘序列号、`Win32_Processor.ProcessorId`、或 `MachineGuid` 注册表。云电脑测试用 `-FixturePath` 注入虚构清单。

### 展示层（dry-run 合同）

本机 `/sys/class/dmi/id` 通常是内核提供的，无特权无法安全改写，本框架**不**在 dry-run 上改写活的 sysfs。Desktop 与 verify 读的是盒子前缀下的展示层：

| 路径 | 内容 |
|------|------|
| `$CLAUDE_BOX_PREFIX/env/host-model.env` | `CLAUDE_BOX_HOST_MANUFACTURER` / `CLAUDE_BOX_HOST_PRODUCT` |
| `$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor` | 厂商 |
| `$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name` | 产品型号 |
| `$CLAUDE_BOX_PREFIX/var/host-identity.allowed.yaml` | 净化后的允许字段副本 |

`guest/apply-host-identity.sh` 会丢弃清单里的禁止字段，并拒绝把看起来像 UUID / MAC 的值当作型号发布。`guest/verify-host-identity.sh` 在型号可见、禁止值出现在展示层或 advertised env、或夹具序列号泄漏时失败。既有 TZ / locale 探针仍须通过。

真机 WSL 若以后要做 DMI overlay，也必须遵守同一合同：只发布型号，不发布编码；仍不改写「隐藏 WSL」为目标。

### 尚未完成

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
- 伴生指纹浏览器（OA）：[companion-fingerprint-browser.md](./companion-fingerprint-browser.md)；桩 `src/wsl-box/companion-browser/`
- 身份保险库：[identity-vault.md](./identity-vault.md)；桩 `src/wsl-box/identity-vault/`
