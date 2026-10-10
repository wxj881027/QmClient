#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GORES_DROWN_TRACKER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GORES_DROWN_TRACKER_H

#include <game/teamscore.h>

#include <array>
#include <limits>
#include <string>
#include <string_view>

class CQmGoresDrownTracker
{
	struct SPlayer
	{
		std::string m_Name;
		std::string m_Clan;
		int m_Team = -1;
		int m_Count = 0;
		bool m_HasObservedCharacter = false;
		bool m_SeenCharacter = false;
		bool m_Frozen = false;
		bool m_Hooking = false;
		bool m_Armed = false;
	};
	std::array<SPlayer, MAX_CLIENTS> m_aPlayers{};

public:
	// 榜单只支持服务器声明的 Gores 模式，Team0 的统计与显示共用过滤策略。
	static bool IsTrackedTeam(int Team, bool IncludeTeamZero)
	{
		return Team >= TEAM_FLOCK && Team < NUM_DDRACE_TEAMS && (IncludeTeamZero || Team != TEAM_FLOCK);
	}

	static bool IsSameTrackedTeam(int LocalTeam, int PlayerTeam, bool IncludeTeamZero)
	{
		return IsTrackedTeam(LocalTeam, IncludeTeamZero) && LocalTeam == PlayerTeam;
	}

	static bool IsBoardVisible(bool GoresGameMode, bool HasLocalClient, int LocalTeam, bool IncludeTeamZero)
	{
		return GoresGameMode && HasLocalClient && IsTrackedTeam(LocalTeam, IncludeTeamZero);
	}

	void Reset()
	{
		m_aPlayers = {};
	}

	int Count(int ClientId) const
	{
		return ClientId >= 0 && ClientId < MAX_CLIENTS ? m_aPlayers[ClientId].m_Count : 0;
	}

	void Observe(int ClientId, bool Active, std::string_view Name, std::string_view Clan, int Team, bool HasCharacter, bool Frozen, bool Hooking, bool IncludeTeamZero)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return;
		SPlayer &Player = m_aPlayers[ClientId];
		if(!Active || !IsTrackedTeam(Team, IncludeTeamZero))
		{
			Player = {};
			return;
		}
		// 身份或队伍改变后不复用旧槽位，避免离服、换队与 ClientId 重用串榜。
		if(Player.m_Team != Team || Player.m_Name != Name || Player.m_Clan != Clan)
		{
			Player = {};
			Player.m_Name = Name;
			Player.m_Clan = Clan;
			Player.m_Team = Team;
		}
		if(!HasCharacter)
		{
			// 角色暂时不在 snapshot 中时保留次数，不推断冻结或出钩事件。
			Player.m_SeenCharacter = false;
			Player.m_Frozen = false;
			Player.m_Hooking = false;
			Player.m_Armed = false;
			return;
		}
		if(!Player.m_SeenCharacter)
		{
			Player.m_SeenCharacter = true;
			Player.m_Frozen = Frozen;
			Player.m_Hooking = Hooking;
			// 首次看到已冻结角色只建立基线；正常角色的首次落水可计数。
			Player.m_Armed = !Frozen && !Player.m_HasObservedCharacter;
			Player.m_HasObservedCharacter = true;
			return;
		}
		if(Frozen)
		{
			if(!Player.m_Frozen && Player.m_Armed && Player.m_Count < std::numeric_limits<int>::max())
				++Player.m_Count;
			Player.m_Armed = false;
		}
		else if(Hooking && !Player.m_Hooking)
		{
			// 救起后实际重新出钩才允许下一次落水，冻结中出钩不会重新解锁。
			Player.m_Armed = true;
		}
		Player.m_Frozen = Frozen;
		Player.m_Hooking = Hooking;
	}
};

#endif
