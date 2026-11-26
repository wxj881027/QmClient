#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AUDIO_PACK_CATALOG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AUDIO_PACK_CATALOG_H

#include <base/str.h>

#include <engine/shared/localization.h>

#include <algorithm>
#include <array>
#include <vector>

namespace qm_audio_pack
{
	enum class ECategory
	{
		ALL,
		WEAPONS,
		HOOK,
		MOVEMENT,
		PLAYER_STATE,
		PICKUPS,
		CHAT,
		FLAGS,
		MUSIC,
		OTHER,
		INTERFACE,
		MAP_ENVIRONMENT,
		COUNT,
	};

	inline constexpr std::array EDITOR_CATEGORIES = {
		ECategory::ALL, ECategory::MOVEMENT, ECategory::WEAPONS, ECategory::HOOK,
		ECategory::INTERFACE, ECategory::MAP_ENVIRONMENT, ECategory::OTHER};

	inline ECategory EditorCategory(ECategory Category)
	{
		switch(Category)
		{
		case ECategory::CHAT:
		case ECategory::MUSIC: return ECategory::INTERFACE;
		case ECategory::PICKUPS:
		case ECategory::FLAGS: return ECategory::MAP_ENVIRONMENT;
		case ECategory::PLAYER_STATE: return ECategory::OTHER;
		default: return Category;
		}
	}

	enum class EContext
	{
		DDNET,
		OTHER_MODES,
		SERVER_DEPENDENT,
	};

	struct SSlot
	{
		const char *m_pDisplayName = nullptr;
		const char *m_pSetName = nullptr;
		const char *m_pRelativePath = nullptr;
		int m_SetId = -1;
		int m_VariantIndex = 0;
		int m_VariantCount = 0;
	};

	struct SSoundInfo
	{
		const char *m_pSetName;
		const char *m_pName;
		const char *m_pUsage;
		ECategory m_Category;
		EContext m_Context;
	};

	inline const char *CategoryName(ECategory Category)
	{
		switch(Category)
		{
		case ECategory::ALL: return Localizable("All sounds");
		case ECategory::WEAPONS: return Localizable("Weapons");
		case ECategory::HOOK: return Localizable("Hook");
		case ECategory::MOVEMENT: return Localizable("Movement");
		case ECategory::PLAYER_STATE: return Localizable("Player state");
		case ECategory::PICKUPS: return Localizable("Pickups");
		case ECategory::CHAT: return Localizable("Chat");
		case ECategory::FLAGS: return Localizable("Capture the flag");
		case ECategory::MUSIC: return Localizable("Music");
		case ECategory::INTERFACE: return Localizable("Interface");
		case ECategory::MAP_ENVIRONMENT: return Localizable("Map and environment");
		default: return Localizable("Other sounds");
		}
	}

	inline const char *ContextName(EContext Context)
	{
		switch(Context)
		{
		case EContext::DDNET: return "DDNet";
		case EContext::OTHER_MODES: return Localizable("Other game modes");
		default: return Localizable("Server-dependent");
		}
	}

	inline const SSoundInfo &SoundInfo(const char *pSetName)
	{
		// 描述依据资源注册和真实触发点；未找到直接触发点的资源不推测跑图用途。
		static const SSoundInfo s_aSounds[] = {
			{"gun_fire", Localizable("Gun fire"), Localizable("Played when firing this weapon."), ECategory::WEAPONS, EContext::DDNET},
			{"shotgun_fire", Localizable("Shotgun fire"), Localizable("Played when firing this weapon."), ECategory::WEAPONS, EContext::DDNET},
			{"grenade_fire", Localizable("Grenade launch"), Localizable("Played when firing this weapon."), ECategory::WEAPONS, EContext::DDNET},
			{"hammer_fire", Localizable("Hammer swing"), Localizable("Played when swinging the hammer, even without hitting a player."), ECategory::WEAPONS, EContext::DDNET},
			{"hammer_hit", Localizable("Hammer hit"), Localizable("Played when the hammer hits another player."), ECategory::WEAPONS, EContext::DDNET},
			{"ninja_fire", Localizable("Ninja attack"), Localizable("Played when starting a ninja dash attack."), ECategory::WEAPONS, EContext::DDNET},
			{"grenade_explode", Localizable("Grenade explosion"), Localizable("Played when a grenade explodes."), ECategory::WEAPONS, EContext::DDNET},
			{"ninja_hit", Localizable("Ninja hit"), Localizable("Played when a ninja dash hits another player."), ECategory::WEAPONS, EContext::DDNET},
			{"laser_fire", Localizable("Laser fire"), Localizable("Played when firing this weapon."), ECategory::WEAPONS, EContext::DDNET},
			{"laser_bounce", Localizable("Laser / shotgun bounce"), Localizable("Played when a laser or DDNet shotgun beam bounces."), ECategory::WEAPONS, EContext::DDNET},
			{"weapon_switch", Localizable("Weapon switch"), Localizable("Played when switching the active weapon."), ECategory::WEAPONS, EContext::DDNET},
			{"player_pain_short", Localizable("Short pain voice"), Localizable("Registered sound; no direct trigger was found in this DDNet server. Other servers may use it."), ECategory::PLAYER_STATE, EContext::SERVER_DEPENDENT},
			{"player_pain_long", Localizable("Frozen fire attempt"), Localizable("Played when pressing fire while frozen."), ECategory::PLAYER_STATE, EContext::DDNET},
			{"body_land", Localizable("Landing"), Localizable("Registered sound; no direct trigger was found in this DDNet server. Other servers may use it."), ECategory::MOVEMENT, EContext::SERVER_DEPENDENT},
			{"player_airjump", Localizable("Air jump"), Localizable("Played when using the second jump in the air."), ECategory::MOVEMENT, EContext::DDNET},
			{"player_jump", Localizable("Ground jump"), Localizable("Played when jumping from the ground. Left and right foot files belong to this same sound group."), ECategory::MOVEMENT, EContext::DDNET},
			{"player_die", Localizable("Player death"), Localizable("Played when a player dies, including suicide."), ECategory::PLAYER_STATE, EContext::DDNET},
			{"player_spawn", Localizable("Player spawn"), Localizable("Played when a player spawns or respawns."), ECategory::PLAYER_STATE, EContext::DDNET},
			{"player_skid", Localizable("Skidding"), Localizable("Played when moving fast on the ground and pressing the opposite direction."), ECategory::MOVEMENT, EContext::DDNET},
			{"tee_cry", Localizable("Tee cry"), Localizable("Registered sound; no direct trigger was found in this DDNet server. Other servers may use it."), ECategory::PLAYER_STATE, EContext::SERVER_DEPENDENT},
			{"hook_loop", Localizable("Hook loop"), Localizable("Registered sound; no direct trigger was found in this DDNet server. Other servers may use it."), ECategory::HOOK, EContext::SERVER_DEPENDENT},
			{"hook_attach_ground", Localizable("Hook attaches to ground"), Localizable("Played when the hook attaches to a hookable surface."), ECategory::HOOK, EContext::DDNET},
			{"hook_attach_player", Localizable("Hook attaches to player"), Localizable("Played when the hook attaches to another player."), ECategory::HOOK, EContext::DDNET},
			{"hook_noattach", Localizable("Hook hits unhookable surface"), Localizable("Played when the hook hits an unhookable surface."), ECategory::HOOK, EContext::DDNET},
			{"pickup_health", Localizable("Freeze heart / health pickup"), Localizable("In DDNet, played when a freeze heart freezes the player. Other modes may use it for health pickups."), ECategory::PICKUPS, EContext::DDNET},
			{"pickup_armor", Localizable("Weapon removal / armor pickup"), Localizable("In DDNet, played when weapons are removed. Other modes may use it for armor pickups."), ECategory::PICKUPS, EContext::DDNET},
			{"pickup_grenade", Localizable("Grenade pickup"), Localizable("Played when picking up a grenade weapon."), ECategory::PICKUPS, EContext::DDNET},
			{"pickup_shotgun", Localizable("Shotgun / laser pickup"), Localizable("Played when picking up a shotgun or laser weapon."), ECategory::PICKUPS, EContext::DDNET},
			{"pickup_ninja", Localizable("Ninja pickup"), Localizable("Registered sound; no direct trigger was found in this DDNet server. Other servers may use it."), ECategory::PICKUPS, EContext::SERVER_DEPENDENT},
			{"weapon_spawn", Localizable("Weapon teleport"), Localizable("Played when weapon teleportation moves the player in DDNet."), ECategory::PICKUPS, EContext::DDNET},
			{"weapon_noammo", Localizable("No ammunition"), Localizable("Registered sound; no direct trigger was found in this DDNet server. Other servers may use it."), ECategory::WEAPONS, EContext::SERVER_DEPENDENT},
			{"hit", Localizable("Hit feedback"), Localizable("Registered sound; no direct trigger was found in this DDNet server. Other servers may use it."), ECategory::WEAPONS, EContext::SERVER_DEPENDENT},
			{"chat_server", Localizable("Server message"), Localizable("Played for incoming server chat messages when their notification sound is enabled."), ECategory::CHAT, EContext::DDNET},
			{"chat_client", Localizable("Player chat message"), Localizable("Played for incoming player chat messages when their notification sound is enabled."), ECategory::CHAT, EContext::DDNET},
			{"chat_highlight", Localizable("Chat mention / vote alert"), Localizable("Played for chat highlights and vote alerts when their notification sound is enabled."), ECategory::CHAT, EContext::DDNET},
			{"ctf_drop", Localizable("Flag dropped"), Localizable("Capture-the-flag event cue, used by CTF servers rather than ordinary DDNet racing."), ECategory::FLAGS, EContext::OTHER_MODES},
			{"ctf_return", Localizable("Flag returned"), Localizable("Capture-the-flag event cue, used by CTF servers rather than ordinary DDNet racing."), ECategory::FLAGS, EContext::OTHER_MODES},
			{"ctf_grab_pl", Localizable("Flag grab: player side"), Localizable("Capture-the-flag event cue, used by CTF servers rather than ordinary DDNet racing."), ECategory::FLAGS, EContext::OTHER_MODES},
			{"ctf_grab_en", Localizable("Flag grab: enemy side"), Localizable("Capture-the-flag event cue, used by CTF servers rather than ordinary DDNet racing."), ECategory::FLAGS, EContext::OTHER_MODES},
			{"ctf_capture", Localizable("Flag captured"), Localizable("Capture-the-flag event cue, used by CTF servers rather than ordinary DDNet racing."), ECategory::FLAGS, EContext::OTHER_MODES},
			{"menu", Localizable("Menu music"), Localizable("Played while offline when menu music is enabled."), ECategory::MUSIC, EContext::DDNET},
		};
		for(const auto &Info : s_aSounds)
			if(pSetName != nullptr && str_comp(Info.m_pSetName, pSetName) == 0)
				return Info;
		static const SSoundInfo s_Unknown = {"", Localizable("Other sounds"), Localizable("Its use depends on the server or game mode."), ECategory::OTHER, EContext::SERVER_DEPENDENT};
		return s_Unknown;
	}

	struct SGroup
	{
		int m_SetId;
		const char *m_pSetName;
		const SSoundInfo *m_pInfo;
		std::vector<int> m_vSlotIndices;
	};

	inline std::vector<SGroup> BuildGroups(const std::vector<SSlot> &vSlots)
	{
		std::vector<SGroup> vGroups;
		for(int Index = 0; Index < (int)vSlots.size(); ++Index)
		{
			const auto &Slot = vSlots[Index];
			auto It = std::find_if(vGroups.begin(), vGroups.end(), [&](const SGroup &Group) { return Group.m_SetId == Slot.m_SetId; });
			if(It == vGroups.end())
				vGroups.push_back({Slot.m_SetId, Slot.m_pSetName, &SoundInfo(Slot.m_pSetName), {Index}});
			else
				It->m_vSlotIndices.push_back(Index);
		}
		std::stable_sort(vGroups.begin(), vGroups.end(), [](const SGroup &A, const SGroup &B) { return A.m_pInfo->m_Category < B.m_pInfo->m_Category; });
		return vGroups;
	}

	using FTranslate = const char *(*)(const char *);

	inline std::vector<int> FilterGroups(const std::vector<SGroup> &vGroups, const std::vector<SSlot> &vSlots, ECategory Category, const char *pQuery, FTranslate pTranslate = nullptr)
	{
		std::vector<int> vVisible;
		for(int Index = 0; Index < (int)vGroups.size(); ++Index)
		{
			const auto &Group = vGroups[Index];
			if(Category != ECategory::ALL && Category != Group.m_pInfo->m_Category && Category != EditorCategory(Group.m_pInfo->m_Category))
				continue;
			bool Matches = pQuery == nullptr || pQuery[0] == '\0';
			const char *apTexts[] = {Group.m_pSetName, Group.m_pInfo->m_pName, Group.m_pInfo->m_pUsage, CategoryName(Group.m_pInfo->m_Category), CategoryName(EditorCategory(Group.m_pInfo->m_Category))};
			for(const char *pText : apTexts)
			{
				Matches |= str_utf8_find_nocase(pText, pQuery != nullptr ? pQuery : "") != nullptr;
				if(pTranslate != nullptr)
					Matches |= str_utf8_find_nocase(pTranslate(pText), pQuery != nullptr ? pQuery : "") != nullptr;
			}
			for(int SlotIndex : Group.m_vSlotIndices)
				Matches |= str_utf8_find_nocase(vSlots[SlotIndex].m_pRelativePath, pQuery != nullptr ? pQuery : "") != nullptr;
			if(Matches)
				vVisible.push_back(Index);
		}
		return vVisible;
	}

	inline int SelectedGroup(const std::vector<SGroup> &vGroups, int SlotIndex)
	{
		for(int Index = 0; Index < (int)vGroups.size(); ++Index)
			if(std::find(vGroups[Index].m_vSlotIndices.begin(), vGroups[Index].m_vSlotIndices.end(), SlotIndex) != vGroups[Index].m_vSlotIndices.end())
				return Index;
		return -1;
	}

	inline int SelectGroupSlot(const SGroup &Group, int PreviousSlot)
	{
		if(std::find(Group.m_vSlotIndices.begin(), Group.m_vSlotIndices.end(), PreviousSlot) != Group.m_vSlotIndices.end())
			return PreviousSlot;
		return Group.m_vSlotIndices.empty() ? -1 : Group.m_vSlotIndices.front();
	}
}

#endif
