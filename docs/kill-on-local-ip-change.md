# 可选策略 · 本机 IP 波动时断开网关并结束 Claude（已知有效 · 默认关 · 唯一网关模块）

> 决策日：2026-11-02（Asia/Tokyo）；2026-11-12 补充动作与模块归属  
> 决策者：和零（回复此前 [claude-switcher](https://github.com/YOURSJEYO/claude-switcher) 笔记；2026-11-12 明确：与唯一网关同一大模块，IP 变化时断开链接与结束进程同时启动）  
> 状态：**可选开关，默认 OFF**；明确记为已知有效方案，非 MVP 必开项。归属 **唯一网关模块**（P6 家族），不是 Desktop 启停（P10）的附属功能。

## 问题

本机出口 / 局域网 IP 抖动（Wi‑Fi 重连、切换网卡、DHCP 续约、VPN 启停等）时：

- 网关上游（Clash 等）可能短暂掉线或换出口；
- Claude Desktop 仍可能带着旧会话 / 半开连接继续跑，出现「代理已死、进程还活」的窗口。

[claude-switcher](https://github.com/YOURSJEYO/claude-switcher) 用「代理掉线 → 杀 Claude」证明：**轮询检测 + 快速杀进程**在真实使用里有效；其启停节奏（先礼貌关窗口，超时再强杀）避免拷坏 Electron 数据目录。

## 策略（Claude Box）

| 项 | 约定 |
|----|------|
| 模块 | **唯一网关模块**。设置页的开关与选项在「网关」页，不把本策略当成单独的功能边角。 |
| 触发 | **宿主本机 IP 集合发生变化**（非仅公网 IP；优先看活动网卡 IPv4/IPv6 地址集合的 diff；不含回环） |
| 立即并行动作 | IP 集合一变，**断开唯一网关** 与 **开始礼貌关闭盒子内 Claude 进程** 作为两条线程**同时启动**，互不等待。不要写成「先断开网关，再礼貌关」，也不要等网关停完才开始关进程，或等礼貌关结束才断网关。 |
| 进程节奏 | **学 claude-switcher**：① 礼貌关闭（发关闭信号 / 关主窗口）→ ② 等待短超时（默认 10s，`kill_grace_seconds`）→ ③ 仍存活则强杀。这段 grace **只约束进程**，**不**推迟网关断开。 |
| 开关 | 配置项 `features.kill_on_local_ip_change`（或等价字段），**默认 `false`** |
| 作用域 | 只动 box 内 / 由 box 启动的 Claude（`claude-desktop` / `claude` / `Claude`）；**不**改宿主全局时区/语言；**不**结束机器上的其他进程 |

两条最有效的手段——**丢掉链接**和**结束进程**——要尽快一起生效。本策略仍是出口身份突变时的快速止血，不能取代「所有出网必须经唯一网关」这条硬约束本身。

## 配置

见 `src/wsl-box/config/wsl-box.example.yaml`：

```yaml
features:
  kill_on_local_ip_change: false   # 默认关；true 时 IP 变化则并行启动：断开网关 + 礼貌关进程
  kill_grace_seconds: 10           # 仅用于进程；不断开网关不为此等待
  watch_interval_seconds: 5
```

进程名范围：`claude-desktop`、`claude`、`Claude`。

## 实现落点

| 轨 | 落点 |
|----|------|
| C++ 核心 / 设置页 | `claudebox/gateway_ip.hpp`：可注入上一组 / 当前地址集合做 dry-run 计划；与设置页「网关」栏共用。云电脑上不读真网卡，不启动/停止活的 gost，不杀真实进程。 |
| WSL 主轨 | 宿主 PowerShell 监视器：`src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1`。武装后，IP 变化则立刻并行启动 `scripts/win11/Stop-ClaudeBoxGateway.ps1`（或 `Stop-ClaudeBoxGateway`）与发行版内礼貌关；grace 后再强杀。默认关时退出码 0 且不行动。不安装 Windows 服务。 |
| `cbox` 后备 | 对 job object 整树关闭（kill-on-close 已有则优先 `TerminateJobObject`）；节奏仍建议先尝试优雅退出，并同时断开网关 |

## 验收（打开开关后）

1. 默认关：改宿主 IP 或禁用网卡，本策略 **不**断开网关、也 **不**杀 Claude。  
2. 打开后：本机地址集合变化 → 网关断开与礼貌关进程同时开始；卡死的进程在 grace 后被强杀。网关断开不排在 grace 之后。  
3. 不误杀宿主其它无关进程；不改宿主时区/语言。  
4. 云电脑 dry-run：注入地址集合即可得到上述并行动作计划，不得宣称已经在 Win11 上停过 gost 或杀过真进程。

## 相关

- 架构：[architecture-v2-wsl.md](./architecture-v2-wsl.md)「网关与可选止血」  
- 伪装边界：[spoofing-timezone-locale.md](./spoofing-timezone-locale.md)（伪装在盒子内；本策略不碰宿主区域设置）  
- SCOPE v0.4.1：可选策略，默认关，归属唯一网关模块  
