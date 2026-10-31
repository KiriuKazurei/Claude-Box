# Windows Claude Desktop 安装包与载荷分析（用于自建专用容器）

分析日期：2026-10-31（JST）。分析环境：Linux 工作机（box），**仅静态分析，未在 Windows 上运行**。
凡未在 Windows 实机验证的结论标注 **待验证**。安装包与载荷均未提交入库（`installers/` 已被 `.git/info/exclude` 忽略）。

## 0. 工具与 rea 情况

- [morluto/rea](https://github.com/morluto/rea)（npm 包 `rea-agents`，本次版本 rea@6.8.0）：“Reverse Engineer Anything”，一个把逆向能力以 MCP / CLI 暴露给 AI 代理的工具集。原生二进制分析需要外部引擎（Ghidra 12.1.x / Hopper / IDA / Kuna）；JS/Electron 静态分析内置。
- 在 Linux 上 `npm i -g rea-agents` 安装成功（Node 24.22.1）。
  - `rea analyze Claude_Setup.exe` → `provider_unavailable`：box 上没有 Ghidra/Hopper/IDA，Kuna 不支持 PE。**原生 PE 分析未跑通**（装 Ghidra 12.1 + JDK 后可重试）。
  - `rea analyze-javascript-application <asar 目录>` 运行 ~5 分钟后被超时取消，且 `fileIndexWorker.js`、`index.chunk-2SHPxESV.js`（6.8 MB）超出其 1 GiB V8 堆上限，只拿到部分观测。**实际结论主要来自 7z / strings / @electron/asar / Python 正则检索**。
- 其它工具：`7z`、`strings`、`npx @electron/asar`、Python。

## 1. 安装器 `Claude_Setup.exe`

- sha256 `63ba573d…f7617`，约 6.9 MB。**Go 编写的在线引导器（bootstrapper）**，不是 NSIS，也不是经典 Squirrel Setup.exe。证据：大量 Go runtime 字符串（`runtime.SetFinalizer`、`*main.latestVersion`、`net/http` 等）。
- 签名：内嵌 DigiCert 证书链字符串（`http://ocsp.digicert.com`），版权 “2025 Anthropic PBC”；使用 `WinVerifyTrust`（日志串 `Verifying Authenticode signature on %s`、`WinVerifyTrust: %s signature is valid`）对下载物校验。Authenticode 签名者本身未在 Linux 上验签（osslsigncode 未安装）——**待验证**。
- 工作流程（日志字符串推断）：
  1. 查版本：`%s/%s/%s/latest`（Release API，base 为 `https://releases.claude.com/api/desktop/win32` 或 `https://api.anthropic.com/api/desktop/win32`）；失败则 `Release API lookup failed, falling back to CDN .latest` → `%s/releases/win32/%s/.latest`，base `https://downloads.claude.ai`。
  2. 下载 MSIX（`Claude-*.msix`），打印 `Download complete: %d bytes, SHA256: %s`，再 `Installing MSIX from %s (all-users: %v)`，用 WinRT `IPackageManager`（`AddPackageAsync`，可选 `ProvisionPackageForAllUsersAsync`）安装。
  3. MSIX 失败可 `User chose Squirrel fallback` → 下载 `%s/releases/win32/%s/%s/Claude-%s.exe`（Squirrel 版）。
  4. 会清理旧安装：`Removing conflicting CoworkVMService...`、`WARNING: LOCALAPPDATA not set, skipping Squirrel uninstall`、结束已有 Claude 进程。
  5. 检查 `EditionID`、`S Mode`、开发者模式提示；日志 `ClaudeSetup.log`。参数：`--all-users`、`--msix-path`、`--log-path`、`uninstall`。
  6. 读取 WinHTTP 系统代理（`WinHttpGetIEProxyConfigForCurrentUser`、`WinHttpGetProxyForUrl`）——**安装器会走系统代理，可用于让安装也经过网关**。

## 2. 获取到的载荷（官方 CDN）

- `GET https://downloads.claude.ai/releases/win32/x64/.latest` →
  `{"version":"4.4212.0","hash":"cbfe3c56d49c49332ad5a9588303c9dbc70a5e7f"}`
- MSIX：`https://downloads.claude.ai/releases/win32/x64/4.4212.0/Claude-cbfe3c56d49c49332ad5a9588303c9dbc70a5e7f.msix`
  283,055,488 字节，sha256 `5e7cd0edfe4a5561e6f69b0249757d23de752a74151e016fe81bc939ddc5439e`（Last-Modified 2026-10-29 16:16 GMT ＝ 10-30 01:16 JST）。
- 同目录还有 Squirrel 版 `Claude-<hash>.exe`（HEAD 200，249,972,384 字节，未下载）。
- 本地位置：`installers/payload/Claude.msix`，解包到 `installers/payload/msix/`，asar 解包到 `installers/payload/asar/`。

## 3. 载荷清单

### 3.1 MSIX 结构（`AppxManifest.xml`）
- Identity `Name="Claude"`，x64，`Version="4.4212.0.0"`，Publisher `CN="Anthropic, PBC", … SERIALNUMBER=4860621 …`。
- 安装位置：MSIX → `C:\Program Files\WindowsApps\Claude_4.4212.0.0_x64__<publisherId>\`（标准 MSIX 行为，publisherId 未计算，**待验证**）。Squirrel 回退版在 `%LOCALAPPDATA%\AnthropicClaude`（按 Squirrel 惯例推断，**待验证**）。
- Capabilities：`runFullTrust`、`localSystemServices`、`packagedServices`、`unvirtualizedResources`、`internetClient`。
- `desktop6:RegistryWriteVirtualization=disabled`，并把以下 HKCU 键排除出虚拟化（直接写真实注册表）：
  - `HKCU\SOFTWARE\{Google\Chrome, Microsoft\Edge, BraveSoftware\Brave-Browser, Chromium, Vivaldi, ArcBrowser\Arc, Opera Software\Opera Stable, Island\Island, Palo Alto Networks\PrismaAccessBrowser}\NativeMessagingHosts\com.anthropic.claude_browser_extension`
  - `HKCU\SOFTWARE\Google\Chrome\Extensions\fcoeoabgfenejglbffodgkkbkcdhcgfn`、`…\dngcpimnedloihjnnfngkgjoidhnaolf`
  - `HKCU\SOFTWARE\Microsoft\Office\16.0\WEF\TrustedCatalogs`
- 文件写虚拟化排除目录：`%LOCALAPPDATA%\Microsoft\Office\16.0\WEF`、`%LOCALAPPDATA%\Claude-3p`、`%LOCALAPPDATA%\Claude-Data`、`%LOCALAPPDATA%\Claude\logs`。
- 扩展：开机启动任务 `ClaudeStartup`（`--startup`，默认禁用）；协议 `claude://`；执行别名 `claude-desktop.exe`、`claude-ssh-askpass.exe`、`claude-ssh-proxy.exe`；**Windows 服务 `CoworkVMService`**（`app\resources\cowork-svc.exe`，`localSystem`，auto，由命名管道 `\pipe\cowork-vm-service` 相关 ETW 触发 / 应用自行拉起）；`claude-make-data-folder.exe`（mediumIL）。

### 3.2 文件布局（`app\`）
- Electron 外壳：`claude.exe`、`resources.pak`、`icudtl.dat`、`v8_context_snapshot.bin`、`ffmpeg.dll`、`d3dcompiler_47.dll`、`dxcompiler.dll`/`dxil.dll`、`vk_swiftshader*`、`vulkan-1.dll`、`locales\`。
- `resources\`：`app.asar`（主程序，`@ant/desktop` 4.4212.0，入口 `.vite/build/index.pre.js`）、`ion-dist.asar`、`smol-bin.x64.vhdx`（36 MB）、`cowork-svc.exe`、`chrome-native-host.exe`、`claude-ssh-{askpass,proxy,broker}.exe`、`claude-make-data-folder.exe`。
- `app.asar.unpacked\`：原生模块 `@ant/claude-native/claude-native-binding.node`、`node-pty`（`conpty.node`）、`resources\github-mcp\github-mcp-server.exe`、`resources\office365-mcp\msalruntime.dll` 等。
- `package.json` 依赖：`@ant/claude-for-chrome-mcp`、`@ant/claude-native`、`@ant/claude-swift`、`@ant/computer-use-mcp`、`@ant/imagine-server`、`@loc/electron-window`、`https-proxy-agent`、`ws`。

### 3.3 Cowork / VM 组件（对容器方案影响很大）
- `cowork-svc.exe`（Go，源路径 `C:/a/apps/apps/packages/desktop/cowork-win32-service/vm/hcs.go`）：通过 **HCS（`computecore.dll`，`HcsCreate…`/`HcsTerminateComputeSystem`）+ HCN（`computenetwork.dll`）** 启动一个轻量 Linux VM（`Boot mode: LinuxKernelDirect (x64)`），用 HVSock/vsock RPC、Plan9 共享（`Guest mounted %d/%d fixed-drive Plan9 shares`）、内置 gVisor 风格 netstack（`pkg/tcpip`）。
- `smol-bin.x64.vhdx` 卷标 `SDK_DAEMON`，内含 `sdk-daemon`、`sandbox-helper`、`cli-wrapper`、`claude-shim`、`srt-settings.json`。
- 即：**Claude Desktop 自己就带一个基于 Hyper-V 计算服务的 VM 沙箱（Cowork）**。Windows 11 家庭版是否能用 HCS（家庭版可开“虚拟机平台”给 WSL2 用）——**待验证**。该服务以 LocalSystem 运行，自建用户态容器内无法“装进去”，需单独处理（见 §5）。

### 3.4 数据目录 / 标识
- userData：Electron 默认 `%APPDATA%\Claude`（代码中 `join(app.getPath("appData"),"Claude")`）；在 MSIX 下会被移出包虚拟化目录到 `%LOCALAPPDATA%\Claude-Data`（manifest 注释 + `fX="-Data"`、`mX()` 拼接 `${LOCALAPPDATA}\${name}-Data`）；三方模式 `%LOCALAPPDATA%\Claude-3p`；日志 `%LOCALAPPDATA%\Claude\logs`。具体哪种安装落哪里——**待验证**（需在 Windows 实机看）。
- 配置：`claude_desktop_config.json`（MCP 配置；Windows 下 `zu()` 返回 `%LOCALAPPDATA%\…`，或 `CLAUDE_USER_DATA_DIR`）。
- **安装 ID**：`userData\ant-did`，首次启动写入随机 `randomUUID()`（base64，权限 0600，拒绝硬链接）。这是主要的“设备 ID”——容器每个身份应使用独立 userData，ID 自然不同。
- `CLAUDE_USER_DATA_DIR` 环境变量：代码中有，但日志串明确 `CLAUDE_USER_DATA_DIR is set and signed builds ignore it`——**签名正式版不能靠这个变量重定向**，只能靠文件系统层重定向。
- 策略注册表：`HKLM\SOFTWARE\Policies\ClaudeCode`、`HKCU\SOFTWARE\Policies\ClaudeCode`（企业配置；参见 support 文章 12622667）。另会写 HKCU 下 “Open with” shell 项、Chrome 扩展 `update_url` 键。

### 3.5 网络端点（代码内“出站端点注册表”，含开关）
代码 `index.chunk-2SHPxESV.js` 中有一张带 `toggleKey` 的端点表（似乎用于企业防火墙/CSP 说明）：

| 组 | 开关 | 主机 / 路径 |
|---|---|---|
| updater | `disableAutoUpdates` | `api.anthropic.com` / `releases.claude.com` `/api/desktop/...` |
| sentry | `disableEssentialTelemetry` | `*.sentry.io`、`sentry.io`、`o1158394.ingest.us.sentry.io` |
| datadog-rum | `disableEssentialTelemetry` | `<anthropicHost>/api/v2/rum`、`browser-intake-*datadoghq.com/.eu`、`browser-intake-ddog-gov.com` |
| anthropic-telemetry | `disableNonessentialTelemetry` | `<anthropicHost>/api/event_logging/`（渲染进程）、`api.anthropic.com/api/event_logging/`（Claude Code 子进程） |
| connector-favicons | `disableNonessentialServices` | `www.google.com/s2/favicons`、`*.gstatic.com/faviconV2` |
| artifact/mcp-app sandbox | `disableNonessentialServices` | `*.claudemcpcontent.com`、`sandbox.claudemcpcontent.com` |
| spellcheck-dictionary | `disableNonessentialServices` | `redirector.gvt1.com`、`*.gvt1.com` |

其它出现的域：`claude.ai`、`claude.com`、`assets.claude.ai`、`preview.claude.ai/.com`、`code.claude.com`、`platform.claude.com`、`downloads.claude.ai`、`*.hcaptcha.com`、`accounts.google.com`/`oauth2.googleapis.com`、`login.microsoftonline.com`、`microsoft365.mcp.claude.com`、各类 MCP（`mcp.notion.com` 等，用户启用才连）、`cdn.jsdelivr.net`/`cdnjs.cloudflare.com`/`esm.sh`/`unpkg.com`（artifact 依赖）。
- 主进程 Sentry DSN：`https://2f98127c…@o1158394.ingest.us.sentry.io/4507368973008896`。
- 未发现 Segment、Statsig 服务端域（`statsig`/`segment` 字串命中为无关代码）。
- 主进程使用 `https-proxy-agent`，代码中有 `CLAUDE_GATEWAY_*`、`CLAUDE_CODE_USE_GATEWAY` 等变量（具体语义**待验证**）。

### 3.6 自动更新
- Electron `autoUpdater.setFeedURL({url, serverType:"json"?})`，URL 形如 `${base}/api/desktop/${platform}/${arch}/${type}/update?...&channelUuid=`，base 为 `oX.updateApiBase` 或 `releases.claude.com` / `api.anthropic.com`。通道：`stable` / `latest` / `staggered`。
- 可由策略 `disableAutoUpdates` 关闭（写在哪个注册表值名下——**待验证**）。

### 3.7 环境 / 指纹 API（代码检索命中）
- `os.hostname()`：Sentry `serverName`（`SENTRY_NAME || hostname()`）；某同意记录文件里写 `hostname`；UNC 路径判断。
- `os.networkInterfaces()`：远程/在线检测与调试信息（外部 IPv4 网卡数量）。
- `os.userInfo()`（28 处）、`os.totalmem`、`process.getSystemMemoryInfo`、`getSystemVersion`、`app.getLocale`、`Intl.DateTimeFormat`（时区）。
- `deviceId` / `device_id` 大量出现（多为 Chrome 配对、遥测上下文）。
- **未发现** `node-machine-id`、`MachineGuid`、`systeminformation`。
- 有杀软探测标志表（`av_avast_avg`、`av_norton`、`av_mcafee`、`av_kaspers…`），用途**待验证**（疑似诊断）。
- 原生模块（`claude-native-binding.node`）会读注册表 (`readRegistryValues`) 等系统信息，具体导出未完整枚举——**待验证**。

### 3.8 Computer use
- `@ant/computer-use-mcp` + `claude-native-binding.node`：`SendInput`、`GetCursorPos`、`SetWindowsHookExW`（键鼠钩子）、UIAutomation、`cuListDisplays`/`cuHideApps`；截图用 Electron `desktopCapturer.getSources({types:["screen"]})` 与 `screen.getAllDisplays()`。
- 结论：Computer use 需要**真实交互式桌面会话**（同一 Window Station/Desktop）、能对其它窗口注入输入与截屏；AppContainer/低完整性进程会被 UIPI 阻止向高完整性窗口发送输入（**待验证**具体影响）。

## 4. 子进程 / 辅助组件小结
`claude.exe`（Electron 主/渲染/GPU/utility 多进程）、`pty-host`（node-pty/ConPTY）、各 worker、`chrome-native-host.exe`（浏览器扩展原生消息）、`claude-ssh-*`、`github-mcp-server.exe`、office365-mcp（MSAL）、`cowork-svc.exe`（LocalSystem 服务 → HCS VM）、崩溃转储（Electron crashpad，`crashDumps` 路径被重设）。

## 5. 如何封装进自建专用容器（建议）

1. **安装形态**：MSIX 需要包注册与 WindowsApps，且带系统服务，不适合直接“搬进”用户态容器。建议：
   - 方案 A（推荐先试）：从 MSIX 解出 `app\` 目录作为**免安装便携目录**放进容器根（如 `E:\AI\ClaudeBox\rootfs\app`），由容器启动器直接起 `claude.exe`。是否能在非包上下文正常运行（包身份 API、`devirtualizeMsixPath` 等）——**待验证**。
   - 方案 B：使用官方 Squirrel `.exe` 版本（`Claude-<hash>.exe`），它装到 `%LOCALAPPDATA%`，更容易被文件重定向——**待验证**。
2. **目录重定向**（容器内每个身份一套）：`%APPDATA%\Claude`、`%LOCALAPPDATA%\Claude`、`Claude-Data`、`Claude-3p`、`%LOCALAPPDATA%\AnthropicClaude`、`%TEMP%`、`%USERPROFILE%\.claude`（Claude Code 配置，`CLAUDE_CONFIG_DIR` 相关）、Office WEF 目录。`ant-did` 随之隔离。由于签名版忽略 `CLAUDE_USER_DATA_DIR`，需在**进程环境变量（APPDATA/LOCALAPPDATA/USERPROFILE/TEMP 指向容器内路径）+ 文件系统过滤/ACL** 层做，或直接用专用低权限本地账户的配置文件目录。
3. **注册表虚拟化**：HKCU 下 `SOFTWARE\Policies\ClaudeCode`、各浏览器 `NativeMessagingHosts\com.anthropic.claude_browser_extension`、Chrome `Extensions\fcoeo…`/`dngcp…`、`Office\16.0\WEF\TrustedCatalogs`、`Software\Classes\claude`（协议）、“Open with” 项、`Run`（开机启动）。用专用账户可天然隔离 HKCU；HKLM 策略可用来**主动下发**：关闭自动更新与非必要遥测。
4. **伪装/统一的环境 API**：主机名（Sentry serverName）、用户名（`os.userInfo`）、时区/区域、内存/系统版本、网卡列表。用户态容器很难伪装 `hostname`/系统版本（需 API hook，如 Detours 注入 `GetComputerNameExW` 等）——**待验证**可行性与稳定性；最低限度可设 `SENTRY_NAME` 环境变量覆盖 Sentry 上报的主机名（代码 `global.process.env.SENTRY_NAME||y.hostname()`）。
5. **网络**：容器内进程只允许连本地网关（Windows 防火墙按程序路径规则 + 系统代理/`HTTPS_PROXY`，主进程用 `https-proxy-agent`，Chromium 层可加 `--proxy-server`——是否被应用尊重**待验证**）。网关策略：
   - 必须放行：`claude.ai`、`*.claude.ai`、`claude.com`、`api.anthropic.com`、`assets.claude.ai`、`*.hcaptcha.com`（登录）、OAuth 域（按需）。
   - 建议拦截：`*.sentry.io`、`*.ingest.us.sentry.io`、`browser-intake-*datadoghq.*`、`<anthropic>/api/v2/rum`、`/api/event_logging/`（需 TLS 解密或按路径——SNI 层无法区分同域路径，只能用策略开关关闭）、`*.gvt1.com`、`www.google.com/s2/favicons`。
   - 优先使用应用自带开关 `disableEssentialTelemetry` / `disableNonessentialTelemetry` / `disableNonessentialServices` / `disableAutoUpdates`（下发方式**待验证**，参考 Anthropic 企业配置文档）。
6. **更新通道**：关闭 autoUpdater（策略），屏蔽 `releases.claude.com`、`/api/desktop/*/update`、`downloads.claude.ai`；由容器管理工具手动拉取 `.latest` → 校验 sha256 → 替换 `app\` 目录（版本固定、可回滚）。
7. **Cowork VM 服务**：`CoworkVMService` 以 LocalSystem + HCS 运行，越出容器边界；建议容器模式下**不注册该服务、禁用 Cowork 功能**；若需要，需单独评估 HCS 在家庭版可用性及其网络（自带 netstack/HCN）是否会绕过网关——**待验证，高风险**。
8. **Computer use**：需要交互式桌面。用户态容器下进程与宿主共享桌面，可以操作宿主机其它窗口——这与隔离目标冲突。可选：容器使用独立 Desktop 对象（`CreateDesktop`）或独立账户的 RDP/会话，但 `desktopCapturer`/UIAutomation 在非活动桌面能否工作——**待验证**。
9. **其它辅助进程**：禁止或隔离 `chrome-native-host.exe`（会写宿主浏览器的 NativeMessaging 键，连通宿主浏览器）、`claude-ssh-*`；Office 插件目录不要映射。

## 6. 后续验证清单
- [ ] Windows 实机：从 MSIX 解出的 `app\claude.exe` 能否在非包上下文启动并登录。
- [ ] 实测 userData 实际路径、`ant-did` 位置。
- [ ] 企业策略注册表值名与四个 disable 开关的效果（抓网关日志对比）。
- [ ] 家庭版 HCS / Cowork 可用性。
- [ ] `--proxy-server` / `HTTPS_PROXY` 是否覆盖全部出站。
- [ ] 安装 Ghidra 后用 rea 分析 `Claude_Setup.exe`、`cowork-svc.exe`、`claude-native-binding.node`。
