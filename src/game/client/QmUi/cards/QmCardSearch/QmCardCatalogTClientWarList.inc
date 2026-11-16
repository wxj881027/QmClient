// 声明式搜索索引：文案随所属卡片声明，静态初始化时自动注册。
namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_deck_tclient_warlist[] = {
		{Localizable("Allow Duplicate Entries"), nullptr, "允许重复条目"},
		{Localizable("Are you sure that you want to remove '%s' from your war groups?"), nullptr, "您确定要从您的敌对群组中删除 '%s' 吗？"},
		{Localizable("Block enemy chat"), nullptr, "屏蔽敌对玩家聊天"},
		{Localizable("Fully block enemy players"), nullptr, "完全屏蔽敌对玩家 拉黑 默认皮肤 数字ID"},
		{Localizable("Clan"), nullptr, "战队"},
		{Localizable("Color"), nullptr, "颜色"},
		{Localizable("Show enemy colors in chat"), nullptr, "聊天框中显示敌对玩家的颜色"},
		{Localizable("Show enemy colors in scoreboard"), nullptr, "计分板中显示敌对玩家的颜色"},
		{Localizable("Enable warlist"), nullptr, "启用敌对列表"},
		{Localizable("Group name"), nullptr, "分组名称"},
		{Localizable("Name"), nullptr, "名称"},
		{Localizable("No"), nullptr, "否"},
		{Localizable("Online Players"), nullptr, "在线玩家"},
		{Localizable("Reason"), nullptr, "原因"},
		{Localizable("Settings"), nullptr, "设置"},
		{Localizable("Show clan for enemies"), nullptr, "敌对玩家显示战队"},
		{Localizable("Show enemy color in spectator menu"), nullptr, "旁观选择中显示颜色"},
		{Localizable("Edit Entry"), nullptr, "编辑条目"},
		{Localizable("War Entries"), nullptr, "敌对条目"},
		{Localizable("War Groups"), nullptr, "敌对群组"},
		{Localizable("Yes"), nullptr, "是"},
		{nullptr, "qm_warlist", nullptr},
		{nullptr, "qm_warlist_allow_duplicates", nullptr},
		{nullptr, "qm_warlist_block_enemy_chat", nullptr},
		{nullptr, "qm_warlist_block_enemies", nullptr},
		{nullptr, "qm_warlist_chat", nullptr},
		{nullptr, "qm_warlist_scoreboard", nullptr},
		{nullptr, "qm_warlist_show_clan_if_war", nullptr},
		{nullptr, "qm_warlist_spectate", nullptr},
	};
	QM_CARD_SEARCH_REGISTER("deck:tclient-warlist", deck_tclient_warlist);
}
