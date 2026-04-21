# setup-opus.ps1 - 生成 opus crate 所需的 MSVC 导入库
#
# audiopus_sys (opus crate 的依赖) 需要链接 opus 库。
# ddnet-libs 中的 opus.lib 是 MinGW 格式，MSVC 链接器无法使用。
# 此脚本从 ddnet-libs 中的 libopus.dll 生成正确的 MSVC 导入库。
#
# 用法: 在项目根目录运行
#   pwsh -File qmclient_scripts/setup-opus.ps1

param()

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..")

$DllDir = Join-Path $ProjectRoot "ddnet-libs" "opus" "windows" "lib64"
$OutputDir = Join-Path $ProjectRoot "src" "rust-bridge" "voice" "opus-prebuilt" "lib"

Write-Host "=== QmClient opus 库设置 ===" -ForegroundColor Cyan
Write-Host "项目根目录: $ProjectRoot"
Write-Host "DLL 目录:   $DllDir"
Write-Host "输出目录:   $OutputDir"

$DllPath = Join-Path $DllDir "libopus.dll"
if (-not (Test-Path $DllPath)) {
    Write-Error "找不到 libopus.dll: $DllPath"
    Write-Error "请先运行: git submodule update --init --recursive"
    exit 1
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) {
    $VsWhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $VsWhere)) {
    Write-Error "找不到 vswhere.exe，请安装 Visual Studio 2022"
    exit 1
}

$VsPath = & $VsWhere -latest -property installationPath
if (-not $VsPath) {
    Write-Error "找不到 Visual Studio 安装路径"
    exit 1
}

$MsvcPath = Get-ChildItem "$VsPath\VC\Tools\MSVC" | Sort-Object -Descending | Select-Object -First 1
$BinPath = Join-Path $MsvcPath.FullName "bin" "HostX64" "x64"

$Dumpbin = Join-Path $BinPath "dumpbin.exe"
$Lib = Join-Path $BinPath "lib.exe"

if (-not (Test-Path $Dumpbin)) {
    Write-Error "找不到 dumpbin.exe: $Dumpbin"
    exit 1
}
if (-not (Test-Path $Lib)) {
    Write-Error "找不到 lib.exe: $Lib"
    exit 1
}

Write-Host ""
Write-Host "步骤 1/3: 从 DLL 提取导出符号..." -ForegroundColor Yellow

$Exports = & $Dumpbin /EXPORTS $DllPath 2>&1 |
    Select-String "^\s+\d+\s+" |
    ForEach-Object { ($_ -split '\s+')[-1] } |
    Where-Object { $_ -match "^opus_" }

if (-not $Exports) {
    Write-Error "未能从 DLL 提取 opus 导出符号"
    exit 1
}

Write-Host "  找到 $($Exports.Count) 个导出符号"

$DefPath = Join-Path $OutputDir "opus.def"
$DefContent = "LIBRARY libopus`nEXPORTS`n" + ($Exports -join "`n")
[System.IO.File]::WriteAllText($DefPath, $DefContent, [System.Text.Encoding]::ASCII)

Write-Host "  已生成: $DefPath"

Write-Host ""
Write-Host "步骤 2/3: 生成 MSVC 导入库..." -ForegroundColor Yellow

$LibPath = Join-Path $OutputDir "opus.lib"
& $Lib /def:$DefPath /out:$LibPath /machine:x64 2>&1 | Out-Null

if (-not (Test-Path $LibPath)) {
    Write-Error "生成导入库失败"
    exit 1
}

Write-Host "  已生成: $LibPath"

Write-Host ""
Write-Host "步骤 3/3: 复制运行时 DLL..." -ForegroundColor Yellow

$RuntimeDlls = @("libopus.dll", "libwinpthread-1.dll")
foreach ($dll in $RuntimeDlls) {
    $src = Join-Path $DllDir $dll
    if (Test-Path $src) {
        $TargetDir = Join-Path $ProjectRoot "target" "debug" "deps"
        if (Test-Path $TargetDir) {
            Copy-Item $src $TargetDir -Force
            Write-Host "  已复制 $dll -> target/debug/deps/"
        }
    }
}

Write-Host ""
Write-Host "=== 设置完成! ===" -ForegroundColor Green
Write-Host ""
Write-Host "现在可以运行 cargo build / cargo test 了。"
Write-Host "注意: 运行测试或客户端时，libopus.dll 需要在 PATH 或可执行文件目录中。"
