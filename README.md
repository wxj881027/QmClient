# Q1menG Client

<p align="center">
   <img src="data\qmclient\gui_logo.png" alt="Q1menG Client Logo" width="380" />
</p>

<p align="center">
  基于 DDNet / TaterClient 的个性化客户端重制项目
</p>

## 📝 项目简介

Q1menG Client 是在 DDNet 与 TaterClient 基础上进行的定制化客户端改造。  
目标是在保持核心玩法兼容的前提下，提供更现代的 UI 体验、更多可配置视觉效果，以及更顺手的日常使用细节。

## ✨ 特色功能

- UI 平滑过渡与 HUD 动画
- 输入与交互体验增强
- 更多客户端配置项与个性化开关
- 保持与 DDNet 生态兼容的基础能力

## ❤️ Contributors

感谢所有为本项目提交代码、反馈问题与提出改进建议的贡献者。

[![Contributors](https://contrib.rocks/image?repo=wxj881027/Q1menG_Client)](https://github.com/wxj881027/Q1menG_Client/graphs/contributors)

## 🚀 构建方式

```sh
cmake -S . -B cmake-build-release
cmake --build cmake-build-release --target game-client -j 10
```

## ✅ 测试命令

```sh
cmake --build cmake-build-release --target run_cxx_tests
cmake --build cmake-build-release --target run_rust_tests
cmake --build cmake-build-release --target run_tests
```

## ⚙️ 推荐 UI 配置（示例）

```cfg
cl_hud_animations 1
cl_hud_animation_speed 100
```

## 🙏 Special Thanks

- DDNet / Teeworlds / DDRace / TaterClient 全体贡献者
- 参与测试、反馈和提供灵感的朋友们
- 所有为开源生态持续投入的人

## 🏛 Credits

- Teeworlds — Magnus Auvinen
- DDRace — Shereef Marzouk
- DDNet — Dennis Felsing and contributors
- TaterClient — Community modifications

## 📜 License

本项目遵循 zlib/libpng 许可协议，与上游 DDNet / TaterClient 保持一致。  
修改版本需要明确标注，不得歪曲原始作者身份。

## 📮 Notes

本项目为个人定制化修改，不代表 DDNet 或 TaterClient 官方立场。
