# UI 改动计划与模型分工

状态：已完成已确认项的实现、补充测试代码与翻译生成；Windows Release `all` 全目标构建通过。额外构建的 `testrunner` 因现有链接依赖缺口未通过，测试未执行。

## 本轮已确认

- Claude 负责列计划、判断工作量和分工；Gemini 接轻量任务，Codex 接重量任务。
- 卡片逐张切换全宽和默认宽度，不做整页统一切换。
- 计分板音量控制针对当前音乐播放器，不是游戏音效或系统总音量。
- 用户明确忽略“聊天信息 + / -”和“落水卡锤加钩锤按键”，从本轮删除。
- 截图已勾选的聊天指令小字检查、demo 回放 HUD 不派新增任务。
- 饼菜单去毛刺已有其他任务的实现和测试代码，沿用现有成果；本次随全量目标编译，未做运行验证。
- 用户已授权实现后执行一次全量编译；不运行测试或 gate、不启动客户端；不提交、推送或改版本。
- 播放/暂停、上一首、下一首、播放器音量放在计分板右侧，沿用底部歌曲信息条。
- 昵称高级模式只控制可见性，基础项保留范围、大小和位置，高级项含特效、密集降级和回放效果，不改隐藏配置值。
- HUD 交互复用现有计分板鼠标入口，支持媒体和投票。
- 用户看过实际截图后撤销快捷聊天命令浮层：删除 bindchat 候选、显示开关和专属接线；保留原有 bindchat 执行及斜杠指令补全。

## Claude 的分级与派工

Claude 将首批轻量工作定为可独立完成的调查、文案建议和接入清单。涉及输入所有权、状态、布局和共享文件的修改由 Codex 整合。待接口和行为明确后，局部渲染草稿可以再派给 Gemini。

| ID | 负责人 | 任务 | 当前状态与依赖 |
| --- | --- | --- | --- |
| G1 | Gemini，轻量 | 核对当前媒体状态、切歌、暂停、音量与平台接口 | 已完成；现有接口不含播放器音量 |
| G2 | Gemini，轻量 | 梳理昵称四张卡片的现有文案，提出易懂用词、范围语义及普通/高级分组建议 | 已返回；不采纳混淆当前角色、本体/分身的文案，见下文 |
| G3 | Gemini，轻量 | 梳理聊天命令数据、Tab 补全、小字说明与快捷命令现状 | 已完成；既有补全和用途说明不等于多候选 HUD |
| G4 | Gemini，轻量 | 列出设置控件接入小字提示的代表性清单 | 已完成；已有头衔设置示例和未迁移的普通提示调用 |
| C1 | Codex，重量 | 扩展现有鼠标入口与投票 HUD 交互，协调计分板避让、当前键位提示和改票 | 已实现；复用计分板鼠标，投票卡临时置于顶部留白，保留投后常显配置 |
| C2 | Codex，重量 | 计分板媒体按钮与当前播放器音量控制 | 已实现右侧控制条；后台通过播放器身份匹配 Core Audio 会话，无法匹配则禁用音量 |
| C4 | Codex，重量 | 可见的命令补全 HUD | 保留服务器命令候选，点击只填入，回车继续原有发送路径；bindchat 快捷浮层依用户截图反馈删除 |
| C6 | Gemini 局部补丁，Codex 审核整合 | 昵称高级选项开关、测量失效处理及文案集成 | 已实现；高级文字效果卡折叠配置行，不改配置值，页面与搜索测量同步失效 |
| C7 | Codex，重量 | 每张卡片全宽/默认切换，保持布局持久化兼容 | 已实现；卡片标题宽度按钮复用既有 column 持久化，当前会话恢复原列与顺序 |
| C8 | Codex 整合 | 将设置项悬浮说明接入现有小字提示机制 | 已实现；设置页复用原说明文字，立即显示小字，保留既有 hover 判定 |

C3（聊天 +/-）与 C5（落水钩锤）依用户最新要求取消，不继续调查或实现。

## 实施前核对的源码事实

- `src/game/client/QmUi/cards/QmCardCatalogNameplate.cpp` 的 `BuildNameplateCard` 提供独立昵称卡片构造，已接入 `MeasureContent`，不能把自适应高度当作完全缺失的功能。
- `src/game/client/components/scoreboard.cpp` 已有 `ConToggleScoreboardCursor`；`OnCursorMove` 和 `OnInput` 接管光标并转交 UI。先复用现有入口，不新增另一套光标状态和命中队列。
- `src/game/client/components/voting.cpp` 的 `CVoting::Render` 已支持在关闭 `cl_show_votes_after_voting` 时投后隐藏、计分板打开仍显示；其渲染还包括 HUD 编辑器变换与迷你投票分支，避让需同时考虑这些路径。
- `src/game/server/gamecontext.cpp` 的 `OnVoteNetMessage` 在投票期间可更新已有选择，并受三秒防刷检查影响；发起者投反对票会取消投票。这只说明本仓库服务端行为，外部服务器仍以其实际响应为准。不需要改协议。
- `src/game/client/components/system_media_controls.h` 的 `SState` 有媒体标题、作者、播放状态及控制能力；`CSystemMediaControls` 提供 `Previous`、`PlayPause`、`Next`，没有音量接口。播放器音量不能接到游戏音效或系统总音量上冒充实现。
- `src/game/client/components/chat.cpp` 的 `BuildCommandUsagePreview` 是用途说明；`/` 命令补全读取 `m_vServerCommands`。用途说明和候选列表是不同数据职责。
- `src/game/client/components/tclient/bindchat.h` / `bindchat.cpp` 有 `ChatDoBinds` 和现有聊天命令绑定机制，可作为“快捷命令 HUD”讨论依据，但不直接等同于用户想要的界面。
- `src/game/client/components/tooltips.cpp` 已有立即显示的小字提示 `DoSmallToolTip`，普通提示包装不代表该能力不存在。
- `src/game/client/QmUi/QmCardOrderModel.h` 的 `SEntry::m_Column` 已区分全宽、左列、右列，`CModel::Serialize` 定义现有序列化格式。复用 `qm_global_card_order`，不为宽度功能自动提升 `QmCardLayoutVersion` 或重置用户布局。

## Gemini 报告的采纳与校正

- 采纳 G1 的接口复用路径和音量缺口结论；平台不可用时的控件状态在实现阶段按实际能力处理，不把空接口显示为可用控制。
- G2 的“Current = 当前焦点/目标”不采纳：配置说明明确是当前角色。“Own characters”包含自己的多个角色；“Others and own”明确排除当前角色，不能简化成没有该区别的“他人与自己”。依据为 `config_variables_qmclient.h` 中 `QmNameplateShowScope` 的各值说明。
- G2 的基础/高级分组仍是建议，不直接改变配置可见性；其余文案需与实际渲染范围核对后再采纳。
- 采纳 G3 对现有补全状态和用途说明的区分；候选来源是命令列表，`QmChatCommandPreview` 只负责说明文本，不用它替代候选模型。
- G4 只作为代表性接入清单，不代表全设置页覆盖完成；不因报告建议额外新增全局开关或外部 wiki 系统。

## 实施顺序与共享文件

1. Gemini 的 G1-G4 已返回；Codex 已汇总可采纳结论并标记文案误判。
2. 优先细化已确认的逐卡宽度切换、播放器音量方案；其余项先补齐下述行为定义。
3. 公共输入与状态接口确定后，再决定是否将独立渲染部分派给 Gemini。
4. `SettingsCardDeck`、`QmCardCatalog`、配置头和翻译源由一个执行者串行修改；禁止两个模型同时编辑同一共享文件。
5. 翻译维护 `qmclient_scripts/languages_qmclient/translations/i18n/*.toml`，运行时 `data/languages/*.txt` 由生成链产生，不手改。

## 已确认的交互与实现取舍

- 昵称高级模式只改变可见性；默认关闭，在文字效果卡首行开启，其他三张昵称卡保留基础项。
- 游戏内鼠标复用计分板的既有入口，不增加新的激活方式。
- 斜杠命令候选只填入文本，保留参数，按回车发送。指令补全开关默认开启，位于现有聊天设置卡；不再提供快捷聊天命令浮层及其开关。
- 小字说明按现有 tips 内容实现，不增加外部 wiki 数据源。
- 音量匹配当前播放器的应用身份或完整 exe 名，只调节匹配到的应用会话；不回退到系统总音量。拖动请求合并并校验会话代次。
- 普通/迷你投票卡在计分板开启期间临时放到顶部，关闭后恢复原 HUD 位置；`cl_show_votes_after_voting` 的用户显式设置保留。

逐卡布局不迁移配置格式、不提升布局版本。跨重启保留当前宽度，恢复分列时采用注册表默认列；同一会话内优先恢复切换前的列和顺序。

## 证据与进度

- [x] 实际调用 Claude CLI，先只读调查再输出分级计划。
- [x] 核对 Claude 初稿并反馈源码误判，取得修订计划。
- [x] 按 Claude 的 G1-G4 实际调用 Gemini CLI。
- [x] Codex 接收重量任务，完成光标、改票、媒体、卡片布局主要入口调查。
- [x] 同步用户的逐卡宽度、播放器音量及两项忽略要求。
- [x] Gemini G1-G4 最终报告回收与任务状态更新。
- [x] Gemini 昵称高级模式局部补丁回收，由 Codex 审核后合入。
- [x] 媒体、投票、聊天 HUD、逐卡宽度与小字提示实现及静态审阅。
- [x] 新增相关行为与布局测试代码，更新翻译源并生成十二种语言文件。
- [x] 用户授权的 Release `all` 全量编译通过，产物 `cmake-build-release/DDNet.exe`。

原始调用材料保存在工作区 `tmp/model-coordination-20261001/`：

- `claude-planning-request.md`、`claude-planning-stream.jsonl`：首次调查；初稿含误判，不作为实施规格。
- `claude-revision-request.md`、`claude-final.json`：源码证据反馈与 Claude 修订计划。
- `gemini-assignment.md`、`gemini-stream.jsonl`：Claude 的首批轻量派单和 Gemini 执行记录。
- `claude-plan-original.md`、`gemini-report-original.md`：便于阅读的原始模型输出；与本文件中的用户最新范围或校正冲突时，以本文件为准。

测试代码已补充，未执行测试或 gate，未启动客户端。构建命令：`cmd /c .\qmclient_scripts\cmake-windows.cmd --build cmake-build-release --target all -j 14`。日志保存在 `tmp/model-coordination-20261001/full-build*.log`。

首轮编译发现并修复了新增小字提示调用遗漏字号的问题，以及现有 Tee 卡片引用未声明的 `FONT_ICON_CHECK`；后者仅改为项目 HUD 已使用的对应图标码点，保留该卡片的其他未提交改动。

## 最终构建结果

- `all`：退出码 0。客户端、服务端、工具及配置启用的辅助程序已完成编译链接。最终成功日志为 `tmp/model-coordination-20261001/full-build-final.log`；前续编译日志保留在同目录。
- Windows 音量模块首次编译发现 `NOGDI` 与多媒体 SDK 类型声明冲突，已局部恢复所需声明，未修改全局构建宏。
- 客户端最后链接曾被运行中的工作区 `DDNet.exe` 占用。用户明确授权后，通过 `CloseMainWindow` 正常关闭该开发实例，随后链接成功；未重启客户端。
- 额外执行 `--target testrunner`：新增和相关测试源均编译成功，但整个测试可执行文件链接失败，缺少 `CBinds` 构造/析构/Bind/Get 和 `CTextCursor::SetPosition` 的实现链接。日志为 `tmp/model-coordination-20261001/test-build.log`。没有删除测试、添加伪实现或运行测试。
- 翻译通过既有提取/生成链更新。Windows `write_text` 错误使用临时目录生成、逐语言进程和原子替换完成恢复；临时驱动与日志保留在 `tmp/model-coordination-20261001/`。
- `git diff --check` 通过。未运行 gate、单元测试、集成测试或游戏内视觉验证；未提交、推送或修改版本号。

## 截图反馈：删除快捷聊天命令浮层

- 依用户最新要求删除 bindchat 快捷候选、设置开关、专属缓存修订接口及两条翻译；原有 bindchat 执行和斜杠指令补全保留。
- 清理快捷候选专属测试，更新空输入、普通文字不显示候选列表的回归测试；测试未执行。
- 重新提取并生成语言文件，Release `all` 构建退出码 0，日志为 `tmp/model-coordination-20261001/remove-shortcuts-build.log`。
- 用户授权正常关闭新开发实例后，核对发现进程已退出；本轮没有再发关闭请求或重启客户端，未进行游戏内验证。
