# 计分板底部横栏

## 确认行为

- SMTC 从右侧面板改为主体下方的整宽横栏，只显示歌曲标题和艺术家；移除计分板中的三个媒体控制按钮。
- 沿用 `qm_smtc_enable`；关闭、没有媒体会话或没有可显示的标题/艺术家时隐藏媒体横栏。
- 旁观者有人时显示在 SMTC 下方；SMTC 隐藏时直接接在主体下方。无人时不绘制、不占高度。
- 单列及多列横栏均跟随主体宽度；旁观者文字可以换行，背景按实际行数收缩，受现有底部可用高度限制。
- 保留有效旁观者判定、身份隐藏、名称颜色及剩余人数提示。原有比赛限制信息保留。

## 实现与测试

- `src/game/client/components/qmclient/scoreboard_footer.h` 负责横栏布局。
- `src/game/client/components/scoreboard.cpp` 获取媒体信息、统计旁观者并绘制横栏。
- `src/test/QmLayoutTest.cpp` 补充空状态、仅媒体、仅旁观者、上下排列、单/多列宽度及比赛限制占位后的可用高度测试。
- `src/test/qm_new_ui_menu_branch_test.cpp` 将媒体按钮测试更新为计分板不包含播放控制的回归约束。
- 版本从 `3.9.6` 更新为 `3.9.7`。

## 验证

- 先补测试代码再实现；遵循本次工作约定，未编译、未运行测试，因此没有执行红/绿测试阶段。
- 只读代码审查未发现本次改动的阻断问题；实际游戏画面尚未验证。
- `git diff --check` 通过；修改的底部渲染段和新增布局头文件通过 clang-format 只读检查。
- `python qmclient_scripts/gate/check_gate.py --mode quick` 通过：11 项通过，0 警告，0 失败；未执行构建与测试。
