// 快速练习卡的检索项。绑定行的文案与快捷菜单同源：这里直接引用条目表，
// 菜单改词时搜索索引跟着变，不会留下搜不到旧行的死条目。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_fast_practice[] = {
		{Localizable("Fast practice"), nullptr, "快速练习 分支世界 练习模式 单图练习 练习场 影子 计时"},
		{Localizable("Show main-world ghost"), nullptr, "主世界影子 真实位置 服务器位置 另一个我 视觉干扰"},
		{Localizable("Toggle main-world ghost"), nullptr, "影子开关键 一键隐藏 按键切换"},
		{Localizable("Main-world ghost opacity"), nullptr, "影子透明度 不透明度 淡化 淡化程度"},
		{Localizable("Show practice timer"), nullptr, "计时 用时 秒表 计时器 HUD"},
		{qm_practice_menu::s_aFastPracticeEntries[0].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[1].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[2].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[3].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[4].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[5].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[6].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[7].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[8].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[9].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[10].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[11].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[12].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[13].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[14].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[15].m_pName, nullptr, nullptr},
		{qm_practice_menu::s_aFastPracticeEntries[16].m_pName, nullptr, nullptr},
		{nullptr, "qm_fast_practice_main_world_alpha", nullptr},
		{nullptr, "qm_fast_practice_show_main_world", nullptr},
		{nullptr, "qm_fast_practice_timer", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("qm:fast_practice", qm_fast_practice);
}
