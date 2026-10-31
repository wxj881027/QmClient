// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_console[] = {
		{Localizable("Console background color"), nullptr, "控制台背景色"},
		{Localizable("Console background opacity"), nullptr, "控制台背景不透明度"},
		{Localizable("Console color scheme"), nullptr, "控制台配色方案 终端主题"},
		{Localizable("Console command color"), nullptr, "控制台命令颜色"},
		{Localizable("Console font size"), nullptr, "控制台字号"},
		{Localizable("Console link color"), nullptr, "控制台链接颜色"},
		{Localizable("Console number color"), nullptr, "控制台数字颜色"},
		{Localizable("Console parameter color"), nullptr, "控制台参数颜色"},
		{Localizable("Console search match color"), nullptr, "控制台搜索匹配颜色"},
		{Localizable("Console selected search match color"), nullptr, "控制台选中搜索匹配颜色"},
		{Localizable("Console string color"), nullptr, "控制台字符串颜色"},
		{Localizable("Console text color"), nullptr, "控制台文字颜色"},
		{Localizable("Highlight console commands"), nullptr, "高亮控制台命令"},
		{Localizable("Classic PowerShell"), nullptr, "经典 PowerShell"},
		{Localizable("Modern terminal"), nullptr, "现代终端"},
		{nullptr, "qm_console_background_color", nullptr},
		{nullptr, "qm_console_color_scheme", nullptr},
		{nullptr, "qm_console_command_color", nullptr},
		{nullptr, "qm_console_font_size", nullptr},
		{nullptr, "qm_console_highlight_commands", nullptr},
		{nullptr, "qm_console_link_color", nullptr},
		{nullptr, "qm_console_number_color", nullptr},
		{nullptr, "qm_console_opacity", nullptr},
		{nullptr, "qm_console_parameter_color", nullptr},
		{nullptr, "qm_console_search_color", nullptr},
		{nullptr, "qm_console_search_selected_color", nullptr},
		{nullptr, "qm_console_string_color", nullptr},
		{nullptr, "qm_console_text_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:console", qm_console);
}
