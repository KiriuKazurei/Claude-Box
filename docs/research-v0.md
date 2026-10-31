# Claude Box · 初步技术调研笔记 v0

- **日期**：2026-10-31（Asia/Tokyo）
- **对应范围**：[SCOPE.md](../SCOPE.md) v0.1
- **性质**：公开资料汇总 + 合理假设；标「待验证」处禁止当定论
- **不做**：本笔记不写产品实现代码，不 push 远程

---

## 0. 结论摘要（先看这里）

1. **优先路线（MVP）**：在 Windows 11 上，用 **Docker Desktop（WSL2 后端）跑 Linux 版 Claude Desktop**（官方 Linux beta `.deb` / apt），GUI 经 **WSLg（X11/Wayland）** 透出到原生桌面窗口；出入网由 **透明代理 sidecar（redsocks / gost 类）** 统一走控件配置的 HTTP/SOCKS5。  
2. **不优先**：把 **Windows 原生 Claude Desktop（MSIX）塞进 Windows 容器**——Windows 容器几乎没有可用的交互桌面会话，公开实践也倾向「要 GUI 就用 VM/Sandbox」。  
3. **「唯一网关」可行**，但必须是 **网络命名空间级透明劫持**（共享 netns + iptables 重定向），不能只靠 `HTTP_PROXY`/`HTTPS_PROXY`（Electron/Chromium 部分流量可不走环境变量）。UDP/DNS/QUIC 是常见坑，需单独策略。  
4. **环境伪装**：容器内独立 `TZ`、locale、hostname、`/etc/machine-id` 等，对「读容器内系统接口」的遥测有效；**「全盘可读但仍对 Claude 隐藏宿主机时区」在挂载整盘时基本不现实**——时区信息就在被挂载的文件系统与 OS API 里。范围里的降级（控件选可见目录）应作为 **默认真路径**，全盘+挡时区仅作可选尝试。  
5. **反遥测**：公开文档对 **企业 3P 部署**（Bedrock/Vertex/Foundry）列出了可关遥测开关与 egress 主机（Sentry、Datadog 等）；**消费级 Claude.ai 登录路径的字段级明细不完全等同**，部分依赖逆向文章——一律标待验证。网关拦截 + 环境伪装是范围目标的主手段；官方 managed config 是补充，不能假设用户有企业策略。

**建议优先技术路线**：**候选 A（主）** Linux Claude + Docker/WSL2 + WSLg + 透明代理网关；**候选 B（备）** Hyper-V 轻量 Linux/Windows VM（GUI 稳、隔离硬，但「Docker-like 控件」与密度更差）。macOS 列为次优先级，XQuartz/TCP DISPLAY 可用但体验与 GPU 弱于 WSLg。

---

## 1. 产品约束如何映射到技术问题

| 范围要求 | 技术问题 |
|---|---|
| Docker-like 容器跑 Claude Desktop | 跑 **哪一平台二进制**？Win MSIX / macOS app / Linux deb？ |
| 原生 GUI 窗口透出 | 容器/VM 的显示管道：WSLg / X11 / RDP / VNC？ |
| 出入网唯一网关 + HTTP/SOCKS5 | 透明代理 vs 应用层代理；DNS/UDP |
| 独立控件管理 | 需稳定 CLI/API（Docker Engine、`.wsb`、Hyper-V） |
| 隔离目录 / 挡环境遥测 | 挂载策略 vs OS API 泄漏面 |

**关键事实（已公开）**：

- Claude Desktop 现已有 **macOS、Windows、Linux（beta）**。Linux 要求 Ubuntu 22.04+ / Debian 12+，x64/arm64；可用 apt 或 `.deb`。  
  来源：[Install Claude Desktop](https://support.claude.com/en/articles/10065433-install-claude-desktop)
- Linux 版限制（相对桌面版）：Computer use 不可用；Dictation 不可用；Quick Entry 在 X11 可用，原生 Wayland 依赖 GlobalShortcuts portal。  
- Cowork 在 Linux 上需要 **KVM + QEMU 等**（安装 apt 包会拉依赖）；在「容器里再嵌套虚拟化」通常困难 → **Cowork 进容器属高风险，待验证 / MVP 可先不做保证**。

这对 Claude Box 意味着：**在 Linux 容器里跑官方 Linux Claude**，比硬塞 Windows GUI 进容器更贴近「Docker-like」叙事；但功能集可能与宿主机原生 Win/Mac 版不完全一致（待和零确认是否接受）。

---

## 2. GUI 进容器 / 轻量 VM：选项对比

### 2.1 总表（Windows 11 优先）

| 方案 | GUI 现实 | 与 Claude 二进制匹配 | 网络可控性 | 环境伪装 | 控件化难度 | MVP 适合度 |
|---|---|---|---|---|---|---|
| **Docker Desktop + Linux 容器 + WSLg** | 较好：可出独立窗口（X11/Wayland）；Docker Desktop 需特殊挂载路径 | **Linux Claude** | 高（sidecar/iptables） | 高（容器本征） | 中（Docker API） | **高（主候选）** |
| **WSL2 发行版内直接装 Claude（非 Docker）** | 最好：WSLg 原生支持 | Linux Claude | 中（需自建网关/代理） | 中高 | 中低 | 中（可作 GUI 基线实验） |
| **Podman Desktop（WSL2/Hyper-V machine）** | 类似 Docker + WSLg，待验证 | Linux Claude | 高 | 高 | 中 | 中（Docker 替代） |
| **Windows Sandbox + Win Claude** | 好：原生 Win 桌面 / RDP 式会话 | **Windows Claude** | 弱：`.wsb` 仅开/关网络，无 SOCKS5 控件 | 弱：默认继承宿主机时区等 | 低（`.wsb` XML） | 低–中（原型隔离可，网关不符） |
| **Hyper-V VM（Win 或 Linux 访客）** | 好：Enhanced Session / RDP | Win 或 Linux Claude | 高（虚拟交换机 + 网关 VM） | 高（独立 OS） | 中高 | **中（备选，更重）** |
| **Windows 容器跑 Win GUI** | **差**：无正常交互桌面；RDP/VNC 脆弱 | Win Claude | — | — | — | **不推荐** |
| **macOS：Docker Desktop + XQuartz** | 可用：`DISPLAY=host.docker.internal:0`（TCP，非 UDS） | Linux Claude（Rosetta/模拟时性能待验证） | 高（同 sidecar） | 高 | 中 | 中（次平台） |
| **macOS：Apple `container` / Linux VM** | 需自建 framebuffer+VNC；无 Metal 直通 | Linux Claude | 中高 | 高 | 中 | 低（体验偏远程桌面，范围暂不进 Web RD） |
| **macOS 原生 Claude 进 macOS 访客 VM** | 好但重；授权/镜像复杂 | macOS Claude | 中高 | 高 | 高 | 低（非 MVP） |

### 2.2 Docker Desktop + WSLg（主路径细节）

公开做法（WSLg 官方容器样例）：

- 挂载：`/tmp/.X11-unix`（X11）、`/mnt/wslg`（Wayland / Pulse）
- 环境：`DISPLAY`、`WAYLAND_DISPLAY`、`XDG_RUNTIME_DIR`、`PULSE_SERVER`

**Docker Desktop 坑**：桌面发行版路径与「普通 WSL 发行版里跑 docker」不同。社区方案常需把路径改成类似：

- `/run/desktop/mnt/host/wslg/.X11-unix` → 容器 `/tmp/.X11-unix`
- `/run/desktop/mnt/host/wslg` → `/mnt/wslg`

并显式设 `DISPLAY=:0` 等（见 Stack Overflow / docker/desktop-linux#298）。**待验证**：当前 Docker Desktop 版本下挂载路径是否仍如此；Electron（Claude）在 WSLg 下是否白屏/无 GPU。

GPU：WSLg vGPU 进容器需 Mesa D3D12、`/dev/dxg`、`/usr/lib/wsl` 等；不少报告显示 Docker Desktop 下 **无硬件加速、走软件渲染**。对聊天 UI 可能可接受，对重图形 **待验证**。

社区已有「Claude Desktop 进 Ubuntu 容器 + VNC/noVNC」方向（如 `fjmerc/claude-desktop-docker`），但范围要求 **原生 GUI 而非 Web 远程桌面**，VNC 仅可作调试后备，不宜当 MVP 主 UX。

### 2.3 Windows Sandbox

- 优点：轻量、Hypervisor 隔离、原生 Win GUI、`.wsb` 可 MappedFolders（可选只读）、可关网络。  
- 缺点：关闭即丢状态（Win11 22H2+ 沙箱内重启可保留，关窗口仍丢）；**无代理地址配置项**；时区等默认跟宿主机；不像 Docker 那样好做「唯一网关控件」。  
- 适合：快速验证「Win Claude + 目录隔离」；**不适合**单独满足 v0.1 网络网关。

### 2.4 Hyper-V

- 持久 Linux/Windows 访客；独立时区/语言/机器身份自然成立。  
- 网络：可把访客默认路由指到「网关 VM/容器」或宿主机上的透明代理。  
- GUI：Linux 可用 xrdp + Enhanced Session；Windows 访客原生。  
- 代价：资源更重、启动慢、控件要管 VM 而非「像 Docker 一样」；嵌套虚拟化（访客内再跑 Cowork/KVM）另议。

### 2.5 macOS（次优先）

- Docker Desktop 在 Mac 上容器跑在 Linux VM 内，**不能**挂 macOS 的 X11 UDS；惯例用 **XQuartz + TCP** `host.docker.internal:0`，每次重启 XQuartz 常需 `xhost +localhost`。  
- Apple Virtualization / 官方 `container` 工具：**无 GPU/Metal 给 Linux 访客**；GUI 多靠 VNC 类，与「原生窗口」目标有落差。  
- Apple Silicon 上跑 `linux/amd64` Claude 镜像可能走模拟，**性能待验证**；优先找 arm64 Linux Claude 包。

---

## 3. 唯一网关 + HTTP / SOCKS5

### 3.1 目标语义

范围：「出入网经 Docker 唯一网关；控件可指定 HTTP 与 SOCKS5」。  
理解为：Claude（及同 netns 进程）的出站 **默认只能经该网关/上游代理**，而不是「设置了代理环境变量但应用可直连绕过」。

### 3.2 常见方案

| 方案 | 原理 | 优点 | 坑 |
|---|---|---|---|
| **仅环境变量** `HTTP(S)_PROXY` / `ALL_PROXY` | 应用自愿遵守 | 实现简单 | Chromium/Electron、部分原生库、自定义 socket **可不走**；SOCKS 支持不一致 |
| **透明代理 sidecar**（redsocks / gost + iptables）+ `network_mode: service:proxy` | 同 netns，TCP 重定向到本地再上游 | 应用无感；贴近「唯一网关」 | 需 `NET_ADMIN`/`NET_RAW`；**UDP 常不代理**；DNS 易泄漏；QUIC/HTTP3 可能绕过或失败 |
| **tun2socks / 用户态 TUN** | L3 全流量进代理 | 覆盖更全 | 权限与 Docker Desktop 网络模型更复杂；Win/Mac 上待验证 |
| **Docker Desktop 宿主机代理设置** | 桌面把流量导入系统/配置的代理 | 对部分拉取镜像有效 | **不等于**容器内应用强制代理；文档也区分 daemon 与容器内行为 |
| **仅防火墙放行上游 + DROP 其他** | 强制必须经代理 IP | 与 sidecar 互补 | 要正确排除 Docker 内网 CIDR、DNS、WSLg 相关通道 |

开源参考（模式级，非背书）：`mietzen/docker-redsocks-proxy`、`Xavier-Lam/proxy-sidecar`、`Pugemon/docker-proxy-sidecar`、`xjasonlyu/tun2socks`。

### 3.3 设计建议（MVP）

1. **控件**保存：上游类型（HTTP CONNECT / SOCKS5）、地址、端口、可选账号；是否允许 UDP；DNS 策略（DoH via proxy / 固定 DNS / 阻断）。  
2. **运行时**：代理 sidecar 为网络所有者；Claude 容器 `network_mode: service:<gateway>`。  
3. **默认 DROP** 非代理出站 TCP；明确记录「无法透明代理的流量类型」。  
4. **入站**：MVP 可无入站服务；若以后要，也只经网关映射。  
5. **Docker Desktop 无宿主机 `docker0`**、容器 IP 从宿主机不可直达——调试网关时要用容器内抓包/`curl` 出口 IP 检测，不能假设宿主机路由可见。

### 3.4 与 Claude 出站的关系

企业 3P 文档列出大量 egress 主机（`api.anthropic.com`、`downloads.claude.ai`、`*.sentry.io`、Datadog intake、更新源等），并说明可用 managed config 关闭各类遥测/更新。  
来源：[Telemetry and egress (3P)](https://claude.com/docs/third-party/claude-desktop/telemetry)

对 Claude Box：

- **网关可拦**：未允许的遥测/分析域名（SNI/IP 级，TLS 不解密则主要靠域名/IP 策略）。  
- **不能拦却仍「伪装」**：若必须放行 `api.anthropic.com` 才能聊天，则请求里若带 OS/时区等字段，只能靠 **环境伪装** 让字段变「无效/非宿主机」，不能靠断网消除。  
- 消费级登录路径的完整字段表 **待验证**（见第 4 节）。

---

## 4. 环境伪装与反遥测

### 4.1 容器内「像一台独立机器」——通常能做的

| 维度 | 做法 | 挡得住什么 | 挡不住什么 |
|---|---|---|---|
| 时区 | `-e TZ=...`，**不要**挂载宿主机 `/etc/localtime`；挂只读 zoneinfo | `date`、多数 libc、`Intl` 默认时区 | 若把宿主机 `/etc` 或整盘挂进来仍可读到真时区文件；Win 注册表时区（若可见 Win 文件） |
| 语言/Locale | `LANG`/`LC_ALL`，安装对应 locale | 进程环境与部分 UI | 云端账号偏好、已同步设置 |
| Hostname | `hostname` / Docker `hostname` | `hostname`、部分 telemetry device name | 账号 ID、硬件级 ID（见下） |
| machine-id | 容器私有 `/etc/machine-id`，勿挂宿主机 | 读 systemd machine-id 的逻辑 | Electron/Chromium 自建 machine GUID、登录态 |
| 用户名 | 容器内独立用户 | `os.userInfo`、OTLP `process.owner` 一类 | 云端账号邮箱 |
| MAC / 网卡名 | 自定义 Docker 网络 | 简单网卡指纹 | 更强浏览器指纹、canvas 等（Electron） |
| DNS 名 | 自定义 resolv | 部分探测 | DoH 直连若未拦 |

Red Hat 等公开文强调：**容器默认共享宿主机时钟，但时区是配置问题**；绑宿主机 `/etc/localtime` 会泄漏宿主机时区。

### 4.2 Electron / Claude 遥测——公开信息 vs 假设

**公开（偏企业 3P 文档）**：

- Essential：崩溃/错误/性能；含 app version、OS、error type、打码栈等；**声称不含 prompt/response**；可 `disableEssentialTelemetry`。出口含 Sentry、Datadog intake、`claude.ai` 等。  
- Non-essential：产品分析、会话计数等；可关。  
- 另有更新检查、模型目录、`downloads.claude.ai` 等。  
- 自建 OTLP 时，文档写明可带 `process.owner`（OS 登录名）等——说明 **桌面端确实会读 OS 身份类元数据**（至少在 OTLP 路径）。

**不确定 / 待验证**：

- 消费级（claude.ai 账号）默认上报字段是否与 3P 文档一一对应。  
- 是否采集：时区、locale、屏幕、CPU、GPU、开机时间、稳定 device id、主机名等（业界 Electron/Sentry 常见会采一部分；**第三方逆向文不可当官方事实**）。  
- 在 WSLg/容器里 `process.platform` 为 `linux`，与宿主机 Win/Mac 不同——这对「伪装」是加分，但对「功能一致性」是减分。

**合理假设（需实验证伪）**：

1. 拦 Sentry/Datadog 等主机可显著削减崩溃/分析类外泄（可能影响官方排障）。  
2. 即使拦了分析域，**主 API 流量仍可能带客户端元数据** → 必须环境伪装。  
3. 「给非法遥测无效信息」= 独立环境真值，而不是在宿主机进程里 hook 每一个 Win32/`Intl` 调用（后者成本极高）。

### 4.3 「全盘可读但仍隐藏宿主机时区」现实性

**结论：在「把宿主机根目录或系统目录几乎全部挂进同一环境」的前提下，基本不现实；应把降级写成默认方案。**

原因：

1. 时区不是单一 API：Linux 上有 `/etc/localtime`、`/etc/timezone`、`$TZ`；Windows 上有注册表与 Win32；JS `Intl.DateTimeFormat().resolvedOptions().timeZone` 读运行环境。  
2. **全盘可读** ⇒ Claude 所在环境能打开宿主机的时区文件/注册表导出/配置，除非做细粒度文件系统虚拟化（overlay 隐藏特定路径、或 Windows 上过滤驱动）——远超 MVP。  
3. 若 Claude 跑在 **宿主机原生进程**（仅「文件夹权限」限制），挡时区几乎要做 API 层拦截，与「Docker-like」路线不一致。  
4. 若 Claude 跑在 **容器/VM**：不挂系统路径时，时区天然独立；一旦「全盘 bind mount」，伪装被自己破坏。

**写入方案的推荐表述**：

- **默认**：控件多选「可见目录」bind-mount（可写/只读可配）；容器内固定独立 TZ/locale/hostname/machine-id。  
- **可选「宽松文件访问」**：允许挂更多用户数据目录，但 **永远不挂** 宿主机 `/etc`、Windows `\Windows\System32\config` 等系统身份路径；UI 上不叫「全盘」，叫「扩展目录」。  
- **「全盘」实验档**：仅高级选项；文档标明 **无法保证** 隐藏时区/机器指纹；用于接受风险的用户。  
- 范围原文「做不到则控件选可见目录」→ 调研结论：**按做不到处理，可见目录为 MVP 主路径**。

---

## 5. MVP 技术选型建议

### 5.1 候选 A（推荐主路径）— 「Linux Claude in Docker + WSLg + Gateway」

**组成**：

- 宿主机：Windows 11 + WSL2 + WSLg + Docker Desktop  
- 镜像：Debian 12/Ubuntu 22.04+，安装官方 `claude-desktop`  
- GUI：WSLg 挂载（处理 Desktop 路径差异）  
- 网络：gateway sidecar（HTTP/SOCKS5）+ 共享 netns + 默认拒绝直连  
- 文件：可见目录列表；独立身份环境变量与文件  
- 控件：本地小 UI/CLI，调 Docker API（启停、目录、代理、日志）

**优点**：最贴近范围用词「Docker」；网关与伪装自然；Linux 官方包存在。  
**风险**：WSLg+Docker Desktop+Electron 兼容性；无/弱 GPU；Linux 功能子集；Cowork/KVM 难；Docker Desktop 许可（商业公司需注意，个人待确认）。  

### 5.2 候选 B（备选）— 「Hyper-V 轻量访客」

**组成**：固定模板 VM（Linux 优先，或 Win 若必须原生 Win Claude）；控件管 VM 生命周期、共享文件夹、虚拟网卡网关。  

**优点**：GUI/兼容性更硬；身份隔离最干净。  
**风险**：不像 Docker；体积与启动；实现控件成本高。  

### 5.3 明确不选为 MVP 主路径

- Windows 容器跑 Win GUI  
- 以 noVNC/Web 为唯一 UI（范围暂不进 Web 远程桌面）  
- 仅靠环境变量代理  
- 承诺「全盘可读且时区对 Claude 不可见」

### 5.4 macOS 策略

- MVP 可先 **文档级支持**：同一 Linux 镜像 + XQuartz 说明。  
- 验收以 Win11 为准；Mac 列为「尽力 / 第二阶段」。

---

## 6. 风险清单

| ID | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R1 | WSLg + Docker Desktop 下 Claude Electron 无法稳定显示 | 主路径失败 | 先在「纯 WSL 发行版」验证 GUI；再迁 Docker；备选 Hyper-V/RDP |
| R2 | 透明代理漏 UDP/QUIC/DNS | 遥测或直连泄漏 | DROP UDP 试验；强制 DNS；抓包对比 |
| R3 | Linux Claude 缺 Computer use 等能力 | 用户期望落差 | 范围沟通；必要时候选 B 用 Win Claude |
| R4 | Cowork 需 KVM，容器/嵌套虚拟化失败 | Cowork 不可用 | MVP 标明不支持或改 VM |
| R5 | 消费级遥测字段未知 | 「无效信息」无法证明 | 抓包/代理日志实验；不夸大 |
| R6 | 上游改版 / 反容器检测 | 登录或功能异常 | 版本钉扎；最小化指纹矛盾（一致的假环境） |
| R7 | Docker Desktop 许可与分发 | 产品化受阻 | 评估 Engineless 方案、Podman、自带 VM |
| R8 | 挂载过宽导致密钥/浏览器配置泄漏进容器 | 安全事故 | 默认最小目录；只读选项；路径黑名单 |
| R9 | macOS GUI/GPU 体验差 | 次平台口碑 | 延后；或接受 X11 窗口观感 |
| R10 | 「原生窗口」与 RDP/VNC 观感争议 | 范围争议 | WSLg 优先；避免把浏览器 noVNC 当完成 |

---

## 7. 下一步验证实验（建议在和零的 Win11 上做；本笔记不要求做完）

按优先级：

1. **E1 · Linux Claude 基线**  
   - 在 WSL2 Ubuntu/Debian 安装官方 `claude-desktop`，确认 WSLg 窗口可登录、可对话。  
   - 记录：功能是否缺 Computer use；Cowork 是否提示 KVM。

2. **E2 · Docker + WSLg**  
   - 最小镜像跑 `xclock`/`gedit`，验证 Desktop 挂载路径。  
   - 再装 Claude Desktop；记录启动参数、白屏/GPU、剪贴板。

3. **E3 · 代理唯一性**  
   - sidecar SOCKS5/HTTP；容器内 `curl` 出口 IP；故意直连应失败。  
   - 启动 Claude 后抓连接列表：哪些主机仍出站；关 Sentry/Datadog 是否仍可用聊天。

4. **E4 · 环境伪装**  
   - 容器 `TZ=America/New_York`、`LANG=en_US.UTF-8`、独立 hostname/machine-id。  
   - 容器内用小脚本打印：`date`、`timedatectl`（若有）、Node/Electron `Intl` 时区。  
   - **对比**：bind-mount 宿主机 `/etc/localtime` 前后差异。  
   - **对比**：额外挂载宿主机用户目录 vs 挂载 `C:\` 或 `/mnt/c` 时，能否读到宿主机时区文件。

5. **E5 · 目录隔离**  
   - 仅挂 `D:\work\proj`；确认无法读未挂路径。  
   - 列出「扩展目录」黑名单候选（`/etc`、`/mnt/c/Windows`、浏览器配置目录等）。

6. **E6 ·（可选）Windows Sandbox**  
   - `.wsb` 映射单文件夹 + 安装 Win Claude；感受 GUI 与网络限制；确认无法用 `.wsb` 配 SOCKS5。

产出物建议：把 E1–E4 结果追加为 `docs/research-v0-experiments.md`（另开，不在本次）。

---

## 8. 建议的仓库文档后续（非本次）

- 更新 `README.md` 链到本笔记  
- 定稿后可拆：`docs/adr-001-runtime.md`（选 A 或 B）  
- 控件 UX 草图与代理配置 schema

---

## 9. 参考链接（抽样）

- 范围：仓库内 `SCOPE.md`  
- Claude Desktop 安装与 Linux 说明：https://support.claude.com/en/articles/10065433-install-claude-desktop  
- Claude Desktop 3P 遥测与 egress：https://claude.com/docs/third-party/claude-desktop/telemetry  
- WSLg：https://github.com/microsoft/wslg ；容器样例 `samples/container/Containers.md`  
- WSLg + Docker Desktop 路径讨论：docker/desktop-linux#298 ；Stack Overflow「GUI apps from docker desktop container on windows 11」  
- Windows Sandbox 配置：https://learn.microsoft.com/en-us/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-configure-using-wsb-file  
- Docker Desktop 网络特性：https://docs.docker.com/desktop/features/networking/networking-how-tos/  
- 透明代理 sidecar 模式：https://github.com/mietzen/docker-redsocks-proxy ；https://github.com/Xavier-Lam/proxy-sidecar  
- 容器时区：Red Hat「Tick-tock. Does your container know what time it is?」  
- macOS Docker GUI / XQuartz：各类公开教程（`DISPLAY=host.docker.internal:0`）  
- Apple 容器与 GUI/GPU 限制：公开博文（Virtualization 无 Metal 给 Linux 访客）— 作背景，细节待验证

---

## 10. 一句话给和零

**先押「Win11 + Docker/WSL2 + 官方 Linux Claude + WSLg + 透明代理网关」，目录隔离默认「可见目录」；全盘且藏时区不做承诺。** 用 E1–E4 小实验打脸或坐实后再写 ADR、开控件骨架。
