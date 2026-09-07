/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_LOGIC_H

#include <array>
#include <cstdint>
#include <limits>

constexpr int QM_AUTO_TEAM_LOCK_DUMMY_SLOT_COUNT = 2;

struct SQmAutoTeamLockInput
{
	bool m_Enabled = false;
	bool m_Online = false;
	bool m_LocalPlayerValid = false;
	bool m_TeamCanBeLocked = false;
	int m_Team = 0;
	int m_Dummy = 0;
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
	struct SState
	{
		bool m_HadTeam = false;
		bool m_LastTeamCanBeLocked = false;
		int m_LastTeam = 0;
		bool m_Pending = false;
		int64_t m_DeadlineTick = 0;
	};

	std::array<SState, QM_AUTO_TEAM_LOCK_DUMMY_SLOT_COUNT> m_aStates;

	static int Slot(int Dummy)
	{
		return Dummy <= 0 ? 0 : (Dummy >= QM_AUTO_TEAM_LOCK_DUMMY_SLOT_COUNT ? QM_AUTO_TEAM_LOCK_DUMMY_SLOT_COUNT - 1 : Dummy);
	}

public:
	void Reset()
	{
		m_aStates = {};
	}

	void Disable()
	{
		for(SState &State : m_aStates)
		{
			State.m_Pending = false;
			State.m_DeadlineTick = 0;
		}
	}

	SQmAutoTeamLockAction Update(const SQmAutoTeamLockInput &Input)
	{
		SQmAutoTeamLockAction Action;
		if(!Input.m_Online || !Input.m_LocalPlayerValid || Input.m_TickSpeed <= 0)
		{
			Reset();
			return Action;
		}

		SState &State = m_aStates[Slot(Input.m_Dummy)];

		if(!Input.m_Enabled)
		{
			State.m_LastTeam = Input.m_Team;
			State.m_LastTeamCanBeLocked = Input.m_TeamCanBeLocked;
			State.m_HadTeam = true;
			State.m_Pending = false;
			State.m_DeadlineTick = 0;
			return Action;
		}

		if(Input.m_TeamCanBeLocked &&
			(!State.m_HadTeam || !State.m_LastTeamCanBeLocked || Input.m_Team != State.m_LastTeam))
		{
			const int64_t DelayTicks = Input.m_DelaySeconds > 0 ?
				(Input.m_DelaySeconds > std::numeric_limits<int64_t>::max() / Input.m_TickSpeed ?
					std::numeric_limits<int64_t>::max() : Input.m_DelaySeconds * Input.m_TickSpeed) :
				0;
			State.m_DeadlineTick = Input.m_CurrentTick > std::numeric_limits<int64_t>::max() - DelayTicks ?
				std::numeric_limits<int64_t>::max() : Input.m_CurrentTick + DelayTicks;
			State.m_Pending = true;
		}
		else if(!Input.m_TeamCanBeLocked)
		{
			State.m_Pending = false;
			State.m_DeadlineTick = 0;
		}

		if(State.m_Pending && Input.m_TeamCanBeLocked && Input.m_CurrentTick >= State.m_DeadlineTick)
		{
			State.m_Pending = false;
			State.m_DeadlineTick = 0;
			Action.m_SendLockCommand = true;
		}

		State.m_LastTeam = Input.m_Team;
		State.m_LastTeamCanBeLocked = Input.m_TeamCanBeLocked;
		State.m_HadTeam = true;
		return Action;
	}
};

#endif
