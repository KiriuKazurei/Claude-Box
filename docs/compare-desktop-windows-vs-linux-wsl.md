# Claude Desktop：Windows vs Linux（WSL）对比

> 调研日：2026-11-02（Asia/Tokyo）  
> 面向：Claude Box 选型（Win11 家庭版 · 要 Desktop 非 Code CLI · Computer use 已降权）  
> 完整交互版 Page：[Claude Desktop：Windows vs Linux（WSL）对比](https://cursor.com/canvas/store-01655bd8-6b09-4124-a7fb-409efbba2fd1/3210d6b6-96c4-4111-a60d-bc50a22fdd6a)

## 结论

若暂时放弃 Computer use、仍要 Claude Desktop 原生窗口：在 Win11 家庭版上，**B（WSL2 内装 Linux Claude Desktop）比 A（自建 Windows 容器/沙箱跑 Windows Desktop）更适合当验证路径**——隔离、TZ/LANG、网关、目录可见性都更容易落地。

但官方并不支持「在 WSL 里装 Linux Desktop」；官方 WSL 路径是「装 Windows Desktop，Code 会话进 WSL」。B 的硬风险是 Linux Cowork 依赖 QEMU/KVM（WSL 里要嵌套虚拟化 + `/dev/kvm` + `vhost_vsock`），以及默认 `/mnt/c` 暴露宿主机。

**Computer use**：官方明确 macOS/Windows 有，Linux 暂无（coming soon）。

## 平台与安装

| 平台 | 包格式 | 系统要求 | 更新 | 官方对 WSL |
|------|--------|----------|------|------------|
| Windows | 主流 `.msix`（Cowork 需要）；旧 `.exe` 无 Cowork | Windows 10+ | 应用内 / MSIX | 推荐装 Windows Desktop，Code 会话进 WSL |
| macOS | `.dmg` / 企业 PKG | macOS 13+ | 应用内 | — |
| Linux (beta) | apt 或 `.deb`（x64/arm64） | Ubuntu 22.04+ / Debian 12+ | `apt`（无应用内自更新） | 文档写明：WSL2 上应装 Windows 桌面版 |

## 能力对照（要点）

- Chat / Code / Cowork：三端 Desktop 均有（Linux Cowork 需 QEMU+KVM）
- Computer use：仅 macOS/Windows；Linux 官方无
- Dictation：Linux 无
- Quick Entry：Linux 上 X11 可用；Wayland 依赖 GlobalShortcuts portal
- Windows Cowork 本地沙箱：Hyper-V；Linux：QEMU/KVM
- Win11 Home：Windows Cowork/Hyper-V 有社区失败报告，需真机核实

## 硬约束拟合

| 约束 | 更易方案 | 说明 |
|------|----------|------|
| 隔离跑 Claude | B | WSL2 自带 VM 边界 |
| 原生窗口 | A 更纯 / B 可用 | B 靠 WSLg |
| 唯一网关 | B | Clash Allow LAN / mirrored 成熟 |
| 目录可见性 | 双方都要做 | B 默认 `/mnt/c` 更危险，必须切断 |
| TZ/语言伪装 | B | `TZ`/`LANG`/`/etc/localtime` |
| 反遥测 | B 略简 | 仍是 Electron |
| Computer use | 仅 A | Linux 官方无 |

## 推荐

- **短期验证**：B · WSL2 + Linux Claude Desktop
- **主工程后备**：A · 自建 Windows 沙箱 + Windows Desktop（若要 CU，或 B 的 KVM/目录隔离翻车）
- **不做主推**：仅 Code CLI；以及「Windows Desktop 留宿主机 + WSL Code」（隔离不合格）

### 真机优先核实

1. WSLg 下 Linux Desktop 能否稳定登录与 Chat  
2. Cowork 嵌套 KVM 是否可用  
3. 切断 `/mnt/c` 后网关与 TZ 伪装是否满足硬约束  

## 主要来源

- https://claude.com/download/
- https://support.claude.com/en/articles/10065433-install-claude-desktop
- https://code.claude.com/docs/en/desktop-linux
- https://code.claude.com/docs/en/desktop-wsl
- https://support.claude.com/en/articles/14128542-let-claude-use-your-computer-in-cowork
- https://support.claude.com/en/articles/14479288-claude-cowork-architecture-overview
- https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps
