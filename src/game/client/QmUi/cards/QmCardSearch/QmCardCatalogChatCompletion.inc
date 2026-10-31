// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_chat_completion[] = {
		{Localizable("Background color"), nullptr, "背景颜色"},
		{Localizable("Background opacity"), nullptr, "背景不透明度"},
		{Localizable("Reset chat completion appearance"), nullptr, "恢复聊天补全默认外观"},
		{Localizable("Selected text color"), nullptr, "选中文字颜色"},
		{Localizable("Selection background color"), nullptr, "选中项背景颜色"},
		{Localizable("Text color"), nullptr, "文字颜色"},
		{nullptr, "qm_chat_completion_bg_color", nullptr},
		{nullptr, "qm_chat_completion_opacity", nullptr},
		{nullptr, "qm_chat_completion_selected_color", nullptr},
		{nullptr, "qm_chat_completion_selected_text_color", nullptr},
		{nullptr, "qm_chat_completion_text_color", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:chat_completion", qm_chat_completion);
}
