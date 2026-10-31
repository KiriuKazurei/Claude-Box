# 调研 v2 · 自建专用容器（Windows 11 家庭版，无 Hyper-V）

> 日期：2026-10-31。背景：用户机器为 Win11 Home，无 Hyper-V / Windows Sandbox / Docker Windows 容器。决策：放弃 Hyper-V，**自建宿主机上的专用容器**跑 Windows Claude Desktop。
> 标记「**待验证**」的条目尚未在真机上试过，不得当作结论。

## 1. 关键事实（有出处）

| # | 事实 | 出处 |
|---|---|---|
| F1 | AppContainer（旧名 LowBox）进程以 **Low IL** 运行，token 带 Package SID + Capability SID；访问权限 = 用户/组 SID 与 AppContainer SID 授权的**交集**；无 `internetClient` 等能力则无网络。 | [Launch an AppContainer](https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer) · [AppContainer for legacy apps](https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-for-legacy-applications-) |
| F2 | AppContainer 由 `CreateAppContainerProfile` + `STARTUPINFOEX` 的 `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` 启动；子进程继承容器。LPAC 通过 `PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY` 进一步去掉 ALL APPLICATION PACKAGES。 | 同上；[UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute) |
| F3 | `SendInput` 受 **UIPI** 约束：只能向同级或更低 IL 注入，失败时不报错。AppContainer 是 Low IL → **无法向普通（Medium）窗口注入输入**。 | [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput) |
| F4 | 当前 Windows 版 Claude Desktop 默认是 **MSIX 包**（`C:\Program Files\WindowsApps\Claude_<ver>_x64__pzs8sxrjxfjjc\app\Claude.exe`，PFN `Claude_pzs8sxrjxfjjc`）；`ClaudeSetup.exe` 是引导器，会安装 MSIX；旧版为 Squirrel（`%LOCALAPPDATA%\AnthropicClaude\app-x\claude.exe`）。Desktop 还会在 `%APPDATA%\Claude\claude-code\<ver>\claude.exe` 下载自带 CLI。 | [claude-code#36079](https://github.com/anthropics/claude-code/issues/36079) · [#25162](https://github.com/anthropics/claude-code/issues/25162) · [#87006](https://github.com/anthropics/claude-code/issues/87006) |
| F5 | 官方提供**离线 MSIX**（内含 CLI 与 Cowork VM bundle）；Cowork 需要硬件虚拟化 + Virtual Machine Platform。 | [Claude Desktop installation](https://claude.com/docs/third-party/claude-desktop/installation) |
| F6 | Chromium 在 Windows 上自身就用 restricted token + job object + 备用 desktop + AppContainer（渲染/GPU 等子进程）做沙箱；浏览器主进程（broker）本身不在沙箱里。 | [Chromium Sandbox design](https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md) |
| F7 | Sandboxie-Plus 用内核驱动 + 注入 DLL 实现文件/注册表/对象命名空间重定向（copy-on-write 到 box 目录），开源（GPLv3）。 | [sandboxie-plus/Sandboxie](https://github.com/sandboxie-plus/Sandboxie) |
| F8 | WFP 可在 `ALE_AUTH_CONNECT_V4/V6` 层按 **AppContainer Package SID**（`FWPM_CONDITION_ALE_PACKAGE_ID`）或用户 SID（`FWPM_CONDITION_ALE_USER_ID`）或 App ID 过滤；用户态 `FwpmFilterAdd0` 即可，需管理员，Home 版可用。 | [WFP filtering conditions](https://learn.microsoft.com/en-us/windows/win32/fwp/filtering-condition-identifiers-) |
| F9 | Job object 可限制进程树（`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`、`JOB_OBJECT_LIMIT_BREAKAWAY_OK` 关闭、UI 限制 `JOB_OBJECT_UILIMIT_*`）。 | [Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) |
| F10 | 用户态 API hook 常用库：Microsoft Detours（MIT）、MinHook（BSD）。 | [Detours](https://github.com/microsoft/Detours) · [MinHook](https://github.com/TsudaKageyu/minhook) |

## 2. 核心矛盾：Computer use vs 隔离

Computer use 要**截整个用户桌面**并**向任意窗口注入键鼠**。

- 若 Claude 在 AppContainer（Low IL）里：截屏（GDI/DXGI 读同桌面）大概率可行（**待验证**，AppContainer 下 DXGI Desktop Duplication 是否允许），但 `SendInput` 对 Medium 窗口被 UIPI 静默丢弃（F3）→ Computer use 基本残废。
- 若 Claude 以**独立低权限本地用户**跑在同一桌面（`runas`/`CreateProcessWithLogonW`）：IL 仍是 Medium，可注入；但该用户默认无权访问 `WinSta0\Default` 桌面对象，需要给该用户 SID 授权桌面/窗口站 ACL（**待验证**具体步骤）。
- 结论：**Claude 主进程不能放 AppContainer**（除非放弃 Computer use 或另做 Medium IL 的输入代理 broker）。推荐 = 独立用户 + 受限 token（去掉 Administrators、保留 Medium IL）+ job object + WFP，AppContainer 作为可选的「无 Computer use 严格模式」。
- 另一个诚实结论：Computer use 能看到并操作的**是真实宿主桌面**，这本身就让「完全伪装环境」不可能——屏幕上的一切都会被截图上传。

## 3. MSIX 带来的问题

- MSIX 应用以包身份激活（`IApplicationActivationManager` / 协议 `claude://`），由系统而非我们的启动器创建进程，**难以塞进我们的 job/用户/token**（**待验证**：用 `PROC_THREAD_ATTRIBUTE_*` 或 `Invoke-CommandInDesktopPackage` 能否控制）。
- 文件/注册表对 MSIX 有自带的 VFS 重定向（`%LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\...`），与我们的 FS 重定向叠加，行为复杂。
- 可选路线（均**待验证**）：
  1. 在容器用户下装 MSIX（按用户注册），由容器用户会话激活 → 隔离靠「用户边界 + WFP 按用户 SID」；
  2. 解包 MSIX 的 `app\` 目录，以**非打包**方式直接运行 `Claude.exe`（Electron 通常能跑，但包身份相关功能/自动更新可能失效）→ 我们完全控制进程创建；
  3. 若能拿到 Squirrel 版安装包则最简单（但官方已转向 MSIX，可能不再提供）。
- C0 建议先走路线 2（可控性最高），失败再走路线 1。

## 4. 机制选型表

| 维度 | 方案 | Home 可用 | 强度 | 工作量 | 结论 |
|---|---|---|---|---|---|
| 进程树 | Job object | ✅ | 中（防逃逸靠禁 breakaway） | 小 | **采用** |
| 身份/权限 | 独立本地标准用户 `ClaudeBoxUser` | ✅ | 中高（NTFS ACL 天然隔开宿主用户 profile） | 小 | **采用（主边界）** |
| 权限收紧 | 受限 token（`CreateRestrictedToken`，去管理员组/特权） | ✅ | 中 | 小 | **采用** |
| 强沙箱 | AppContainer / LPAC | ✅ | 高 | 中 | 可选严格模式；与 Computer use 冲突 |
| 文件系统 | 用户 profile 天然独立 + NTFS ACL 拒绝 `C:\Users\<宿主>` | ✅ | 中高 | 小 | **采用（C0）** |
| 文件系统 | 用户态 hook 重定向（Detours/MinHook 拦 `NtCreateFile` 等） | ✅ | 低（可被直接 syscall 绕过） | 大 | 仅作补充 |
| 文件系统 | minifilter 驱动 | ⚠️ 需驱动签名（测试签名模式不适合日常） | 高 | 很大 | 暂不做；参考 Sandboxie |
| 网络 | WFP 过滤：容器用户/包 SID 仅允许连 `127.0.0.1:<网关端口>`，其它 block | ✅ | 高（内核层强制） | 中 | **采用** |
| 网络 | 进程代理环境变量 / `--proxy-server` | ✅ | 低（应用可忽略） | 小 | 辅助（让流量走网关而非被丢） |
| 环境伪装 | hook DLL：`GetComputerNameEx`、`GetTimeZoneInformation`、`GetUserDefaultLocaleName`、WMI/SMBIOS 查询、`GetVolumeInformation`、MAC 等 | ✅ | 低~中（可绕过；Electron 子进程需逐个注入） | 大 | C2 做；**待验证**覆盖面 |
| 环境伪装 | 容器用户独立 TZ/locale（用户级设置） | ✅ | 中（locale 用户级可设；TZ 是系统级）| 小 | locale 采用；TZ 需 hook 或 `TZ` 变量（**待验证** Chromium 是否读） |
| 参考 | Sandboxie-Plus 直接用 | ✅ | 高 | 小 | 可作为对照/备胎，但非「自建」 |

## 5. 诚实的局限（相对 VM）

- 同一内核：CPU 型号、SMBIOS/主板序列号、GPU、磁盘序列号、MachineGuid（`HKLM\SOFTWARE\Microsoft\Cryptography`）、已装字体、显示器 EDID 等**无法从根本上隐藏**；只能 hook 用户态常见 API，直接 syscall / 驱动查询仍会泄漏。
- 主机名系统级：可 hook 返回假值，但 NetBIOS/DNS 层面对外仍是真名（流量走网关后影响较小）。
- Computer use 截的就是宿主真实桌面（第 2 节）。
- 目标降为：**直连必失败 + 宿主用户文件不可见 + 进程树受控 + 常见环境 API 返回假值**；遥测「大幅降低」而非清零。

## 6. 推荐栈

1. **启动器**（C#/.NET 8，P/Invoke）：以容器用户 `ClaudeBoxUser` 的受限 token 创建挂起进程 → 加入 job（禁 breakaway、kill-on-close）→（C2）注入 hook DLL → 恢复。
2. **容器根**：`<ContainerRoot>\profile`（容器用户 profile / `APPDATA` / `LOCALAPPDATA` 重定向）、`app\`（解包的 Claude）、`logs\`。NTFS ACL：容器用户对宿主用户 profile 与 `C:\` 下非系统目录 Deny。
3. **网络**：沿用 M2 的 gost 网关 + JSONL 日志，监听 `127.0.0.1`；WFP 规则（按容器用户 SID）：允许 → 网关端口，允许 → loopback 必要端口（**待验证** Electron 自身是否需要），其余 block；同时设 `HTTPS_PROXY` 与 `--proxy-server` 让应用主动走网关。
4. **环境伪装**（C2）：MinHook DLL + 配置文件；Electron 所有子进程通过 job 内创建通知注入（**待验证**）。
5. **visible_dirs**（C3）：对白名单目录给容器用户 ACL Allow（或 junction 进容器根）。
6. **UI**（C4）：复用 M4 WinForms 控件。

## 7. 待验证清单

- V1 解包 MSIX `app\Claude.exe` 非打包运行是否正常登录/更新。
- V2 容器用户在宿主交互桌面上显示窗口所需的 WinSta0/Desktop ACL。
- V3 容器用户下 Computer use 截屏 + 输入是否可用。
- V4 WFP 按用户 SID 过滤对 Electron 网络服务进程（可能在 `svchost`/NetworkService 中做 DNS）的影响；DNS 是否需走网关。
- V5 hook DLL 能否覆盖 Electron 所有子进程，Chromium 渲染沙箱是否拒绝注入。
- V6 在线引导器 `Claude_Setup.exe` 能否在容器内跑；不行则改用离线 MSIX（F5）。
