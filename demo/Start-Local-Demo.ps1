$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$serverPath = Join-Path $repoRoot 'build\native-gcc\win_server.exe'
$clientPath = Join-Path $repoRoot 'build\qt-client\qt_client.exe'

if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    throw "找不到服务端：$serverPath"
}

if (-not (Test-Path -LiteralPath $clientPath -PathType Leaf)) {
    throw "找不到 Qt 客户端：$clientPath"
}

$listener = Get-NetTCPConnection -LocalPort 9999 -State Listen -ErrorAction SilentlyContinue
if ($listener) {
    throw "端口 9999 已被进程 $($listener.OwningProcess) 占用。请先关闭旧服务端，或运行 Stop-Local-Demo.ps1。"
}

$server = Start-Process `
    -FilePath $serverPath `
    -WorkingDirectory (Split-Path -Parent $serverPath) `
    -PassThru

Start-Sleep -Seconds 1

if ($server.HasExited) {
    throw "服务端启动后立即退出，退出代码：$($server.ExitCode)"
}

$client = Start-Process `
    -FilePath $clientPath `
    -ArgumentList '--host', '127.0.0.1', '--port', '9999', '--connect' `
    -WorkingDirectory (Split-Path -Parent $clientPath) `
    -PassThru

Write-Host "本机演示已启动。服务端 PID：$($server.Id)，客户端 PID：$($client.Id)" -ForegroundColor Green
Write-Host '注意：本机连接会出现屏幕套屏，只用于练习和链路检查。' -ForegroundColor Yellow
Write-Host '结束时运行 demo\Stop-Local-Demo.ps1。'
