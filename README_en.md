# Q1menG Client

<p align="center">
   <img src="data/qmclient/gui_logo.png" alt="Q1menG Client Logo" style="width:60%; max-width:760px;" />
</p>

<p align="center">
  A project to rebuild a customised client based on DDNet / TaterClient
</p>

> This document is also available in [中文](README.md).

## 📝 Project Overview

Q1menG Client (QmClient) is a customised client built upon DDNet and TaterClient.
The aim is to provide a more modern UI experience, a wider range of configurable visual effects, and more user-friendly day-to-day features, whilst maintaining compatibility with the core gameplay.

## 🛡️ Original Code, Art Assets & Development Stance

**1. Development Stance: Respect Real Gameplay, Reject Shoddy Imitation**

Every original feature and interaction detail in QmClient is the result of real gameplay and repeated hands-on testing by our team. We firmly believe that **game developers must actually play the game they develop and test their own features**—that is the most basic standard of the craft.

We respect every developer who genuinely plays the game and puts care into their work. At the same time, we strongly oppose "wishful-thinking" development detached from actual gameplay, low-quality projects cobbled together by AI, and gimmick-over-substance, untested rip-offs. We do not accept seeing our team's carefully refined ideas and features degraded in this manner.

**2. Original Code & Design Copyright (All Rights Reserved)**

Except for upstream open-source code from DDNet and TaterClient, all original code in QmClient (including but not limited to custom UI frameworks, visual rendering shaders/effects, interaction modules, and client feature implementations) is **source-available only, with All Rights Reserved. No licence is granted for learning, reference, copying, or any other use.**

Without explicit written permission from the QmClient team, no individual or organisation may plagiarise, copy verbatim, port, integrate our original code into other projects, or use it for commercial purposes. These restrictions also apply to QmClient's **original interaction designs, UI/UX presentation, and overall feature solutions**—unauthorised copying through direct porting, secondary rewriting, or AI-assisted recreation and paraphrasing is strictly prohibited, especially for low-quality AI-cobbled projects.

**3. Art Assets Copyright**

All art assets located in `data/qmclient` (including but not limited to icons, logos, UI graphics, textures, and splash screens) are **copyrighted by their respective artists. They are NOT covered by the upstream open-source licence (such as zlib/libpng)** and are licensed exclusively for use in official QmClient releases.

Commercial use, modification, redistribution, extraction/reuse, or use in AI model training is strictly prohibited. All forks and derivative projects must completely delete or replace all art assets under this directory.

**4. Third-Party Quality Disclaimer**

Any crashes, bugs, degraded user experience, security vulnerabilities, or account risks caused by third-party clients that copy, modify, cobble together, or recreate QmClient features without authorisation have no affiliation with QmClient. The official team provides no support for such builds and does not endorse their quality.

## 📢 Notice Regarding Forks & Derivative Clients

To protect the reputation and original work of QmClient, and to prevent third-party modified builds from impersonating the project, copying original work without authorisation, or misleading users, we hereby issue the following requirements to all individuals and organisations who fork this repository, perform secondary development, repackage, or distribute derivative clients:

**1. Prior Approval Required for Forks & Derivative Projects Involving Over 20 People**

Whether measured by **the number of developers/contributors** or **the target user base and distribution scale**, any fork or secondary development involving **more than 20 people** must first contact the QmClient team via a **GitHub Issue** in this repository and obtain explicit consent before proceeding.

Even if permission is granted, unless separately authorised in writing by the team, all terms in this notice and the original code & art asset copyright statements above remain strictly binding.

**2. Removal of Communication with Official QmClient Remote Servers**

All forks and derivative projects must completely remove all code and configurations that communicate with official QmClient remote servers and APIs—including API requests, telemetry/reporting, update checks, announcement fetching, and persistent connections. Projects requiring such services must deploy and maintain their own independent infrastructure.

You may not reuse official server addresses, access credentials, or authentication tokens, nor bypass this requirement via proxy forwarding or spoofed client identities. Merely hiding UI entries, disabling features by default, or altering display text does not constitute removal.

**3. Independent Naming, Branding & Art Asset Removal**

Derivative clients must use their own project name, icon, version identifier, and publishing identity, and must completely remove or replace all art assets in `data/qmclient`. Without explicit authorisation, names and abbreviations such as "QM", "QmClient", "Q1menG Client", or "Qm 客户端", as well as this project's logo, icons, splash screens, and other brand assets, may not be used for naming, branding, or promotional endorsement.

Misleading labels such as "QmClient Enhanced", "QmClient Internal", "Official Fork", or "Author Authorised" are prohibited, as are prefixes, suffixes, or casing changes intended to lead users to believe a build is an official QmClient release.

**4. Cleanup of QM-Related Content in `Version.cpp`**

Aside from comments, `Version.cpp` must not retain any code or identifiers associated with QM / QmClient, including macro definitions, constants, strings, version metadata, client identity, or references to official services.

Derivative builds must provide their own genuine, independent version and identity metadata, and must not evade this requirement through code relocation, string concatenation, or any other obfuscation, nor identify themselves as an official QmClient build to users or servers.

**5. Zero Tolerance for Cheat Development**

QmClient firmly opposes any individual or organisation using this repository to develop, integrate, release, sell, or distribute clients, plugins, scripts, or other derivatives containing cheat functionality. Such projects receive no recognition, endorsement, or official support.

**Publishing, distributing, or promoting any build with cheat functionality under the QM / QmClient name is strictly forbidden.** Regardless of modification scope, pricing, or whether distribution is public, you may not leverage this project's name, brand assets, author identity, or official services to gain trust for cheat content.

Posting, promoting, trading, or distributing cheat content is strictly banned in official QmClient communities. Cheat features added by third parties do not represent the stance of QmClient or its maintainers.

**6. Immediate Compliance, Enforcement & Community Reporting**

All relevant developers must immediately inspect their existing repositories, build artefacts, release pages, and promotional materials. Anyone found to be engaging in unauthorised copying or AI-assisted rewriting of original code/designs, stealing art assets, impersonating the project, retaining official server communications, or distributing cheats must immediately cease such activities and complete full cleanup of code, assets, server communications, branding, and version identifiers.

For verified violations, the QmClient team may remove related content from community groups, ban offending parties from official communities, restrict access to official QmClient services, and **reserves the right to submit DMCA / platform takedown notices to GitHub and other relevant platforms, as well as pursue further legal action**.

We welcome players and community members to help keep the community healthy. If you discover unauthorised ports, AI-cobbled clones, stolen artist assets, or impersonating derivative clients, please report them to our team via a **GitHub Issue** in this repository.

This notice does not alter the existing licence of upstream code. When removing QmClient branding and original content, you must still preserve required copyright, licence, and attribution notices for upstream projects in accordance with their applicable licences; factual attribution of code origin must not be used to imply official endorsement or authorisation.

**Third-party modified builds must be published under their own identity and bear full responsibility for their modifications and actions. QmClient does not accept anyone attaching its name to cheat functionality, nor does it tolerate unauthorised copying or the degradation of its original work.

## ✨ Features

- **Modern UI & Native Animations**
  Built with Skia and RmlUi for a modern interface experience, featuring smooth UI transitions, HUD animations, a customisable sidebar, and dynamic island-style overlays.
- **Gameplay Visuals & Custom Effects**
  Refined for DDNet and Gores gameplay with hook collision visualisation, tile outlines, custom particle trails, and extensive client customisation options.
- **Enhanced Input & Interaction**
  Integrates native SDL3 IME candidate rendering for a smoother in-game text input experience, alongside community splash screens and quality-of-life improvements.
- **In-Game Music & Lyrics Integration**
  Supports multi-platform lyrics fetching and parsing to display real-time lyrics and media info directly in-game.
- **Full Core Ecosystem Compatibility**
  Delivers a major upgrade to visuals and interaction while maintaining full compatibility with DDNet servers, maps, and core gameplay.

## ❤️ Contributors

We would like to thank everyone who has contributed code, reported issues, tested builds, and shared suggestions for QmClient.

[![Contributors](https://contrib.rocks/image?repo=wxj881027/QmClient)](https://github.com/wxj881027/QmClient/graphs/contributors)

## 🚀 Build

### Windows

Use the repository wrapper script so `cmake` always runs inside a properly configured MSVC developer environment, even from a standard PowerShell or `cmd.exe` session:

```bat
qmclient_scripts/cmake-windows.cmd -S . -B cmake-build-release
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 14
```

### macOS / Linux / Already-Initialised Developer Shell

```sh
cmake -S . -B cmake-build-release
cmake --build cmake-build-release --target game-client -j 14
```

## ✅ Test

### Windows

```bat
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_cxx_tests
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_rust_tests
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_tests
```

### macOS / Linux / Already-Initialised Developer Shell

```sh
cmake --build cmake-build-release --target run_cxx_tests
cmake --build cmake-build-release --target run_rust_tests
cmake --build cmake-build-release --target run_tests
```

## 🙏 Special Thanks

- All contributors to DDNet, Teeworlds, DDRace, TaterClient, RClient, Best Client, and CactusClient
- Friends who participated in hands-on testing, provided feedback, and sparked new ideas
- Everyone who continues to contribute to the open-source community
- All donors and supporters – thank you

## 🏛 Credits

### Game & Client Upstream
- Teeworlds — Magnus Auvinen
- DDRace — Shereef Marzouk
- DDNet — Dennis Felsing and contributors
- TaterClient — Community modifications
- Best Client — Community modifications

### Music & Lyrics Ecosystem
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

## 📜 License

This project is based on DDNet and TaterClient. **Only upstream code from DDNet and TaterClient remains under the zlib/libpng licence.**

**All original code in QmClient and all art assets in `data/qmclient` are excluded from the above open-source licence, with All Rights Reserved.** Modified versions must be clearly attributed, must not misrepresent the identity of the original authors, and must comply with the copyright and derivative client notices in this repository.