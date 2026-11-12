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

## 📝 项目概述

QmClient 客户端是基于 DDNet 和 TaterClient 构建的定制版本。
项目旨在提供更现代的 UI 体验、更丰富的视觉效果配置选项，同时保持与核心游戏玩法的兼容性。

## 📢 Fork 与衍生客户端声明

为维护 QmClient 项目声誉，防止第三方修改版本冒用项目身份、误导用户，现向所有 Fork 本仓库、进行二次开发、重新打包或分发衍生客户端的个人及组织，郑重声明以下要求：

**一、必须移除与 QmClient 官方远程服务器的通信**

所有 Fork 及二次开发项目，必须移除与 QmClient 官方远程服务器及相关接口通信的代码和配置，包括接口请求、数据上报、更新检查、公告获取、长连接等相关内容。需要使用相应功能的，应自行部署并维护独立服务。

不得擅自沿用官方服务地址、接入凭据或认证信息，不得通过代理转发、伪造客户端身份等方式继续接入。仅隐藏入口、默认关闭功能或修改界面显示，不视为完成移除。

**二、衍生项目必须使用独立名称与标识**

衍生客户端必须使用自己的项目名称、图标、版本标识及发布身份。未经明确授权，不得将“QM”“QmClient”“Qm 客户端”等名称、简称，以及本项目的 Logo、图标、启动画面和其他品牌资源，用作衍生客户端的名称、标识或宣传背书。

不得以“QmClient 增强版”“QmClient 内部版”“官方分支”“作者授权”等表述进行误导性宣传，也不得通过增加前后缀、变更大小写等方式，使用户误认为其属于 QmClient 官方版本。

**三、必须清理 `Version.cpp` 中的 QM 相关内容**

除注释外，`Version.cpp` 内不得保留任何与 QM／QmClient 有关的代码或标识，包括相关宏定义、常量、字符串、版本信息、客户端身份及官方服务关联内容。

衍生版本必须填写自身真实、独立的版本与身份信息，不得通过转移代码、字符串拼接或其他方式规避清理要求，更不得继续向用户或服务器标识为 QmClient 官方客户端。

**四、严正反对将本仓库用于任何作弊开发**

QmClient 坚决反对任何个人或组织利用本仓库开发、集成、发布、销售或传播包含作弊功能的客户端、插件、脚本及其他衍生内容，对此不予认可、背书或官方支持。

**严禁在添加作弊功能后，仍以 QM／QmClient 的名称发布、传播或宣传。** 无论修改规模大小、是否收费、是否公开发布，均不得借用本项目的名称、品牌资源、作者身份或官方服务为作弊内容获取信任。

QmClient 官方社区严禁发布、推广、交易或传播任何作弊内容。第三方加入的作弊功能及相关行为，不代表 QmClient 项目及维护者的立场。

**五、立即自查并整改**

请所有相关开发者立即检查已有仓库、构建产物、发布页面和宣传内容。存在冒用名称、保留官方服务器通信、伪装官方版本或传播作弊内容等情况的，应立即停止相关行为，并完成通信移除、独立命名、资源替换及版本标识清理。

对经核实存在上述行为的个人或项目，将视情况采取清理群内内容、移出官方社区、限制其接入 QmClient 官方服务及向相关平台投诉等措施。

本公告不变更代码及资源既有许可证。清理品牌标识时，仍须按照适用许可证保留必要的版权、许可及来源声明；客观说明代码来源，不得被用于暗示官方授权或认可。

**第三方修改版本必须以自己的身份发布，并对自己的修改和行为负责。QmClient 不接受任何人将作弊功能冠以本项目之名。**

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
