#Requires -Version 5.1
# C0 skeleton wrapper. NOT IMPLEMENTED: only validates params and creates the container root.
param(
    [Parameter(Mandatory)][string]$ContainerRoot,
    [Parameter(Mandatory)][string]$ClaudeExe,
    [string]$Gateway = 'http://127.0.0.1:7890'
)
if ($ContainerRoot -match '^[Cc]:\\?$') { throw 'ContainerRoot 不能是宿主 C:\ 根' }
if (-not (Test-Path $ClaudeExe)) { throw "找不到 $ClaudeExe" }
foreach ($d in 'profile','app','logs') { New-Item -ItemType Directory -Force -Path (Join-Path $ContainerRoot $d) | Out-Null }
Write-Warning 'C0 启动器尚未实现（见 Launcher.cs TODO）。未启动任何进程。'
