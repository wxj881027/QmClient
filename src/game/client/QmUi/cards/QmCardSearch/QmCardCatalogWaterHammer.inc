// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_water_hammer[] = {
		{Localizable("Highlight held hammer and hook inputs while frozen"), "qm_water_hammer_highlight", "水域卡锤卡钩高亮 冻结 卡锤 卡钩 按键显示"},
		{Localizable("Stuck input display scope"), "qm_water_input_scope", "卡锤 卡钩 显示范围"},
		{Localizable("Own", "Stuck input display scope"), nullptr, "自己的 本体 分身"},
		{Localizable("Others", "Stuck input display scope"), nullptr, "别人的 其他玩家"},
		{Localizable("All", "Stuck input display scope"), nullptr, "全部的"},
		{Localizable("Show hammer icon"), "qm_water_hammer_icon", "卡锤 锤子 图标"},
		{Localizable("Show hook icon"), "qm_water_hook_icon", "卡钩 钩子 图标"},
		{Localizable("Pressed hammer icon color"), "qm_water_hammer_color", "卡锤 按下 图标 颜色"},
		{Localizable("Pressed hook icon color"), "qm_water_hook_color", "卡钩 钩索 按下 图标 颜色"},
	};
	QM_CARD_SEARCH_REGISTER("qm:water_hammer", qm_water_hammer);
}
