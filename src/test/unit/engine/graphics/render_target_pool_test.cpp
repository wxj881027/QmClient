#include <engine/client/render_target_pool.h>

#include <gtest/gtest.h>

#include <vector>

TEST(RenderTargetPool, DeferredDestructionKeepsSlotReservedUntilPassEnds)
{
	CRenderTargetPool Pool;
	const int Target = Pool.Allocate(32, 16);
	std::vector<int> vDestroyed;
	const auto Destroy = [&](int Id) { vDestroyed.push_back(Id); };
	Pool.Release(Target, true, Destroy);
	EXPECT_FALSE(Pool.IsLive(Target));
	EXPECT_EQ(Pool.Size(Target), ivec2(0, 0));
	EXPECT_TRUE(vDestroyed.empty());
	const int Replacement = Pool.Allocate(64, 32);
	EXPECT_NE(Replacement, Target);
	Pool.FlushDeferred(Destroy);
	EXPECT_EQ(vDestroyed, (std::vector<int>{Target}));
	EXPECT_EQ(Pool.Allocate(128, 64), Target);
	EXPECT_EQ(Pool.Size(Replacement), ivec2(64, 32));
}

TEST(RenderTargetPool, SampleCountSurvivesAllocationAndResetsOnReuse)
{
	CRenderTargetPool Pool;
	const int Multisampled = Pool.Allocate(64, 32, 4);
	ASSERT_TRUE(Pool.IsLive(Multisampled));
	EXPECT_EQ(Pool.SampleCount(Multisampled), 4);
	EXPECT_EQ(Pool.Size(Multisampled), ivec2(64, 32));
	Pool.Release(Multisampled, false, [](int) {});
	EXPECT_EQ(Pool.SampleCount(Multisampled), 1);
	const int Reused = Pool.Allocate(16, 16);
	EXPECT_EQ(Reused, Multisampled);
	EXPECT_EQ(Pool.SampleCount(Reused), 1);
}

TEST(RenderTargetPool, DuplicateReleaseCannotQueueTwoDestructions)
{
	CRenderTargetPool Pool;
	const int Target = Pool.Allocate(16, 16);
	int DestroyCount = 0;
	const auto Destroy = [&](int) { ++DestroyCount; };
	Pool.Release(Target, true, Destroy);
	Pool.Release(Target, true, Destroy);
	Pool.Release(Target, false, Destroy);
	Pool.FlushDeferred(Destroy);
	Pool.FlushDeferred(Destroy);
	EXPECT_EQ(DestroyCount, 1);
	EXPECT_EQ(Pool.Allocate(32, 32), Target);
	EXPECT_NE(Pool.Allocate(32, 32), Target);
}

TEST(RenderTargetPool, ImmediateReleaseQueuesDestructionBeforeSlotReuse)
{
	CRenderTargetPool Pool;
	const int Target = Pool.Allocate(16, 16);
	int DuringDestruction = -1;
	Pool.Release(Target, false, [&](int Id) {
		EXPECT_EQ(Id, Target);
		DuringDestruction = Pool.Allocate(32, 32);
		EXPECT_NE(DuringDestruction, Target);
	});
	EXPECT_EQ(Pool.Allocate(16, 32), Target);
	EXPECT_TRUE(Pool.IsLive(DuringDestruction));
}

TEST(RenderTargetPool, MultipleDeferredSlotsSurvivePoolGrowth)
{
	CRenderTargetPool Pool;
	const int First = Pool.Allocate(8, 8);
	const int Second = Pool.Allocate(16, 16);
	std::vector<int> vDestroyed;
	const auto Destroy = [&](int Id) { vDestroyed.push_back(Id); };
	Pool.Release(First, true, Destroy);
	Pool.Release(Second, true, Destroy);
	for(int i = 0; i < 100; ++i)
	{
		const int Id = Pool.Allocate(i + 1, i + 2);
		EXPECT_NE(Id, First);
		EXPECT_NE(Id, Second);
		EXPECT_EQ(Pool.Size(Id), ivec2(i + 1, i + 2));
	}
	Pool.FlushDeferred(Destroy);
	EXPECT_EQ(vDestroyed, (std::vector<int>{First, Second}));
}

TEST(RenderTargetPool, InvalidRequestsDoNotConsumeSlotsOrDestroyResources)
{
	CRenderTargetPool Pool;
	EXPECT_EQ(Pool.Allocate(0, 1), -1);
	EXPECT_EQ(Pool.Allocate(1, -1), -1);
	EXPECT_FALSE(Pool.IsLive(-1));
	EXPECT_FALSE(Pool.IsLive(0));
	EXPECT_EQ(Pool.Size(100), ivec2(0, 0));
	Pool.Release(-1, false, [](int) { ADD_FAILURE(); });
	Pool.Release(100, true, [](int) { ADD_FAILURE(); });
	Pool.FlushDeferred([](int) { ADD_FAILURE(); });
	EXPECT_EQ(Pool.Allocate(1, 1), 0);
}

TEST(RenderTargetPool, ResetDiscardsDestructionsBelongingToOldDevice)
{
	CRenderTargetPool Pool;
	const int Old = Pool.Allocate(16, 16);
	Pool.Release(Old, true, [](int) { ADD_FAILURE(); });
	Pool.Clear();
	const int Current = Pool.Allocate(32, 32);
	EXPECT_EQ(Current, Old);
	Pool.FlushDeferred([](int) { ADD_FAILURE(); });
	EXPECT_EQ(Pool.Size(Current), ivec2(32, 32));
}
