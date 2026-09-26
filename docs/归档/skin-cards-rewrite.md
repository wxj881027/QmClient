# 皮肤设置卡片重写

## 已确认范围

- 保留现有功能、配置名、默认值和运行结果；卡片沿用其他全局卡片的统一样式。
- 拆成「Tee 外观」与「皮肤切换动画」两张全局卡片，在视觉分类页和搜索页复用。
- 外观卡包含本体/分身与其他玩家描边、颜色、粗细、不透明度、循环自定义色调、分身同步、速度和表情阴影。
- 动画卡包含独立的锤中偷皮开关、动画开关、7 种动画、3 种范围、时长、4 种缓动和强度。关闭动画时仅隐藏五行高级参数。
- 继续保留循环色调仅作用于自定义颜色、TClient 彩虹 Tee 优先以及关闭色调时速度不写回的行为。

## 实现

- `src/game/client/QmUi/cards/QmCardCatalogSkin.cpp` 集中两张卡的构造、预布局输入及内容渲染；`QmCardCatalogSkinMetrics.h` 管理各自高度。
- 卡片通过 `QmCardRenderHook` 调用菜单内容助手；分类页和搜索页沿用已有 `BuildCards` 入口。
- `qm:skin_transition` 和 `skin_transition` 折叠 key 继续对应动画卡。新增 `qm:skin_appearance` 与独立折叠 key；既有全局布局加载机制自动补入新卡，原卡的用户布局继续按 stableId 恢复。
- 新模块加入 `GAME_CLIENT` 源文件清单，搜索关键字分别对应外观与动画内容。
- 全部界面文案复用现有翻译 key。

## 验证约定

测试代码先行补充卡片搜索归属、旧布局兼容、独立折叠和卡片高度，并更新迁移后内容函数的现有回归检查。遵照用户约定，不编译、不运行 C++/Rust/Python 测试；只执行源码门禁、翻译维护校验和只读审查。

## 验证结果（2026-09-20）

- `python qmclient_scripts/gate/check_gate.py --mode quick --report-json-path tmp/skin-cards-rewrite/gate.json`：PASS，11 项通过，0 警告、0 失败；未构建、未运行测试。
- `extract_strings.py` → `generate_all.py` → `validate.py` → `review_duplicate_entries.py --show-groups 0 --show-unused 0`：均成功。脚本位于 `qmclient_scripts/languages_qmclient/`；12 个语言产物校验通过，重复 key 和空翻译均为 0。翻译审计仍列出 281 项人工复核提示，阻断项为 0。
- `clang-format --dry-run --Werror`：新卡片模块、高度头文件及新增搜索/持久化用例所在文件通过格式检查。
- `git diff --check`：通过。源码沿用原文件的 BOM、换行与缩进。
- 版本从 `3.9.45` 更新为 `3.9.46`。
- 本次测试用例仅补全源码，未验证运行结果；客户端视觉效果与交互尚未通过编译或启动实测。

## 只读审查

发现并修复：迁移后的下拉标签通过变量调用 `Localize`，翻译提取器未能识别类型和缓动文案；改为在调用处显式翻译三个标签，生成产物仅删除不再使用的旧卡片标题。

结论：对照任务开始时的工作区快照，配置绑定、取值范围、条件显示和运行时逻辑保持一致；未发现本次差异中仍需修复的问题。旧模块枚举值保留，新枚举追加到末尾；原动画卡的 stableId、折叠 key 和用户布局继续兼容。审查与源码门禁不替代上述尚未运行的测试及客户端实测。
