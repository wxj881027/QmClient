/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_LOGIC_H

#include <cstdint>
#include <limits>

struct SQmAutoTeamLockInput
{
	bool m_Enabled = false;
	bool m_Online = false;
	bool m_LocalPlayerValid = false;
	bool m_TeamCanBeLocked = false;
	int m_Team = 0;
	int64_t m_CurrentTick = 0;
	int64_t m_TickSpeed = 0;
	int64_t m_DelaySeconds = 0;
};

struct SQmAutoTeamLockAction
{
	bool m_SendLockCommand = false;
};

class CQmAutoTeamLockLogic final
{
	bool m_HadTeam = false;
	bool m_LastTeamCanBeLocked = false;
	int m_LastTeam = 0;
	bool m_Pending = false;
	int64_t m_DeadlineTick = 0;

public:
	void Reset()
	{
		m_HadTeam = false;
		m_LastTeamCanBeLocked = false;
		m_LastTeam = 0;
		m_Pending = false;
		m_DeadlineTick = 0;
	}

	SQmAutoTeamLockAction Update(const SQmAutoTeamLockInput &Input)
	{
		SQmAutoTeamLockAction Action;
		if(!Input.m_Online || !Input.m_LocalPlayerValid || Input.m_TickSpeed <= 0)
		{
			Reset();
			return Action;
		}

		if(!Input.m_Enabled)
		{
			m_LastTeam = Input.m_Team;
			m_LastTeamCanBeLocked = Input.m_TeamCanBeLocked;
			m_HadTeam = true;
			m_Pending = false;
			m_DeadlineTick = 0;
			return Action;
		}

		if(Input.m_TeamCanBeLocked &&
			(!m_HadTeam || !m_LastTeamCanBeLocked || Input.m_Team != m_LastTeam))
		{
			const int64_t DelayTicks = Input.m_DelaySeconds > 0 ?
				(Input.m_DelaySeconds > std::numeric_limits<int64_t>::max() / Input.m_TickSpeed ?
					std::numeric_limits<int64_t>::max() : Input.m_DelaySeconds * Input.m_TickSpeed) :
				0;
			m_DeadlineTick = Input.m_CurrentTick > std::numeric_limits<int64_t>::max() - DelayTicks ?
				std::numeric_limits<int64_t>::max() : Input.m_CurrentTick + DelayTicks;
			m_Pending = true;
		}
		else if(!Input.m_TeamCanBeLocked)
		{
			m_Pending = false;
			m_DeadlineTick = 0;
		}

		if(m_Pending && Input.m_TeamCanBeLocked && Input.m_CurrentTick >= m_DeadlineTick)
		{
			m_Pending = false;
			m_DeadlineTick = 0;
			Action.m_SendLockCommand = true;
		}

		m_LastTeam = Input.m_Team;
		m_LastTeamCanBeLocked = Input.m_TeamCanBeLocked;
		m_HadTeam = true;
		return Action;
	}
};

#endif
