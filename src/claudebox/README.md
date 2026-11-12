# claudebox · C++ 核心（P1 起）

身份策略引擎：读取电脑信息清单，按每字段的 `expose` / `hide` / `override` 生成展示层，并校验展示层没有泄漏被隐藏的值。

默认策略：厂商 / 型号可原样展示；序列号、UUID、MAC 等硬件编码隐藏。用户策略可以改任一字段。时区 / 语言不在本库处理。

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
