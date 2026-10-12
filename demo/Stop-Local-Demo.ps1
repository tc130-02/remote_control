$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$names = @('win_server', 'win_client', 'qt_client')
$stopped = @()

foreach ($name in $names) {
    foreach ($process in (Get-Process -Name $name -ErrorAction SilentlyContinue)) {
        $path = $null
        try {
            $path = $process.Path
        } catch {
            continue
        }

        if ($path -and [IO.Path]::GetFullPath($path).StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
            Stop-Process -Id $process.Id -Force
            $stopped += "$($process.ProcessName) (PID $($process.Id))"
        }
    }
}

if ($stopped.Count -eq 0) {
    Write-Host '没有发现当前仓库启动的演示进程。'
} else {
    Write-Host '已结束以下演示进程：' -ForegroundColor Green
    $stopped | ForEach-Object { Write-Host "- $_" }
}
