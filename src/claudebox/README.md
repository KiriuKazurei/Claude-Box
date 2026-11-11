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
