# ClaudeBox.Guest

在 **Hyper-V Windows 访客内** 运行的脚本：身份伪装、系统代理/强制出站、引导安装 Windows Claude、健康检查。

- 由 Control 注入；不要假设与宿主机共享文件系统（除可见目录）
- M0：占位脚本，不改系统
- 安装包不进仓库

```powershell
# Inside guest:
.\Initialize-ClaudeBoxGuest.ps1 -IdentityHostname claude-box -Timezone UTC -GatewayHttp http://192.168.87.1:8080
.\Test-ClaudeBoxGuestHealth.ps1
```
