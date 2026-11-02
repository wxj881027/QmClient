# preview 收口合并 master

状态：2026-10-25 已完成代码集成、preview 分支删除和 Windows 安装包构建。Logo 行为与后续视觉验收继续由[开发者展示与节日侧栏规格](../规格/2026-10-25-开发者展示与节日侧栏.md)维护。

## 基线与合并

| 节点 | 提交或 PR |
| --- | --- |
| 原 master | `c61e057ec21674d58ec80f5b7b44e4fe1e8f4d02` |
| 本次修复前的 preview | `faa3ea75464f2925b2b4558e47f48e6b425ee130` |
| Logo 距离场修复 | `327c332ad4bccc025a2e77d4c2628e15244caa88`，由 [PR #509](https://github.com/wxj881027/QmClient/pull/509) 合入 preview |
| 收口时的 preview | `a5d5e7868591a36be15a2749354041ad96be4ecb` |
| 合并后的 master | `2e4cbabb9e915339d7f8b201bb3785572ae0acc6`，[PR #510](https://github.com/wxj881027/QmClient/pull/510) |

两次 PR 均使用普通 merge，保留已有提交历史。原 master 是 preview 的祖先；合并完成后，preview 收口提交是新 master 的祖先，两者树内容一致，没有冲突取舍。

preview 的 Axiom 移动方块预测、上行冗余灰测、输入法恢复、好友与查分功能、开发者展示、节日侧栏、歌词与录制预览修复全部进入 master。

## 保持的移除与边界

- 保留 PR #498 对柔光发言样式和对应离屏资源的移除，没有恢复旧样式。
- Logo 修复沿用 preview 的几何回退；头衔文字、胶囊与日月图案继续使用原有实现。
- 远程 preview 随 PR 合并自动删除，本地 preview 分支也已删除。

## 验证与产物

实际构建源码为 `2e4cbabb9e915339d7f8b201bb3785572ae0acc6`，使用已有 `3.3` 版本标识。

- 静态审阅、差异空白检查和 Node.js 生成器语法检查已完成。
- Windows x64 Release 的客户端、服务端、网易云/汽水/酷狗/QQ 音乐辅助程序及更新器构建成功，使用 `qmclient_scripts/cmake-windows.cmd`，并行数为 6。
- 已从该构建准备普通安装载荷，确认包含 `data/qmclient/title_badges/logo_sdf.png`。
- Inno 安装内核和原生安装界面构建成功，产物为 `QmClient-Setup.exe`，大小 108,685,312 字节。
- 新增 9 项 C++ 单元测试与 3 项 Node.js 生成器测试，本地未执行；未运行 gate，也未启动客户端进行视觉或性能验收。
