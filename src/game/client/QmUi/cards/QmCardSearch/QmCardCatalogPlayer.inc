// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_player_country[] = {
		{nullptr, "cl_dummy_country", nullptr},
		{nullptr, "player_country", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:player-country", deck_player_country);
}

// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_player_identity[] = {
		{Localizable("Clan"), nullptr, "战队"},
		{Localizable("Name"), nullptr, "名称"},
		{nullptr, "cl_dummy_clan", nullptr},
		{nullptr, "cl_dummy_name", nullptr},
		{nullptr, "player_clan", nullptr},
		{nullptr, "player_name", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:player-identity", deck_player_identity);
}
