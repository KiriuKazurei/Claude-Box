# 调研笔记 · Claude Cage（Lee-xeo）

> 查阅日：2026-11-11（Asia/Tokyo）  
> 来源：[项目主页](https://lee-xeo.github.io/Claude-Cage/) · [GitHub: Lee-xeo/Claude-Cage](https://github.com/Lee-xeo/Claude-Cage)（当时 `0.1.7-rc.1`，MIT；基于 [agent-vm](https://github.com/Lee-xeo/Claude-Cage) / [Lima](https://github.com/lima-vm/lima) 二次开发）  
> 本地只读检出：云电脑 `/home/box/projects/research/Claude-Cage`（**不**并进本仓库）  
> 目的：看「低成本固定化环境」怎么实现，对照 Claude Box 两条思路。本笔记只记隔离、网络、伪装与平台限制，不把它当成账号策略保证。

## 它是什么

开箱即用的桌面工作台：把终端、Cage 浏览器、编辑器和官方 Claude Desktop 放进 **独立客机**。Cage 接管客机全部出网（随附 Mihomo），时区/语言在客机内设置，可选跟随代理出口定位。检查失败或证据不完整时 **暂停业务外网**，**没有直连回退**。项目文件夹按需共享；主机 Claude 登录、浏览器资料和 SSH 密钥不会自动导入。

他们自己也写明：网络检查只展示当前配置和采样证据，**不能保证**账号不受平台限制，也不能代替完整虚拟化安全审计。

## 实现骨架

| 层 | 做法 |
|----|------|
| 工作台 | Go 后端 + 桌面 UI（WebView2 / 原生壳）；安装包自带 QEMU、OpenSSH、Mihomo，不必另装 Docker / Clash / Node |
| 本机客机 | **Lima 管理虚拟机**。Mac：Virtualization.framework（macOS 或 Linux 客机）。Windows：Linux 客机走 **QEMU + Windows 虚拟机监控程序平台（WHPX）**；Windows 11 客机走 **完整 Hyper-V**。Linux 主机：KVM |
| 云端 | SSH 到用户自己的 Linux VPS，用 **容器** 跑环境，服务器不必有 KVM |
| 网络 | 客机无常规直连网卡（Windows 客机「零常规 Hyper-V 网卡」）；流量进 Cage 网关。可导入 Clash/Mihomo 订阅或手填 HTTP/SOCKS5，支持链式代理。不改主机系统代理 / TUN 来「带客机上网」 |
| 隔离检查 | 启动后先关业务外网，再查出口 IPv4、DNS（禁止直连公共 DNS/DoT）、IPv6（全局地址应为 0）、宿主管理接口、共享目录等；任一项失败或证据缺失则保持离线 |
| 时区 / 语言 | 只改 **客机**：`guest-env.sh` 写 `/etc/localtime`、`/etc/timezone`、`TZ`/`LANG`/`LC_ALL`。可跟随出口 IP（`ipwho.is`）或手动设；不用主机位置替代 |
| 浏览器 / OA | 客机内 **Chrome for Testing**（经 noVNC 嵌进工作台）。Desktop 登录和外部授权页在 Cage 浏览器打开，回调后再回 Desktop。不走宿主默认浏览器 |
| Desktop | 按客机系统装官方应用：Linux 客机用 Linux Desktop，Windows 客机用 Windows Desktop；资料留在客机磁盘 |
| 多账号 | Claude Code 用独立配置目录调度；网页 / Desktop 登录另存，不自动并进 Code 调度 |

仓库里的 Lima WSL2 driver 只是上游注册残留。文档写明：Windows 上的 Debian/Ubuntu 路线 **不依赖 WSL 或 Docker**。

## 和零这台 Win11 家庭版意味着什么

- **不能**走 Cage 的 Windows 11 客机（要专业版/企业版/教育版的完整 Hyper-V）。
- **可以**走它的低成本路径：在家庭版上启用「Windows 虚拟机监控程序平台」，用 QEMU 跑 Debian 13 或 Ubuntu 22.04，再在客机里装 Linux Claude Desktop。
- 镜像大约几百 MiB（Ubuntu 云镜像约 0.7 GiB 量级），虚拟磁盘按写入增长，创建前需预留十几 GiB 以上。

这和 Claude Box「弃用 Hyper-V、宿主时区不动」是对得上的：固定环境在客机里，不必上完整 Hyper-V。

## 对照 Claude Box

| | Claude Cage | Claude Box（当前口径） |
|--|-------------|------------------------|
| 形态 | 完整产品：工作台 + 真虚拟机 / 云容器 | 两条思路并行：A WSL2、B 不依赖 WSL 的 Windows 盒子；都还在搭框架 |
| 家庭版 Win11 | Linux 客机用 WHPX+QEMU | 思路 A 试 WSL；思路 B 为 `cbox` / 专属目录，尚未用 WHPX 虚机 |
| 唯一网关 | 自带 Mihomo，失败则断外网 | 计划经 gost / 宿主 Clash；尚无「检查失败就断网」的完整闸门 |
| 时区语言 | 只改客机，可跟出口 IP | 只在盒子/进程内伪装，**不改宿主**；未做「自动跟出口」 |
| OA 浏览器 | 客机内 Chrome for Testing | 已规划伴生指纹浏览器，尚未接线 |
| 身份 | 客机磁盘 + Code 账号调度 | 已规划盒子内保险库，尚未实现 |
| 共享目录 | 明确打开的主机文件夹，改动直接写回原文件 | `visible_dirs`，切断默认整盘 |

可参考、且不违背「尽量不改宿主」的点：

1. 家庭版用 **WHPX + QEMU 跑 Linux 客机**，作为思路 B 之外、或与 WSL 并列的「真固定环境」候选（仍要和零批准再动机器）。
2. **失败关闭外网、禁止直连回退**，比只设代理环境变量更硬。
3. **OA / Desktop 登录关在客机浏览器里**，与已定的伴生指纹浏览器目标一致。
4. 时区语言只落到客机脚本（与我们的 `apply-spoof` 同族）；「跟随出口」可作可选项，不必默认改宿主。

不必整盘搬来的：手机遥控、Tailscale 工作台、把 Cage 账号体系和云端容器当主路径。也不要把他们的检测页当成「过审保证」。

## 和本仓库的关系

只作调研对照，**没有**把 Cage 源码合并进 Claude-Box，也 **没有** 在和零电脑上安装 Cage。若要在真机试用官方安装包，需另行明确批准。
