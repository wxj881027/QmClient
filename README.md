# QmClient Qm客户端

<p align="center">
   <img src="data/qmclient/gui_logo.png" alt="Q1menG 客户端标志" style="width:60%; max-width:760px;" />
</p>

<p align="center">
  基于 DDNet / TaterClient 构建的定制客户端项目
</p>

> 本文档另有 <a href="README_en.md"> 版本 English</p>
>
>
>

## [📝 项目概述](README.md)

[QmClient 客户端是基于 DDNet 和 TaterClient 构建的定制版本。
项目旨在提供更现代的 UI 体验、更丰富的视觉效果配置选项，同时保持与核心游戏玩法的兼容性。]

## ✨ 功能特性

- 流畅的 UI 过渡和 HUD 动画
- 增强的输入和交互体验
- 更丰富的客户端配置选项和自定义设置
- 保持与 DDNet 生态系统的核心兼容性

## ❤️ 贡献者

感谢所有为该项目提交代码、报告问题和提出改进建议的贡献者。

[![贡献者](https://contrib.rocks/image?repo=wxj881027/QmClient)](https://github.com/wxj881027/QmClient/graphs/contributors)

## 🚀 构建

### Windows

使用仓库包装脚本，`cmake` 始终在配置的 MSVC 开发环境中运行，即使从普通的 PowerShell 或 `cmd.exe` 会话：

```bat
qmclient_scripts/cmake-windows.cmd -S . -B cmake-build-release
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 14
```

### macOS / Linux / 已初始化的开发人员环境

```sh
cmake -S . -B cmake-build-release
cmake --build cmake-build-release --target game-client -j 14
```

## ✅ 测试

### Windows

```bat
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_cxx_tests
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_rust_tests
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_tests
```

### macOS / Linux / 已初始化的开发人员环境

```sh
cmake --build cmake-build-release --target run_cxx_tests
cmake --build cmake-build-release --target run_rust_tests
cmake --build cmake-build-release --target run_tests
```

## 🙏 特别感谢

- DDNet、Teeworlds、DDRace、TaterClient、RClient、Best Client 和 Cactus Client 的所有贡献者
- 参与测试、提供反馈和启发灵感的朋友们
- 继续为开源社区做出贡献的每一个人
- 所有捐赠者 – 感谢你们

## 🏛 致谢

- Teeworlds — Magnus Auvinen
- DDRace — Shereef Marzouk
- DDNet — Dennis Felsing 和贡献者
- TaterClient — 社区修改版本
- Best Client — 社区修改版本
- [BetterLyrics](https://github.com/jayfunc/BetterLyrics) — [jayfunc](https://github.com/jayfunc)
- [Lyricify Lyrics Helper](https://github.com/WXRIW/Lyricify-Lyrics-Helper) — [XY Wang (WXRIW)](https://github.com/WXRIW)
- [163MusicLyrics](https://github.com/jitwxs/163MusicLyrics)
- [NeteaseCloudMusicApi](https://github.com/Binaryify/NeteaseCloudMusicApi)
- [qq-music-api](https://github.com/Rain120/qq-music-api)
- [QQMusicApi](https://github.com/jsososo/QQMusicApi)
- [LyricCapture](https://github.com/ElliottSilence/LyricCapture)
- [QRCD](https://github.com/xmcp/QRCD)
- [ntextcat](https://github.com/ivanakcheurov/ntextcat)
- [LyricParser](https://github.com/HyPlayer/LyricParser)

## 📜 许可证

本项目基于 DDNet 和 TaterClient。上游代码仍遵循 zlib/libpng 许可证。
修改版本必须明确标注来源，不得歪曲原作者身份。

## 📮 说明

本项目为个人定制版本，不代表 DDNet 或 TaterClient 的官方立场。
