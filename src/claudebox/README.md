# claudebox · C++ 核心（P1 起）

身份策略引擎：读取电脑信息清单，按每字段的 `expose` / `hide` / `override` 生成展示层，并校验展示层没有泄漏被隐藏的值。

默认策略：厂商 / 型号可原样展示；序列号、UUID、MAC 等硬件编码隐藏。用户策略可以改任一字段。P7 起时区 / 语言伪装由 `claudebox/spoof.hpp` 写入盒子前缀，不改宿主时区或语言。

## 构建与测试（云电脑）

```bash
cmake -S src/claudebox -B src/claudebox/build
cmake --build src/claudebox/build
ctest --test-dir src/claudebox/build --output-on-failure
# 或：
bash tests/claudebox/test-p1-gate.sh
```

## 命令行

```bash
./src/claudebox/build/claudebox-identity apply \
  --inventory tests/wsl-box/fixtures/host-inventory.raw.yaml \
  --prefix /tmp/claude-box-p1
./src/claudebox/build/claudebox-identity verify \
  --inventory tests/wsl-box/fixtures/host-inventory.raw.yaml \
  --prefix /tmp/claude-box-p1
./src/claudebox/build/claudebox-identity print-defaults
```

`--policy` 省略时使用默认策略。不要把真实编码提交进 git。

## 采集（P2）

```bash
./src/claudebox/build/claudebox-identity collect \
  --source-root tests/claudebox/fixtures/host-sys \
  --prefix /tmp/claude-box-p2
# 原始清单：/tmp/claude-box-p2/var/host-inventory.raw.yaml
# 不会自动生成展示层。随后可用同一 prefix 做 apply / verify。

bash tests/claudebox/test-p1-p2-gate.sh
```

## 展示层接线（P3）

`src/wsl-box/guest/apply-host-identity.sh` 和 `verify-host-identity.sh` 调用本目录编出的 `claudebox-identity`，按用户策略写展示层。采集（collect）仍然只写原始清单。

查找二进制的顺序：`CLAUDE_BOX_IDENTITY_BIN`，`$CLAUDE_BOX_PREFIX/bin/claudebox-identity`（`Install-ClaudeBoxFramework.sh` 会编译并拷到这里），仓库内 `src/claudebox/build/claudebox-identity`。可选策略文件：`CLAUDE_BOX_IDENTITY_POLICY` 或 `$CLAUDE_BOX_PREFIX/var/host-identity.policy.yaml`；省略则用默认策略。

```bash
bash tests/claudebox/test-p1-p2-p3-gate.sh
```

## 设置页（P4）

盒子配置的读写（`wsl-box.yaml`）和设置会话在本库：`claudebox/config.hpp`、`claudebox/settings.hpp`。Qt 设置页在 [src/claudebox-ui/](../claudebox-ui/README.md)。

```bash
bash tests/claudebox/test-p4-gate.sh
```

## 运行时与目录隔离（P5）

`claudebox/mount.hpp` 根据 `visible_dirs` 生成 dry-run 挂载计划，并规划发行版生命周期步骤（不调用 `wsl.exe`）。默认拒绝宿主系统盘根（`C:` / `C:\` / `C:/` / `/mnt/c`）、整棵 `C:\Users` 或整个用户配置目录，以及把 `/mnt/c` 当客机挂载点。客机路径必须落在 `/mnt/claude-box` 下。`exclude_host_system_drive: true` 与 `disable_automount: true` 必须保持开启。设置页保存前走同一套校验，拒绝时不会写坏配置。

```bash
bash tests/claudebox/test-p4-p5-gate.sh
```

## 唯一网关（P6）

`claudebox/gateway.hpp` 根据已加载的盒子配置和前缀生成 dry-run 中继计划、校验上游、按 `ClaudeBox.Gateway` 的后缀规则匹配 blocklist，并读写 JSONL 连接日志。空日志路径解析为 `<prefix>/var/logs/gateway.jsonl`，在这台 Linux 云电脑上不会落到宿主 `%LOCALAPPDATA%`。本库不启动 gost，也不绑定端口；Win11 上的进程启停仍由 `src/ClaudeBox.Gateway/` 负责。

路径级规则（如 `api.anthropic.com/api/event_logging`）只拦该路径，不会把 `api.anthropic.com` 的正常对话接口一票否决。默认示例名单仍是 Sentry / Datadog 的主机后缀。

```bash
bash tests/claudebox/test-p6-gate.sh
```

## 时区 / 语言伪装（P7）

`claudebox/spoof.hpp` 把配置里的盒子伪装值应用到前缀：`env/spoof.env`、`env/session.env` 里的 `TZ` / `LANG` / `LC_*`，以及前缀内的 `etc/localtime` 与 `etc/timezone`。这与现有 `apply-spoof.sh` 的前缀合同一致，但**不会**写宿主 `/etc/localtime` 或改宿主 TZ。Intl 探针使用配置中的假时区 / 语言，不把宿主真实时区当作默认可展示字段。设置页保存身份伪装时走同一条路径。

```bash
bash tests/claudebox/test-p7-gate.sh
```

## 本机 IP 波动响应（唯一网关模块）

`claudebox/gateway_ip.hpp` 根据注入的上一组 / 当前本机地址集合，规划「断开唯一网关」与「礼貌关闭盒子内 Claude」的**并行启动**（互不等待），并在 `kill_grace_seconds`（默认 10）后强杀仍存活的进程。默认 `features.kill_on_local_ip_change: false`。这是 dry-run：不读真网卡，不停止活的 gost，不杀真实进程。

```bash
bash tests/claudebox/test-p7-ip-gate.sh
```

## 伴生指纹浏览器（P8）

`claudebox/companion.hpp` 规划并（在引擎就绪时）打开 OAuth / 授权 URL。出网走唯一网关代理，进程环境使用盒子伪装时区 / 语言，资料目录固定在 `<prefix>/var/companion-browser/profile`。设置页保存时会在前缀内安装 `bin/xdg-open`、`bin/www-browser` 和 `env/companion.env`（`BROWSER` 指向盒子内包装），不会改宿主默认浏览器。

引擎来自 `CLAUDE_BOX_COMPANION_BROWSER` 或 `<prefix>/opt/companion-browser/engine`。本模块**不会**自动下载 Fingerprint Chromium，也不把引擎提交进 git。引擎未就绪、开关关闭、或唯一网关上游未配置时，打开失败并明确拒绝回退到宿主 `xdg-open` / `chrome` / `msedge`。

```bash
./src/claudebox/build/claudebox-companion status --prefix /tmp/claude-box-p8
./src/claudebox/build/claudebox-companion open --prefix /tmp/claude-box-p8 \
  --url 'https://claude.ai/login' --dry-run
bash tests/claudebox/test-p8-gate.sh
```

客机入口仍是 `src/wsl-box/companion-browser/open-oauth-url.sh`。

## 身份保险库（P9）

`claudebox/vault.hpp` 在盒子前缀内归档、切换和回滚身份工作副本（默认 `<prefix>/var/identity-vault`）。归档是 AES-256-GCM（PBKDF2-HMAC-SHA256，OpenSSL 3）认证加密，与 CCBaga `.baga` 不必兼容。口令来自 `CLAUDE_BOX_VAULT_PASSPHRASE_FILE` 或 `CLAUDE_BOX_VAULT_PASSPHRASE`，不会写进日志。每个身份可绑定盒子内独立的伴生浏览器资料目录；OAuth 仍不回退到宿主浏览器。

切换 / 归档前会按现有节奏规划礼貌关闭盒子内 Claude 和已绑定的伴生浏览器；云电脑上只做 dry-run，不会结束真实进程，也不会改宿主时区、语言、默认浏览器或系统代理。

```bash
# 口令文件放在临时目录，不要把口令写进仓库或命令行历史展示。
export CLAUDE_BOX_VAULT_PASSPHRASE_FILE=/tmp/claude-box-vault-pass
./src/claudebox/build/claudebox-vault archive --prefix /tmp/claude-box-p9
./src/claudebox/build/claudebox-vault switch --prefix /tmp/claude-box-p9 --target empty
./src/claudebox/build/claudebox-vault rollback --prefix /tmp/claude-box-p9
bash tests/claudebox/test-p9-gate.sh
```
