# 伴生指纹浏览器 · OAuth（OA）跳转不走宿主默认浏览器

> 决策日：2026-11-11（Asia/Tokyo）  
> 决策者：和零（看过 [liseami/CCBaga](https://github.com/liseami/CCBaga) 后，只采纳这一条）  
> 状态：**产品策略，规划实现**（W6）；与 [identity-vault.md](./identity-vault.md) 配套。不改宿主时区 / 语言 / 默认浏览器。

## 只学这一条

CCBaga 里有用、要跟的，只有 **伴生指纹浏览器**（它那边是独立资料目录的 CCBaga-B / Fingerprint Chromium）。

**明确不跟：**

- 直接改宿主时区、语言、地区
- 独立 Network Skill 去配 VPS / 家宽

身份保险库 **要学**，但只放在盒子专属目录，见 [identity-vault.md](./identity-vault.md)。

宿主 Win11 仍保持东八区 + 简中；伪装只在盒子 / 伴生浏览器进程内做。

## 问题

Claude Desktop 登录或续权时，常会 **打开系统默认浏览器** 去走 OAuth（OA）授权页。若落到宿主 Chrome / Edge：

- 带上宿主已有 Cookie、登录态、扩展、书签与历史
- 浏览器指纹（时区、语言、字体、WebGL、CPU 等）暴露真机，与盒子内伪装不一致
- 授权域与日常浏览资料混在同一 profile

这是 **浏览器侧泄漏**，网关拦遥测、进程内 `TZ`/`LANG` 都挡不住。

## 策略（Claude Box）

| 项 | 约定 |
|----|------|
| 触发 | Claude（Desktop 或相关进程）**跳转 OAuth / 授权页**（典型：`claude.ai`、`console.anthropic.com` 等登录 / `authorize` / `callback` URL） |
| 动作 | 用 **独立伴生指纹浏览器** 打开该 URL，**禁止**落到宿主默认浏览器 |
| 资料 | 独立 `user-data-dir`（与宿主 Chrome/Edge、也与 Claude Electron userData 分开） |
| 环境 | 语言 / 时区与盒子 `identity` 对齐（如 `America/Los_Angeles` + `en-US`）；**不改宿主** |
| 网络 | 仍走 **唯一网关**（HTTP/SOCKS5）；不另开一条绕过盒子的出口 |
| 开关 | `features.oauth_open_in_fingerprint_browser`（**默认 `true`**，意图即如此；未接好拦截前不得假装已生效） |

绿色核验、指纹轮换可以后做；本决策的硬目标是：**OA 不走宿主默认浏览器，避免从浏览器侧泄漏。** 不宣称匿名或账号一定过审。

## 实现落点（尚未接线）

拦截优先于「请用户自己改默认浏览器」：

1. 发行版内接管 `xdg-open` / `BROWSER` / `www-browser`，OAuth 类 URL 交给伴生浏览器；其它 URL 策略另议（默认同样不落到宿主）。
2. 若 Desktop 用 Electron `shell.openExternal` 直达宿主，再考虑发行版内包装或备用 hook（主路径仍是不改程序）。
3. 引擎候选：独立 Chromium + 专用 profile；可参考 [Fingerprint Chromium](https://github.com/adryfish/fingerprint-chromium)（CCBaga-B 所用），**不**自动下载、不把引擎提交进 Git。
4. 桩：`src/wsl-box/companion-browser/open-oauth-url.sh`。未安装引擎时清晰失败，不回退到宿主浏览器。

## 验收（W6）

- 人为或脚本触发一次「打开 OA URL」：进程是伴生浏览器，**不是**宿主 `chrome.exe` / `msedge.exe`。
- 伴生 profile 目录与宿主浏览器、Claude userData 分离。
- 伴生进程可见的时区 / 语言与盒子 `identity` 一致；宿主显示仍是东八区 / 简中。
- 该浏览流量出现在网关 JSONL；关网关则打不开授权页（或明确失败）。
- 开关为 `true` 但引擎未就绪：报错并拒绝打开，**绝不**静默改用宿主默认浏览器。

## 与 CCBaga 的边界

| | CCBaga | Claude Box |
|--|--------|------------|
| 伴生浏览器用途 | 核验环境、换指纹、跟身份绑定 | **专管 Claude 跳出的 OA / 授权浏览**，防宿主浏览器泄漏 |
| 系统时区语言 | 可改本机设置 | **不改宿主** |
| 身份快照 | 加密保险库切换本机默认 Claude 目录 | **要学**，但只管理盒子内资料，不改宿主默认目录；见 [identity-vault.md](./identity-vault.md) |
