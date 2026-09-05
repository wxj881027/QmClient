#include <game/client/components/qmclient/features/auto_team_lock/qm_auto_team_lock_logic.h>

#include <gtest/gtest.h>

TEST(QmAutoTeamLock, SendsOnceAfterJoiningLockableTeam)
{
	CQmAutoTeamLockLogic Lock;
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = true;
	Input.m_Online = true;
	Input.m_LocalPlayerValid = true;
	Input.m_TeamCanBeLocked = true;
	Input.m_Team = 1;
	Input.m_TickSpeed = 50;
	Input.m_DelaySeconds = 5;
	Input.m_CurrentTick = 100;

	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
	Input.m_CurrentTick = 349;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
	Input.m_CurrentTick = 350;
	EXPECT_TRUE(Lock.Update(Input).m_SendLockCommand);
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
}

TEST(QmAutoTeamLock, ReArmsWhenTeamChanges)
{
	CQmAutoTeamLockLogic Lock;
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = true;
	Input.m_Online = true;
	Input.m_LocalPlayerValid = true;
	Input.m_TeamCanBeLocked = true;
	Input.m_TickSpeed = 50;
	Input.m_DelaySeconds = 1;
	Input.m_CurrentTick = 100;

	Input.m_Team = 1;
	Lock.Update(Input);
	Input.m_CurrentTick = 150;
	EXPECT_TRUE(Lock.Update(Input).m_SendLockCommand);
	Input.m_Team = 2;
	Input.m_CurrentTick = 151;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
	Input.m_CurrentTick = 201;
	EXPECT_TRUE(Lock.Update(Input).m_SendLockCommand);
}

TEST(QmAutoTeamLock, DisabledAndOfflineDoNotSend)
{
	CQmAutoTeamLockLogic Lock;
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = false;
	Input.m_Online = true;
	Input.m_LocalPlayerValid = true;
	Input.m_TeamCanBeLocked = true;
	Input.m_Team = 1;
	Input.m_TickSpeed = 50;
	Input.m_DelaySeconds = 0;
	Input.m_CurrentTick = 100;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);

	Input.m_Enabled = true;
	Input.m_Online = false;
	Input.m_CurrentTick = 200;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
}

TEST(QmAutoTeamLock, LeavingTeamCancelsPendingCommand)
{
	CQmAutoTeamLockLogic Lock;
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = true;
	Input.m_Online = true;
	Input.m_LocalPlayerValid = true;
	Input.m_TeamCanBeLocked = true;
	Input.m_Team = 1;
	Input.m_TickSpeed = 50;
	Input.m_DelaySeconds = 5;
	Input.m_CurrentTick = 100;
	Lock.Update(Input);

	Input.m_TeamCanBeLocked = false;
	Input.m_Team = 0;
	Input.m_CurrentTick = 350;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
}
