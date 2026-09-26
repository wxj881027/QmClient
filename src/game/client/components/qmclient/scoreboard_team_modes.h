// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_TEAM_MODES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_TEAM_MODES_H

#include <generated/protocol.h>

#include <game/teamscore.h>

#include <array>

constexpr int QM_SCOREBOARD_TEAM_MODE_MASK = CHARACTERFLAG_PRACTICE_MODE | CHARACTERFLAG_TEAM0_MODE | CHARACTERFLAG_LOCK_MODE;

struct SQmScoreboardTeamModeState
{
	bool m_Known = false;
	int m_Flags = 0;

	constexpr bool Practice() const { return (m_Flags & CHARACTERFLAG_PRACTICE_MODE) != 0; }
	constexpr bool Team0Mode() const { return (m_Flags & CHARACTERFLAG_TEAM0_MODE) != 0; }
	constexpr bool Locked() const { return (m_Flags & CHARACTERFLAG_LOCK_MODE) != 0; }
};

inline void AccumulateQmScoreboardTeamModeState(SQmScoreboardTeamModeState &State, bool HasExtendedDisplayInfo, int CharacterFlags)
{
	if(!HasExtendedDisplayInfo)
		return;
	State.m_Known = true;
	State.m_Flags |= CharacterFlags & QM_SCOREBOARD_TEAM_MODE_MASK;
}

constexpr int QmScoreboardEffectivePlayerTeam(int PlayerTeam, bool IsSpec, bool IsTeamPlay)
{
	return !IsTeamPlay && IsSpec && PlayerTeam == TEAM_SPECTATORS ? TEAM_GAME : PlayerTeam;
}

// aTeamHasPlayer[Team] 表示该队伍本帧仍有成员行（角色数据可能缺失：整队死亡、进旁观镜头等）。
// 成员行存在但 m_Known 为假时，用缓存恢复上一次已知的模式状态，避免练习/锁队图标闪没。
// 队伍解散（成员行消失）时不恢复，防止残留旧状态。
inline void CacheAndRestoreQmScoreboardTeamModes(
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> &aTeamModes,
	const std::array<bool, NUM_DDRACE_TEAMS> &aTeamHasPlayer,
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> &aCachedTeamModes)
{
	for(int Team = TEAM_FLOCK; Team < NUM_DDRACE_TEAMS; ++Team)
	{
		if(aTeamModes[Team].m_Known)
			aCachedTeamModes[Team] = aTeamModes[Team];
		else if(aTeamHasPlayer[Team] && aCachedTeamModes[Team].m_Known)
			aTeamModes[Team] = aCachedTeamModes[Team];
	}
}

#endif
