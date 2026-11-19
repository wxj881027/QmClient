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

// 筛选后的行计划只读取缓存，不能用未显示的成员或局部标记覆盖完整快照缓存。
inline void RestoreQmScoreboardTeamModes(
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> &aTeamModes,
	const std::array<bool, NUM_DDRACE_TEAMS> &aTeamHasPlayer,
	const std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> &aCachedTeamModes)
{
	for(int Team = TEAM_FLOCK; Team < NUM_DDRACE_TEAMS; ++Team)
	{
		if(!aTeamModes[Team].m_Known && aTeamHasPlayer[Team] && aCachedTeamModes[Team].m_Known)
			aTeamModes[Team] = aCachedTeamModes[Team];
	}
}

// 仅完整玩家快照可以更新缓存；角色信息暂缺时保留，队伍成员全部消失时清除。
inline void CacheAndRestoreQmScoreboardTeamModes(
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> &aTeamModes,
	const std::array<bool, NUM_DDRACE_TEAMS> &aTeamHasPlayer,
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> &aCachedTeamModes)
{
	for(int Team = TEAM_FLOCK; Team < NUM_DDRACE_TEAMS; ++Team)
	{
		if(aTeamModes[Team].m_Known)
			aCachedTeamModes[Team] = aTeamModes[Team];
		else if(!aTeamHasPlayer[Team])
			aCachedTeamModes[Team] = {};
	}
	RestoreQmScoreboardTeamModes(aTeamModes, aTeamHasPlayer, aCachedTeamModes);
}

#endif
