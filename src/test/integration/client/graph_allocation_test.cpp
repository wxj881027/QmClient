#include <engine/client/graph.h>

#include <gtest/gtest.h>
#include <test/support/ringbuffer_allocation_probe.h>

#include <limits>

TEST(GraphAllocation, FailedBufferRejectsRecordsAndAllowsResetAndScale)
{
	CRingBufferAllocationProbe Probe;
	Probe.m_Fail = true;
	CGraph Graph(128, 2, true, Probe.Allocate);
	Graph.Init(-150.0f, 150.0f);
	EXPECT_FALSE(Graph.InsertAt(100, 10.0f));
	EXPECT_FALSE(Graph.Add(20.0f));
	Graph.Scale(100);
	Graph.Init(0.0f, 120.0f);
	Graph.Scale(100);
	EXPECT_FALSE(Graph.InsertAt(200, 30.0f));
	EXPECT_EQ(Probe.m_Requests, 3);
}

TEST(GraphAllocation, FailedBufferDoesNotDisableAnotherGraph)
{
	CRingBufferAllocationProbe Probe;
	Probe.m_Fail = true;
	CGraph Failed(128, 2, true, Probe.Allocate);
	CGraph Healthy(128, 2, true);
	Failed.Init(-150.0f, 150.0f);
	Healthy.Init(-150.0f, 150.0f);
	EXPECT_FALSE(Failed.InsertAt(100, 10.0f));
	EXPECT_TRUE(Healthy.InsertAt(100, 10.0f));
	EXPECT_TRUE(Healthy.InsertAt(125, -20.0f));
	Healthy.Scale(100);
	EXPECT_TRUE(Healthy.Add(30.0f));
	EXPECT_EQ(Probe.m_Requests, 2);
}

TEST(GraphAllocation, ZeroEntriesDoesNotRequestStorage)
{
	CRingBufferAllocationProbe Probe;
	CGraph Graph(0, 0, false, Probe.Allocate);
	Graph.Init(0.0f, 120.0f);
	EXPECT_FALSE(Graph.InsertAt(100, 10.0f));
	Graph.Scale(100);
	EXPECT_EQ(Probe.m_Requests, 0);
}

TEST(GraphAllocation, NegativeEntriesDoesNotRequestStorage)
{
	CRingBufferAllocationProbe Probe;
	CGraph Graph(-1, 0, false, Probe.Allocate);
	Graph.Init(0.0f, 120.0f);
	EXPECT_FALSE(Graph.InsertAt(100, 10.0f));
	Graph.Scale(100);
	EXPECT_EQ(Probe.m_Requests, 0);
}

TEST(GraphAllocation, UnrepresentableByteCapacityDoesNotRequestStorage)
{
	CRingBufferAllocationProbe Probe;
	CGraph Graph(std::numeric_limits<int>::max(), 0, false, Probe.Allocate);
	Graph.Init(0.0f, 120.0f);
	EXPECT_FALSE(Graph.InsertAt(100, 10.0f));
	Graph.Scale(100);
	EXPECT_EQ(Probe.m_Requests, 0);
}

TEST(GraphAllocation, SmallBufferKeepsAcceptingAndScalingRecycledRecords)
{
	CGraph Graph(1, 0, false);
	Graph.Init(0.0f, 120.0f);
	for(int Time = 1; Time <= 100; ++Time)
	{
		EXPECT_TRUE(Graph.InsertAt(Time, static_cast<float>(Time)));
		Graph.Scale(100);
	}
}

TEST(GraphAllocation, LaterInitializationRecoversStorageAndThenReusesIt)
{
	CRingBufferAllocationProbe Probe;
	Probe.m_Fail = true;
	CGraph Graph(128, 2, true, Probe.Allocate);
	EXPECT_FALSE(Graph.InsertAt(100, 10.0f));
	Probe.m_Fail = false;
	Graph.Init(-150.0f, 150.0f);
	EXPECT_TRUE(Graph.InsertAt(100, 10.0f));
	EXPECT_TRUE(Graph.InsertAt(125, -20.0f));
	Graph.Scale(100);
	EXPECT_EQ(Probe.m_Requests, 2);
	Graph.Init(0.0f, 120.0f);
	EXPECT_TRUE(Graph.InsertAt(200, 30.0f));
	Graph.Scale(100);
	EXPECT_EQ(Probe.m_Requests, 2);
}
