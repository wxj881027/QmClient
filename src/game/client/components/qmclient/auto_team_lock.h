#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AUTO_TEAM_LOCK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AUTO_TEAM_LOCK_H

#include <engine/client/enums.h>

#include <game/teamscore.h>

#include <algorithm>
#include <array>
#include <cstdint>

// 自动锁队按连接独立计时；同一队伍只发出一次锁队命令。
struct SQmAutoTeamLockRequest
{
	int m_Connection = -1;
	int m_Team = TEAM_FLOCK;
};

struct SQmAutoTeamLockPlan
{
	std::array<SQmAutoTeamLockRequest, NUM_DUMMIES> m_aRequests{};
	int m_RequestCount = 0;
};

class CQmAutoTeamLockState
{
	struct SConnectionState
	{
		int m_LastTeam = TEAM_FLOCK;
		int64_t m_DeadlineTick = 0;
		bool m_Pending = false;
	};

	std::array<SConnectionState, NUM_DUMMIES> m_aConnections{};

public:
	void Reset()
	{
		m_aConnections = {};
	}

	void ResetConnection(int Connection)
	{
		if(Connection >= 0 && Connection < NUM_DUMMIES)
			m_aConnections[Connection] = {};
	}

	SQmAutoTeamLockPlan Update(
		bool Enabled,
		int DelaySeconds,
		int TickSpeed,
		const std::array<int, NUM_DUMMIES> &aTeams,
		const std::array<int, NUM_DUMMIES> &aTeamSupers,
		const std::array<int64_t, NUM_DUMMIES> &aGameTicks,
		const std::array<bool, NUM_DUMMIES> &aConnected,
		const std::array<bool, NUM_DUMMIES> &aSuppressLock = {})
	{
		std::array<bool, NUM_DUMMIES> aDue{};
		for(int Connection = 0; Connection < NUM_DUMMIES; ++Connection)
		{
			SConnectionState &State = m_aConnections[Connection];
			if(!aConnected[Connection])
			{
				State = {};
				continue;
			}

			const int Team = aTeams[Connection];
			const int TeamSuper = aTeamSupers[Connection];
			const bool TeamCanBeLocked = Team > TEAM_FLOCK && Team < TeamSuper;
			if(!Enabled || aSuppressLock[Connection])
			{
				State.m_LastTeam = Team;
				State.m_DeadlineTick = 0;
				State.m_Pending = false;
				continue;
			}

			const bool LastTeamCanBeLocked = State.m_LastTeam > TEAM_FLOCK && State.m_LastTeam < TeamSuper;
			if(TeamCanBeLocked && (!LastTeamCanBeLocked || Team != State.m_LastTeam))
			{
				const int64_t DelayTicks = static_cast<int64_t>(std::max(0, DelaySeconds)) * std::max(0, TickSpeed);
				State.m_DeadlineTick = aGameTicks[Connection] + DelayTicks;
				State.m_Pending = true;
			}
			else if(!TeamCanBeLocked)
			{
				State.m_DeadlineTick = 0;
				State.m_Pending = false;
			}

			if(State.m_Pending && TeamCanBeLocked && aGameTicks[Connection] >= State.m_DeadlineTick)
				aDue[Connection] = true;
			State.m_LastTeam = Team;
		}

		SQmAutoTeamLockPlan Plan;
		for(int Connection = 0; Connection < NUM_DUMMIES; ++Connection)
		{
			if(!aDue[Connection])
				continue;

			const int Team = aTeams[Connection];
			const bool AlreadyRequested = std::any_of(
				Plan.m_aRequests.begin(),
				Plan.m_aRequests.begin() + Plan.m_RequestCount,
				[Team](const SQmAutoTeamLockRequest &Request) { return Request.m_Team == Team; });
			if(!AlreadyRequested)
			{
				Plan.m_aRequests[Plan.m_RequestCount++] = {Connection, Team};
			}
			for(int Other = 0; Other < NUM_DUMMIES; ++Other)
			{
				if(aTeams[Other] == Team)
					m_aConnections[Other].m_Pending = false;
			}
		}
		return Plan;
	}
};

#endif
