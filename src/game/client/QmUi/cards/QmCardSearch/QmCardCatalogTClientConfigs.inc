// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_configs_actions[] = {
		{Localizable("Audio"), nullptr, "语音音效"},
		{Localizable("Auto"), nullptr, "自动"},
		{Localizable("Camera"), nullptr, "镜头视野"},
		{Localizable("Changes: %d"), nullptr, "更改数：%d"},
		{Localizable("Chat"), nullptr, "聊天"},
		{Localizable("Compact"), nullptr, "紧凑列表"},
		{Localizable("Config Changes"), nullptr, "配置更改"},
		{Localizable("Config Filters"), nullptr, "配置过滤器"},
		{Localizable("DDNet"), nullptr, "DDNet"},
		{Localizable("Gameplay"), nullptr, "游戏玩法"},
		{Localizable("HUD"), nullptr, "HUD"},
		{Localizable("Input"), nullptr, "输入"},
		{Localizable("Misc"), nullptr, "其他杂项"},
		{Localizable("Modified"), nullptr, "仅已修改"},
		{Localizable("QmClient"), nullptr, "栖梦"},
		{Localizable("Search"), nullptr, "搜索"},
		{Localizable("Social"), nullptr, "社交功能"},
		{Localizable("TClient"), nullptr, "TClient"},
		{Localizable("Visual"), nullptr, "视觉效果"},
		{Localizable("default"), nullptr, "默认"},
		{Localizable("maximum"), nullptr, "最大值"},
		{Localizable("minimum"), nullptr, "最小值"},
		{nullptr, "qm_ui_compact_list", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-configs-actions", deck_tclient_configs_actions);
}
