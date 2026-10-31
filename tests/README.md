# tests

| 测试 | 环境 | 说明 |
|---|---|---|
| `Test-Structure.ps1` | Linux pwsh 或 Win | M0：文件存在、PS 语法、模块可导入 |
| Hyper-V 集成 | 仅 Win11 | 见 `docs/architecture-v0.md` §11；尚未自动化 |

```bash
# On this Linux box (pwsh available):
pwsh -File tests/Test-Structure.ps1
```
