# Windows Claude Desktop 安装包与载荷分析（用于自建专用容器）

分析日期：2026-10-31（JST）。分析环境：Linux 工作机（box），**仅静态分析，未在 Windows 上运行**。
凡未在 Windows 实机验证的结论标注 **待验证**。安装包与载荷均未提交入库（`installers/` 已被 `.git/info/exclude` 忽略）。

## 0. 工具与 rea 情况

- [morluto/rea](https://github.com/morluto/rea)（npm 包 `rea-agents`，本次版本 rea@6.8.0）：“Reverse Engineer Anything”，一个把逆向能力以 MCP / CLI 暴露给 AI 代理的工具集。原生二进制分析需要外部引擎（Ghidra 12.1.x / Hopper / IDA / Kuna）；JS/Electron 静态分析内置。
- 在 Linux 上 `npm i -g rea-agents` 安装成功（Node 24.22.1）。
  - `rea analyze Claude_Setup.exe` → 早期曾 `provider_unavailable`（当时无 Ghidra）。现已安装 Ghidra 12.2，但对 236 MB Electron 外壳做全量 rea/Ghidra 反编译不现实；本次用 Ghidra headless **轻量导入**（见 §7）。
  - `rea analyze-javascript-application <asar 目录>` 运行 ~5 分钟后被超时取消，且 `fileIndexWorker.js`、`index.chunk-2SHPxESV.js`（6.8 MB）超出其 1 GiB V8 堆上限，只拿到部分观测。**JS/网络端点结论主要来自 7z / strings / @electron/asar / Python 正则检索**。
- 其它工具：`7z`、`strings`、`npx @electron/asar`、Python `pefile`、**Ghidra 12.2**（见下）。
- **Ghidra 安装位置（供后续复用）**：
  - 本体：`/home/box/tools/ghidra` → `ghidra_12.2_PUBLIC`
  - JDK：`/home/box/tools/jdk-25.0.5+8`（Temurin 25）
  - 环境：`export JAVA_HOME=/home/box/tools/jdk-25.0.5+8; export PATH="$JAVA_HOME/bin:$PATH"`
  - 工程目录：`/home/box/tools/ghidra-projects/claude-box/`（含 `ClaudeExe`、`ClaudeNative` 项目与 `dumps/`）
  - 轻量脚本：`/home/box/tools/ghidra-scripts/DumpClaudeLight.java`（只抽 PE 元数据/导入/导出/关键词串，**不做全量反编译**）
  - 用户扩展：已解压 Jython 到 `~/.config/ghidra/ghidra_12.2_PUBLIC/Extensions/Jython`

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
- [x] 安装 Ghidra 12.2 + JDK 25；对 `claude.exe` / `claude-native-binding.node` 做 headless 轻量分析（§7）。
- [x] 用 Ghidra headless 轻量分析 `cowork-svc.exe` / `claude-ssh-proxy.exe` / `claude-make-data-folder.exe`（§8；**未**全量反编译）。
- [ ] 用 Ghidra/rea 深入 `Claude_Setup.exe`（仍未做）。
- [ ] 在 Windows 上确认 native 模块 NAPI 名与 Computer use 实际调用链。

## 7. Ghidra 对 `claude.exe` 的分析

分析时间：2026-10-31 约 18:17–18:22（Asia/Tokyo）。工具：Ghidra 12.2 headless + 自写脚本 `DumpClaudeLight.java`；辅以 `pefile` / `strings`（因 236 MB 的 `.text`/`.rdata` 不宜在 Ghidra 内全量扫串）。**未对整二进制做反编译**（会打满约 15 GiB 内存）；命令使用 `-noanalysis` 仅导入 PE 结构后跑脚本。原始 dumps 在 `/home/box/tools/ghidra-projects/claude-box/dumps/`（不入库）。

### 7.1 结论摘要：是不是「原版 Electron 外壳」？

**基本是库存 Electron/Chromium 外壳，再贴 Anthropic 版本资源。** 证据：

| 证据 | 值 |
|---|---|
| 版本资源（PE Version Info） | `CompanyName=Anthropic`，`ProductName/FileDescription=Claude`，`FileVersion/ProductVersion=4.4212.0`，`OriginalFilename=claude.exe`，另有 `SquirrelAwareVersion=1` |
| 内嵌 UA / 版本串（`strings`） | `Chrome/152.0.7977.167 Electron/44.9.3`；模板 `%s/%s Chrome/%s Electron/44.9.3` |
| Ghidra Language / CompilerSpec | `x86:LE:64:default` / **`clangwindows`**（Chromium/Electron 常见 clang 工具链） |
| 映像体量 | 文件约 236 MB；`SizeOfImage≈248 MB`；`.text` 约 **183.9 MB**（典型 Chromium 主进程壳） |
| COFF TimeDateStamp | `1792540522` → **2026-10-20 23:55:22 UTC**（Asia/Tokyo：**2026-10-21 08:55:22**） |
| 导出符号 | Ghidra 见 **2524** 个 external entry；大量 `v8::` / `node::` 装饰名，以及 Electron fuse 查询如 `electron::fuses::IsRunAsNodeEnabled`、`IsEmbeddedAsarIntegrityValidationEnabled`、`IsCookieEncryptionEnabled` 等 |
| 源路径串（`strings`） | 大量 `..\..\electron\shell\browser\api\electron_api_*.cc`、`third_party\electron_node\...` |

未发现把业务逻辑编进 `claude.exe` 本体的迹象：应用代码在 `resources/app.asar`（此前 §3）。**待验证**：是否相对上游 Electron 44.9.3 打了私有补丁（需对照官方 Electron 同版本二进制哈希/导出集合；本次未做）。

### 7.2 导入表要点（Ghidra；含 delay-load）

- Ghidra 统计：**59** 个外部 DLL、**1284** 条导入（含 delay-load）。`pefile` 仅解析到 7 个「立即」导入 DLL（KERNEL32/WS2_32/WINMM/CRYPT32/ntdll/VERSION/DWrite），其余多为 **DELAY_IMPORT**（USER32、WINHTTP、ADVAPI32、GDI32、ffmpeg 等）——封装时不要只看非 delay 表。
- **无 WFP / Fwpm\*** 导入 → 出站过滤不能指望 Claude 自己；需容器侧 WFP/防火墙。
- **有** `USERENV.DLL!DeriveAppContainerSidFromAppContainerName`（仅此相关符号；未见 `CreateAppContainerProfile` 等）——**待验证**运行时是否真的创建 AppContainer。
- Job：`CreateJobObjectW` / `AssignProcessToJobObject` / `TerminateJobObject`（Electron/Chromium 子进程树常见）。
- 进程：`CreateProcessW`、`CreateProcessAsUserW`、`CreateRemoteThread`、`OpenProcess`、`WriteProcessMemory`、`VirtualAlloc(Ex)`、`Process32FirstW/NextW`。
- 网络：`WINHTTP.DLL` 全套（`WinHttpOpen/Connect/OpenRequest/SendRequest/...`）+ **`WinHttpGetIEProxyConfigForCurrentUser` / `WinHttpGetProxyForUrl`** → 会读系统/IE 代理，利于走容器网关；另有 WS2_32。
- 凭据/DPAPI：`CryptProtectData` / `CryptUnprotectData`、NCrypt 系列、大量 Cert\*。
- 输入/剪贴板（壳层也有，不只 native）：`SendInput`、`SetWindowsHookExW`、`GetAsyncKeyState`、剪贴板 API、`BitBlt`/`GetDC` 等。

### 7.3 字符串关键词（离线 `strings`，因 Ghidra 跳过了 >40 MB 的 `.text`/`.rdata`）

命中：`Electron/44.9.3`、`Chrome/152.0.7977.167`、大量 `ELECTRON_*` 环境变量名（含 `ELECTRON_RUN_AS_NODE`、`ELECTRON_DISABLE_SANDBOX`、`ELECTRON_DEBUG_MSIX_UPDATER`、`ELECTRON_FORCE_IS_PACKAGED` 等）。**未在外壳串中 prioritise 到** `api.anthropic.com` / Sentry DSN（那些在 asar/JS，见 §3.5）。`AppContainer` 字面串未作为独立业务逻辑突出出现（仅有上述 USERENV 导入）。

### 7.4 附：`claude-native-binding.node`（Ghidra + pefile）

路径：`resources/app.asar.unpacked/node_modules/@ant/claude-native/claude-native-binding.node`（约 4.0 MB）。

| 项 | 值 |
|---|---|
| 格式 | PE DLL（Node N-API 原生模块） |
| 语言线索 | **Rust**（`napi` 2.16 / `rustc/...` 路径；Ghidra 应用了 `rust-common` 数据类型库） |
| 时间戳 | `1793254172` → **2026-10-29 06:09:32 UTC**（Asia/Tokyo：**15:09:32**） |
| 导出名 | 仅 **`napi_register_module_v1`**（+ PE entry）；业务函数在 `napi_register_module_v1` 里动态注册 |
| Ghidra | 导入 275 / 22 DLL；`-noanalysis` 下仍见已有函数约 1.3 万（先前一次完整分析残留）；关键词串命中 `cu_*` / `SendInput` / `CreateJobObjectW` 等 |

**Computer use 相关 NAPI 名**（来自 `Failed to register function \`...\`` 字符串，静态证据）：  
`cu_app_under_point`、`cu_control_at_point`、`cu_display_for_pid`、`cu_excluded_window_rects`、`cu_focused_control`、`cu_get_app_icon`、`cu_get_own_bundle_id`、`cu_hide_apps`、`cu_unhide_apps`、`cu_is_remote_session`、`cu_list_displays`、`cu_list_installed_apps`、`cu_list_open_windows`、`cu_list_running_apps`、`cu_masked_window_rects`、`cu_windows_directory`、`mouse_button`、`mouse_location`、`mouse_scroll`、`move_mouse`、`type_text`、`type_text_paced`、`validate_keys`、`focus_window`、`encode_frame_jpeg` 等。

对应 Win32 导入：`SendInput`、`GetAsyncKeyState`、`SetWindowsHookExW`/`SetWinEventHook`、`EnumWindows`、`GetCursorPos`、`GetDC`、`GetForegroundWindow`、`MapVirtualKeyExW`、`VkKeyScanExW`；另有 `CreateJobObjectW`/`AssignProcessToJobObject`/`TerminateJobObject`、`CryptProtectData`/`NCrypt*`。

**同模块还注册了大量非 CU API**（同样来自注册失败字符串，**待验证**调用面）：进程/Job（`create_process_job`、`join_app_job`、`is_process_in_app_job`…）、HCS（`get_hcs_status`）、MSIX 包（`get_current_package_family_name`、`switch_to_package`…）、注册表、Windows Hello、DPAPI、命名管道、录屏/VoiceOver（`watch_recorder_*`、`watch_voiceover_*`）等。说明 native 层不止 Computer use，容器方案需一并考虑这些能力是否越权。

### 7.5 对容器方案的直接影响

1. `claude.exe` ≈ Electron 44.9.3 + Chrome 152 壳 → 隔离目标是**进程树 + 数据目录 + 网络**，不是「逆向改造一个专用二进制」。
2. 代理：外壳具备 WinHTTP 代理发现 API；与 §5 的系统代理 / `--proxy-server` 路线一致（**待验证** Chromium 网栈是否全部走代理）。
3. Computer use / 键鼠注入在 **native `.node`**，不在壳的业务逻辑里；关掉 Computer use 时仍可能加载该模块（**待验证**）。
4. 全量 Ghidra 反编译 `claude.exe` 性价比极低；后续优先 asar/JS、native `.node`、`cowork-svc.exe`。

### 7.6 本次失败 / 限制（如实记录）

- 首次脚本用 Jython：Ghidra 12.2 默认未启用 Jython → 已解压官方 `Jython` 扩展；最终改用 Java 脚本成功。
- `claude.exe` 的 Ghidra 串扫描跳过了 `.text`（≈183 MB）与 `.rdata`（≈43 MB），Electron 版本串来自主机 `strings`，不是 Ghidra 内存扫描。
- 未分析 `Claude_Setup.exe` / `cowork-svc.exe` 的 Ghidra 工程（时间盒内优先主程序 + native）。
- 未在 Windows 上动态验证任何导入的实际调用。

## 8. 后台进程深挖（2026-10-31，Asia/Tokyo）

静态分析范围：`AppxManifest.xml`、`installers/payload/asar/.vite/build/*.js`、`app/resources/*.exe`、`app.asar.unpacked`；Ghidra 12.2 headless + `DumpClaudeLight.java`（`-noanalysis`，**未**对 Electron 主程序或 helper 做全量反编译）。**未在 Windows 实机跑过 Claude**——动态进程树、实际是否 StartService、代理是否覆盖 helper，一律标 **待验证**。

### 8.1 Manifest 登记面（Application / 启动 / 服务 / 协议 / 别名 / 防火墙）

来源：`installers/payload/msix/AppxManifest.xml`。

| Id / 名称 | Executable | 类型 | 要点 |
|---|---|---|---|
| `Claude` | `app\Claude.exe` | Application（FullTrust） | 主 UI；`internetClient` + 包级出站 TCP 防火墙规则 |
| `ClaudeStartup` | 同上 + `--startup` | `windows.startupTask` | **Enabled="false"**（默认不开机自启） |
| 协议 `claude` | 主应用 | `windows.protocol` | `claude://` deep link |
| 别名 `claude-desktop.exe` | 主应用 | `appExecutionAlias` | |
| **`CoworkVMService`** | `app\resources\cowork-svc.exe` | **`windows.service`**，`StartupType=auto`，**`StartAccount=localSystem`** | ETW 自定义触发 Subtype `1f81d131-3e60-4c76-9860-37334e4ffce3`，StringData `\pipe\cowork-vm-service`；注释写明 **pipe 活动不会触发**，**应用自行把已停止服务拉起** |
| `SshAskpass` | `app\resources\claude-ssh-askpass.exe` | Application，`AppListEntry=none` | 别名同名 |
| `SshProxy` | `app\resources\claude-ssh-proxy.exe` | 同上 | 别名同名 |
| `MakeDataFolder` | `app\resources\claude-make-data-folder.exe` | `win32App`，**`TrustLevel=mediumIL`** | 别名同名；专门绕过包虚拟化建数据目录 |
| （包扩展） | `Claude.exe` / `cowork-svc.exe` | `windows.firewallRules` | 各一条 **出站 TCP、Profile=all** |

Capabilities：`runFullTrust`、`localSystemServices`、`packagedServices`、`unvirtualizedResources`、`internetClient`。

### 8.2 Helper / 附属二进制清单

路径相对 `installers/payload/msix/app/`。

| 文件 | 约大小 | PE / 语言（证据） | 角色（静态） |
|---|---:|---|---|
| `resources/cowork-svc.exe` | 13.1 MB | Go（Ghidra `CompilerSpec=golang`；包 `github.com/anthropics/cowork-win32-service`） | Windows 服务：HCS 起 LinuxKernelDirect VM、Plan9 共享盘、HVSock/vsock RPC、gvisor-tap-vsock 用户态网络、guest HTTP 代理刷新 |
| `resources/smol-bin.x64.vhdx` | 36.0 MB | VHDX | Cowork 访客根盘（`SDK_DAEMON`）；主进程启动时复制为会话内 `smol-bin.vhdx` |
| `resources/claude-ssh-proxy.exe` | 1.9 MB | Go（`anthropics/ssh-askpass/cmd/claude-ssh-proxy`） | SSH 侧通道：把命令放进 Job；读 `SSH_ASKPASS*` / `CLAUDE_SSH_ASKPASS_*` |
| `resources/claude-ssh-askpass.exe` | 2.5 MB | Go（`…/cmd/claude-ssh-askpass`） | SSH 口令/密钥提示；经 broker/relay 命名管道与桌面通信 |
| `resources/claude-ssh-broker.exe` | 2.5 MB | Go（`…/cmd/claude-ssh-broker`） | 命名管道监听（`ListenPipe` / `makeServerPipe`）；协调 askpass |
| `resources/claude-make-data-folder.exe` | 1.5 MB | Go（`anthropics/win32-data-folder`）；Subsystem=2 GUI | mediumIL 下在真实 `%LOCALAPPDATA%` 建 `Claude-Data` 类目录（避开 MSIX 写虚拟化） |
| `resources/chrome-native-host.exe` | 1.1 MB | Rust（tokio/mio named_pipe；`chrome-native-host0.1.0`） | Chrome/Edge 等 Native Messaging ↔ 管道 `\\.\pipe\claude-mcp-browser-bridge-*` |
| `resources/app.asar.unpacked/resources/github-mcp/github-mcp-server.exe` | 24.5 MB | Go（`github.com/github/github-mcp-server`） | 内置 GitHub MCP（stdio）；可由 `CLAUDE_DESKTOP_GITHUB_MCP_BINARY` 覆盖路径 |
| `…/office365-mcp/msalruntime.dll` + `msal-node-runtime.node` | 3.3+0.4 MB | MSAL 原生 | Office365 MCP / WAM broker；JS 侧另 spawn `office365-mcp-stdio.mjs` |
| `…/node_modules/@ant/claude-native/claude-native-binding.node` | 4.0 MB | Rust N-API | Computer use、**`startWindowsService` / `queryWindowsServiceState`**、HCS 状态、Job 等（见 §7.4） |
| `…/node-pty/.../conpty*.node` | ~0.5 MB | 原生 | 终端 PTY（经 `pty-host` UtilityProcess） |

Electron **同树 UtilityProcess / Worker**（非独立 exe，仍是 `claude.exe` 子进程）：`.vite/build/` 下  
`pty-host`、`mcp-runtime`（含 `directMcpHost.js`）、`msal-broker-worker`、`file-index-worker`、`heavy-work-worker`、`shell-path-worker`、`frame-local-worker`、`transcript-search-worker`、`stall-sampler-worker`。主进程用 `utilityProcess.fork(...)`（证据：`index.chunk-2SHPxESV.js` `@1437084`、`@2769053`）。

### 8.3 命名管道 / IPC / 网络（静态串）

**Cowork（`cowork-svc.exe`）**
- `\\.\pipe\cowork-vm-service`（主控；JS：`OJn="\\\\.\\pipe\\cowork-vm-service"`，`index.chunk-2SHPxESV.js` `@3102049`）
- `\\.\pipe\cowork-vm-service-store`
- `\\.\pipe\cowork-vm-console-…` / `cowork-daemon-console` / `cowork-daemon-console-store`
- 内部名线索：`cowork-vm`、`cowork-vm-nat`、`cowork-vm-vnet`、`cowork-vm-store`
- HCS：动态加载 `computecore.dll` / `computenetwork.dll`；串含 `HcsCreateComputeSystem`、`HcsStart/Terminate/Modify…`、`HcnEnumerateNetworks`、`Boot mode: LinuxKernelDirect (x64)`、`Guest mounted %d/%d fixed-drive Plan9 shares`、`Plan9 share via HcsModifyComputeSystem`
- 网络：`gvisor-tap-vsock`、`win-httpproxy`（`DetectAutoProxyConfigURL`）、向 guest 下发 `httpProxy`/`httpsProxy`/`noProxy`；**有自己的用户态转发，可能不走宿主进程代理**——**待验证**是否绕过 cbox 网关
- 服务：`CoworkVMService` / `CoworkVMServiceStore`；`OpenSCManager` / `StartService` / `StartServiceCtrlDispatcher`

**Chrome native host**
- `\\.\pipe\claude-mcp-browser-bridge-…`；日志 `%LOCALAPPDATA%\Claude\logs\chrome-native-host.log`

**SSH helpers**
- 环境变量：`SSH_ASKPASS`、`SSH_ASKPASS_PROMPT`、`SSH_ASKPASS_REQUIRE`、`CLAUDE_SSH_ASKPASS_ASKER` / `_RELAY` / `_BROKER`；桌面侧另有 `CLAUDE_DESKTOP_SSH_ASKPASS_HELPER`
- Broker：`ListenPipe`；Proxy：把目标命令放进 **Job Object**（Ghidra 串 `cannot place the command in its job`）
- 出站：**SSH 直连远端主机**，不经过 Claude 的 HTTP 代理栈——**待验证**；cbox WFP 需按 exe 路径单独管

**GitHub MCP**
- 路径解析：`resourcesPath/app.asar.unpacked/resources/github-mcp/github-mcp-server.exe`（`index.chunk-Be7fdDOi.js`）；出站 `api.github.com`（stdio 宿主进程代发或自连——**待验证**）

### 8.4 主进程如何拉起（代码证据）

1. **CoworkVMService（越权面最大）**  
   - Native：`claude-native-binding` 暴露 `queryWindowsServiceState` / `startWindowsService`（JS：`Di()?.startWindowsService(xM)`，`xM="CoworkVMService"`）。  
   - 流程（`index.chunk-2SHPxESV.js` ~`@3094757+`）：查 SCM 状态 → 若 stopped 则 `StartService` → 轮询直到 running → 再 `connect` 到 `\\.\pipe\cowork-vm-service`。  
   - VM 启动：复制 `process.resourcesPath/smol-bin.<arch>.vhdx` → 用户数据下 `smol-bin.vhdx`，再 `g.configure()`（Windows VM service）。  
   - 权限：**LocalSystem 服务**，不在用户 Job 内；可 HCS 建 VM、Plan9 挂宿主盘符、自带 netstack。  
   - 持久化：包注册时服务 `StartupType=auto`；即使服务停着，**用户态主进程也能再 Start**。

2. **claude-make-data-folder.exe**  
   - Manifest `mediumIL` + JS 常量 `_Ue="claude-make-data-folder"` 与退出码表（`made`/`already_there`/`refused`/…）。  
   - 用途：在**真实** LocalAppData 建数据目录（配合 §3.1 写虚拟化排除）。具体 `spawn`/`shell:AppsFolder\…MakeDataFolder` 调用点在压缩包内较散——**待验证**实机调用链；便携解包模式下可能根本用不到。

3. **SSH 三角**  
   - 从 `resourcesPath` 找 `claude-ssh-askpass.exe`（`index.chunk-Cj24FzSL.js`）；失败文案要求放行 askpass/broker/proxy（`index.chunk-UlAM2G-q.js`）。  
   - 用户态、随 SSH 功能启停；Broker 管道 IPC。

4. **chrome-native-host.exe**  
   - 注册 `HKCU\…\NativeMessagingHosts\com.anthropic.claude_browser_extension`；浏览器拉起 host，host 再连 Claude 的 named pipe。  
   - **会写宿主浏览器注册表**（manifest 已 ExcludedKey）。

5. **MCP / UtilityProcess**  
   - `utilityProcess.fork` 拉 `mcp-runtime`、各 `*-worker`、`pty-host`；GitHub MCP 为独立 exe；Office365 为 `office365-mcp-stdio.mjs` + MSAL broker worker。  
   - 均随主进程用户身份；期望落在同一 Job（Electron 自身也用 Job——§7.2）。

6. **StartupTask / crashReporter / autoUpdater**  
   - StartupTask 默认禁用。  
   - `crashReporter.addExtraParameter`（GPU 退出码等）；crash 目录归入 `app:crash` 存储分类。  
   - autoUpdater 见 §3.6（非常驻独立 exe）。

### 8.5 预期进程树（功能开关下的静态推断，**待验证**）

```
claude.exe                          # Browser / 主进程
├─ claude.exe (GPU)
├─ claude.exe (Renderer ×N)
├─ claude.exe (Utility / Network …) # Chromium
├─ UtilityProcess: pty-host         # 终端 / Claude Code
├─ UtilityProcess: mcp-runtime / workers…
├─ [可选] github-mcp-server.exe     # MCP: GitHub
├─ [可选] node… office365-mcp-stdio # MCP: M365（或同树 utility）
├─ [可选] chrome-native-host.exe    # 浏览器扩展（常由浏览器拉起）
├─ [可选] claude-ssh-{broker,askpass,proxy}.exe
└─ （同会话 Computer use：仍在主进程 + native .node，无单独 exe）

另树 / 系统服务（**不在用户 Job 内**）：
  cowork-svc.exe  (CoworkVMService, LocalSystem)
    └─ HCS VM (LinuxKernelDirect) + Plan9 + vsock
```

Cowork **关**：应无 `cowork-svc` 活动与 VHDX 复制（若服务仍 auto 安装，空闲服务进程是否驻留——**待验证**）。  
Cowork **开**：LocalSystem 服务 + 可能挂载宿主固定盘 Plan9——**容器逃逸级**。

### 8.6 Ghidra 轻量结果（helper）

工程 dumps：`/home/box/tools/ghidra-projects/claude-box/dumps/{cowork-svc,claude-ssh-proxy,claude-make-data-folder}.exe_ghidra_*`。

| 二进制 | Language/Compiler | 导入 DLL（PE） | 备注 |
|---|---|---|---|
| `cowork-svc.exe` | x86-64 / **golang** | 表面仅 `KERNEL32`（Go 动态 `LoadLibrary*`）；Interesting=`LoadLibraryW/ExW`,`VirtualAlloc` | 真实 HCS/SCM/Winsock 经运行时解析；串扫描已覆盖 HCS/Plan9/pipe |
| `claude-ssh-proxy.exe` | golang | 同上模式 | Job + Askpass 环境变量 |
| `claude-make-data-folder.exe` | golang；Subsystem GUI | 同上 | `KnownFolderPath` / `CreateDirectory` / package family |

### 8.7 对 cbox（C0）封装的 allow / deny

| 组件 | C0 建议 | 理由 |
|---|---|---|
| `claude.exe` 树 + UtilityProcess/workers/pty-host | **ALLOW**（容器用户 + Job） | 核心 UI；出站只准本地网关（WFP 按路径） |
| `claude-native-binding.node` | ALLOW 加载，但 **阻断** `startWindowsService` 效果 | 不注册/删除 `CoworkVMService`；SCM Start 应失败或无服务 |
| **`cowork-svc.exe` / `smol-bin*.vhdx` / Cowork 功能** | **DENY / 不安装服务、不复制 VHDX、策略关 Cowork** | LocalSystem + HCS + Plan9 挂盘 + 自有 netstack = **逃逸与绕过网关** |
| `claude-make-data-folder.exe` | 便携模式下 **可不需要**；若 spawn，ALLOW 但限制只能写容器 LocalAppData | mediumIL 写真实磁盘；ACL 必须卡住宿主用户目录 |
| `claude-ssh-*` | C0 **DENY**（功能非 MVP） | SSH 直连；WFP 否则要为三件套单独开洞 |
| `chrome-native-host.exe` | C0 **DENY** | 写宿主浏览器 NativeMessaging；桥接宿主 Chrome |
| `github-mcp-server.exe` / office365-mcp | C0 **DENY**（可后续按需 ALLOW + 网关域名白名单） | 额外出站与凭据缓存 |
| StartupTask `ClaudeStartup` | 保持禁用 | 避免宿主登录自启 |
| 包防火墙规则（出站 TCP all） | **不可依赖**；cbox 自管 WFP | Manifest 规则在便携解包下不生效；正式 MSIX 下过宽 |

**WFP 要点**：规则要绑 **具体 exe 路径**（主程序 + 若将来放行的 MCP/SSH），默认拒绝出站，仅允许 → 本机网关。对 **LocalSystem 的 cowork-svc** 即使放行用户 Job 规则也管不到——必须从根上不装/不启服务。Guest 侧 Plan9 看到的是**宿主盘**，与 cbox「可见目录」模型冲突。

### 8.8 待验证

- [ ] 实机：默认安装后 `CoworkVMService` 是否已 Running；关 Cowork UI 后服务是否退出。
- [ ] 家庭版：无 Hyper-V 功能时 HCS/`CoworkVMService` Start 的错误码与 UI 表现。
- [ ] 便携解包：`startWindowsService` 是否直接 `native_unavailable`；无服务时 Cowork 入口行为。
- [ ] `claude-make-data-folder` 实际启动方式（AppsFolder vs 直接路径）及退出码。
- [ ] SSH/GitHub MCP 出站是否尊重系统/`HTTPS_PROXY`。
- [ ] UtilityProcess 子进程是否自动加入 Electron Job（以及 cbox 外层 Job 能否覆盖）。
- [ ] chrome-native-host 由浏览器拉起时，是否继承容器用户/环境。

