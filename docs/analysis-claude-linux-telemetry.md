# Linux Claude Desktop 静态分析：遥测 + 时区/语言钩子

分析日期：2026-11-02（JST）。环境：box（Linux），**仅静态分析，未实际启动 GUI / 未登录**。  
对比对象：同版本 Windows Desktop 载荷（见 `docs/analysis-claude-windows.md`，以及对话中对 `Ph` / `LTt` / `m5r` / `W_a` 等的检索）。  
安装包与解包目录均在本地 `installers/`（已被 `.git/info/exclude` 忽略），**未 push**。

## 0. 下载与版本

| 项 | 值 |
|---|---|
| 来源 | 官方 apt 池 `https://downloads.claude.ai/claude-desktop/apt/stable/` |
| 索引 | `dists/stable/main/binary-amd64/Packages` |
| 包 | `claude-desktop_4.4212.0_amd64.deb` |
| 完整 URL | `https://downloads.claude.ai/claude-desktop/apt/stable/pool/main/c/claude-desktop/claude-desktop_4.4212.0_amd64.deb` |
| Size | 168,999,876 字节 |
| SHA256 | `4fcf658e0fc895390ab82593287281dfbce261b0ec3ea90060e82d8bfbfd7a84`（与 Packages 索引一致） |
| 产品版本 | **4.4212.0**（与当时 Windows MSIX `.latest` **同版本**） |
| Electron / Chromium | Electron **44.9.3** / Chrome **152.0.7977.167**（二进制字符串） |
| 本地路径 | `installers/linux/claude-desktop_4.4212.0_amd64.deb` |
| 解包 | `installers/linux/payload/`（`dpkg-deb -x`） |
| asar | `installers/linux/asar/`（`@electron/asar extract …/resources/app.asar`） |
| ion UI 包 | `resources/ion-dist.asar`（另解到 `/tmp/claude-linux-analysis/ion`，未入库） |

文档入口：`https://code.claude.com/docs/en/desktop-linux`、`https://claude.com/download/`。

## 1. 载荷布局（相对 `payload/`）

```
usr/bin/claude-desktop                    # 启动入口（指向 lib 内二进制）
usr/lib/claude-desktop/claude-desktop     # Electron 主二进制
usr/lib/claude-desktop/locales/           # Chromium 语言包
usr/lib/claude-desktop/resources/
  app.asar                                # 主程序 @ant/desktop 4.4212.0
  app.asar.unpacked/
    node_modules/@ant/claude-native/claude-native-binding.node
    node_modules/node-pty/.../pty.node
  ion-dist.asar                           # 渲染层 UI（含 Datadog RUM vendor）
  smol-bin.x64.img                        # Cowork Linux VM 镜像（~27MB；Win 侧为 vhdx）
  cowork-linux-helper                     # Go；拉 QEMU/KVM + virtiofsd
  virtiofsd, chrome-native-host, claude-ssh-askpass, …
usr/share/applications/*.desktop
usr/share/icons/...
```

`package.json`：`name=@ant/desktop`，`main=.vite/build/index.pre.js`，依赖含 `@ant/claude-native`、`@ant/computer-use-mcp`（**产品层 Computer Use 在 Linux 仍标为不可用**；包内仍带 MCP 依赖）、`https-proxy-agent`、`ws`。

**与 Windows 同构点**：同一 Electron 应用、同一 asar 主链路、同一 Sentry DSN、同一遥测开关键。  
**Linux 特有**：`cowork-linux-helper` + QEMU/KVM + `smol-bin.x64.img`（替代 Win 的 `cowork-svc.exe`/HCS + vhdx）；无 `GetTimeZoneInformation` 业务钩子（Win32 API 字符串只在 Win 外壳里出现）。

## 2. 遥测库存（高信号优先）

企业/防火墙说明用的「出站端点注册表」在  
`asar/.vite/build/index.chunk-CvhJZ02X.js`（与 Windows 的 `index.chunk-2SHPxESV.js` 同角色；**块文件名因打包哈希不同**）。

### 2.1 Essential（`disableEssentialTelemetry` / `telemetry.disableEssential`）

| 组 | 端点 | 作用 |
|---|---|---|
| **sentry** | `*.sentry.io`、`sentry.io`；防火墙旁注 `*.ingest.us.sentry.io`；具体 ingest host 常量 `o1158394.ingest.us.sentry.io` | 崩溃/异常/性能上下文 |
| **datadog-rum** | `{anthropicHost}/api/v2/rum`（exactPath）+ `browser-intake-{datadoghq.com,us3,us5,ap1,ap2}.datadoghq.com`、`browser-intake-datadoghq.eu`、`browser-intake-ddog-gov.com` | 前端 RUM |

主进程 Sentry 初始化（`index.pre.js`）：

```text
dsn: "https://2f98127cbffe4740b1f767a2de77d23b@o1158394.ingest.us.sentry.io/4507368973008896"
```

与 Windows **同一 DSN / 同一 org/project**。  
`serverName`：`SENTRY_NAME` 环境变量，否则 `os.hostname()`。

Sentry 上下文集成还会带（`culture` 开关打开时）：

- `lg()` → `{ locale, timezone }`（见 §3）
- `ug()` app 内存/启动时间
- `dg()` / `cg()`→`vg()` 设备与 **Linux 发行版**（读 `/etc/*-release`）
- `yg()` 云厂商环境变量探测

渲染进程窗口（`mainWindow.js` / `aboutWindow.js` / `quickWindow.js` / `findInPage.js`）内置 Sentry `CultureContext` 插件，把 `locale`/`timezone`/`calendar` 并入事件。

Electron `crashReporter.start({ submitURL: "https://f.a.k/e", uploadToServer: false, … })`：本地 Crashpad，**不直接上传到假 URL**；minidump 经 Sentry Electron 集成再上报（受 essential 开关约束——**待运行验证**）。

### 2.2 Nonessential（`disableNonessentialTelemetry`）

| 组 | 端点 |
|---|---|
| **anthropic-telemetry** | `{anthropicHost}/api/event_logging/`（renderer）+ `api.anthropic.com/api/event_logging/`（subprocess，文案为 Claude Code usage telemetry） |

刷新实现可见 `…/api/event_logging/v2/batch`（同 chunk）。

### 2.3 Datadog RUM（ion-dist）

`ion-dist.asar` 内明确带 Datadog browser agent：

- `assets/v1/vendor-datadog-ed8Ee4eE.js`
- `assets/v1/index-C5736t2V.js` 硬编码示例/生产配置片段：  
  - `clientToken: "pub71869dceb5b70dba6123af9ca357d1f9"`  
  - `applicationId: "df447632-9210-4ee5-a49a-348e4fa17665"`  
- `rumEncoder.worker-*.js`

流量可走 Anthropic 反代 `/api/v2/rum` 或直连 `browser-intake-*`。

### 2.4 功能开关 / 实验（非独立「商业分析 SDK」出站为主）

- **GrowthBook**：主进程 chunk 内有完整 `initGrowthBook` / feature 解析；特征多半随 Anthropic API / bootstrap 下发，**未见 `cdn.growthbook.com` 硬编码**。
- **Statsig**：仅见 3P 自定义 bootstrap 里空壳 `statsig:{user,values,values_hash:"custom3p"}`；`segment.com` / `amplitude.com` / `mixpanel.com` / `posthog.com` / `launchdarkly.com` / `statsig.com` 出现在 **允许域名大词表**（`index.chunk-Biv_6KPS.js`），**不是**内嵌 Segment/Amplitude SDK。
- **OpenTelemetry**：Sentry Node 集成字符串提到 `openTelemetryInstrument…`，属 Sentry 选项旁路，不是独立 OTel exporter。

### 2.5 设备标识与其它会「离开机器」的字段

- **`ant-did`**：userData 下随机 UUID 文件（与 Windows 相同逻辑；Linux 默认 Electron `userData` ≈ `~/.config/Claude`——**路径待实机确认**）。
- 请求头族：`anthropic-client-platform=desktop_app`、`anthropic-client-app=com.anthropic.claudefordesktop`、`anthropic-client-version`、`device-class`、`total-memory-gb`、`os-platform`、`os-version`（`index.chunk-CvhJZ02X.js` 的 `rx`/`RYt`）。
- 桌面 managed config 字符串（`FY()`）会拼 `preferredSystemLanguages`、`homePath`、`deviceClass`、`appVersion` 等（`--desktop-managed-config=`）。

### 2.6 原生模块

`claude-native-binding.node`：**未**发现 Sentry/Datadog/timezone Win32 类符号；与遥测无额外出站。  
`cowork-linux-helper`：QEMU/KVM/virtiofsd 编排；无遥测 SDK 字符串。  
主二进制 Chromium/ICU：内含 `/etc/timezone`、`ICU_TIMEZONE_FILES_DIR`、ICU TimeZone 类名——**JS 全挡后 ICU 仍可能读系统时区**（与 Windows 经 ICU/Win32 泄漏同理）。

## 3. 时区与系统语言钩子（与 Windows 对照）

Linux / Windows 是**同一套 JS**，仅 minify 符号名不同。

### 3.1 直接读 IANA 时区名（`Intl.DateTimeFormat().resolvedOptions().timeZone`）

| 角色（Windows 对话中的名） | Linux 符号 | 文件 | 行为 |
|---|---|---|---|
| `Ph()` | **`lg()`** | `index.pre.js` | 校验 ICU 可用后返回 `{ locale, timezone }`；写入 Sentry culture |
| `Ls()` / `Is()` / `Ms()` | **`Ls()` / `Is()` / `Ms()`**（同名） | `mainWindow.js` / `aboutWindow.js`+`quickWindow.js` / `findInPage.js` | Sentry `CultureContext`：`{ locale, timezone, calendar }` |
| `W_a()` | **`xma()`** | `index.chunk-CvhJZ02X.js` | `return Intl.DateTimeFormat().resolvedOptions().timeZone` |
| （校验/回退） | `Cma` / **`wma()`** / **`Tma()`** | 同上 | IANA 校验；失败则用 `getTimezoneOffset` 造 `Etc/GMT±N`；定时任务前缀 `CRON_TZ=<iana>` |
| 提示词 `{{currentTimezone}}` | （`eF` 附近） | `index.chunk-BeHaB40K.js` | 替换进 prompt |
| 调度迁移参数 | （`Qr` 附近） | `index.chunk-CP2jc2hA.js` | 把当前时区传入迁移 |
| Cowork/Code 子进程 `TZ` | （两处 env） | `index.chunk-BK_sL6Pi.js` | `env: { TZ: Intl.DateTimeFormat().resolvedOptions().timeZone }` 注入 VM bash / Claude Code |

`lg()` 原文要点：

```js
function lg(){
  try {
    if (typeof process.versions.icu != "string") return;
    // Intl "es" month sanity check …
    let e = Intl.DateTimeFormat().resolvedOptions();
    return { locale: e.locale, timezone: e.timeZone };
  } catch {}
}
```

### 3.2 只读 UTC 偏移（`getTimezoneOffset`）

- `_ma` / `vma`：cron 本地时换算（`CvhJZ02X.js`）
- `w()`：`index.chunk-CzEDa01f.js` → 调试/EDR 风格日志 `UTC±HH:MM`
- Buddy BLE：`index.chunk-DZ-bSRBO.js` → `[unix, -offset*60]` 发给设备
- 日期格式库、office365-mcp/pdf 等库内偏移（次要）

### 3.3 系统语言 / Locale

| 角色（Windows） | Linux 符号 | 文件 | API |
|---|---|---|---|
| `LTt()` | **`bCt()`** | `index.chunk-CvhJZ02X.js` | `app.getPreferredSystemLanguages()`，空则 `app.getLocale()`，再匹配 `resources/i18n/*.json` |
| `m5r()` | **`W3r()`**（方向） | 同上 | `app.getLocale()` + `--force-ui-direction` → RTL/LTR（`U3r`） |
| （额外） | **`FY()` + `i6r()`** | 同上 | managed-config JSON 写入 `preferredSystemLanguages: i6r(app.getPreferredSystemLanguages())`（规范化，丢掉 `C`/`POSIX`） |
| 诊断转储 | （`H` 附近） | `index.chunk-Bq0psiXU.js` | 文本里写 `Locale: app.getLocale()`、`System Locale: app.getSystemLocale()` |

Electron 这些 API 在 Linux 上最终来自 **glibc locale / `LANG`·`LC_*` / 桌面门户**，再经 Chromium ICU；**不是**单独的 `/etc/timezone` 用户态读取。但 Chromium 二进制字符串含 `/etc/timezone`，ICU 仍可能读它。

### 3.4 硬编码 / 非探测

- `America/Los_Angeles`：一处调度/展示用 `Intl.DateTimeFormat("en-US",{timeZone:"America/Los_Angeles",…})`（**写死**，不是探测本机）。
- `set_time_zone` 类远程动作名若存在，仍是白名单动作名而非探测（与 Windows 结论一致）。

### 3.5 对照小结

| 能力 | Windows | Linux 4.4212.0 |
|---|---|---|
| Sentry culture `{locale,timezone}` | `Ph` + 各窗 `Ls/Is/Ms` | **`lg` + 同名窗函数**（逻辑相同） |
| UI 语言包选择 | `LTt` ← preferred langs | **`bCt`** 相同 API |
| RTL | `m5r` | **`W3r`** |
| Cron `CRON_TZ` | `W_a` 链 | **`xma`→`wma`→`Tma`** |
| 子进程 `TZ=` | WNSDfQz8 类 chunk | **`BK_sL6Pi.js`** |
| Win32 `GetTimeZoneInformation` | Electron/`cowork-svc` 字符串可见 | **无**；改走 ICU + `/etc/timezone` + `TZ` |
| Cowork 沙箱 | HCS + vhdx | QEMU/KVM + `smol-bin.x64.img` |

**结论：遥测栈与时区/语言钩子与 Windows Desktop 实质同栈；伪装策略可复用，且在 WSL/Linux 上用 `TZ` / `LANG` / `LC_*` / 假 `/etc/localtime` 更易生效。**

## 4. Claude Box（WSL 路径）建议拦截 / 伪装点

优先级从高到低：

1. **网络闸**（最省事、最稳）  
   - 拦或改道：`*.sentry.io` / `o1158394.ingest.us.sentry.io`  
   - `browser-intake-*.datadoghq.*` / `*.datadoghq.eu` / `browser-intake-ddog-gov.com`  
   - `{anthropicHost}/api/v2/rum`、`/api/event_logging/**`  
   - 可选：企业策略键 `disableEssentialTelemetry` / `disableNonessentialTelemetry`（代码支持；**正式签名包是否接受本地策略文件 — 待验证**）

2. **环境伪装（主路径 · 2026-11-02 和零确认：不改程序）** — 见 [spoofing-timezone-locale.md](./spoofing-timezone-locale.md)  
   - `TZ=<目标 IANA>`（挡住 `Intl`、`CRON_TZ`、子进程 `TZ` 注入）  
   - `LANG` / `LC_ALL` / `LANGUAGE`（挡住 `getPreferredSystemLanguages` / `getLocale`）  
   - 统一 `/etc/localtime` → 目标 zoneinfo；必要时 `ICU_TIMEZONE_FILES_DIR`  
   - 可选 Electron `--lang=`；`SENTRY_NAME=<假主机名>`（只盖 Sentry serverName）

3. **身份隔离**  
   - 独立 userData（每身份一份 `ant-did`）；勿依赖签名版对 `CLAUDE_USER_DATA_DIR` 的尊重（Windows 已知签名包忽略；Linux **待验证**）

4. **JS hook / asar 改写（备用 · 仅当环境不够）**  
   - Patch/preload：`Intl.DateTimeFormat.prototype.resolvedOptions`、`Date.prototype.getTimezoneOffset`  
   - Electron：`app.getPreferredSystemLanguages` / `getLocale` / `getSystemLocale`  
   - 或改 asar 内 `lg()` / `bCt()`（Linux）与 Windows 同角色函数

5. **不必优先**  
   - `claude-native-binding.node`（无遥测/时区符号）  
   - 词表里的 Segment/Amplitude 域名（非 SDK）

## 5. 未做 / 待验证

- 未启动 GUI、未抓包确认 RUM/Sentry 在登录后的实际频率与字段。  
- 未验证 Linux 上 `CLAUDE_USER_DATA_DIR`、策略文件、`disableEssentialTelemetry` 在签名包中的生效路径。  
- 未对 `ion-dist` 全量做与 Windows ion 的 diff。  
- WSL2 嵌套 QEMU/KVM Cowork 能否跑 —— 产品与对比报告已标风险，本次未测。  
- Computer Use：官方仍写 Linux 不可用；包内仍有 `@ant/computer-use-mcp` 依赖字符串。

## 6. 复现命令（摘要）

```bash
# 索引 + 最新 deb
curl -fsSL https://downloads.claude.ai/claude-desktop/apt/stable/dists/stable/main/binary-amd64/Packages -o Packages.amd64
# 本分析固定版本：
curl -fLO https://downloads.claude.ai/claude-desktop/apt/stable/pool/main/c/claude-desktop/claude-desktop_4.4212.0_amd64.deb
sha256sum claude-desktop_4.4212.0_amd64.deb  # expect 4fcf658e…

dpkg-deb -x claude-desktop_4.4212.0_amd64.deb payload/
npx @electron/asar extract payload/usr/lib/claude-desktop/resources/app.asar asar/
```

