# Policy simulation review (cloud computer dry-run)

- Date: 2026-11-12 (Asia/Tokyo, JST / UTC+9)
- Repo: `/home/box/projects/Claude-Box`, branch `main`
- Review baseline HEAD: `a80554bb3185dd9730370f498017f659fa34a995` (`a80554b docs/test(claudebox): P11 near-beta dry-run integration`)
- Local follow-up commit (no push): `9bc6a88658a4e15622974d791a3b9278ca2ab9eb` (`9bc6a88 fix(claudebox): make companion intercept install idempotent`)
- This report file is left **uncommitted**.
- Simulation prefix: `/tmp/claude-box-policy-sim/prefix`
- Raw logs: `/tmp/claude-box-policy-sim/out/`
- Official baseline gate: `bash tests/claudebox/test-p11-near-beta-gate.sh`

This pass compared the current implementation against the earlier reverse-engineering notes and exercised the real binaries and scripts against a temporary prefix. It is **not** a real-machine beta. WSL was not enabled, W1 was not started, and nothing was pushed.

Host on this Debian cloud computer (must stay as-is):

```
date: 2026-11-12 21:40:04 JST +0900 (Asia/Tokyo box clock)
readlink /etc/localtime: /usr/share/zoneinfo/Asia/Tokyo
timezone file: Asia/Tokyo
md5 /etc/localtime: 38620155fabd5572c5a4b1db051b3cc8
LANG=C.UTF-8
LC_ALL=C.UTF-8
default-web-browser: box-chrome.desktop
BROWSER=/usr/local/bin/box-chrome
(no proxy env)
```

Note: the host locale here is `C.UTF-8`, not Chinese. The RE / SCOPE documents talk about the owner’s Win11 staying 东八区 + 简中; that host was not touched.

After the simulation and the P11 rebuild, `/etc/localtime` was still `Asia/Tokyo` with the same md5 and mtime (`1794484292`), `/etc/timezone` was still `Asia/Tokyo`, and `xdg-settings get default-web-browser` was still `box-chrome.desktop`.

## Result counts

Interpretation of the ten RE-derived policies below, plus explicitly listed gaps:

| Result | Count | What it covers |
|--------|------:|----------------|
| PASS | 9 | Policies 1, 2, 4, 5, 6, 7, 8, 9, and the implemented parts of policy 3 (matcher, upstream validation, JSONL, plan-only gost command) are recorded as PASS in the table, with caveats in the notes. |
| FAIL | 0 | One intercept/settings-save bug was found and fixed (see below). After the fix, the prefix simulation driver reported `PASS=97 FAIL=0`, and P11 was all green. |
| NOT IMPLEMENTED | 4 | Default blocklist does not include `event_logging` / `/api/v2/rum` / bare `sentry.io` / several Datadog intakes; gost relay plan does not encode the blocklist as ACL; `SENTRY_NAME` is not wired; `ICU_TIMEZONE_FILES_DIR` is not wired. |
| GAP | 12 | Items that cannot be proven on this cloud computer, or are intentionally out of scope. Listed in the gaps section. |

Policy 3 is **not** counted as a FAIL: the matcher and validation work as coded and as the unit tests document. The missing default rules and the missing gost ACL are incomplete relative to SCOPE / RE §4, so they are listed as NOT IMPLEMENTED.

## Policy comparison

### 1. Timezone / locale spoof

- **RE finding:** `docs/analysis-claude-linux-telemetry.md` §3–§4: the main hook is `Intl.DateTimeFormat().resolvedOptions().timeZone` (`lg()` / `xma()`). Also `getTimezoneOffset`, `LANG`/`LC_*` for `getPreferredSystemLanguages` / `getLocale`, and `/etc/localtime` (and possibly `/etc/timezone`) for ICU. Recommended env path: `TZ`, `LANG`/`LC_*`, prefix/guest `/etc/localtime`, optional `--lang=`, optional `SENTRY_NAME` / `ICU_TIMEZONE_FILES_DIR`. See also `docs/spoofing-timezone-locale.md`.
- **Promised policy:** Spoof only inside the box / process. Do not change the host timezone or locale. Settings show box spoof values, not the host TZ.
- **How simulated:** `claudebox-desktop start --dry-run` (this calls `claudebox::spoof::apply`) on the prefix, then a Node Intl probe under `env/spoof.env` + `env/session.env`. The C++ driver also ran `spoof::verify` / `probe_intl`. Host `/etc/localtime` was snapshotted before and after.
- **Result:** PASS
- **Evidence:**

Host probe (no spoof):

```
{"timeZone":"Asia/Tokyo","locale":"en-US","offset":-540,"date":"Thu Nov 12 2026 21:40:04 GMT+0900 (Japan Standard Time)"}
```

Prefix artifacts after apply:

```
/tmp/claude-box-policy-sim/prefix/etc/localtime -> /usr/share/zoneinfo/America/Los_Angeles
prefix etc/timezone: America/Los_Angeles
```

`env/spoof.env`:

```
export TZ="America/Los_Angeles"
export LANG="en_US.UTF-8"
export LC_ALL="en_US.UTF-8"
export LANGUAGE="en_US"
```

`env/session.env` also has `CLAUDE_BOX_ELECTRON_LANG="en-US"` and `CLAUDE_BOX_HOSTNAME="claude-box"`. The desktop dry-run plan included `TZ=America/Los_Angeles LANG=en_US.UTF-8 --lang=en-US`.

Spoofed Node probe:

```
{"timeZone":"America/Los_Angeles","locale":"en-US","offset":480,"date":"Thu Nov 12 2026 04:40:04 GMT-0800 (Pacific Standard Time)"}
```

C++ verify: `spoof verify ok=1 probe.ran=1 probe.tz=America/Los_Angeles probe.locale=en-US source=intl-node`. `writes_host_etc` is false. Host `/etc/localtime` md5 and mtime were unchanged.

- **Notes:** This box does not have the `en_US.UTF-8` glibc locale generated, so sourcing `LC_ALL=en_US.UTF-8` printed `setlocale: LC_ALL: cannot change locale (en_US.UTF-8)`. Intl still reported `en-US`. A live WSL distro should have that locale generated; Electron `app.getPreferredSystemLanguages()` / `getLocale()` was not executed (GAP). `SENTRY_NAME` and `ICU_TIMEZONE_FILES_DIR` are not written into the spoof env (NOT IMPLEMENTED). Prefix `etc/localtime` is an artifact for the guest; it is not bind-mounted over the host `/etc`.

### 2. Identity presentation

- **RE finding:** Device fields can leave the machine (`docs/analysis-claude-linux-telemetry.md` §2.5: `ant-did`, client headers, managed-config). SCOPE v0.4.1 / `docs/identity-presentation.md`: vendor/model may be exposed; serial, UUID, MAC, disk serial, CPU ID, MachineGuid, device instance path default hidden. `hide_identifiers=false` must fail and is not a reveal-all switch. Raw inventory is not the presentation layer.
- **Promised policy:** Collect into `var/host-inventory.raw.yaml`. Apply writes only authorized fields to `var/host-identity.presented.yaml`, `env/host-model.env`, and `presentation/dmi/id/`.
- **How simulated:** `claudebox-identity collect --source-root tests/claudebox/fixtures/host-sys`, then `apply-host-identity.sh` / `verify-host-identity.sh` with default policy. Then `CLAUDE_BOX_HIDE_IDENTIFIERS=false` on apply/verify.
- **Result:** PASS
- **Evidence:** Collect wrote 12 fields, including `SN-C0NT0S0-9F3A-0001`, UUIDs, `02:11:22:33:44:55`, `WD-WCC4N7FIXTURE`, `BFEBFBFF000906EA`, `8d4f3c2a-1b6e-4f90-9c11-deadbeef1234`. Presentation yaml was only:

```
manufacturer: Contoso Computers
product: ContosoBook 15 G3
```

`verify-host-identity OK` / `hidden identifier values not present in presentation`. Hidden values were present in the raw inventory and absent from presentation yaml, `host-model.env`, and `presentation/`. Raw inventory ≠ presentation yaml.

`hide_identifiers=false` exit codes were 1:

```
error: host_identity.hide_identifiers must stay true (got false)
error: to show or change a field, use a per-field identity policy (expose / override); this flag is not a dump-all switch
```

Presentation still did not contain identifiers after that refusal.

- **Notes:** Live `/sys/class/dmi` is not rewritten (intentional). WSL/virtualization traces are not hidden (intentional). Live Win11 collection was not done (GAP).

### 3. Unique gateway + telemetry blocklist

- **RE finding:** §4 priority 1: block `*.sentry.io` / `o1158394.ingest.us.sentry.io`; `browser-intake-*` Datadog hosts; `{anthropicHost}/api/v2/rum`; `api.anthropic.com/api/event_logging/**`. Path-level `event_logging` must not blanket-deny the main Anthropic API.
- **Promised policy:** Unique gateway + optional JSONL. Refuse missing/invalid upstream. Plan a gost relay (do not start a live gateway on this computer). Default example list is Sentry / Datadog host suffixes; path rules are supported in the C++ matcher.
- **How simulated:** C++ `gateway::validate`, `plan_relay`, `host_blocked` / `request_blocked` on the default list and on an extended list, plus JSONL append/parse. No live gost process was started. Host proxy env was not changed.
- **Result:** PASS for matcher, upstream validation, JSONL, and plan-only relay command. NOT IMPLEMENTED for default coverage of all RE §4 endpoints and for compiling the blocklist into the gost command/YAML. GAP for live packet capture.
- **Evidence:**

Validate accepts `http://127.0.0.1:7890`. Missing upstream is refused (`需要至少配置一个上游`). Invalid scheme `ftp://127.0.0.1:21` is refused. Relay status is `plan-only`. Command:

```
gost -L http://127.0.0.1:3080 -F http://127.0.0.1:7890
```

The generated gost YAML is a listen + forwarder only. It does not mention `blocklist`, `bypass`, or Sentry. Empty `logging.path` resolved to `/tmp/claude-box-policy-sim/prefix/var/logs/gateway.jsonl`, not `%LOCALAPPDATA%`.

Default list (`ingest.us.sentry.io`, `browser-intake-datadoghq.com`, `browser-intake-us5-datadoghq.com`):

- blocks `o1158394.ingest.us.sentry.io` and `browser-intake-datadoghq.com`
- does **not** block bare `sentry.io` (unit tests document this)
- does **not** block `browser-intake-us3-datadoghq.com`, `browser-intake-datadoghq.eu`, `browser-intake-ddog-gov.com`
- does **not** block `api.anthropic.com/api/event_logging` or `{host}/api/v2/rum`
- does **not** blanket-deny `api.anthropic.com/v1/messages`

When path rules `api.anthropic.com/api/event_logging` and `/api/v2/rum` (and `sentry.io`) are added: `/api/event_logging` and `/api/event_logging/v2/batch` are blocked, `/v1/messages` and `/api/event_logging2` are not, `/api/v2/rum` matches, `foo.sentry.io` is blocked.

JSONL round-trip parsed `action=deny` for `o1158394.ingest.us.sentry.io` and counted deny + proxy entries.

PowerShell `Test-ClaudeBoxGatewayHostBlocked` is host-suffix only; it has no path-level matcher. Live Win11 enforcement of `event_logging` would need the C++ rule to be translated into gost/ACL (currently it is not).

- **Notes:** This is the largest doc/code mismatch versus SCOPE’s sentence “网关拦 Sentry / Datadog RUM / event_logging”. The matcher exists; the default list and the planned gost process do not yet carry the full RE §4 set.

### 4. Filesystem / visible_dirs

- **RE / policy:** Cut default `/mnt/c`. Only mount `visible_dirs` under `/mnt/claude-box`. Refuse `C:`, `C:\`, `/mnt/c`, whole `C:\Users`, and a guest remount of `/mnt/c`. `disable_automount` and `exclude_host_system_drive` must stay true.
- **How simulated:** `claudebox::mount::plan_mounts` on the example config and on rejected variants. Settings save was also tried with bad mounts.
- **Result:** PASS
- **Evidence:** Good plan is ok, `disable_automount=true`, `exclude_host_system_drive=true`, guest path `/mnt/claude-box/demo`. Rejects included: `拒绝挂载宿主系统盘根：C:\`, `/mnt/c`, `拒绝挂载整棵 Users 目录…C:\Users`, `拒绝把整个宿主 C: 挂到客机：/mnt/c`, and both flags set to false. Settings save rejected `C:\` → `/mnt/c` and whole `C:\Users` without rewriting the config. No `wsl.exe` was invoked.

- **Notes:** A live `/mnt/c` cut on Win11 is still a GAP.

### 5. Companion fingerprint browser

- **RE / policy:** OA/login URLs go to an isolated `user-data-dir` + spoof env + unique gateway. Missing engine fails closed; never fall back to host `xdg-open` / chrome / msedge / `$BROWSER`. Do not download Fingerprint Chromium.
- **How simulated:** Mock engine at `<prefix>/opt/companion-browser/engine`. `claudebox-companion install-intercept / status / open`. Prefix `bin/xdg-open` with and without `HTTP_PROXY`. Missing-engine prefix. `CLAUDE_BOX_COMPANION_BROWSER=/usr/bin/google-chrome`. `open-oauth-url.sh` with no engine.
- **Result:** PASS (after the intercept-install bugfix)
- **Evidence:** Dry-run command:

```
'/tmp/claude-box-policy-sim/prefix/opt/companion-browser/engine' '--user-data-dir=/tmp/claude-box-policy-sim/prefix/var/companion-browser/profile' '--no-first-run' '--no-default-browser-check' '--proxy-server=http://127.0.0.1:3080' '--lang=en-US' '--accept-lang=en-US' 'https://claude.ai/login'
```

Mock engine record included `TZ=America/Los_Angeles`, `HTTP_PROXY=http://127.0.0.1:3080`, `BROWSER=/tmp/claude-box-policy-sim/prefix/bin/xdg-open`. Profile is under the prefix, not a host Chrome/Edge or Claude userData path.

Missing engine exit 3, text includes `Refusing to open the host default browser`. Host chrome as engine was refused (`refusing to use a host default browser as the companion engine: /usr/bin/google-chrome`). `open-oauth-url.sh` with no engine also refused fallback.

Prefix `env/session.env` and `env/companion.env` do **not** themselves export `HTTP_PROXY`. Without a start-path injection, prefix `xdg-open` on an OA URL exits 2: `unique gateway proxy is not configured… Refusing to open a bypass path or the host default browser`. With `HTTP_PROXY=http://127.0.0.1:3080` (as `Start-ClaudeBoxWsl.ps1` / `Install-ClaudeBoxFramework.sh` inject on a real distro), prefix `xdg-open` opened the mock engine (exit 0).

- **Notes:** First simulation found a real bug: a second `install_intercept` / settings save failed with `filesystem error: cannot copy file: File exists […/bin/open-oauth-url] […/bin/open-oauth-url]` when `CLAUDE_BOX_OPEN_OAUTH` (from a sourced `companion.env`) pointed at the already installed wrapper. That would break a second settings save inside a box session. Fixed in `9bc6a88`. After the fix, the driver and `test_companion` (`passed=104 failed=0` during the rebuild, then P11 `companion_browser` passed) were green. Live OA, Fingerprint Chromium, and `shell.openExternal` bypass of `xdg-open` remain GAPs. Chromium was not downloaded.

### 6. Identity vault

- **RE / policy:** Archive / switch / rollback only inside the box prefix. Forced failure must not move the working copy or switch to the failed target. Passphrase from file or env, never logged. Do not use the host default Claude directory as the working copy.
- **How simulated:** Seeded a fixture workspace under `<prefix>/var/identity-vault/workspace`. `claudebox-vault archive / switch empty / switch back`. Wrong passphrase when switching to an existing generation. C++ `force_fail_after=after_verify` on switch-to-empty.
- **Result:** PASS
- **Evidence:** `ok archive`, switch to empty produced an empty identity, switch back restored `{"id":"fixture-a"}` and `{"token":"token-aaa","note":"simulated-login"}`. All vault files were under the prefix (`generations/gen-NNNN/archive.enc`, `current`, `workspace/…`). Status text and vault dir text did not contain the passphrase.

Wrong passphrase against an existing generation: exit 1, `error: vault archive authentication failed`. Live workspace still had `fixture-a` / `token-aaa`. (An earlier script-side `diff` false alarm was caused by a leftover nested backup directory from the first run, not by a workspace rewrite.)

Forced failure: operation returned not ok (`forced failure after_verify`), working copy did not switch to empty, and the failed empty generation did not become current. A `.partial` generation was left behind (ignored by listing). Unit tests document that a **pre-switch safety archive** may become the current pointer; that is a snapshot of the same identity, not a switch to the failed target.

- **Notes:** Process close is dry-run only. Live login-state vault on Win11 is a GAP. Switching *to empty* with a different passphrase is not the same as a decrypt failure (a safety archive can be written with the passphrase you just supplied). The authentic failure case is switching to an existing generation with the wrong passphrase.

### 7. IP-change response

- **RE / policy:** Default OFF does nothing and exits 0. When armed, a local IP set change starts gateway disconnect and polite-kill in parallel (not serialize disconnect-then-kill). Grace applies only to processes. Scope is `claude-desktop` / `claude` / `Claude`. Do not kill unrelated processes.
- **How simulated:** `Watch-ClaudeBoxLocalIp.ps1 -DryRun -Once` with injected fingerprints, default config (OFF) and `-ForceEnable`. C++ `dry_run_ip_change` with mixed process names.
- **Result:** PASS
- **Evidence:**

Default OFF, even with `192.168.1.10/24` → `192.168.1.20/24`:

```
[claude-box] kill_on_local_ip_change is OFF (default). Pass -ForceEnable or set features.kill_on_local_ip_change: true. Exiting.
```

exit 0. C++: `noop`, gateway not stopped, no processes killed.

Armed dry-run:

```
[claude-box] local IP set changed — start gateway disconnect and polite close in parallel (neither waits for the other)
[claude-box] DRY-RUN thread=gateway start now: Stop-ClaudeBoxGateway … — not waiting for process close
[claude-box] DRY-RUN thread=process start now: polite close claude-desktop / claude / Claude — not waiting for gateway disconnect
[claude-box] DRY-RUN thread=process after 10s: force kill remaining box Claude processes (grace applies to processes only)
```

C++: both StopGateway and PoliteClose have `start_offset 0`; force-kill offset equals grace (10s); gateway disconnect is not delayed until grace; targets are `claude-desktop` / `claude` / `Claude`; `gost`, `clash`, `firefox`, `google-chrome`, `claudebox-identity` are not targeted. `is_box_claude_process("claudebox-desktop")` is false.

- **Notes:** No real NIC was read, no live gost was stopped, no real processes were signaled. Live Win11 IP-change is a GAP.

### 8. Desktop start / stop dry-run

- **RE / policy:** Start plan includes spoof env, presentation, and gateway proxy. Missing binary or missing proxy => not-started, never a fake live session. Stop only against injected PIDs (polite → grace → force).
- **How simulated:** `claudebox-desktop start --dry-run` and `status` on the prefix (no desktop binary). C++ `start` with proxy removed; `stop` with injected `claude-desktop#9001`, `firefox#9002`, `gost#9003`.
- **Result:** PASS
- **Evidence:**

```
二进制：未找到
唯一网关代理：http://127.0.0.1:3080
盒子伪装：TZ=America/Los_Angeles LANG=en_US.UTF-8 --lang=en-US
伪装环境：已有 …/env/spoof.env
宿主型号展示：已有 …/env/host-model.env
会话：未启动
not started (dry-run, binary missing)
```

`running=false`, `started=false`. Missing proxy => `not-started`, not a fake session. Stop log:

```
dry-run polite close of box Claude Desktop (not host software)
t=0 polite_close claude-desktop#9001
wait grace 10s
t=10 force_kill claude-desktop#9001
```

`firefox` and `gost` were not targeted.

- **Notes:** `src/wsl-box/guest/start-claude-desktop.sh` only **warns** if `HTTP_PROXY`/`ALL_PROXY` is missing, then still `exec`s. The C++ start path refuses. That is a script/C++ mismatch; C++ is what the settings/CLI dry-run uses. Official Anthropic still does not support Linux Desktop inside WSL (GAP / product risk). No window was opened.

### 9. Settings / policy consistency

- **RE / policy:** Saving bad mounts or a bad gateway upstream must be rejected. `hide_identifiers` stays true. Settings page surfaces box spoof values (not host TZ as a default exposable field) and the IP-change switch on the gateway page.
- **How simulated:** `claudebox::settings::Session` load/save on the prefix (same path the Qt page uses). Inspected `settings_window.cpp` for the spoof page and gateway IP-change checkbox. Official `settings_ui` ctest ran as part of P11.
- **Result:** PASS (after the intercept-install fix)
- **Evidence:** Loaded config keeps `hide_identifiers=true` and box values `America/Los_Angeles` / `en_US.UTF-8`. Forcing `hide_identifiers=false` and saving still persists `hide_identifiers: true` (and never `false`). Bad mount and missing/invalid upstream were rejected without rewriting the config (`拒绝挂载宿主系统盘根：C:\`; upstream required / invalid URL). The Qt page captions state that spoof fields are box values, not the host timezone, and the IP-change switch is on the gateway page (`killOnIpChangeCheck`, group “本机 IP 波动响应”). P11: `settings_ui` Passed (2.17s).

- **Notes:** The first simulation hit the intercept self-copy bug at the end of `Session::save()`. After `9bc6a88`, a second save with `CLAUDE_BOX_OPEN_OAUTH` pointing at the installed wrapper succeeds.

### 10. Gaps vs RE / SCOPE (untestable here, or never implemented)

These are **not** marked success.

| Item | Result | Notes |
|------|--------|-------|
| Live packet capture of Sentry / RUM after login | GAP | No live Desktop, no live gost, no capture. |
| Whether a signed Linux package honors `disableEssentialTelemetry` / managed policy files / `CLAUDE_USER_DATA_DIR` | GAP | RE §5: not verified. Windows signed builds are already known to ignore `CLAUDE_USER_DATA_DIR`. |
| `SENTRY_NAME` | NOT IMPLEMENTED | Mentioned in RE §4 as optional; not written by `spoof::apply`. |
| `ICU_TIMEZONE_FILES_DIR` | NOT IMPLEMENTED | Mentioned in RE §4 as “必要时”; not wired. |
| Electron `app.getPreferredSystemLanguages` / `getLocale` / `getSystemLocale` | GAP | Approximated with `LANG`/`LC_*` + Intl. No live Electron. |
| `openExternal` bypass of the `xdg-open` wrapper | GAP | Documented in `docs/companion-fingerprint-browser.md` and the P11 acceptance list. Needs a live Desktop on Win11. |
| Live WSL `/mnt/c` cut | GAP | Mount planner only. W1 not started. |
| Live gost + Clash | GAP | Plan-only. Host proxy was not changed. |
| Live OA in a real companion engine | GAP | Mock engine only. Chromium was not downloaded. |
| Live vault of a real login | GAP | Fixture workspace only. |
| Official: Anthropic does not support Linux Desktop inside WSL | GAP / product risk | Still true. Cannot be cleared by a cloud dry-run. |
| `ant-did` isolation on Linux (`~/.config/Claude` vs prefix) | GAP | Vault/workspace isolation is implemented under the prefix; a live Desktop userData path was not observed. |
| Do not hide WSL / virtualization traces | intentional | Not a defect. Confirmed: no code path claims to hide `microsoft-standard-WSL2`. |
| China + UTC8 special-quote fingerprint (`docs/analysis-china-utc8-quote-fingerprint.md`) | out of scope / not claimed | Verdict in that doc is **NO** client-side quote-marker path in 4.4212.0. Claude Box policies do not claim to address server-side conditioning on the `Timezone:` env string. Client spoofing still changes the IANA string that would go into `<env>`. |

Asar / JS hooks (`lg()`, `bCt()`, patched `Intl`) remain an intentional non-default backup (`desktop.asar_patch: false`, SCOPE “暂不进 MVP”).

## What is solid on this cloud computer

- Prefix spoof makes a Node Intl probe report `America/Los_Angeles` / offset 480, not the host `Asia/Tokyo` / -540, without rewriting host `/etc/localtime`.
- Presentation exposes only Contoso vendor/model; fixture identifiers stay in the raw inventory.
- `hide_identifiers=false` is refused and is not a dump-all switch.
- Mount planner and settings save reject system-drive / whole-Users / `/mnt/c` remounts and require automount disable + `exclude_host_system_drive`.
- Gateway validation, JSONL schema, and path-level blocking **when a path rule is present** work; missing/invalid upstream is refused.
- Companion OA path uses an isolated profile, spoof TZ/lang, and the unique-gateway proxy; missing engine and host Chrome fail closed.
- Vault archive / switch / rollback stay in the prefix; wrong passphrase does not reveal the passphrase in logs; forced failure does not switch the working copy to the failed target.
- IP-change default OFF is a no-op (exit 0); armed dry-run starts disconnect and polite-close in parallel.
- Desktop dry-run is honestly `not-started` when the binary is missing; stop only targets injected box Claude PIDs.

## What is still only a plan

- Everything on the “仍须在 Win11 真机上核验” list in `docs/near-beta-dry-run-acceptance.md` (W1, live mounts, live gost+Clash, live IP-change, live spoof inside Desktop, live OA, live vault login, WSLg window).
- ~~gost is a planned `gost -L … -F …` command. The blocklist is not compiled into that plan.~~ **已在 2026-11-12 补丁中关闭**，见文末 follow-up。
- ~~Default `wsl-box.yaml` blocklist does not yet encode the full RE §4 set.~~ **已在 2026-11-12 补丁中关闭**，见文末 follow-up。
- Official Linux-Desktop-in-WSL support risk is unchanged.

## Bugs found

1. **Companion intercept / settings save was not idempotent inside a box session** (clear regression of a promised save path). `install_intercept` copied `CLAUDE_BOX_OPEN_OAUTH` onto itself and threw `File exists`, so a second settings save failed after intercept wrappers were already installed. **Fixed** in local commit `9bc6a88` (`fix(claudebox): make companion intercept install idempotent`). Regression coverage is in `tests/test_companion.cpp`. Re-ran `test_companion` and the full P11 gate after the fix.
2. No other promised-policy regressions were fixed in this pass. Incomplete default telemetry coverage and the missing gost ACL were recorded as NOT IMPLEMENTED, not patched here.

## Doc / code mismatches (not treated as regressions to fix in this pass)

- 上述 SCOPE / start 脚本 / session 代理 / vault 文档不一致，已在 2026-11-12 follow-up 中按实现收紧或修掉（见文末）。

## P11 baseline

Exact gate result line (exit 0):

```
ALL GREEN (P11 near-beta dry-run integration: P1–P10 proven on this cloud computer; real-machine W1 not started)
```

This was run after the intercept fix, with a restored local toolchain on the cloud computer (`g++`, `cmake`, `make`, `qt6-base-dev`) because those tools were missing from PATH at the start of this pass. Installing them did not change host timezone, locale, default browser, or system proxy.

## Hard constraints (this pass)

- Did **not** `git push`.
- Did **not** enable WSL, start W1, or touch the owner’s Win11 machine.
- Did **not** change host `/etc/localtime`, `/etc/timezone`, display language, default browser (`box-chrome.desktop`), or system proxy.
- Did **not** download Fingerprint Chromium or commit binaries.
- Did **not** pretend a live Claude Desktop, live gost, or live OA session was running.
- Did **not** start new feature work to close RE gaps.

This review does **not** claim that beta is complete on a real machine.

## Follow-up · 2026-11-12（Asia/Tokyo）— 先补上问题

和零指示「先补上问题」。本轮在既有 checkout（起点 `9bc6a88`）上只做本地提交，没有 push，没有开 PR，没有启用 WSL / 启动 W1 / 改动 Win11，也没有改宿主时区、语言、默认浏览器或系统代理。

### 已修复

**A. 默认遥测黑名单与 RE §4 对齐**

- 单一来源：`claudebox::config::default_gateway_blocklist()`，示例 `wsl-box.example.yaml` 与 C++ 默认一致。
- 主机后缀：`sentry.io`（覆盖 `*.sentry.io`、`o1158394.ingest.us.sentry.io`）、全部列出的 Datadog intake 主机（`browser-intake-datadoghq.com`、`us3`/`us5`/`ap1`/`ap2`、`.eu`、`ddog-gov.com`）。
- 路径规则：`api.anthropic.com/api/event_logging`、`/api/event_logging`、`/api/v2/rum`。边界语义不变：拦 `/api/event_logging` 与 `/api/event_logging/v2/batch`，不拦 `/api/event_logging2`、`/api/v2/rumble`，也不整段拦 `api.anthropic.com/v1/messages`。
- 裸 `sentry.io` 现在会被拦截；原先文档化相反行为的单测已改。
- `Test-ClaudeBoxGatewayHostBlocked` 仍只做主机后缀匹配，含 `/` 的路径规则会被跳过。路径级执行落在 C++ `request_blocked` 与 gost hopGroup。

**B. gost 计划真正编码黑名单**

- `plan_relay` / `New-ClaudeBoxGatewayRelayPlan` 改为 `gost -C <prefix>/var/gateway/gost.relay.yml`（go-gost v3）。
- 主机拒绝：bypass 默认黑名单模式（命中即终止转发，不是绕行直连），matcher 为 `sentry.io` + `*.sentry.io` 以及各 Datadog 主机。
- 路径拒绝：hopGroup + `PathRegexp("^…(?:/|\?|#|$)")`（不用 PathPrefix，以免误伤 `/api/event_logging2`）。需要 go-gost ≥ 3.3.0。
- 主 API 主机不会被写成 host bypass。
- 本机用 **go-gost 3.3.3**（只下到 `/tmp/gost-v3`，不进仓库）对生成形状的 YAML 做了临时端口 config-check：`127.0.0.1:34407` 绑定成功；`sentry.io`、`o1158394.ingest.us.sentry.io`、`browser-intake-us3-datadoghq.com` 返回 **403** 且未到上游。测完已拆除，未改系统代理，无残留监听。
- **残留（如实）**：HTTPS CONNECT 到 `api.anthropic.com` 在不做 TLS MITM（默认/MVP 不做）时看不到路径，因此路径规则只对可见 HTTP 路径生效。本轮临时 bind 里，指向 `api.anthropic.com` 的 HTTP 请求（`/v1/messages`、`/api/event_logging`、near-miss）都返回 500，未能在活 gost 上把路径允许/拒绝区分开。C++ 匹配器与 YAML 中的 PathRegexp 已覆盖这些路径；活路径拦截仍待有真实 HTTP 代理上游（及真机 Clash）时再核验。

**C. RE 标明尚未实现的 spoof 环境变量**

- `SENTRY_NAME` 与 `CLAUDE_BOX_HOSTNAME` 写入 `env/spoof.env` 与 `env/session.env`，值为盒子主机名（默认 `claude-box`），**不是**宿主真实主机名（本机探测为 `grok-bot-vm-456957338`）。
- `ICU_TIMEZONE_FILES_DIR` 指向前缀 `etc/zoneinfo`，只安装伪装时区（及 UTC），不含宿主 `Asia/Tokyo`。不改写宿主 `/etc/localtime`、`/etc/timezone` 或宿主 ICU 路径。
- Node Intl 在设置了这些变量后仍报告 `America/Los_Angeles`。`apply-spoof.sh` 同步写入这些变量（非 root 时仍跳过宿主 `/etc`）。

**D. 其它一致性**

- `session.env` / `companion.env` 导出 `HTTP_PROXY` / `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY=localhost,127.0.0.1`（及小写）。拦截在缺代理时仍 fail-closed。
- `start-claude-desktop.sh` 与 C++ `desktop::start` 对齐：缺代理则 exit 2，**不** exec Desktop；检查放在 `apply-spoof.sh` 之前。
- 保险库文档已收紧：强制失败时失败的目标不会成为当前身份；切换前同身份的 pre-switch 安全归档可能成为当前指针。未改事务代码。

### 多轮模拟（每轮新鲜或重置前缀；每轮结束核宿主安全）

在 `/tmp/claude-box-fix-rounds` 驱动（不提交）上跑过多轮，修正快照误报与启动脚本顺序后，汇总 **PASS=42 FAIL=0**（2026-11-12 22:11:05 JST / Asia/Tokyo）。要点：

| 轮次 | 结果 | 证据摘要 |
|------|------|----------|
| 1 幸福路径（驱动跑 2 次） | PASS | 默认策略；Intl=`America/Los_Angeles`；SENTRY_NAME/ICU 在 spoof+session env；gost YAML 含完整 RE §4 且不主机拒绝主 API；伴生 intercept 带独立 profile 与 HTTP_PROXY；Desktop dry-run 未真启动 |
| 2 遥测矩阵（跑 2 次，example yaml + C++ 默认） | PASS | DENY：裸 `sentry.io`、`ingest.us.sentry.io`、`o1158394…`、全部列出的 Datadog intake、`/api/v2/rum`、`/api/event_logging`、`/v2/batch`。ALLOW：`/v1/messages`、`/api/event_logging2`。`host_blocked(api.anthropic.com)=false` |
| 3 拒绝路径 | PASS | 缺/非法上游拒绝；`hide_identifiers=false` 被拒绝或强制回 true；拒绝 `C:\`、整棵 `C:\Users`、`/mnt/c`；缺引擎不回落到宿主浏览器；缺 Desktop 二进制 honestly not-started；缺代理 C++ 与 shell 均不启动（shell rc=2，无 exec；重复 3 次） |
| 4 IP 变化 | PASS | 默认 OFF 为 no-op；armed dry-run 在 t=0 并行断开网关 + 礼貌关闭；firefox/sshd 不被瞄准。官方 `test_gateway_ip` 通过 |
| 5 保险库 | PASS | 前缀内 archive / switch empty / rollback；错误口令未出现在输出中；官方 `test_vault` 覆盖「强制失败不会让失败目标成为当前」 |
| 6 宿主安全（每轮结束） | PASS | `/etc/localtime` → `Asia/Tokyo`，md5 `38620155fabd5572c5a4b1db051b3cc8`，mtime 未变；`/etc/timezone=Asia/Tokyo`；默认浏览器 `box-chrome.desktop`；`LANG=C.UTF-8`；无 HTTP(S)/ALL_PROXY |
| 7 幂等 | PASS（2 次） | spoof apply 两次；plan_relay 两次 YAML 稳定；`install_intercept` 两次（9bc6a88 回归仍绿） |
| 8 gost 临时 bind | 见上 B | 3.3.3 接受 YAML；主机拒绝得 403；路径级 HTTP 在本次 bind 上未区分（均 500）；已拆除，无残留 gost |

另外：`tests/wsl-box/test-linux-framework.sh` 为 `ALL GREEN (linux framework)`（含 start 脚本拒 exec）；`tests/Test-Structure.ps1` 为 `RESULT: PASSED`。

### P11

同一条绿线，连续跑了两遍，均为 exit 0：

```
ALL GREEN (P11 near-beta dry-run integration: P1–P10 proven on this cloud computer; real-machine W1 not started)
```

### 仍未关闭 / 不得标成功的项

- Win11 真机 W1、活挂载、活 gost+Clash、活 Desktop Intl、活 OA、活保险库登录、官方 WSL 支持风险：仍未在真机证明。
- gost 路径级规则在 HTTPS CONNECT 上不可见（无 TLS MITM）；本次临时 bind 也还没把 HTTP 路径允许/拒绝分成两种明确响应。
- 不隐藏 WSL 痕迹、不加 asar/JS hook：仍明确不在范围内。
- 官方 Anthropic-WSL 风险仍按原文记录，不得改成「已成功」。

本 follow-up **不**声称 beta 已在真机完成。
