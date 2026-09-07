#include <game/client/components/qmclient/features/auto_team_lock/qm_auto_team_lock_logic.h>
#include <game/client/components/qmclient/features/auto_team_lock/qm_auto_team_lock.h>

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

TEST(QmAutoTeamLock, KeepsDummySlotsSeparate)
{
	CQmAutoTeamLockLogic Lock;
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = true;
	Input.m_Online = true;
	Input.m_LocalPlayerValid = true;
	Input.m_TeamCanBeLocked = true;
	Input.m_Team = 1;
	Input.m_TickSpeed = 50;
	Input.m_DelaySeconds = 1;
	Input.m_CurrentTick = 100;

	Input.m_Dummy = 0;
	Lock.Update(Input);
	Input.m_CurrentTick = 150;
	EXPECT_TRUE(Lock.Update(Input).m_SendLockCommand);

	Input.m_Dummy = 1;
	Input.m_CurrentTick = 151;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
	Input.m_CurrentTick = 201;
	EXPECT_TRUE(Lock.Update(Input).m_SendLockCommand);

	Input.m_Dummy = 0;
	Input.m_CurrentTick = 202;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);
}

TEST(QmAutoTeamLock, DisabledSendsAgainOnlyAfterTeamChange)
{
	CQmAutoTeamLockLogic Lock;
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = true;
	Input.m_Online = true;
	Input.m_LocalPlayerValid = true;
	Input.m_TeamCanBeLocked = true;
	Input.m_Team = 1;
	Input.m_TickSpeed = 50;
	Input.m_DelaySeconds = 1;
	Input.m_CurrentTick = 100;
	Lock.Update(Input);

	Input.m_Enabled = false;
	Lock.Update(Input);
	Input.m_Enabled = true;
	Input.m_CurrentTick = 150;
	EXPECT_FALSE(Lock.Update(Input).m_SendLockCommand);

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

TEST(QmAutoTeamLock, DisablingFeatureClearsPendingState)
{
	CQmAutoTeamLock Feature;
	SQmAutoTeamLockInput Input;
	Input.m_Enabled = true;
	Input.m_Online = true;
	Input.m_LocalPlayerValid = true;
	Input.m_TeamCanBeLocked = true;
	Input.m_Team = 1;
	Input.m_TickSpeed = 50;
	Input.m_DelaySeconds = 5;
	Input.m_CurrentTick = 100;
	Feature.UpdateModel(true, true);
	Feature.Update(Input);

	Feature.UpdateModel(false, true);
	Feature.UpdateModel(true, true);
	Input.m_CurrentTick = 350;
	EXPECT_FALSE(Feature.Update(Input).m_SendLockCommand);
}

TEST(QmAutoTeamLock, UnavailableFeatureCannotEnterEnabledDispatch)
{
	CQmAutoTeamLock Feature;
	for(const bool ConfigEnabled : {false, true})
	{
		Feature.UpdateModel(ConfigEnabled, false);
		EXPECT_FALSE(Feature.Model().m_Enabled);
		EXPECT_FALSE(Feature.Model().m_Available);
		Feature.UpdateModel(ConfigEnabled, true);
		EXPECT_EQ(Feature.Model().m_Enabled, ConfigEnabled);
		EXPECT_TRUE(Feature.Model().m_Available);
	}
}
