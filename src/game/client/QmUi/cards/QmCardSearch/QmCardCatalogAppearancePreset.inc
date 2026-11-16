// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_appearance_preset[] = {
		{Localizable("Applying this preset changes the appearance settings listed below."), nullptr, "应用预设会修改下面列出的外观设置。"},
		{Localizable("CJK font"), nullptr, "中日韩文字字体"},
		{Localizable("LXGW WenKai is available."), nullptr, "霞鹜文楷可用。"},
		{Localizable("LXGW WenKai is unavailable. Current fonts and weights will be kept."), nullptr, "未找到霞鹜文楷，将保留当前字体和字重。"},
		{Localizable("Latin font"), nullptr, "拉丁文字字体"},
		{nullptr, "qm_custom_font", nullptr},
		{nullptr, "qm_custom_font_cjk", nullptr},
		{nullptr, "qm_custom_font_weight", nullptr},
		{nullptr, "qm_custom_font_weight_cjk", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:appearance_preset", qm_appearance_preset);
}
