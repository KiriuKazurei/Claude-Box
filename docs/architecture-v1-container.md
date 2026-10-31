# 架构方案 v1 · 自建专用容器（主路径）

> 取代 [architecture-v0.md](./architecture-v0.md) 的 Hyper-V 主路径。依据见 [research-v2-container.md](./research-v2-container.md)。
> Hyper-V 访客路径**降级为可选/已弃用**（Win11 家庭版无 Hyper-V）；M2 网关、配置、M4 控件可复用。

## 组件

```
宿主 Win11 Home
├─ ClaudeBox.Control (M4 UI，复用)
├─ ClaudeBox.Gateway (gost + JSONL，监听 127.0.0.1，复用 M2)
├─ cbox (新，Rust 工作区 src/cbox/，Docker 式 CLI)
│   ├─ cbox-cli / cbox-core / cbox-win（windows-rs）
│   ├─ 运行：容器用户 + 受限 token + job object
│   ├─ Net：WFP 规则（容器用户 SID → 只许连网关）
│   ├─ cbox-hook（C2，cdylib）：环境伪装
│   └─ ContainerRoot\{profile,app,logs}
└─ Claude.exe 及全部子进程（运行在容器用户下、job 内）
```

## 里程碑

| 里程碑 | 交付 | 验收 |
|---|---|---|
| **C0 容器 MVP（第一优先）** | 以专用本地用户 `ClaudeBoxUser` + 受限 token 在 job object 中启动 Claude（安装器/解包后的 `Claude.exe`）；数据根在 `ContainerRoot`；进程级代理指向本地网关 | 见下 |
| C1 网络强制 | WFP 规则：容器用户 SID 只能连 `127.0.0.1:<gateway>`；DNS 方案 | 关网关时 Claude 无法联网；`Test-NetConnection` 以容器用户直连公网失败；JSONL 有全部连接 |
| C2 环境伪装 | MinHook DLL：hostname / TZ / locale / 卷序列号 / MachineGuid / WMI 常见查询 | 容器内探针读取到的值 = 配置假值 |
| C3 visible_dirs | 对白名单目录授予容器用户 ACL；拒绝宿主 `C:\Users\<宿主>` 与 `C:\` 根 | 白名单可读写，其它宿主用户文件拒绝访问 |
| C4 UI | M4 控件改为驱动 Container 而非 VM | 一键启停、查看日志 |
| 可选 | AppContainer 严格模式（无 Computer use）；Hyper-V/VirtualBox 访客（已弃用） | — |

### C0 定义

**在宿主上启动 Claude_Setup / Claude 于容器内：隔离 FS 根 + 强制经网关 + 进程树受限（job object）。**

C0 验收检查：
1. 进程：Claude 主进程与全部子进程的用户 = `ClaudeBoxUser`，且都在同一 job（Process Explorer「Job」页 / `IsProcessInJob`）；关闭启动器 → 全部进程退出（kill-on-close）；尝试 breakaway 失败。
2. 权限：token 无 Administrators 组、无 `SeDebugPrivilege` 等；IL = Medium（保 Computer use）。
3. FS：Claude 的 `APPDATA/LOCALAPPDATA` 落在 `ContainerRoot\profile`；从容器内读取宿主用户 `C:\Users\<宿主>\Documents` → 拒绝访问。
4. 网络（C0 级）：Claude 流量出现在网关 JSONL；停网关后 Claude 无法联网（C0 仅靠代理设置，**强制**在 C1 由 WFP 保证）。
5. GUI：Claude 窗口出现在宿主当前桌面，可登录使用（**待验证** V2）。

## 安装器策略（见 research §3）

优先：解包 MSIX/离线 MSIX 的 `app\` 到 `ContainerRoot\app` 非打包运行；备选：在 `ClaudeBoxUser` 下注册 MSIX。均为**待验证**。

## 复用与弃用

- 复用：`config/`、`ClaudeBox.Gateway`（改为监听 loopback）、`ClaudeBox.Control` UI。
- 弃用（保留代码，不再主线）：`scripts/win11` 的 Hyper-V 建机/交换机脚本、`ClaudeBox.Guest`（访客内脚本）。

## 实现：Rust `cbox`（Docker 式体验）

- `cbox import <解包的 app 目录> <image>` → `<store>\images\<image>`（只读，ACL 给容器用户 RX）
- `cbox run <spec.toml>` → 建实例（用户、根目录、job、WFP），启动 entrypoint；`ps/stop/rm` 管理实例。
- spec 字段：`image / entrypoint / root / isolation(user|appcontainer) / [network] gateway, enforce / [[mounts]] / [env_spoof]`，示例见 `src/cbox/examples/claude.cbox.toml`；挂载拒绝 `C:\` 与 `C:\Users`。
- C0 = `cbox run` 能完成上面 C0 验收 1–5；C4 的 UI 只是调 `cbox` 的前端。
- 不是真容器：Windows 无内核 namespace，见 research §8。
