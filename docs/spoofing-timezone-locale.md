# 时区 / 系统语言伪装策略

> 决策日：2026-11-02（Asia/Tokyo）  
> 决策者：和零  
> 适用：Claude Desktop（Windows / Linux；短期验证可走 WSL2 + Linux Desktop）  
> 证据：`docs/analysis-claude-linux-telemetry.md`、Windows 解包检索（`Ph`/`LTt`/`W_a` 等）、`docs/compare-desktop-windows-vs-linux-wsl.md`

## 决策（已定）

对 Claude Desktop **内部读到的**时区与系统语言：

| 优先级 | 策略 | 说明 |
|--------|------|------|
| **主路径（优先）** | **不改程序** | 启动前用进程环境与系统区文件伪装，让 Electron / ICU / `Intl` 自然读到假值 |
| **备用** | 改 asar 或 hook API | 仅当环境伪装不足以挡住某条钩子（或 Chromium/ICU 仍泄漏）时使用 |

网关侧拦 Sentry / Datadog RUM / `event_logging` 仍是独立的反遥测手段，与「值伪装」互补：伪装降低外泄内容；拦截降低外泄通道。

## 主路径：环境伪装（不改 Desktop）

启动 Claude Desktop（及同 job / 同 WSL 会话内子进程）前设定：

| 手段 | 典型用法 | 挡住的主要钩子 |
|------|----------|----------------|
| `TZ` | 如 `America/Los_Angeles` | `Intl.DateTimeFormat().resolvedOptions().timeZone`；`CRON_TZ=`；注入子进程的 `TZ`；提示词 `{{currentTimezone}}`；Sentry culture 的 timezone |
| `/etc/localtime`（及必要时 zoneinfo） | 与目标 IANA 一致的符号链接 / 拷贝 | ICU / Chromium 在忽略 `TZ` 时仍可能读系统时区文件（Linux） |
| `LANG` / `LC_*` / `LANGUAGE` | 如 `en_US.UTF-8` | Electron `app.getPreferredSystemLanguages()` / `getLocale()`；Sentry culture 的 locale；界面语言包选择 |
| 可选 `--lang=` | Electron/Chromium 启动参数 | 辅助界面语言 |

Windows 自建容器路径上，同等语义是：容器用户环境下的 **时区/区域注册表或进程环境** + 配置里的 `identity.timezone` / `identity.locale`（见 `config/config.example.yaml`），**仍优先不 patch asar**。C2 的 MinHook 若只为 TZ/locale，应降级为备用；hostname / MachineGuid / 卷序列号等非 Intl 字段可继续走 hook。

### 验收（主路径）

在伪装环境下启动 Desktop 后，用探针或日志确认：

1. `Intl.DateTimeFormat().resolvedOptions().timeZone` = 配置假 IANA  
2. 界面 / `app.getLocale()`（或等价）= 配置假 locale  
3. 若抓到 Sentry 事件：`contexts.culture` 与假值一致（或该通道已被网关拦截）

## 备用：改程序 / hook

仅在主路径不足时启用：

1. **asar 定点改写**（升级需重打补丁；可能影响签名/自动更新）  
   - Linux：`lg()`（Sentry culture）、`bCt()` / `W3r()`（语言 / 布局）  
   - Windows：`Ph()`、`LTt()` / `m5r()` 等同角色函数  
   - 或写死 `resolvedOptions().timeZone` / 相关调用点  
2. **preload / MinHook**  
   - JS：`Intl.DateTimeFormat.prototype.resolvedOptions`、`Date.prototype.getTimezoneOffset`  
   - Electron：`getPreferredSystemLanguages` / `getLocale`  
   - Windows 原生：`GetTimeZoneInformation` 等（仅当 JS 全挡仍泄漏）

## 与运行时选型的关系

| 路径 | 主路径伪装落地 | 备注 |
|------|----------------|------|
| **短期验证：WSL2 + Linux Desktop** | `TZ`/`LANG`/`/etc/localtime` 最省事 | 官方不支持「WSL 内装 Linux Desktop」；切断默认 `/mnt/c`；见对比文档 |
| **主工程后备：自建 Windows 容器** | 容器环境 + `identity.*`；hook 留给非 Intl 指纹及备用 | C2 勿默认「必须 patch Claude asar」 |
| Hyper-V 访客（已弃用） | 访客系统盘独立 TZ/locale | 仅历史参考 |

## 明确不做（本决策范围外）

- 不把「改 asar」写成默认交付  
- 不承诺服务端根据 `Timezone:` 提示词字段改模型行为也能被客户端伪装单独消除（需网关/提示词策略另议）  
- 中国区特殊引号指纹：当前 4.4212.0 客户端静态分析未检出；见 `docs/analysis-china-utc8-quote-fingerprint.md`
