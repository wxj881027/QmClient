#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SOLO_SPLIT_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SOLO_SPLIT_STATE_H

#include <base/system.h>

#include <engine/client/enums.h>

#include <game/teamscore.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

struct SQmSoloSplitTeamSelection
{
	int m_Main = -1;
	int m_Dummy = -1;

	bool Valid() const { return m_Main > TEAM_FLOCK && m_Dummy > TEAM_FLOCK && m_Main != m_Dummy; }
};

// 只把其他玩家占用的队伍视为障碍；主号和分身的旧队伍会在换队时释放，不能反过来
// 挤占自己。已经处于两个不同合法队伍时直接复用，避免无意义地重新分队。
inline SQmSoloSplitTeamSelection QmSelectSoloSplitTeams(
	int MainTeam,
	int DummyTeam,
	int TeamSuper,
	const std::array<bool, NUM_DDRACE_TEAMS> &aOccupiedByOthers)
{
	SQmSoloSplitTeamSelection Selection;
	const int TeamLimit = std::min(TeamSuper, (int)NUM_DDRACE_TEAMS);
	const auto IsValid = [TeamLimit](int Team) {
		return Team > TEAM_FLOCK && Team < TeamLimit;
	};

	Selection.m_Main = IsValid(MainTeam) ? MainTeam : -1;
	Selection.m_Dummy = IsValid(DummyTeam) ? DummyTeam : -1;
	if(Selection.m_Main >= 0 && Selection.m_Main == Selection.m_Dummy)
		Selection.m_Dummy = -1;

	for(int Team = TEAM_FLOCK + 1; Team < TeamLimit; ++Team)
	{
		if(aOccupiedByOthers[Team] || Team == Selection.m_Main || Team == Selection.m_Dummy)
			continue;
		if(Selection.m_Main < 0)
			Selection.m_Main = Team;
		else if(Selection.m_Dummy < 0)
		{
			Selection.m_Dummy = Team;
			break;
		}
	}
	return Selection;
}

inline bool QmBuildSoloSplitTeamCommand(char *pBuf, int Size, int Team, bool LockAfterJoin)
{
	if(pBuf == nullptr || Size <= 0 || Team < TEAM_FLOCK || Team >= TEAM_SUPER || (LockAfterJoin && Team == TEAM_FLOCK))
		return false;
	// 服务端每条聊天最多计算 4 条命令的限速；主号进服时更容易已经有其它自动命令，
	// 单独紧跟一条 /lock 会被静默丢弃。mc; 让同一聊天命令按顺序执行进队和锁队，
	// 既保证锁的是刚加入的队伍，也不会额外占用第二个命令限速名额。
	const int Length = LockAfterJoin ?
				   str_format(pBuf, Size, "/mc;team %d; lock 1", Team) :
				   str_format(pBuf, Size, "/team %d", Team);
	return Length > 0 && Length < Size;
}

struct SQmSoloSplitTeamLockMessage
{
	bool m_Recognized = false;
	bool m_Locked = false;
};

// 分身快照可能不包含本体角色，计分板因此读不到本体的锁队标志。服务端仍会向
// 每个连接发送锁队确认；解析确认消息可为本体队伍补一份只用于缺快照时的状态。
inline SQmSoloSplitTeamLockMessage QmParseSoloSplitTeamLockMessage(const char *pMessage, const char *pPlayerName)
{
	if(pMessage == nullptr || pPlayerName == nullptr || pPlayerName[0] == '\0')
		return {};

	char aPrefix[64];
	const int PrefixLength = str_format(aPrefix, sizeof(aPrefix), "'%s'", pPlayerName);
	if(PrefixLength <= 0 || PrefixLength >= (int)sizeof(aPrefix) || !str_startswith(pMessage, aPrefix))
		return {};

	const char *pRest = pMessage + PrefixLength;
	if(str_startswith_nocase(pRest, " locked your team") || str_startswith(pRest, " 锁定了你们的队伍"))
		return {true, true};
	if(str_startswith_nocase(pRest, " unlocked your team") || str_startswith(pRest, " 解锁了你们的队伍"))
		return {true, false};
	return {};
}

// 只记录单刷命令等待服务端确认的队伍；确认成功后供计分板在缺少角色快照时兜底。
class CQmSoloSplitLockHints
{
	std::array<int, NUM_DUMMIES> m_aPendingTeams;
	std::array<int, NUM_DUMMIES> m_aLockedTeams;

public:
	CQmSoloSplitLockHints()
	{
		m_aPendingTeams.fill(-1);
		m_aLockedTeams.fill(-1);
	}

	void Reset()
	{
		m_aPendingTeams.fill(-1);
		m_aLockedTeams.fill(-1);
	}

	void ResetConnection(int Connection)
	{
		if(Connection < 0 || Connection >= NUM_DUMMIES)
			return;
		m_aPendingTeams[Connection] = -1;
		m_aLockedTeams[Connection] = -1;
	}

	void ExpectLock(int Connection, int Team)
	{
		if(Connection < 0 || Connection >= NUM_DUMMIES || Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
			return;
		m_aPendingTeams[Connection] = Team;
	}

	bool ApplyConfirmation(int Connection, bool Locked)
	{
		if(Connection < 0 || Connection >= NUM_DUMMIES)
			return false;
		const int PendingTeam = m_aPendingTeams[Connection];
		if(PendingTeam <= TEAM_FLOCK)
			return false;

		if(Locked)
			m_aLockedTeams[Connection] = PendingTeam;
		else if(m_aLockedTeams[Connection] == PendingTeam)
			m_aLockedTeams[Connection] = -1;
		m_aPendingTeams[Connection] = -1;
		return true;
	}

	bool IsTeamLocked(int Team) const
	{
		return Team > TEAM_FLOCK && Team < TEAM_SUPER &&
		       std::any_of(m_aLockedTeams.begin(), m_aLockedTeams.end(), [Team](int LockedTeam) { return LockedTeam == Team; });
	}
};

// 只管理等待快照、重试和取消；合法队伍选择仍由组件使用当前服务器名单完成。
class CQmSoloSplitState
{
	int m_Action = 0; // 1=进队，2=出队
	bool m_WaitingForPlayers = false;
	int m_Attempts = 0;
	int64_t m_Deadline = 0;
	std::array<int, 2> m_aPreviousTeams = {};
	std::array<int, 2> m_aTargetTeams = {};

public:
	bool Pending() const { return m_Action != 0; }
	bool WaitingForPlayers() const { return m_WaitingForPlayers; }
	bool LocksEnteredTeams() const { return m_Action == 1 && !m_WaitingForPlayers; }
	bool ReachedEnterTarget(const std::array<int, 2> &aCurrentTeams) const
	{
		return m_Action == 1 && !m_WaitingForPlayers && aCurrentTeams == m_aTargetTeams;
	}
	void Reset()
	{
		m_Action = 0;
		m_WaitingForPlayers = false;
		m_Attempts = 0;
		m_Deadline = 0;
		m_aPreviousTeams = {};
		m_aTargetTeams = {};
	}

	void WaitForPlayers(int Action, int64_t Now, int64_t TickFrequency)
	{
		if(Pending())
			return;
		m_Action = Action;
		m_WaitingForPlayers = true;
		m_Deadline = Now + 10 * TickFrequency;
	}

	int TakeReadyAction(bool Online, bool DummyConnected, bool PlayerIdsValid, int64_t Now)
	{
		if(!m_WaitingForPlayers)
			return 0;
		// 连接就绪不代表首个快照已到达；等待期间仍遵守同一个超时窗口。
		if(!Online || Now >= m_Deadline)
		{
			Reset();
			return 0;
		}
		if(!DummyConnected || !PlayerIdsValid)
			return 0;
		const int Action = m_Action;
		Reset();
		return Action;
	}

	void StartTeams(int Action, std::array<int, 2> aPreviousTeams, std::array<int, 2> aTargetTeams, int64_t Now)
	{
		if(Pending())
			return;
		m_Action = Action;
		m_WaitingForPlayers = false;
		m_Attempts = 0;
		m_Deadline = Now;
		m_aPreviousTeams = aPreviousTeams;
		m_aTargetTeams = aTargetTeams;
	}

	std::optional<std::array<int, 2>> UpdateTeams(bool Online, bool DummyConnected, bool PlayerIdsValid, std::array<int, 2> aCurrentTeams, int64_t Now, int64_t TickFrequency)
	{
		if(!Pending() || m_WaitingForPlayers)
			return std::nullopt;
		// 已开始的操作属于当前两条连接；断连或快照失效时取消，不能向新连接回滚。
		if(!Online || !DummyConnected || !PlayerIdsValid)
		{
			Reset();
			return std::nullopt;
		}
		// 进入单刷时先强制发送一次 /team，避免本地队伍表陈旧时误判“已经在目标队”。
		// 离开操作不做这次强制发送，已经回到恢复队时直接结束。
		if(m_Action == 2 && aCurrentTeams == m_aTargetTeams)
		{
			Reset();
			return std::nullopt;
		}
		if(m_Action == 1 && aCurrentTeams == m_aTargetTeams && m_Attempts > 0)
		{
			Reset();
			return std::nullopt;
		}
		if(Now < m_Deadline)
			return std::nullopt;
		if(m_Attempts < 2)
		{
			++m_Attempts;
			m_Deadline = Now + 2 * TickFrequency;
			return m_aTargetTeams;
		}
		const auto aPreviousTeams = m_aPreviousTeams;
		Reset();
		return aPreviousTeams;
	}
};

#endif
