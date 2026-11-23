// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_bind_wheel_editor[] = {
		{Localizable("Identity"), nullptr, "身份 皮肤 名称 战队"},
		{Localizable("Chat"), nullptr, "语录 聊天"},
		{Localizable("Assets"), nullptr, "资源 贴图 实体 背景 资源包"},
		{Localizable("Custom"), nullptr, "自定义 命令 bind cfg"},
		{Localizable("Menu type"), nullptr, "菜单类型"},
		{Localizable("Layout"), nullptr, "布局"},
		{Localizable("Wheel"), nullptr, "轮盘"},
		{Localizable("Panel"), nullptr, "面板 HUD"},
		{Localizable("Trigger key"), nullptr, "触发键 绑定键"},
		{Localizable("Title"), nullptr, "标题 名称"},
		{Localizable("Phrase"), nullptr, "语录 文本"},
		{Localizable("Chat phrase"), nullptr, "聊天内容 语录内容"},
		{Localizable("Name"), nullptr, "名称"},
		{Localizable("Command"), nullptr, "命令"},
		{Localizable("Capture current appearance"), nullptr, "读取当前外观"},
		{Localizable("Use current settings"), nullptr, "读取当前资源设置"},
		{Localizable("Add phrase"), nullptr, "添加语录"},
		{Localizable("Save phrase"), nullptr, "保存语录 编辑语录"},
		{Localizable("Add command"), nullptr, "添加命令"},
		{Localizable("Delete selected"), nullptr, "删除选中项"},
		{Localizable("Team chat"), nullptr, "队伍聊天"},
		{Localizable("No entries yet"), nullptr, "暂无条目"},
		{nullptr, "qm_bindwheel_layout", nullptr},
		{nullptr, "qm_reset_bindwheel_mouse", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-bind-wheel-editor", deck_tclient_bind_wheel_editor);
}
