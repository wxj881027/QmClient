# Q1menG Client

<p align="center">
   <img src="data/qmclient/gui_logo.png" alt="Q1menG Client Logo" style="width:60%; max-width:760px;" />
</p>

<p align="center">
  A project to rebuild a customised client based on DDNet / TaterClient
</p>

## [📝 Project Overview](README_zh.md)

[Q1menG Client is a customised client built upon DDNet and TaterClient.
The aim is to provide a more modern UI experience, a wider range of configurable visual effects, and more user-friendly day-to-day features, whilst maintaining compatibility with the core gameplay.](README_zh.md)

## ✨ Features

- Smooth UI transitions and HUD animations
- Enhanced input and interaction experience
- More client configuration options and customisation settings
- Core capabilities that remain compatible with the DDNet ecosystem

## ❤️ Contributors

We would like to thank all contributors who have submitted code, reported issues and suggested improvements for this project.

[![Contributors](https://contrib.rocks/image?repo=wxj881027/QmClient)](https://github.com/wxj881027/QmClient/graphs/contributors)

## 🚀 Build

### Windows

Use the repository wrapper so `cmake` always runs inside a configured MSVC developer environment, even from a normal PowerShell or `cmd.exe` session:

```bat
qmclient_scripts/cmake-windows.cmd -S . -B cmake-build-release
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 14
```

### macOS / Linux / already-initialised developer shell

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

### macOS / Linux / already-initialised developer shell

```sh
cmake --build cmake-build-release --target run_cxx_tests
cmake --build cmake-build-release --target run_rust_tests
cmake --build cmake-build-release --target run_tests
```

## 🙏 Special Thanks

- All contributors to DDNet, Teeworlds, DDRace, TaterClient, Best Client, RClient and CactusClient
- Friends who have taken part in testing, provided feedback and offered inspiration
- Everyone who continues to contribute to the open-source community
- All donors – thank you

## 🏛 Credits

- Teeworlds — Magnus Auvinen
- DDRace — Shereef Marzouk
- DDNet — Dennis Felsing and contributors
- TaterClient — Community modifications
- Best Client — Community modifications
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

This project is based on DDNet and TaterClient. Upstream code remains under the zlib/libpng licence.
Modified versions must be clearly attributed and must not misrepresent the identity of the original authors.

## 📮 Notes

This project is a personalised customisation and does not represent the official stance of DDNet or TaterClient.
