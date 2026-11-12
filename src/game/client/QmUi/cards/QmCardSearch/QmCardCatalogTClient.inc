// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_tclient_tee_trails[] = {
		{Localizable("Enable tee trails"), nullptr, "启用 Tee 拖尾"},
		{Localizable("Fade trail alpha"), nullptr, "淡化拖尾"},
		{Localizable("Magic: Spirit Script"), nullptr, "魔法风：灵文游丝"},
		{Localizable("Manga: Ink Slash"), nullptr, "漫画风：墨斩"},
		{Localizable("Original"), nullptr, "原版"},
		{Localizable("Pixel: Layer Shift"), nullptr, "像素风：层移"},
		{Localizable("Rainbow"), nullptr, "彩虹"},
		{Localizable("Show other tees' trails"), nullptr, "显示其他 Tee 的拖尾"},
		{Localizable("Solid"), nullptr, "纯色"},
		{Localizable("Speed"), nullptr, "速度"},
		{Localizable("Taper trail width"), nullptr, "锥形拖尾宽度"},
		{Localizable("Tee"), nullptr, "Tee"},
		{Localizable("Tee Trails"), nullptr, "Tee 拖尾"},
		{Localizable("Tee trail color"), nullptr, "Tee 拖尾颜色"},
		{Localizable("Trail alpha"), nullptr, "拖尾透明度"},
		{Localizable("Trail length"), nullptr, "拖尾长度"},
		{Localizable("Trail width"), nullptr, "拖尾宽度"},
		{Localizable("Use style colors"), nullptr, "使用样式颜色"},
		{nullptr, "qm_tee_trail", nullptr},
		{nullptr, "qm_tee_trail_alpha", nullptr},
		{nullptr, "qm_tee_trail_color", nullptr},
		{nullptr, "qm_tee_trail_color_mode", nullptr},
		{nullptr, "qm_tee_trail_fade", nullptr},
		{nullptr, "qm_tee_trail_length", nullptr},
		{nullptr, "qm_tee_trail_others", nullptr},
		{nullptr, "qm_tee_trail_style", nullptr},
		{nullptr, "qm_tee_trail_style_colors", nullptr},
		{nullptr, "qm_tee_trail_taper", nullptr},
		{nullptr, "qm_tee_trail_width", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("tclient:tee-trails", tclient_tee_trails);
}
