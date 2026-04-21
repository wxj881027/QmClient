# Rust 代码检查脚本
# 用于 AI 生成代码后的手动检查
# 使用方法: .\qmclient_scripts\check-rust.ps1

$ErrorActionPreference = "Continue"
$rustBridgePath = Join-Path $PSScriptRoot "..\src\rust-bridge"

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "  Rust 代码检查工具" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host ""

$rustDirs = Get-ChildItem -Path $rustBridgePath -Directory -ErrorAction SilentlyContinue

if (-not $rustDirs) {
    Write-Host "错误: 未找到 Rust 模块目录" -ForegroundColor Red
    Write-Host "请确保在项目根目录运行此脚本" -ForegroundColor Yellow
    exit 1
}

$failedDirs = @()
$passedDirs = @()

foreach ($dir in $rustDirs) {
    Write-Host "正在检查: $($dir.Name)" -ForegroundColor Green
    Write-Host "路径: $($dir.FullName)" -ForegroundColor Gray

    Set-Location $dir.FullName

    # 1. 格式化代码
    Write-Host "  [1/4] 格式化代码..." -ForegroundColor DarkGray
    cargo fmt 2>&1 | Out-Null

    # 2. 快速编译检查
    Write-Host "  [2/4] 编译检查..." -ForegroundColor DarkGray
    cargo check 2>&1 | Out-String -OutVariable checkOutput
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  编译检查失败!" -ForegroundColor Red
        Write-Host $checkOutput -ForegroundColor Red
        $failedDirs += $dir.Name
        Set-Location $PSScriptRoot
        continue
    }

    # 3. Clippy 检查
    Write-Host "  [3/4] Clippy 检查..." -ForegroundColor DarkGray
    cargo clippy -- -W clippy::all 2>&1 | Out-String -OutVariable clippyOutput
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  Clippy 检查失败!" -ForegroundColor Red
        Write-Host $clippyOutput -ForegroundColor Red
        $failedDirs += $dir.Name
        Set-Location $PSScriptRoot
        continue
    }

    # 4. 运行测试
    Write-Host "  [4/4] 运行测试..." -ForegroundColor DarkGray
    cargo test 2>&1 | Out-String -OutVariable testOutput
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  测试失败!" -ForegroundColor Red
        Write-Host $testOutput -ForegroundColor Red
        $failedDirs += $dir.Name
        Set-Location $PSScriptRoot
        continue
    }

    Write-Host "  检查通过!" -ForegroundColor Green
    $passedDirs += $dir.Name
    Write-Host ""

    Set-Location $PSScriptRoot
}

# 汇总结果
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "  检查结果汇总" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan

if ($passedDirs.Count -gt 0) {
    Write-Host "通过模块 ($($passedDirs.Count)):" -ForegroundColor Green
    $passedDirs | ForEach-Object { Write-Host "  - $_" -ForegroundColor Green }
}

if ($failedDirs.Count -gt 0) {
    Write-Host ""
    Write-Host "失败模块 ($($failedDirs.Count)):" -ForegroundColor Red
    $failedDirs | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
} else {
    Write-Host ""
    Write-Host "所有模块检查通过!" -ForegroundColor Green
    exit 0
}
