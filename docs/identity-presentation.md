# 身份展示：采集、设置页、按字段授权

> 决策日：2026-11-11（Asia/Tokyo）  
> 决策者：和零  
> 写入 SCOPE v0.4.0。实现按 [framework-build-plan.md](./framework-build-plan.md) 的 P1–P4 推进。

## 一句话

Claude Box 部署之后会采集这台电脑的有关信息，在设置页列出来。用户决定哪些原样提供给 Claude、哪些不展示、哪些修改后再展示。不是「采集后直接全部交给 Claude」，也不是「编码一律在网关里拦截、用户完全不能选」。

## 默认策略

| 类别 | 默认 | 用户可在设置页改为 |
|------|------|----------------------|
| 厂商 / 电脑型号（如 Manufacturer、Product Name） | **原样展示**（若已采集到） | 隐藏，或改成其他文案再展示 |
| 硬件编码与唯一设备标识：序列号、主板/机箱 UUID、MAC、磁盘序列号、CPU ID、Windows MachineGuid、设备实例路径等 | **隐藏**，不进入展示层 | 原样展示，或修改后再展示 |
| 时区 / 语言 | **只在盒子内伪装**，设置页展示的是盒子伪装值，不是把宿主真实时区默认交给 Claude | 仍只改盒子伪装配置；本清单不把「暴露宿主真实时区 / 语言」当作默认可选项 |

这仍不是「把环境装成普通真机」。内核名 `microsoft-standard-WSL2` 等虚拟化痕迹会保持可见，不当作缺陷去修，也不提供绕开检测的做法。

## 两层数据

1. **原始清单**（盒子内、设置页可读）：部署或刷新时采集的完整信息。默认不交给 Claude。不要把真实编码提交进 git。
2. **展示层**（Desktop / 进程可见）：只包含用户选择「原样展示」或「修改后展示」的字段。隐藏的字段不得出现在展示路径或 advertised env 里。

每字段三种模式：

- `expose`：把原始清单中的值写入展示层
- `hide`：不写入展示层
- `override`：写入用户改过的值

云电脑用虚构夹具（如 Contoso Computers / ContosoBook 15 G3）验证，不读取、不保存和零真机的编码。

## 与现行实现的过渡

P3 已把展示层生成接到 C++ 策略引擎：`apply-host-identity.sh` 调用 `claudebox-identity apply`，`verify-host-identity.sh` 按同一份策略校验。默认策略与上表一致。

`hide_identifiers: true` 仍是过渡约束，设为 false 时 apply / verify 必须失败。它**不是**「把原始清单全部公开」的开关；要原样展示或修改后再展示某个字段，写入每字段策略（`CLAUDE_BOX_IDENTITY_POLICY` 或盒子内 `var/host-identity.policy.yaml`）。宿主导出脚本目前仍只写厂商 / 型号。设置页（P4）已落地：`src/claudebox-ui/` 的 Qt 设置中心可读取原始清单、按字段选择并保存策略与展示层，同时也能编辑 `wsl-box.yaml` 的其他配置段。测试覆盖默认隐藏、选择展示、修改后展示，以及被隐藏或已覆盖的原始值不得泄漏进展示层。

## 设置页（P4）

控件（C++/Qt，`src/claudebox-ui/`）逐项列出已采集字段，并清楚标明当前是原样展示、隐藏还是修改后展示。保存后只更新盒子内配置、`var/host-identity.policy.yaml` 与展示层，不改宿主时区、语言或默认浏览器。时区 / 语言页展示的是盒子伪装值，不提供「暴露宿主真实时区 / 语言」作为清单选项。`hide_identifiers` 在配置里保持为 true，不能当作整份公开开关。

云电脑可用虚构夹具打开设置页，见 [src/claudebox-ui/README.md](../src/claudebox-ui/README.md)。
