#include <game/client/components/qmclient/skin7_resource_lifecycle.h>

#include <gtest/gtest.h>

namespace
{
	using EAction = CQmSkin7ResourceLifecycle::EAction;
}

TEST(Skin7ResourceLifecycle, NormalStartupAndAllSixupFalseStatesDoNotLoad)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	// 原回归触发点：启动进入服务器列表就调用 Refresh，随后后台完成时同步扫盘。
	for(int State = IClient::STATE_OFFLINE; State <= IClient::STATE_RESTARTING; ++State)
	{
		SCOPED_TRACE(State);
		EXPECT_FALSE(QmNeedsSkin7Resources(State, false));
		EXPECT_EQ(Lifecycle.Update(State, false), EAction::NONE);
		EXPECT_FALSE(Lifecycle.BeginRefresh(State, false));
	}
}

TEST(Skin7ResourceLifecycle, OfflineServerListDoesNotLoadEvenWithAStaleSixupFlag)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	// 列表包含 0.7 服务器、或连接结束时 Sixup 尚未清除，都不是资源加载授权。
	for(const int State : {IClient::STATE_OFFLINE, IClient::STATE_QUITTING, IClient::STATE_RESTARTING})
	{
		SCOPED_TRACE(State);
		EXPECT_EQ(Lifecycle.Update(State, true), EAction::NONE);
		EXPECT_FALSE(Lifecycle.BeginRefresh(State, true));
	}
}

TEST(Skin7ResourceLifecycle, ActualSixupConnectionStartsOnceAcrossLoadingAndOnlineFrames)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_CONNECTING, true), EAction::LOAD);
	for(const int State : {IClient::STATE_CONNECTING, IClient::STATE_LOADING, IClient::STATE_ONLINE, IClient::STATE_ONLINE})
		EXPECT_EQ(Lifecycle.Update(State, true), EAction::NONE);
}

TEST(Skin7ResourceLifecycle, LeavingDuringLoadCancelsOnceAndReentryStartsANewLoad)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	ASSERT_EQ(Lifecycle.Update(IClient::STATE_CONNECTING, true), EAction::LOAD);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_OFFLINE, true), EAction::CANCEL);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_OFFLINE, false), EAction::NONE);
	// 退出后迟到的完成通知不能把已取消会话变为 READY。
	EXPECT_FALSE(Lifecycle.CompleteLoad());
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_CONNECTING, true), EAction::LOAD);
	EXPECT_TRUE(Lifecycle.CompleteLoad());
}

TEST(Skin7ResourceLifecycle, SwitchingToSixupFalseCancelsPendingResources)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	ASSERT_EQ(Lifecycle.Update(IClient::STATE_ONLINE, true), EAction::LOAD);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_CONNECTING, false), EAction::CANCEL);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_ONLINE, false), EAction::NONE);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_ONLINE, true), EAction::LOAD);
}

TEST(Skin7ResourceLifecycle, ReadyResourcesRemainCachedWithoutOfflineWorkOrRepeatedLoads)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	ASSERT_EQ(Lifecycle.Update(IClient::STATE_CONNECTING, true), EAction::LOAD);
	ASSERT_TRUE(Lifecycle.CompleteLoad());
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_OFFLINE, false), EAction::NONE);
	EXPECT_FALSE(Lifecycle.BeginRefresh(IClient::STATE_OFFLINE, false));
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_ONLINE, false), EAction::NONE);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_CONNECTING, true), EAction::NONE);
}

TEST(Skin7ResourceLifecycle, ExplicitRefreshIsAllowedOnlyInsideSixupContext)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	ASSERT_TRUE(Lifecycle.BeginRefresh(IClient::STATE_ONLINE, true));
	ASSERT_TRUE(Lifecycle.CompleteLoad());
	ASSERT_TRUE(Lifecycle.BeginRefresh(IClient::STATE_ONLINE, true));
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_OFFLINE, false), EAction::CANCEL);
}

TEST(Skin7ResourceLifecycle, SixupDemoLoadsResourcesToPreservePlaybackCompatibility)
{
	CQmSkin7ResourceLifecycle Lifecycle;
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_DEMOPLAYBACK, true), EAction::LOAD);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_DEMOPLAYBACK, true), EAction::NONE);
	EXPECT_EQ(Lifecycle.Update(IClient::STATE_OFFLINE, false), EAction::CANCEL);
}
