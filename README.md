# QmClient Qm客户端

<p align="center">
   <img src="data/qmclient/gui_logo.png" alt="Q1menG 客户端标志" style="width:60%; max-width:760px;" />
</p>

<p align="center">
  基于 DDNet / TaterClient 构建的定制客户端项目
</p>

> 本文档另有 [English 版本](README_en.md)

## 📝 项目概述

QmClient 客户端是基于 DDNet 和 TaterClient 构建的定制版本。
项目旨在提供更现代的 UI 体验、更丰富的视觉效果配置选项，同时保持与核心游戏玩法的兼容性。

## 🛡️ 原创代码、美术资源与创作立场声明

**一、创作立场：尊重真实体验，拒绝粗制滥造**

QmClient 的每一项原创功能与交互细节，均源于团队成员的真实游玩与反复测试打磨。我们始终认为，**游戏开发者应当亲自游玩自己的游戏、亲手测试自己的功能**，这是最基本的底线。

我们尊重每一位真正热爱游戏、踏实打磨体验的开发者，坚决反对脱离实际游玩的“许愿式开发”、由 AI 拼凑合成的低质量项目，以及噱头大于实际、缺乏基本测试的粗制滥造搬运。我们不接受团队用心打磨的想法与功能被以这种方式糟践。

**二、原创代码与设计版权（保留所有权利）**

除 DDNet 与 TaterClient 上游开源代码外，QmClient 的所有原创代码（包括但不限于自研 UI 框架、视觉渲染特效、交互模块、客户端功能实现等）**仅作源码公开，保留所有权利，不包含任何形式的学习参考、复制或使用授权**。

未经 QmClient 团队明确书面许可，任何个人或组织不得抄袭、照搬、复制、移植、整合本项目的原创代码至其他项目，亦不得将其用于任何商业用途。上述限制同样适用于本项目的**原创交互设计、UI/UX 呈现及整体功能方案**——严禁未经授权，通过直接搬运、二次改写或借助 AI 复刻、洗稿等方式照搬 QmClient 的原创设计与功能，尤其反对将其用于由 AI 拼凑合成的低质量项目。

**三、美术资源版权声明**

`data/qmclient` 目录下的所有美术资源（包括但不限于图标、Logo、界面素材、贴图、启动画面等），**版权均归对应画师所有，不适用项目上游的开源许可证（如 zlib/libpng）**，仅授权 QmClient 官方版本使用。

严禁任何个人或组织对上述美术资源进行商业使用、二次修改、二次分发、提取复用或用于 AI 模型训练。任何 Fork 或衍生项目均须彻底删除或替换该目录下的全部美术资源。

**四、第三方质量免责**

任何未经授权擅自搬运、魔改、拼凑或复刻 QmClient 功能所产生的第三方客户端，其引发的崩溃、BUG、体验劣化、安全隐患或账号风险均与 QmClient 项目无关。官方对此类第三方产物不提供任何技术支持，亦不为其质量背书。

## 📢 Fork 与衍生客户端声明

为维护 QmClient 项目声誉与原创权益，防止第三方修改版本冒用项目身份、未经授权搬运原创内容或误导用户，现向所有 Fork 本仓库、进行二次开发、重新打包或分发衍生客户端的个人及组织，郑重声明以下要求：

**一、超过 20 人规模的 Fork 与二次开发须提前获准**

无论是**参与开发的人数**，还是**面向的用户及分发规模**，只要涉及**超过 20 人**，在进行 Fork 或二次开发前，必须先通过本仓库的 **GitHub Issue** 与 QmClient 团队取得联系，并在获得明确同意后方可开展。

即使获得二次开发许可，除团队另行书面授权外，仍须严格遵守本声明及上述原创代码、美术资源版权条款。

**二、必须移除与 QmClient 官方远程服务器的通信**

所有 Fork 及二次开发项目，必须移除与 QmClient 官方远程服务器及相关接口通信的代码和配置，包括接口请求、数据上报、更新检查、公告获取、长连接等相关内容。需要使用相应功能的，应自行部署并维护独立服务。

不得擅自沿用官方服务地址、接入凭据或认证信息，不得通过代理转发、伪造客户端身份等方式继续接入。仅隐藏入口、默认关闭功能或修改界面显示，不视为完成移除。

**三、衍生项目必须使用独立名称与标识，并清理美术资源**

衍生客户端必须使用自己的项目名称、图标、版本标识及发布身份，并彻底移除或替换 `data/qmclient` 目录下的所有美术资源。未经明确授权，不得将“QM”“QmClient”“Qm 客户端”等名称、简称，以及本项目的 Logo、图标、启动画面和其他品牌资源，用作衍生客户端的名称、标识或宣传背书。

不得以“QmClient 增强版”“QmClient 内部版”“官方分支”“作者授权”等表述进行误导性宣传，也不得通过增加前后缀、变更大小写等方式，使用户误认为其属于 QmClient 官方版本。

**四、必须清理 `Version.cpp` 中的 QM 相关内容**

除注释外，`Version.cpp` 内不得保留任何与 QM／QmClient 有关的代码或标识，包括相关宏定义、常量、字符串、版本信息、客户端身份及官方服务关联内容。

衍生版本必须填写自身真实、独立的版本与身份信息，不得通过转移代码、字符串拼接或其他方式规避清理要求，更不得继续向用户或服务器标识为 QmClient 官方客户端。

**五、严正反对将本仓库用于任何作弊开发**

QmClient 坚决反对任何个人或组织利用本仓库开发、集成、发布、销售或传播包含作弊功能的客户端、插件、脚本及其他衍生内容，对此不予认可、背书或官方支持。

**严禁在添加作弊功能后，仍以 QM／QmClient 的名称发布、传播或宣传。** 无论修改规模大小、是否收费、是否公开发布，均不得借用本项目的名称、品牌资源、作者身份或官方服务为作弊内容获取信任。

QmClient 官方社区严禁发布、推广、交易或传播任何作弊内容。第三方加入的作弊功能及相关行为，不代表 QmClient 项目及维护者的立场。

**六、立即自查整改、侵权追责与社区监督**

请所有相关开发者立即检查已有仓库、构建产物、发布页面和宣传内容。存在未经许可擅自抄袭或 AI 洗稿复刻原创代码与设计、盗用美术资源、冒用名称、保留官方服务器通信、伪装官方版本或传播作弊内容等情况的，应立即停止相关行为，并完成代码与资源清理、通信移除、独立命名及版本标识替换。

对经核实存在上述侵权或违规行为的个人或项目，QmClient 团队将视情况采取清理群内内容、移出官方社区、限制其接入 QmClient 官方服务等措施，并**保留向 GitHub 及相关平台发起 DMCA 下架投诉及进一步追究法律责任的权利**。

欢迎广大玩家与社区成员共同监督。如发现未经授权的搬运、AI 洗稿拼凑、盗用画师美术资源或冒用身份的衍生客户端，欢迎通过本仓库的 **GitHub Issue** 向团队反馈。

本公告不变更上游代码的既有许可证。清理品牌标识与原创内容时，仍须按照适用许可证保留上游项目的必要版权、许可及来源声明；客观说明代码来源，不得被用于暗示官方授权或认可。

**第三方修改版本必须以自己的身份发布，并对自己的修改和行为负责。QmClient 不接受任何人将作弊功能冠以本项目之名，亦不接受任何未经授权的抄袭与糟践。**

## ✨ 功能特性

- **现代 UI 与自绘动效**
  基于 Skia 与 RmlUi 打造的现代界面体系，支持流畅的 UI 过渡、HUD 动画、自定义侧边栏以及灵动岛风格的悬浮组件，告别传统界面的生硬感。
- **玩法视觉与特效定制**
  面向 DDNet 与 Gores 玩法深度打磨，提供子弹碰撞可视化、图块轮廓高亮、自定义粒子拖尾等丰富的视觉效果与精细化配置选项。
- **增强输入与日常交互**
  集成 SDL3 输入法（IME）候选框原生渲染，大幅优化游戏内的中文输入与交互体验，同时保留社区趣味彩蛋与启动画面。
- **游戏内音乐与歌词支持**
  整合多平台歌词获取与解析能力，支持在游玩过程中同步展示实时歌词与媒体信息。
- **核心生态完整兼容**
  在全面升级视觉表现与交互细节的同时，保持与 DDNet 服务器、地图及核心玩法的完整兼容性。

## ❤️ 贡献者

感谢每一位为 QmClient 提交代码、反馈问题、参与测试和提出改进建议的贡献者。

[![贡献者](https://contrib.rocks/image?repo=wxj881027/QmClient)](https://github.com/wxj881027/QmClient/graphs/contributors)

## 🚀 构建

### Windows

使用仓库自带的包装脚本，即使在普通的 PowerShell 或 `cmd.exe` 会话中，也能确保 `cmake` 在正确配置的 MSVC 开发环境中运行：

```bat
qmclient_scripts/cmake-windows.cmd -S . -B cmake-build-release
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 14
```

### macOS / Linux / 已初始化的开发环境

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

### macOS / Linux / 已初始化的开发环境

```sh
cmake --build cmake-build-release --target run_cxx_tests
cmake --build cmake-build-release --target run_rust_tests
cmake --build cmake-build-release --target run_tests
```

## 🙏 特别感谢

- DDNet、Teeworlds、DDRace、TaterClient、RClient、Best Client 与 Cactus Client 的所有贡献者
- 参与实机测试、提供真实反馈与启发灵感的朋友们
- 持续为开源社区贡献力量的每一位开发者
- 所有支持与捐赠本项目的朋友 – *感谢你们*

## 🏛 致谢

### 游戏与客户端上游

- Teeworlds — Magnus Auvinen
- DDRace — Shereef Marzouk
- DDNet — Dennis Felsing 及全体贡献者
- TaterClient — 社区修改版本
- Best Client — 社区修改版本

### 音乐与歌词生态

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

本项目基于 DDNet 和 TaterClient 构建，**仅 DDNet 与 TaterClient 的上游代码沿用 zlib/libpng 许可证**。

**QmClient 的原创代码以及 `data/qmclient` 目录下的所有美术资源均不适用上述开源许可证，保留所有权利。** 任何修改版本均须明确标注来源，不得歪曲原作者身份，且不得违反本仓库的版权与衍生客户端声明。
