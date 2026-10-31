// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_tooltip[] = {
		{Localizable("Tooltip background color"), nullptr, "提示气泡背景颜色"},
		{Localizable("Tooltip bounce animation"), nullptr, "提示气泡回弹动画"},
		{Localizable("Tooltip font size"), nullptr, "提示气泡字号"},
		{Localizable("Tooltip preview"), nullptr, "提示气泡预览"},
		{Localizable("Tooltip text color"), nullptr, "提示气泡文字颜色"},
		{nullptr, "qm_tooltip_animation", nullptr},
		{nullptr, "qm_tooltip_background_color", nullptr},
		{nullptr, "qm_tooltip_font_size", nullptr},
		{nullptr, "qm_tooltip_text_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:tooltip", qm_tooltip);
}
