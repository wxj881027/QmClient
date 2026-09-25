# 网易云注入自检（cmd 排查步骤）

面向用户反馈「DLL 注入不到网易云里」的排查清单。所有命令都在 `cmd.exe` 里执行，实测环境：网易云 3.1.36.205322 x64（`D:\CloudMusic`）。

配套脚本：`qmclient_scripts/support/check_netease_inject.py` 生成的 `tmp/check_netease_inject.bat`，内容与本文件命令一致。

## 0. 先认清两个不同的东西

| 环节 | 落点 | 作用 |
| --- | --- | --- |
| 引导 `version.dll` | 网易云安装目录（如 `D:\CloudMusic\version.dll`） | 让网易云启动时带上 `--remote-debugging-port`，并代理 `GetCommandLineW/A` |
| 注入 `qm-nmt-hook64.dll` | 运行中的 `cloudmusic.exe` 主进程 | 抓取渲染文本 / 播放进度（GDI+、winmm hook） |

代码依据：`src/qm-nmt-hook/qm_netease_hook_helper.cpp`（`TARGET_EXE`、`TARGET_DLL`、`Inject`、`IsAlreadyInjected`）、`qm_netease_bootstrap.h`（`BOOTSTRAP_TARGET_NAME=version.dll`、`BOOTSTRAP_SOURCE_NAME=qm-nmt-bootstrap.dll`）。

**注入成功 ≠ 歌词能显示。** 第 1 步只回答「DLL 有没有进进程」。

## 1. 关键命令：查 DLL 是否进了进程

```cmd
tasklist /fi "imagename eq cloudmusic.exe" /m qm-nmt-hook64.dll
```

判读：

- 输出里出现 `cloudmusic.exe` + PID + `qm-nmt-hook64.dll` → **注入成功**。
- 输出 `INFO: No tasks are running which match the specified criteria.` → 该模块不在任何 `cloudmusic.exe` 里，即没注入成功或注入后已退出。

只看模块名、不关心进程名时：`tasklist /m qm-nmt-hook64.dll`（PID 列出来后可用 `tasklist /svc /fi "pid eq <PID>"` 之类进一步查）。

这一条之所以可信：helper 自己就是用同一份模块快照做的判断（`IsAlreadyInjected` 枚举 `TH32CS_SNAPMODULE`，比对模块 basename），所以 `tasklist /m` 能看到 = helper 也认为已注入。注意 `tasklist` 读不到其他用户/更高权限进程的模块表，无输出时先确认不是权限问题（必要时用管理员 cmd 重跑）。

## 2. 辅助命令

```cmd
:: a. 注入器在不在跑（正常应是随 QmClient 启动的 qm-nmt-helper.exe --watch）
tasklist /fi "imagename eq qm-nmt-helper.exe"

:: b. 目标的位数，必须是 64 位，否则按设计直接跳过
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='cloudmusic.exe'\" | Where-Object { $_.CommandLine -notmatch '--type=' } | ForEach-Object { $_.ProcessId.ToString() + '  ' + $_.CommandLine }"

:: c. 注入的 DLL 是不是真的在客户端目录里（helper 从自己所在目录取 DLL，不是网易云目录）
cd /d <QmClient 目录>
dir /b qm-nmt-hook64.dll qm-nmt-helper.exe

:: d. 引导 version.dll 是否是 QmClient 自己的（不是别家代理）
findstr /m /c:"QmClient.NeteaseBootstrap.v1" "D:\CloudMusic\version.dll"
```

`b` 同时能验证引导：主进程命令行带 `--remote-debugging-address=127.0.0.1 --remote-debugging-port=<端口>`（端口在 40000–49999 之间按 PID 推导）说明 `version.dll` 生效。

## 3. 判读「第 1 步为 No tasks」的常见原因

按代码里的实际门槛，优先级从高到低：

| 现象 | 证据 / 原因 | 处理 |
| --- | --- | --- |
| helper 不在（辅助 a 无输出） | helper 由客户端启动；客户端没起来、helper 已退出（DLL 丢失时 `Watch` 返回 3）或异常结束 | 重启 QmClient，再看 `tasklist /fi "imagename eq qm-nmt-helper.exe"` |
| 目标不是 64 位 | `IsTargetArchitecture` 要求 `IsWow64Process2` 报 `ProcessMachine==UNKNOWN && NativeMachine==AMD64` | 装 x64 网易云；32 位网易云不支持 |
| 客户端目录缺 `qm-nmt-hook64.dll` | helper 找不到 DLL 直接返回 3，不会注入 | 确认 `b`/`c`；从 QmClient 包或 `cmake-build-*/` 复制同版本 DLL |
| DLL 位数/版本与客户端不匹配 | `cmake-build-debug` 与 `cmake-build-release` 的 DLL 不是同一份，混用相当于加载旧行为 | 用与客户端同一次构建产出的 DLL |
| 权限不足 | 网易云以管理员运行、QmClient 普通权限 → `OpenProcess` 失败，或 `LoadLibraryW` 失败 | 让两者同权限级别（都用管理员或都不用） |
| 安全软件拦截远程线程 | `CreateRemoteThread` + `LoadLibraryW` 被拦（两者是同一入口函数，被杀软重点监控） | 加白名单后重启客户端重试 |
| 无 `cloudmusic.dll` 时不会装 hook | DLL 进得去，但 `InstallHooks` 要等同目录 `cloudmusic.dll` | 第 1 步成功但歌词不出，先排查这一层 |

第 1 步成功、歌词仍不出时，顺序查：`version.dll` 是否生效（辅助 b / d）→ 前端 CDP 是否连上 → hook 是否装成。这三层与注入是不同故障面，不要混为一谈。

## 4. 回给用户的采集清单

让反馈者只做两件事，输出就能定位大部分问题：

```cmd
tasklist /fi "imagename eq cloudmusic.exe" /m qm-nmt-hook64.dll
tasklist /fi "imagename eq qm-nmt-helper.exe"
```

再补一句网易云版本号与安装路径、以及网易云/QmClient 是否以管理员运行。
