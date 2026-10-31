# cbox — Claude Box 容器运行时（Rust，C0 骨架，**未实现、未在 Windows 编译**）

Docker 风格 CLI：`cbox import / run / ps / stop / rm`。image = 解包的 Claude app 目录；instance = image + spec（`cbox.toml`）+ 容器根。
诚实限制：Windows 没有内核 namespace，这是基于本地用户 / 受限 token / job object / ACL / WFP 的「Docker 式体验」，不是真正的容器。

- `crates/cbox-cli`：CLI（clap）
- `crates/cbox-core`：spec / 存储 / 状态（跨平台，可在 Linux 上单测）
- `crates/cbox-win`：Windows 后端（windows-rs）
- 计划：`crates/cbox-hook`（C2，cdylib hook DLL，retour 或 minhook 绑定）
- `examples/claude.cbox.toml`：示例 spec
