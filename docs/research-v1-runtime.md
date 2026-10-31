# Claude Box · 隔离运行时选型调研 v1

- **日期**：2026-10-31（Asia/Tokyo）
- **对应范围**：[SCOPE.md](../SCOPE.md) v0.1（运行时形态可替换；Docker-like **非**硬约束）
- **相对 v0**：v0 在「Docker-like」叙事下押 Docker/WSL2；本轮按硬约束重比，**不把容器引擎当默认答案**
- **性质**：公开资料 + 架构推理；标「待验证」处禁止当定论
- **不做**：本笔记不写产品实现代码；不 push 远程

---

## 0. 结论摘要

### 一句话推荐

**Win11 MVP：用「WSL2 专用发行版」当盒子跑官方 Linux Claude，发行版内做透明代理唯一网关；控件管启停 / 可见目录 / 上游 HTTP·SOCKS5。不要死磕 Docker Desktop。**

若和零**必须** Windows Claude（exe/MSIX）完整能力（尤其 **Computer use**），则改主押 **Hyper-V Windows 轻量访客 + 内网交换机强制网关**；Windows Sandbox 只适合冒烟，不适合当产品运行时。

### 为何优于「死磕 Docker」

| 点 | Docker Desktop 死磕 | WSL2 发行版（推荐） |
|---|---|---|
| 原生窗口 | 依赖 Desktop 特有 WSLg 挂载路径，易踩坑 | WSLg 一等公民，独立应用窗口 |
| 唯一网关 | 容器 netns + sidecar 可行 | 发行版内 iptables/tun2socks 同样可行；少一层 Desktop 网络模型 |
| 许可与分发 | Docker Desktop 商业许可要另评估 | 无 Desktop；`wsl.exe` / 发行版可控件化 |
| 后期替换 | 易绑死 compose/Desktop API | 同一套 Linux 访客脚本可迁到 Hyper-V Linux VM |
| 功能集 | 仍是 Linux Claude（缺口一样） | 一样——Docker **解决不了** Linux 缺 Computer use |

Docker/Podman 可留作**可选打包层**（把网关+Claude 打成可复现镜像），但**不应**作为 Win11 前期验证的根基。

### 分叉（先和零对齐）

```
需要 Computer use / 完整 Win Claude？
  ├─ 否 → 【主】WSL2 专用发行版 + Linux Claude + 透明网关
  │         后期可换 Hyper-V Linux 访客（更硬隔离）
  └─ 是 → 【主】Hyper-V Windows 访客 + Win Claude + 强制网关
            Sandbox 仅作 1 天冒烟；勿当产品路径
```

---

## 1. 硬约束 → 选型判据

| 硬约束 | 判据 |
|---|---|
| 隔离跑 Claude Desktop | 独立用户态/OS；默认可控文件系统 |
| 原生 GUI（非 Web RD 主 UX） | 独立应用窗口或接近原生的 Win 会话；noVNC/浏览器桌面降级 |
| 唯一网关 + HTTP/SOCKS5 | **硬强制**（透明劫持 / 虚拟交换机无直连），不只靠 `HTTP_PROXY` |
| 目录可见性 | 默认可见目录；不承诺「全盘+藏宿主机时区」 |
| 环境伪装 / 反遥测 | 独立 TZ/locale/hostname/machine-id；网关可拦分析域 |
| Win11 优先，macOS 次 | Win 成熟度权重大；Mac 可移植性单独记 |
| 前期可借、后期可换 | 抽象「访客生命周期 + 挂载 + 网关」；避免绑死单一引擎专有 API |

**Claude 二进制分叉（影响选型）**：

- **Linux Claude**（官方 beta）：无 Computer use、无 Dictation；Cowork 需 **KVM + QEMU**（容器/部分嵌套环境常失败）。  
  来源：[Install Claude Desktop](https://support.claude.com/en/articles/10065433-install-claude-desktop)、[Desktop Linux](https://code.claude.com/docs/en/desktop-linux)
- **Windows Claude**（exe/MSIX）：功能集更全；必须跑在 Windows 会话（宿主机 / Sandbox / Hyper-V Win 访客），**进不了** WSL/Linux 容器。

---

## 2. 候选对比（统一维度）

评分：● 强 / ◐ 中 / ○ 弱 / ✕ 基本不符。均为公开信息 + 推理；带「?」为待验证。

| 候选 | 原生 GUI | Claude 版本 | 唯一网关硬强制 | 目录隔离 | 环境伪装 | 控件化 | 资源/启动 | 后期换底层 | Win11 成熟度 | macOS 可移植 |
|---|---|---|---|---|---|---|---|---|---|---|
| **1. WSL2 发行版直接跑** | ● WSLg | Linux | ◐ 发行版内透明代理 + 防火墙；mirrored 模式有坑 | ● bind 可见目录 | ● 独立 TZ 等 | ◐ `wsl.exe`/脚本 | ● 轻 | ● → Hyper-V Linux | ● | ○ 概念可迁，实现重写 |
| **2. Docker Desktop + Linux + WSLg** | ◐ 可行但路径脆 | Linux | ● netns+sidecar | ● | ● | ● Docker API | ◐ Desktop 重 | ◐ 易绑 Desktop | ● | ◐ 同镜像+XQuartz |
| **3. Podman（WSL machine）** | ◐ 类似 2 | Linux | ● 同容器网 | ● | ● | ◐ | ◐ | ◐ | ◐ | ◐ |
| **4. Windows Sandbox + Win Claude** | ● Win GUI | **Win** | ○ `.wsb` 仅开/关网；靠内装 Proxifier | ◐ MappedFolders | ○ 时区等跟宿主 | ○ `.wsb` 弱 | ● 轻但关即丢 | ○ 难演进产品 | ● | ✕ |
| **5. Hyper-V 轻量 Linux 访客** | ◐ Enhanced/xrdp 偏会话窗 | Linux | ● 内网交换机+网关 VM | ● 共享文件夹 | ● 完整 OS | ◐ PowerShell/API | ○ 更重 | ● 自建方向 | ● | ○ UTM/VZ 另做 |
| **6. Hyper-V Windows 访客** | ◐ Enhanced Session | **Win 完整** | ● 同 5 | ● | ● | ◐ | ○ 最重 | ● | ● | ✕ Win 访客 |
| **7. 其它**（Firecracker / Sandboxie 等） | 见下 | — | — | — | — | — | — | — | ○/✕ | — |

### 2.1 WSL2 发行版直接跑（推荐主路径）

- **GUI**：WSLg 把 Linux 应用集成到 Windows 桌面（开始菜单、Alt-Tab、剪贴板）。公开社区已有在 WSL 内装 `claude-desktop` 的实践（需 keyring、偶发需 `--disable-gpu` / Wayland ozone——**待验证**本机）。  
  来源：[Run Linux GUI apps with WSL](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps)
- **网关**：在发行版内用 redsocks/gost/tun2socks + iptables/nftables；默认 DROP 非代理出站。WSL `autoProxy` 只是导入 Windows 代理**环境变量**，**不等于**硬强制。`networkingMode=mirrored` 下代理/防火墙行为有已知坑，需实验钉死 NAT vs mirrored。  
  来源：[WSL networking](https://learn.microsoft.com/en-us/windows/wsl/networking)
- **目录**：只 `mount`/`ln` 可见目录；勿挂整个 `/mnt/c`。
- **伪装**：发行版独立 hostname、`/etc/machine-id`、`TZ`、`LANG`；勿绑宿主 `/etc/localtime`。
- **Cowork**：需 `/dev/kvm` + QEMU；WSL2 上是否稳定 **待验证**（官方亦提示 ChromeOS/容器式内核常缺模块）。
- **迁移**：控件接口定为「启动命名发行版 / 注入挂载 / 写网关配置」→ 后期换成 Hyper-V Linux 时替换启动器即可。

### 2.2 Docker Desktop (WSL2) + Linux Claude + WSLg

- v0 主推路径；在「Docker-like 非硬约束」后降为**可选打包**。
- 优点：镜像可复现；sidecar `network_mode: service:gw` 网关语义清晰。
- 缺点：Desktop 挂载 WSLg 路径与普通 WSL 不同（历史讨论 docker/desktop-linux#298）；常无硬件加速；许可；多一层故障面。
- **结论**：验证期不如直接 WSL；若团队已强依赖 OCI 工作流再引入。

### 2.3 Podman / 其它容器引擎

- Podman Desktop 在 Win 上多用独立 WSL machine；GUI 同样靠 WSLg 挂载，成熟度与文档密度通常弱于 Docker Desktop。
- 相对 Docker 的收益（无 daemon、rootless）对「跑一个 Electron Claude」不明显。
- **结论**：不单独开主路径；若排斥 Docker 许可，优先 **纯 WSL**，其次再评 Podman。

### 2.4 Windows Sandbox + Windows Claude

- **优点**：真 Win GUI；MappedFolders 做目录隔离快；适合「exe 能不能装」冒烟。
- **致命**：`.wsb` 网络只有 Enable/Disable/Default，**无代理地址**；社区用沙箱内 Proxifier 等——状态易丢、难产品化控件；时区等默认跟宿主。  
  来源：[Sandbox .wsb](https://learn.microsoft.com/en-us/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-configure-using-wsb-file)；PolarProxy+Proxifier 博文（模式参考，非产品方案）
- **结论**：**不作 Claude Box 运行时**；可做 E-smoke。

### 2.5 Hyper-V 轻量 Linux 访客

- **网关最硬**：访客挂 Internal 交换机，默认路由指向「网关」（宿主机或专用网关 VM）上的透明代理/tun2socks；防火墙禁旁路。  
  来源：[Hyper-V NAT](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/setup-nat-network)；公开 tun2socks-in-guest 实践
- **GUI**：xrdp / Enhanced Session → 更像「一台电脑的窗口」，不如 WSLg「应用窗口」；是否可接受属产品判断（范围禁的是 **Web** 远程桌面当主 UX，RDP 会话窗需和零确认观感）。
- **定位**：WSL 验证通过后、需要更强网络隔离或摆脱 WSL 耦合时的**替换底层**。

### 2.6 Hyper-V Windows 访客 + Windows Claude exe

- **何时选**：必须 Computer use / 与 Win 版功能对齐；或 Linux 版某关键能力不可接受。
- **代价**：镜像大、启动慢、控件要管 Windows 访客（更新、激活、防休眠）；GUI 为 Enhanced Session；共享文件夹与凭据要仔细设计；macOS 无对等「Win 访客」路径。
- **网关**：同 2.5，可用访客内强制代理或双网卡网关 VM。
- **结论**：**exe 路线的正确容器不是 Docker，也不是 Sandbox，而是 Hyper-V（或等价完整 Win VM）**。

### 2.7 其它公开方案（有实质优势才写）

| 方案 | 简述 | 对 Claude Box |
|---|---|---|
| **Firecracker / cloud-hypervisor** | 极轻 microVM，偏 Linux 宿主 | Win11 宿主不现实；嵌套成本高 → **不选** |
| **Sandboxie-Plus 等应用沙箱** | 宿主机上困住 Win 进程 | 隔离弱于 HV；唯一网关/完整假 OS 难 → **不满足核心** |
| **第三方「带代理的 Win 沙箱」** | 各异 | 供应风险；难跨 Mac → 仅调研备忘，不进 MVP |
| **macOS：Docker + XQuartz / 轻量 Linux VM** | 次平台 | 与 Win 主路径共享「Linux 访客+网关」概念；GUI 弱于 WSLg |

---

## 3. 「唯一网关」谁更容易硬强制

目标语义：Claude 出站 **不能直连绕过**，上游由控件配 HTTP CONNECT 或 SOCKS5。

| 层级 | WSL2 发行版 | Docker/Podman netns | Hyper-V 访客 | Windows Sandbox |
|---|---|---|---|---|
| 仅环境变量 | ✕ 不够 | ✕ | ✕ | ✕ |
| 透明代理（iptables/redsocks/tun2socks） | ● 发行版内 | ● sidecar | ● 访客内或网关 VM | ○ 需自带工具且状态丢 |
| 虚拟交换机无默认外网 | ◐ 受 WSL 网络模式限制 | ◐ 受 Desktop 网络限制 | ● Internal + 自建网关 | ✕ 无此控件 |
| DNS/UDP/QUIC 坑 | 有，需策略 | 有 | 有 | 有 |
| 产品化「控件改代理地址」 | 中：写配置+重载服务 | 中高：API 熟 | 中：要编排交换机/网关 | 低 |

**排序（硬强制从易到难，产品视角）**：  
**Hyper-V Internal 网 + 网关 ≥ 容器 netns 透明代理 > WSL2 发行版内透明代理 ≫ Sandbox。**

WSL 仍推荐为 MVP，是因为：**GUI + 启动成本 + 可替换性**综合最优；网关在发行版内做到「默认拒绝直连」对 v0.1 够用，更强隔离留给 Hyper-V 阶段。

---

## 4. 推荐架构（MVP）

```
[Claude Box 控件 · Win11]
    │ 启停 / 可见目录 / 代理(HTTP|SOCKS5) / 身份预设
    ▼
[WSL2 专用发行版「claude-box」]
    ├─ Linux Claude Desktop  ← WSLg 原生窗口
    ├─ gateway 服务（透明代理 → 控件配置的上游）
    ├─ 默认：DROP 非代理 TCP；DNS 策略显式
    └─ 仅绑定可见目录；独立 TZ/locale/hostname/machine-id
```

**明确不绑**：Docker Engine、Docker Desktop、compose 文件（可后加）。

**后期替换示例**：同一 gateway 配置 schema + 同一「可见目录列表」→ 启动目标改为 Hyper-V Linux VM（或 Win VM 若走 exe 分叉）。

### 与和零此前想法的对齐

| 想法 | 本轮结论 |
|---|---|
| 前期借 WSL，后期自建/替换 | **可行且推荐**；先验证盒子语义，再换 Hyper-V |
| 能否用 exe | **能，但换栈**：Hyper-V Win 访客；WSL/Docker 跑不了 exe |
| Docker-like 可放弃 | **已放弃作为根基**；OCI 仅可选 |

---

## 5. 风险清单

| ID | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R1 | Linux Claude 缺 Computer use 等 | 期望落差 | 产品说明；必要则切 Hyper-V Win |
| R2 | WSL 内 Cowork/KVM 失败 | Cowork 不可用 | MVP 标明；或迁 Hyper-V Linux（嵌套仍待验证） |
| R3 | WSL 透明代理在 mirrored 下不稳定 | 网关泄漏/断网 | 钉死 NAT；抓包；Hyper-V 升级路径 |
| R4 | Electron 在 WSLg 白屏/光标/GPU | UX 差 | 社区 flag（ozone/wayland/disable-gpu）待验证 |
| R5 | 仅环境变量被误当成「唯一网关」 | 假安全 | 验收必须「直连失败」 |
| R6 | 挂载过宽泄漏宿主身份文件 | 伪装失败 | 黑名单；禁止挂 `C:\Windows`、`/etc` |
| R7 | Hyper-V Win 路线过重 | 控件与资源 | 仅在必须 exe 时启用 |
| R8 | macOS 与 WSLg 不对等 | 次平台体验 | Win 验收为准；Mac 第二阶段 |
| R9 | 上游改版 / 反虚拟环境 | 登录异常 | 版本钉扎；一致假环境 |

---

## 6. 最小验证清单（Win11 · 5–8 条）

建议在和零机器上按序做；结果另存 `docs/research-v1-experiments.md`（非本次）。

1. **V1 · WSL GUI 基线**  
   干净 Ubuntu 24.04/22.04 或 Debian 12 WSL2；装官方 `claude-desktop`；确认 WSLg 窗口可登录对话。记录 keyring / GPU 相关问题。

2. **V2 · 功能缺口确认**  
   核对 Computer use / Dictation / Cowork 提示（KVM？）。决定是否接受 Linux 子集。

3. **V3 · 目录隔离**  
   仅绑定一个项目目录；确认读不到未挂的 `C:\` 用户文件与 `C:\Windows`。

4. **V4 · 环境伪装**  
   发行版 `TZ`/`LANG`/hostname/machine-id 与宿主不同；容器内（发行版内）脚本打印 `date`、`Intl` 时区；确认未挂宿主 localtime。

5. **V5 · 唯一网关（硬）**  
   发行版内透明代理指到测试 SOCKS5/HTTP；`curl` 出口 IP = 代理出口；关掉代理后 **直连应失败**。再开 Claude，列连接主机；可选拦 Sentry/Datadog 看聊天是否仍可用。

6. **V6 · 网络模式矩阵（简）**  
   各测一次 NAT vs `networkingMode=mirrored` 下 V5 是否仍成立；记下可产品化的默认模式。

7. **V7 ·（可选）Sandbox 冒烟**  
   `.wsb` + MappedFolders 装 Win Claude：只验证 exe GUI；确认无法用 `.wsb` 配 SOCKS——反证「Sandbox≠产品运行时」。

8. **V8 ·（仅当 V2 不可接受）Hyper-V Win 访客尖兵**  
   一代 Windows 访客装 Claude；Internal 交换机 + 简单强制代理；评估启动时间与 Enhanced Session 观感是否可接受。

**建议验证栈（默认）**：V1→V6 在 **WSL2 专用发行版**；V7 可选；V8 仅 exe 分叉。

---

## 7. 与 v0 的差异（给后续 ADR）

| 项 | v0 | v1（本轮） |
|---|---|---|
| 主运行时 | Docker Desktop + Linux Claude | **WSL2 发行版 + Linux Claude** |
| Docker | 主路径 | 可选打包，非根基 |
| exe | 不优先 | **分叉**：必须则 Hyper-V Win，非 Sandbox |
| 唯一网关最硬处 | 容器 sidecar | **Hyper-V 交换机**最硬；MVP 用 WSL 内透明代理 |
| Docker-like | 近硬约束 | **已取消** |

---

## 8. 参考链接（抽样）

- 范围：`SCOPE.md`；前序：`docs/research-v0.md`
- Claude Desktop 安装与 Linux 限制：https://support.claude.com/en/articles/10065433-install-claude-desktop
- Claude Desktop Linux / Cowork·KVM：https://code.claude.com/docs/en/desktop-linux
- WSLg GUI：https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps
- WSL 网络 / mirrored / autoProxy：https://learn.microsoft.com/en-us/windows/wsl/networking
- Windows Sandbox `.wsb`：https://learn.microsoft.com/en-us/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-configure-using-wsb-file
- Hyper-V NAT：https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/setup-nat-network
- 遥测与 egress（3P，消费级字段待验证）：https://claude.com/docs/third-party/claude-desktop/telemetry
- tun2socks：https://github.com/xjasonlyu/tun2socks
- 透明代理 sidecar 模式（容器可选层）：https://github.com/mietzen/docker-redsocks-proxy

---

## 9. 一句话给和零

**最合适的「容器」不是 Docker，而是「控件编排的隔离访客」：Win11 上先用 WSL2 专用发行版把盒子语义跑通；要完整 exe 能力再上 Hyper-V Windows 访客；Docker 只是可选包装纸。**
