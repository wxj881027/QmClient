// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_water_hammer[] = {
		{Localizable("Highlight frozen teammates' hammer and hook inputs"), nullptr, "水域卡锤卡钩高亮 冻结 卡锤 卡钩 按键显示"},
		{nullptr, "qm_water_hammer_highlight", nullptr},
		{Localizable("Pressed hammer icon color"), "qm_water_hammer_color", "卡锤 按下 图标 颜色"},
		{Localizable("Pressed hook icon color"), "qm_water_hook_color", "卡钩 钩索 按下 图标 颜色"},
	};
	QM_CARD_SEARCH_REGISTER("qm:water_hammer", qm_water_hammer);
}
