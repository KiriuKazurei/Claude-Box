# 接近 beta · 云电脑 dry-run 验收清单

> 日期：2026-11-12（Asia/Tokyo）  
> 范围：P1–P10 已在云电脑落地并通过总门禁；**尚未**做真机 W1（启用 WSL、活的 gost / Desktop / OA）。  
> 配套：[framework-build-plan.md](./framework-build-plan.md)、[SCOPE.md](../SCOPE.md) v0.4.1  
> 总门禁：`bash tests/claudebox/test-p11-near-beta-gate.sh`

本清单把「这台 Debian 云电脑上已经证明的事」和「必须放到 Win11 真机上才能证明的事」分开写。缺真机的项**不**标成功。

## 怎么跑

在云电脑仓库根目录（只本地提交，不 push，不启用真机 WSL）：

```bash
bash tests/claudebox/test-p11-near-beta-gate.sh
```

全绿时只打印一行：

```
ALL GREEN (P11 near-beta dry-run integration: P1–P10 proven on this cloud computer; real-machine W1 not started)
```

## 已在云电脑证明（P1–P10）

| 模块 | 已证明的行为 | 门禁 / 测试 |
|------|----------------|--------------|
| **P1 身份策略引擎** | 默认策略下，夹具 Contoso 厂商和型号进入展示层；序列号、UUID、MAC 等硬件编码被隐藏。可按字段改为展示或修改后再展示。 | `test-p1-gate.sh`、`identity_engine` |
| **P2 采集器** | 从夹具树写出完整原始清单（可含编码），只放在盒子前缀 `var/host-inventory.raw.yaml`；采集不会生成展示层。 | `test-p1-p2-gate.sh`、`identity_collect` |
| **P3 展示层接线** | `apply-host-identity` / 校验走 C++ 引擎；采集仍只写原始清单。隐藏 / 修改后展示 / 选择展示用例通过。`hide_identifiers` 仍是过渡开关，关掉不会一键放开整份清单。既有 linux framework smoke 仍绿。 | `test-p1-p2-p3-gate.sh`、`test-linux-framework.sh` |
| **P4 设置页控件壳** | C++20 + Qt 6 设置页可编译；无头测试可编辑全部 `wsl-box.yaml` 段与每字段身份策略并保存再读回。不改宿主时区 / 语言 / 默认浏览器。 | `test-p4-gate.sh`、`settings_ui` |
| **P5 运行时与可见目录** | 挂载计划只挂 `visible_dirs`，拒绝整挂 `C:`、整棵 `C:\Users` 或 `/mnt/c`；拒绝时不写坏配置。发行版生命周期是 dry-run，不调用 `wsl.exe`。 | `test-p4-p5-gate.sh`、`mount_plan` |
| **P6 唯一网关** | 可读写上游 HTTP/SOCKS5 与日志路径；无上游或地址不合法时不保存。计划含监听、`gost -C` YAML 中继（bypass + hopGroup PathRegexp 编码默认 blocklist：Sentry/Datadog 主机与 `/api/event_logging`、`/api/v2/rum`），不整段拦 `api.anthropic.com` 对话接口。HTTPS CONNECT 路径残留（无 TLS MITM）须如实记录。JSONL 可读。不在云电脑启动生产 gost / 不改系统代理。既有 gateway smoke 仍绿。 | `test-p6-gate.sh`、`gateway_plan` |
| **P7 时区 / 语言伪装** | 设置页展示并保存的是盒子伪装值。应用到前缀 `env/spoof.env`、`env/session.env`、前缀内 `etc/localtime`。Intl 探针看到配置中的假时区（如 `America/Los_Angeles`），不是云电脑的 `Asia/Tokyo`。不改宿主 `/etc/localtime`。 | `test-p7-gate.sh`、`spoof_plan` |
| **IP 波动（网关模块）** | 开关默认关，在网关页。打开后，地址集合变化时断开网关与礼貌关进程作为两条线**同时**启动，互不等待；grace 后再强杀。只做 dry-run，不读真网卡，不停活 gost，不杀真实进程。 | `test-p7-ip-gate.sh`、`gateway_ip_change` |
| **P8 伴生指纹浏览器** | 打开夹具 OA URL 走伴生路径，不回退宿主默认浏览器。引擎未就绪时非零退出并报错。资料目录在盒子前缀下，与宿主 Chrome/Edge、Claude userData 分离。计划环境含盒子伪装时区 / 语言和唯一网关代理。不自动下载 Fingerprint Chromium。设置页显示引擎、资料目录和代理。 | `test-p8-gate.sh`、`companion_browser` |
| **P9 身份保险库** | 可归档夹具身份、切到空工作区再切回；强制失败时工作副本与当前指针不切走。只写盒子前缀。加密为 AES-256-GCM + PBKDF2-HMAC-SHA256（OpenSSL 3），不必与 CCBaga `.baga` 兼容。口令来自文件或环境变量，不出现在状态或日志。切换前的进程结束是 dry-run。 | `test-p9-gate.sh`、`identity_vault` |
| **P10 Desktop 启停** | 启动计划包含伪装环境、宿主型号展示路径和唯一网关代理。缺少代理时不启动。云电脑上找不到 Desktop 二进制时，计划与状态都是「未启动」，不会假装已有活会话，也不会弹出窗口。停止是对注入 PID 列表的礼貌关 → grace → 强杀 dry-run。IP 波动杀进程仍在网关模块。 | `test-p10-gate.sh`、`desktop_control` |

以上全部在 `test-p11-near-beta-gate.sh` 里会再跑一遍。

## 仍须在 Win11 真机上核验（未做，不标成功）

这些项**没有**在这台云电脑上证明。未做 W1，未启用 WSL，未改和零的宿主时区、语言、默认浏览器或系统代理。

1. **启用 WSL 并创建 `ClaudeBox` 发行版**（W1）。须和零再次明确批准。包括 `wsl --install`、导出/导入发行版、`New-ClaudeBoxWsl.ps1` 等。
2. **真实挂载 `visible_dirs`**：发行版内确实没有默认 `/mnt/c`，只出现配置的目录；在活的 WSL 里拒绝整挂 `C:` 仍然生效。
3. **活的唯一网关**：在 Win11 上由现有 `ClaudeBox.Gateway` 真正启动 gost、绑定端口、连上 Clash 或其他上游；连接日志来自真实流量，而不只是夹具 JSONL。
4. **活的 IP 波动响应**：真网卡地址集合变化时，真正断开 gost，并按并行节奏礼貌关 / 强杀盒子内 Claude，不影响无关宿主软件。
5. **真机采集与展示**：从真实 Win11 / WSL 采集原始清单（真实编码只进盒子内原始清单，不进 git）；展示层被活的 Desktop 进程看到。
6. **活的时区 / 语言伪装**：WSL 内 Claude Desktop / Intl 看到盒子伪装值；**宿主 Win11 仍是东八区 + 简中**。不得改宿主时区、语言或默认浏览器来「完成」这项。
7. **活的伴生指纹浏览器与 OA**：用户自行放置的独立引擎（例如 Fingerprint Chromium，不自动下载）能打开真实授权页；进程是伴生浏览器，**不是**宿主 `chrome.exe` / `msedge.exe`。若 Desktop 用 `shell.openExternal` 绕过 `xdg-open`，还要在真机上看拦截是否足够。关网关后授权页打不开（或明确失败）；该浏览流量出现在网关 JSONL。
8. **活的身份保险库**：对真实 Desktop 登录态做归档 / 切换 / 回滚；切换前真正结束盒子内 Claude 和已绑定的伴生浏览器；宿主默认 `%APPDATA%\Claude` 不被当作工作副本。
9. **活的 Linux Claude Desktop**：用本地 deb（`Install-ClaudeDesktop.sh --check` / 不自动下载）在发行版内安装；经 WSLg 弹出真实窗口；启动环境含伪装、展示层和网关代理；dry-run 里的「未启动」不得被当成真机已启动。
10. **官方支持风险仍然存在**：Anthropic 建议 WSL 用户使用 Windows Desktop；Linux Desktop-in-WSL 不受官方支持。真机验收不能消除这项产品风险。

## 文档与策略

- 产品策略仍是 SCOPE v0.4.1，本阶段没有另开新的策略版本。
- P8–P10 是把已记录的 W6 / W7 / 启停合同接到可在云电脑证明的实现；不是改策略。
- 真机 W1 不在本清单的「已证明」里。要做 W1，须和零再点头。

## 相关入口

- 核心库与命令行：`src/claudebox/README.md`（`claudebox-identity`、`claudebox-companion`、`claudebox-vault`、`claudebox-desktop`）
- 设置页：`src/claudebox-ui/README.md`
- WSL 轨脚本：`src/wsl-box/README.md`
