# identity-vault（P9）

盒子内 Claude 身份保险库。策略见 [docs/identity-vault.md](../../../docs/identity-vault.md)。

实现在 C++ 核心 `claudebox/vault.hpp` 与命令行 `claudebox-vault`。设置页「身份保险库」可列出快照，并对一个前缀执行归档、切换和回滚。

加密：AES-256-GCM + PBKDF2-HMAC-SHA256（OpenSSL 3）。不必与 CCBaga `.baga` 二进制兼容。口令只从 `CLAUDE_BOX_VAULT_PASSPHRASE_FILE` 或 `CLAUDE_BOX_VAULT_PASSPHRASE` 读取，不会写入日志或设置页状态。

默认目录：`<prefix>/var/identity-vault`（gitignore 的 `.run/` 下同样适用）。也可配置为发行版 `/var/lib/claude-box/identity-vault` 或 `%LOCALAPPDATA%\ClaudeBox\identity-vault\`。拒绝把宿主默认 Claude 目录或宿主浏览器 profile 当作保险库或工作副本。

约束：不改宿主时区、语言、默认浏览器或系统代理。切换前的进程结束在云电脑上只做 dry-run。
