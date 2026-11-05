// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_water_hammer[] = {
		{Localizable("Highlight teammates hammering in death or freeze areas"), nullptr, "高亮在死亡或冻结区域卡锤的队友"},
		{nullptr, "qm_water_hammer_highlight", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:water_hammer", qm_water_hammer);
}
