# 图标选择器点击变深修复

## 确认范围与原因

- 点击画面其他位置不应改变“图标”卡片两行胶囊按钮的深浅；保留按钮悬停、选中及配置更新行为。
- 鼠标按下、按住和释放会触发可见卡片的预布局输入回调。图标选择器在该回调与正式渲染中都调用了 `CapsuleTabBarChrome`，导致半透明黑色轨道重复叠加。
- 修复限于 `src/game/client/components/menus_settings.cpp` 的 `ProcessChoiceRow`：删除预布局阶段的胶囊绘制及对应动画组标识，保留槽位计算与 `DoButtonLogic`。
- 补丁版本从 `3.9.43` 更新为 `3.9.44`。

## 回归约束与验证

- 先修正 `src/test/qm_new_ui_menu_branch_test.cpp` 中 `SettingsChoiceSegmentsUseCapsuleTabBar` 的旧约束：正式渲染仍绘制胶囊，预布局只处理输入，不绘制背景或按钮。
- 测试保留新旧 UI 的命中处理与配置更新约束。按用户要求未编译、未运行测试，不声明红/绿测试通过。
- 人工复验步骤：打开“图标”卡片，在按钮外按下、按住、释放鼠标，确认两行轨道不再变深；悬停及切换颜色、粗细选项仍应正常。
- 当前未运行客户端，画面复验待进行。
- 只读代码审查未发现本次改动的阻断问题；按钮命中、配置更新及正式绘制路径保持原样。
- `git diff --check` 通过。
- `python qmclient_scripts/gate/check_gate.py --mode quick` 通过：11 项通过，0 警告，0 失败；未执行构建与测试。
