# ClaudeBox.Gateway（M2）

唯一网关：Internal 网上访客流量 → **gost 中继**（或等价工具）→ HTTP/SOCKS5 上游（常为宿主 **Clash**）+ **本地 JSONL 连接日志**。

## 能力（M2）

| 项 | 说明 |
|---|---|
| 配置应用 | `ConvertTo-ClaudeBoxGatewayRuntimeConfig` / `Start-ClaudeBoxGateway` |
| 上游 | `gateway.http` / `gateway.socks5` → Clash 端口 |
| 日志 | `Write-ClaudeBoxGatewayLogEntry` → JSONL：`ts,src,dst_ip,dst_port,sni_or_host,proto,action,upstream_ok,bytes`；`action`=`allow`\|`deny`\|`proxy` |
| 中继 | 优先 **gost**；无二进制时只写 relay plan（不假装已强制） |
| 验收 | `Test-ClaudeBoxDirectEgressBlocked`（文档步骤；真访客 **待验证**） |

**不做**：默认 TLS 解密；不在 Linux 开发机宣称 Hyper-V 已验证。

## 在 Win11 上怎么跑

> 需已建 Internal 交换机（`scripts/win11/New-ClaudeBoxSwitch.ps1`），宿主 `vEthernet` 上有 `listen_ip`（默认 `192.168.87.1`）。

### 1. 安装 gost

从 [go-gost/gost](https://github.com/go-gost/gost) 发布页下载 Windows amd64，放到其一：

- `%LOCALAPPDATA%\ClaudeBox\bin\gost.exe`
- 或加入 `PATH`

（仓库不托管二进制。）

### 2. 配置

```powershell
Copy-Item .\config\config.example.yaml .\config\config.yaml
# 确认 gateway.http / socks5 指向本机 Clash（如 127.0.0.1:7890）
# gateway.listen_ip / listen_port；logging.enabled / path
```

### 3. 启动网关

```powershell
cd path\to\Claude-Box
Set-ExecutionPolicy -Scope Process Bypass
Import-Module .\src\ClaudeBox.Gateway\ClaudeBox.Gateway.psd1
# 或：
.\scripts\win11\Start-ClaudeBoxGateway.ps1 -ConfigPath .\config\config.yaml
```

会写入 `%LOCALAPPDATA%\ClaudeBox\gateway\`（`runtime.json`、`relay-plan.txt`、`gost.relay.yml`、`gateway.pid`）。

停止：

```powershell
.\scripts\win11\Stop-ClaudeBoxGateway.ps1
```

### 4. 访客网络（访客内）

```powershell
# 在访客会话中：
.\src\ClaudeBox.Guest\Set-ClaudeBoxGuestNetwork.ps1 `
  -GatewayIp 192.168.87.1 -ProxyPort 3080 -Apply
```

要点：默认路由/网关指向盒子 `listen_ip`；系统代理指向 `http://listen_ip:listen_port`；**不要**给访客挂 Default/External Switch 抄近路。IP 转发/ICS 全放行会破坏「唯一网关」——**待验证**加固（防火墙仅放行代理口）。

### 5. 直连应失败

```powershell
.\scripts\win11\Test-ClaudeBoxDirectEgressBlocked.ps1
# 或在访客内按输出的 curl 步骤操作
```

### 6. 日志复盘

默认 `%LOCALAPPDATA%\ClaudeBox\logs\gateway.jsonl`。跑一段时间后按 `sni_or_host` / `dst_ip` 收紧 `gateway.blocklist`。

样例：`samples/gateway.log.example.jsonl`。

## Linux 开发机

可解析模块、写 JSONL、生成 relay plan；**不会**启动 gost 绑 Hyper-V NIC。

```powershell
pwsh -File tests/Test-Structure.ps1
```

## 与 Clash TUN

访客 → 盒子网关 → Clash **HTTP/SOCKS 口**。宿主开 TUN 只影响宿主其它应用，**不能**替代盒子强制网关。
