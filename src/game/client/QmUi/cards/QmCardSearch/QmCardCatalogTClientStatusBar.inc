// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_status_bar_preview[] = {
		{Localizable("No status bar items"), nullptr, "暂无状态栏项目"},
		{Localizable("Status Bar Codes"), nullptr, "状态栏代码"},
		{Localizable("Status Scheme:"), nullptr, "状态方案："},
		{nullptr, "cl_languagefile", nullptr},
		{nullptr, "qm_statusbar_scheme", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-status-bar-preview", deck_tclient_status_bar_preview);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_status_bar_settings[] = {
		{Localizable("Colors"), nullptr, "颜色"},
		{Localizable("Local Time"), nullptr, "本地时间"},
		{Localizable("Show labels on status bar items"), nullptr, "在状态栏上显示条目标签"},
		{Localizable("Show seconds on clock"), nullptr, "在时间处显示秒数"},
		{Localizable("Show status bar"), nullptr, "显示状态栏"},
		{Localizable("Status bar alpha"), nullptr, "状态栏透明度"},
		{Localizable("Status bar color"), nullptr, "状态栏颜色"},
		{Localizable("Status bar height"), nullptr, "状态栏高度"},
		{Localizable("Text alpha"), nullptr, "文字透明度"},
		{Localizable("Text color"), nullptr, "文本颜色"},
		{Localizable("Use 12 hour clock"), nullptr, "使用 12 小时制"},
		{nullptr, "qm_statusbar", nullptr},
		{nullptr, "qm_statusbar_12_hour_clock", nullptr},
		{nullptr, "qm_statusbar_alpha", nullptr},
		{nullptr, "qm_statusbar_color", nullptr},
		{nullptr, "qm_statusbar_height", nullptr},
		{nullptr, "qm_statusbar_labels", nullptr},
		{nullptr, "qm_statusbar_local_time_seconds", nullptr},
		{nullptr, "qm_statusbar_text_alpha", nullptr},
		{nullptr, "qm_statusbar_text_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-status-bar-settings", deck_tclient_status_bar_settings);
}
