#include <game/client/components/qmclient/connection_resource_preparation.h>

#include <gtest/gtest.h>

// 生产连接状态机约束两组资源、单次放行、断开取消及下一次连接；不读取源码证明接线。
TEST(ConnectionResourcePreparation, WaitsForBothLayerSets)
{
	CConnectionResourcePreparation Preparation;
	Preparation.Begin();
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, false, false));
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, true, false));
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, false, true));
	EXPECT_TRUE(Preparation.Pending());
	EXPECT_TRUE(Preparation.CompleteWhenReady(true, true, true));
}

TEST(ConnectionResourcePreparation, ReadyResourcesReleaseConnectionOnlyOnce)
{
	CConnectionResourcePreparation Preparation;
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, true, true));
	Preparation.Begin();
	EXPECT_TRUE(Preparation.CompleteWhenReady(true, true, true));
	EXPECT_FALSE(Preparation.Pending());
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, true, true));
}

TEST(ConnectionResourcePreparation, DisconnectCancelsPendingRelease)
{
	CConnectionResourcePreparation Preparation;
	Preparation.Begin();
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, true, false));
	Preparation.Cancel();
	Preparation.Cancel();
	EXPECT_FALSE(Preparation.Pending());
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, true, true));
}

TEST(ConnectionResourcePreparation, NewConnectionCanCompleteAfterCancelOrSuccess)
{
	CConnectionResourcePreparation Preparation;
	Preparation.Begin();
	Preparation.Cancel();
	Preparation.Begin();
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, false, true));
	EXPECT_TRUE(Preparation.CompleteWhenReady(true, true, true));
	Preparation.Begin();
	EXPECT_TRUE(Preparation.CompleteWhenReady(true, true, true));
}

TEST(ConnectionResourcePreparation, ReadyLayersStillWaitForMapData)
{
	CConnectionResourcePreparation Preparation;
	Preparation.Begin();
	EXPECT_FALSE(Preparation.CompleteWhenReady(false, true, true));
	EXPECT_TRUE(Preparation.Pending());
	EXPECT_TRUE(Preparation.CompleteWhenReady(true, true, true));
	EXPECT_FALSE(Preparation.CompleteWhenReady(true, true, true));
}
