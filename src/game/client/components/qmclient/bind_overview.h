#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_OVERVIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_OVERVIEW_H

#include <base/str.h>

#include <engine/shared/localization.h>

#include <string>
#include <vector>

// 绑定分组枚举与已知动作表是控制页绑定列表和 Bind 一览卡共用的单一事实源，
// 两处不得各自维护一份命令清单，避免新增动作时表间漂移。
enum class EBindOptionGroup
{
	MOVEMENT,
	WEAPON,
	VOTING,
	CHAT,
	DUMMY,
	MISCELLANEOUS,
	// 已知动作表未命中的命令：控制页显示为自定义绑定，Bind 一览卡显示为“其他”分组。
	CUSTOM,
	NUM,
};

struct SKnownBindAction
{
	EBindOptionGroup m_Group;
	const char *m_pLabel;
	const char *m_pCommand;
};

inline constexpr SKnownBindAction s_aKnownBindActions[] = {
	{EBindOptionGroup::MOVEMENT, Localizable("Move left"), "+left"},
	{EBindOptionGroup::MOVEMENT, Localizable("Move right"), "+right"},
	{EBindOptionGroup::MOVEMENT, Localizable("Jump"), "+jump"},
	{EBindOptionGroup::MOVEMENT, Localizable("Fire"), "+fire"},
	{EBindOptionGroup::MOVEMENT, Localizable("Hook"), "+hook"},
	{EBindOptionGroup::MOVEMENT, Localizable("Hook collisions"), "+showhookcoll"},
	{EBindOptionGroup::MOVEMENT, Localizable("Pause"), "say /pause"},
	{EBindOptionGroup::MOVEMENT, Localizable("Kill"), "kill"},
	{EBindOptionGroup::MOVEMENT, Localizable("Zoom in"), "zoom+"},
	{EBindOptionGroup::MOVEMENT, Localizable("Zoom out"), "zoom-"},
	{EBindOptionGroup::MOVEMENT, Localizable("Default zoom"), "zoom"},
	{EBindOptionGroup::MOVEMENT, Localizable("Show others"), "say /showothers"},
	{EBindOptionGroup::MOVEMENT, Localizable("Show all"), "say /showall"},
	{EBindOptionGroup::MOVEMENT, Localizable("Toggle dyncam"), "toggle cl_dyncam 0 1"},
	{EBindOptionGroup::MOVEMENT, Localizable("Toggle ghost"), "toggle cl_race_show_ghost 0 1"},
	{EBindOptionGroup::WEAPON, Localizable("Hammer"), "+weapon1"},
	{EBindOptionGroup::WEAPON, Localizable("Pistol"), "+weapon2"},
	{EBindOptionGroup::WEAPON, Localizable("Shotgun"), "+weapon3"},
	{EBindOptionGroup::WEAPON, Localizable("Grenade"), "+weapon4"},
	{EBindOptionGroup::WEAPON, Localizable("Laser"), "+weapon5"},
	{EBindOptionGroup::WEAPON, Localizable("Weapon trajectory"), "+showweapontrajectory"},
	{EBindOptionGroup::WEAPON, Localizable("Next weapon"), "+nextweapon"},
	{EBindOptionGroup::WEAPON, Localizable("Prev. weapon"), "+prevweapon"},
	{EBindOptionGroup::VOTING, Localizable("Vote yes"), "vote yes"},
	{EBindOptionGroup::VOTING, Localizable("Vote no"), "vote no"},
	{EBindOptionGroup::CHAT, Localizable("Chat"), "+show_chat; chat all"},
	{EBindOptionGroup::CHAT, Localizable("Team chat"), "+show_chat; chat team"},
	{EBindOptionGroup::CHAT, Localizable("Converse"), "+show_chat; chat all /c "},
	{EBindOptionGroup::CHAT, Localizable("Chat command"), "+show_chat; chat all /"},
	{EBindOptionGroup::CHAT, Localizable("Show chat"), "+show_chat"},
	{EBindOptionGroup::CHAT, Localizable("Repeat message"), "+qm_repeat"},
	{EBindOptionGroup::CHAT, Localizable("Voice chat"), "+qm_voice_ptt"},
	{EBindOptionGroup::DUMMY, Localizable("Toggle dummy"), "toggle cl_dummy 0 1"},
	{EBindOptionGroup::DUMMY, Localizable("Dummy jump"), "+toggle_restore cl_dummy_jump 1"},
	{EBindOptionGroup::DUMMY, Localizable("Dummy fire"), "+toggle_restore cl_dummy_fire 1"},
	{EBindOptionGroup::DUMMY, Localizable("Dummy hook"), "+toggle_restore cl_dummy_hook 1"},
	{EBindOptionGroup::DUMMY, Localizable("Dummy copy"), "toggle cl_dummy_copy_moves 0 1"},
	{EBindOptionGroup::DUMMY, Localizable("Dummy hammer fly"), "toggle cl_dummy_hammer 0 1"},
	{EBindOptionGroup::DUMMY, Localizable("Dummy Control"), "toggle cl_dummy_control 1 0"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Emoticon"), "+emote"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate mode"), "+spectate"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate teleport"), "qm_spec_teleport"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate next"), "spectate_next"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate previous"), "spectate_previous"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Active disconnect"), "qm_timeout_disconnect"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Console"), "toggle_local_console"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Remote console"), "toggle_remote_console"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Screenshot"), "screenshot"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Scoreboard"), "+scoreboard"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Scoreboard cursor"), "toggle_scoreboard_cursor"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Statboard"), "+statboard"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Pie menu"), "+pie_menu"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Fast practice"), "fast_practice_toggle"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Lock team"), "say /lock"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Show entities"), "toggle cl_overlay_entities 0 100"},
	{EBindOptionGroup::MISCELLANEOUS, Localizable("Show HUD"), "toggle cl_showhud 0 1"},
};

namespace qm_bind_overview
{
	// 命令到分组的精确匹配（与控制页绑定列表的整串比较一致，
	// 大小写敏感、不做前缀匹配）；未命中返回 CUSTOM（“其他”）。
	inline EBindOptionGroup GroupForCommand(const char *pCommand)
	{
		for(const SKnownBindAction &Action : s_aKnownBindActions)
		{
			if(str_comp(pCommand, Action.m_pCommand) == 0)
				return Action.m_Group;
		}
		return EBindOptionGroup::CUSTOM;
	}

	struct SBindOverviewEntry
	{
		const char *m_pKey;
		const char *m_pCommand;
	};

	// 复制到剪贴板的文本格式：每行 "key = command"，按传入顺序输出，空列表产生空文本。
	inline std::string Serialize(const std::vector<SBindOverviewEntry> &vEntries)
	{
		std::string Text;
		for(const SBindOverviewEntry &Entry : vEntries)
		{
			Text += Entry.m_pKey;
			Text += " = ";
			Text += Entry.m_pCommand;
			Text += '\n';
		}
		return Text;
	}
} // namespace qm_bind_overview

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_OVERVIEW_H
