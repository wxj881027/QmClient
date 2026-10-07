# master 整合与 PR 验收

状态：本地修复和验证完成，可以进入 PR 审查；尚未完成跨平台和发布验收。基准为 2026-10-07 获取的 origin/master（b0dbeece8c，已合并 PR #309）；dyl_dev 已通过 51657376b5 合并该基准，没有冲突。本文的功能完成度判断不表示代码覆盖率。

## 当前功能与结论

字体、图标、Steam 识别、Windows 三类分发及签名更新均进入本次分支范围。四种 Phosphor 样式、颜色预设和独立自定义颜色、随包资源优先及真实字体回退已实现。普通 ZIP/7z 带明确收录的玩家工具，Setup 和 portable 仅带运行必需工具；便携版使用独立构建和 profile。

按用户“80% 以上功能预期即可进入 PR”的标准，可以提交面向 master 的 PR 审查。主功能和关键失败路径已有行为证据，但不能称 100% 发布验收完成，也不能称现在已具备无条件合并资格。Linux 媒体后端必须由对应平台构建及实际会话验证；删除图标精确页面还需要视觉验收。

## PR #309 修复

已修复禁用／无播放器状态下总线断开不重连、同源缺失属性残留旧元数据和能力、位置读取失败跳零、换曲后误保留旧位置、微秒单位转换溢出、非有限 Rate／Volume、同名服务更换 owner 时旧命令错投，以及裸 DBUS_LIBRARIES 丢失非系统依赖选项。

请求采用整轮 1 秒预算、单请求最多 250ms 和最多 20ms dispatch 片段；候选扫描按持久游标公平轮转，活动源优先，发现阶段保留 200ms 位置读取预算。unique owner 作为实际请求地址，世代号约束命令／音量。NoTrack 哨兵使用元数据身份回退。13 项生产策略测试覆盖取消、断线、恢复、换源、换曲与坏播放器饥饿；这些测试不等于真实 libdbus adapter 验收。bootstrap 仍由 libdbus 握手，不保证所有启动退出情形都在 20ms 内。

## 删除图标与字体回退

资源对比复现了旧不透明 0.85px 外环沿图标内孔扩张形成套框。改为共享背景对比策略：可读时省略，低对比连续增加最多 35% alpha 的弱保护；未知地图背景保留弱保护，彩虹固定弱黑色。嵌套表面恢复已知状态，名牌特效避免 alpha 平方。

即时 TTF 回退原先通过 TextEx 绘制，会忽略设置的保护色。现在共享临时容器绘制入口使用白顶点并清除／恢复分段色，在最终绘制传入真实本体／保护色，测试观察实际参数和资源释放。缓存字体与图集沿同一保护策略。

当前四套资源是 MTSDF，但 shader 本体实际读取 alpha 真 SDF；RGB median 并未用于这些图标本体。此次未切换 shader。CPU 公式模拟不能替代 GPU 尖角和小尺寸视觉验证。

## 实际验证

- default gate：tmp/pr309-icons-final-default-gate.json，15 项通过；后续即时 TTF 接线和背景边界改动由 quick、当前源码构建及过滤回归补验。
- quick：tmp/pr309-trash-final-quick-gate.json，12 项通过；最后测试拆分和 benchmark 矩阵由库存与格式检查及实际执行补验。
- 普通／便携客户端：tmp/pr309-trash-final-stable-build.log、tmp/trash-icon-final-portable-stable-build.log，均成功。便携第一次构建因 MSVC Ninja 前缀重生成报告失败，稳定源码重跑成功，未绕过封装。
- 当前测试二进制：tmp/pr309-trash-final-domain-tests.log，图标／媒体过滤共 197 项通过，包含行为和静态合同；tmp/pr309-trash-final-related-tests.log 中 34 项为本轮策略与绘制边界行为测试，其中 MPRIS 13 项。
- DBus CMake 配置集成：tmp/pr309-dbus-final-test.log，实际 pkg-config 与 FindDBus 的 3 个配置阶段通过，不等于真实库链接或运行。
- 真实进程图标资源：tmp/icon-resources-smoke/run_s9zcf6cv/results.json，OpenGL 清洁／损坏用户资源／缺图集字体回退及 Vulkan 共 4 项通过。取得有效 OpenGL 设置页 125% 缩放截图；没有覆盖反馈中的删除按钮。
- Google Benchmark：tmp/trash-icon-final-benchmark-pinned/summary.json，已知／未知背景 10 case、每项 7 次，1 秒计时、0.2 秒预热，测试进程亲和性掩码 3。CPU 中位数约 4.4–80.2ns，CPU CV 约 1.9–7.5%，real CV 约 1.0–2.5%；首轮高波动证据保留。没有同条件旧版基线，不证明性能提升或 GPU 整帧通过。

## 验收限制

Windows 环境没有可用 WSL Linux 发行版；Linux/BSD 后端编译和真实播放器启停、断线重连、退出耗时尚未验收。现有 Linux CI 已安装 libdbus-1-dev，仍需等待本次提交运行结果。

字体原有四项真实进程证据保持有效。删除按钮精确页面、完整彩虹动画、长译文与弹层鼠标专项路径仍未完成视觉／交互验收。真实 ZIP/7z 规则与隔离 Setup 1113 项载荷哈希证据早于最新字体和图标修复；最终发布前需重新打包。GitHub 正式发布、玩家在线升级及真实安装环境升级未执行。

## 2026-10-07 发布载荷补验

当前63df1261e6代码重新生成普通ZIP／7z及独立portable ZIP／7z，四个实际归档的EXE清单校验通过。记录：tmp/release-acceptance-normal-package.log、tmp/release-acceptance-portable-package.log、tmp/release-acceptance-normal-verify.log、tmp/release-acceptance-portable-verify-corrected.log。便携验证首次误用普通包文件名，改为实际portable文件名后通过。

便携存储真实进程再次通过目录搬家、目录外配置隔离、profile不可用和普通版拒绝存储覆盖；第一次在构建占用EXE时失败，构建完成后串行复测通过。证据tmp/release-acceptance-storage-e2e-retry.log。签名／Setup载荷／归档校验42项测试在工作区隔离cryptography依赖下全部通过，无跳过；tmp/release-acceptance-sign-packaging-tests-with-crypto.log。

最新Setup在独立测试AppId下编译并完成安装、全载荷损坏后的覆盖重装、废弃资源清理与卸载保留未知文件；逐项核对1113文件和46个SPV。证据tmp/release-acceptance-setup-lifecycle.log，产物tmp/release-acceptance-setup/output/QmClient-Setup.exe。该程序是隔离AppId验收产物，不能当作正式发布Setup。编译仍报告中文语言文件缺少部分Inno消息并回退英文；需在安装向导人工验收中确认，不能称完整中文安装体验通过。

上文“载荷早于最新代码”的缺口已由本次重打包补齐；GitHub正式签名上传、真实历史Setup升级、跨平台后端与专项视觉仍待验收。具体步骤和通过标准见《发布人工验收清单》。

## 图标性能决策边界

TTF路径按face／字符／字号／名牌标记缓存字形，已缓存的字形不重复FreeType栅格化；文本容器还能缓存布局和顶点。当前MTSDF路径按图标提交独立绘制，低对比保护会增加一次外环提交。两者稳态均绘制纹理，不能把“离线烘焙”直接等同更快。MSDF主要可验证价值是跨尺寸表示及尖角质量；当前alpha本体尚未体现RGB多通道优势。

完整atlas PNG＋JSON原始13.14MiB、独立7z11.77MiB；四TTF原始1.88MiB、独立7z551.5KiB。每样式2808² RGBA像素30.08MiB，切换时新旧临时共存理论60.16MiB，不是驱动实测显存。证据tmp/icon-atlas-size-audit.json。仅有颜色策略CPU微基准，没有TTF与MTSDF同条件整帧／GPU比较，所以暂不决定替换主路径，也不声称谁更快。比较应固定图标集、尺寸、后端、机器与透明度，分冷启动／首次使用／稳定缓存／样式字号切换，测CPU提交、GPU时间、draw调用、上传、内存及p95／p99帧时，而不是只看平均FPS。
