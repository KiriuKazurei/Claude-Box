# claudebox-ui · 设置页（P4 起）

C++20 + Qt 6 Widgets 设置中心。可以查看并修改 `wsl-box.yaml` 里已有的全部配置段，并用每字段策略决定宿主身份哪些原样展示、隐藏、或修改后再展示。

保存只更新盒子前缀里的配置、`var/host-identity.policy.yaml` 和展示层，**不会**改宿主时区、语言或默认浏览器。网关、伪装应用、伴生浏览器、Desktop 启停的运行时接线属于后续模块；本页把那些字段当作可编辑配置。

## 在云电脑上编译

需要 Qt 6 Widgets 开发包（Debian：`qt6-base-dev`、`qt6-base-dev-tools`）。本仓库核心库由本目录的 CMake 一并编译。

```bash
cmake -S src/claudebox-ui -B src/claudebox-ui/build
cmake --build src/claudebox-ui/build
```

## 用夹具打开设置页

云电脑没有真机清单，也不需要 Win11 / WSL。先把示例配置和虚构库存拷到一个前缀（不要直接保存覆盖仓库里的 `wsl-box.example.yaml`）：

```bash
PREFIX=/tmp/claude-box-settings
mkdir -p "$PREFIX/config" "$PREFIX/var"
cp src/wsl-box/config/wsl-box.example.yaml "$PREFIX/config/wsl-box.yaml"
cp tests/wsl-box/fixtures/host-inventory.raw.yaml "$PREFIX/var/host-inventory.raw.yaml"
./src/claudebox-ui/build/claudebox-settings --prefix "$PREFIX"
```

也可以显式传入路径：

```bash
./src/claudebox-ui/build/claudebox-settings \
  --prefix "$PREFIX" \
  --config "$PREFIX/config/wsl-box.yaml" \
  --inventory tests/wsl-box/fixtures/host-inventory.raw.yaml
```

无图形界面时用离屏插件做冒烟（不点选，只确认能启动）：

```bash
QT_QPA_PLATFORM=offscreen ./src/claudebox-ui/build/claudebox-settings --prefix "$PREFIX" --smoke
```

夹具里的厂商 / 型号是虚构的 Contoso Computers / ContosoBook 15 G3，不要换成任何人的真实编码。

## 页面结构

- **基座**：配置名、发行版名称、首选来源、安装目录
- **网关**：宿主、端口、上游 HTTP / SOCKS5、拦截名单、日志
- **可见目录**：宿主路径 / 客机路径 / 只读 的列表编辑（P5 起保存时会拒绝整挂 `C:`）
- **挂载**：`disable_automount`
- **身份伪装**：时区、locale、语言、主机名、Electron 语言（盒子伪装值）
- **宿主身份**：原始清单 + 每字段「原样展示 / 隐藏 / 修改后展示」。`hide_identifiers` 保持开启，不是整份公开开关
- **桌面**：deb URL / 本地路径、启动命令、额外参数、asar 补丁
- **功能**：排除宿主系统盘、computer use、IP 变化杀进程、伴生指纹浏览器、身份保险库

## 测试

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir src/claudebox-ui/build --output-on-failure
bash tests/claudebox/test-p4-gate.sh
```
