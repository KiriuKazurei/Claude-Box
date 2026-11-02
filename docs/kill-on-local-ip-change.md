# 可选策略 · 本机 IP 波动时杀掉 Claude（已知有效 · 默认关）

> 决策日：2026-11-02（Asia/Tokyo）  
> 决策者：和零（回复此前 [claude-switcher](https://github.com/YOURSJEYO/claude-switcher) 笔记）  
> 状态：**可选开关，默认 OFF**；明确记为已知有效方案，非 MVP 必开项。

## 问题

本机出口 / 局域网 IP 抖动（Wi‑Fi 重连、切换网卡、DHCP 续约、VPN 启停等）时：

- 网关上游（Clash 等）可能短暂掉线或换出口；
- Claude Desktop 仍可能带着旧会话 / 半开连接继续跑，出现「代理已死、进程还活」的窗口。

[claude-switcher](https://github.com/YOURSJEYO/claude-switcher) 用「代理掉线 → 杀 Claude」证明：**轮询检测 + 快速杀进程**在真实使用里有效；其启停节奏（先礼貌关窗口，超时再强杀）避免拷坏 Electron 数据目录。

## 策略（Claude Box）

| 项 | 约定 |
|----|------|
| 触发 | **宿主本机 IP 集合发生变化**（非仅公网 IP；优先看活动网卡 IPv4/IPv6 地址集合的 diff） |
| 动作 | 快速结束 **本 box 相关** 的全部 Claude 进程（WSL 轨：发行版内 `claude*` / Electron；`cbox` 后备：job 内整树） |
| 杀进程节奏 | **学 claude-switcher**：① 礼貌关闭（发关闭信号 / 关主窗口）→ ② 等待短超时（如 10s）→ ③ 仍存活则强杀 |
| 开关 | 配置项 `features.kill_on_local_ip_change`（或等价字段），**默认 `false`** |
| 作用域 | 只动 box 内 / 由 box 启动的 Claude；**不**改宿主全局时区/语言（与伪装文档一致） |

本策略 **不能替代** 唯一网关 / WFP 默认拒绝；它是网关掉线或出口身份突变时的 **快速止血**，与「代理掉线杀进程」同族。

## 配置桩

见 `src/wsl-box/config/wsl-box.example.yaml`：

```yaml
features:
  kill_on_local_ip_change: false   # 默认关；true 时宿主监视本机 IP，变化则礼貌关→强杀 Claude
```

可选细化（实现阶段再加，不必现在写死）：

- `kill_grace_seconds: 10`
- `watch_interval_seconds: 5`
- `match_process_names: [claude-desktop, claude, Claude]`

## 实现落点（尚未编码完整监视器）

| 轨 | 落点 |
|----|------|
| WSL 主轨 | 宿主 PowerShell 监视器（桩：`src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1`）；经 `wsl -d <distro> --` 礼貌信号 + `pkill`/`kill -9`；亦可在宿主杀 WSLg 相关树 |
| `cbox` 后备 | 对 job object 整树关闭（kill-on-close 已有则优先 `TerminateJobObject`）；节奏仍建议先尝试优雅退出 |
| 网关侧 | 可选：上游探测失败时复用同一杀进程入口（与 IP 波动并列触发，仍受同一开关或独立开关约束——**当前仅实现「IP 波动」开关**） |

## 验收（打开开关后）

1. 默认关：改宿主 IP 或禁用网卡，Claude **不被**本策略杀掉。  
2. 打开后：人为改 DHCP / 禁用再启用网卡导致本机地址集合变化 → 在 grace 内进程退出；卡死则强杀。  
3. 不误杀宿主其它无关进程；不改宿主时区/语言。

## 相关

- 架构：[architecture-v2-wsl.md](./architecture-v2-wsl.md)「网关与可选止血」  
- 伪装边界：[spoofing-timezone-locale.md](./spoofing-timezone-locale.md)（伪装在盒子内；杀进程不碰宿主区域设置）  
- SCOPE：可选策略，默认关  
