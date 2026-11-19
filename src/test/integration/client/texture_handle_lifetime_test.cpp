#include <engine/client/texture_slot_pool.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

namespace
{
	class CHandleFactory : public CIconBenchmarkGraphics
	{
	public:
		using IGraphics::CreateTextureHandle;
	};
}

TEST(TextureHandleLifetime, ActualHandleKeepsFullGenerationAcrossTheOldReuseCycle)
{
	CTextureSlotPool<> Pool;
	CHandleFactory Factory;
	ASSERT_TRUE(Pool.Reset(3));
	ASSERT_EQ(Pool.Allocate(), 0);
	ASSERT_EQ(Pool.Allocate(), 1);
	const int Id = Pool.Allocate();
	ASSERT_EQ(Id, 2);
	const auto Old = Factory.CreateTextureHandle(Id, Pool.Generation(Id));
	for(int i = 0; i < 65536; ++i)
	{
		Pool.Release(Id, Pool.Generation(Id));
		ASSERT_EQ(Pool.Allocate(), Id);
	}
	const auto Current = Factory.CreateTextureHandle(Id, Pool.Generation(Id));
	EXPECT_GT(Current.Generation(), 0xffffU);
	EXPECT_FALSE(Pool.IsAllocated(Old.Id(), Old.Generation()));
	EXPECT_TRUE(Pool.IsAllocated(Current.Id(), Current.Generation()));
}

TEST(TextureHandleLifetime, OldReservationCannotReleaseAHandleCreatedAfterDeviceReset)
{
	CTextureSlotPool<> Pool;
	CHandleFactory Factory;
	ASSERT_TRUE(Pool.Reset(3));
	ASSERT_EQ(Pool.Allocate(), 0);
	ASSERT_EQ(Pool.Allocate(), 1);
	IGraphics::CTextureHandle Current;
	{
		auto Old = Pool.Reserve<1>();
		ASSERT_TRUE(Old.IsValid());
		ASSERT_EQ(Old.Id(), 2);
		ASSERT_TRUE(Pool.Reset(3));
		ASSERT_EQ(Pool.Allocate(), 0);
		ASSERT_EQ(Pool.Allocate(), 1);
		const int Id = Pool.Allocate();
		ASSERT_EQ(Id, 2);
		Current = Factory.CreateTextureHandle(Id, Pool.Generation(Id));
	}
	EXPECT_TRUE(Pool.IsAllocated(Current.Id(), Current.Generation()));
	EXPECT_NE(Pool.Allocate(), Current.Id());
}

TEST(TextureHandleLifetime, CommittedPairSurvivesDuplicateAndStaleReleaseOfTheOtherHandle)
{
	CTextureSlotPool<> Pool;
	CHandleFactory Factory;
	ASSERT_TRUE(Pool.Reset(4));
	ASSERT_EQ(Pool.Allocate(), 0);
	ASSERT_EQ(Pool.Allocate(), 1);
	IGraphics::CTextureHandle First, Second;
	{
		auto Pair = Pool.Reserve<2>();
		ASSERT_TRUE(Pair.IsValid());
		First = Factory.CreateTextureHandle(Pair.Id(0), Pair.Generation(0));
		Second = Factory.CreateTextureHandle(Pair.Id(1), Pair.Generation(1));
		Pair.Commit();
	}
	Pool.Release(First.Id(), First.Generation());
	Pool.Release(First.Id(), First.Generation());
	const int Reused = Pool.Allocate();
	ASSERT_EQ(Reused, First.Id());
	const auto New = Factory.CreateTextureHandle(Reused, Pool.Generation(Reused));
	Pool.Release(First.Id(), First.Generation());
	EXPECT_TRUE(Pool.IsAllocated(New.Id(), New.Generation()));
	EXPECT_TRUE(Pool.IsAllocated(Second.Id(), Second.Generation()));
}

TEST(TextureHandleLifetime, HandleFactoryDefaultGenerationDoesNotOwnALiveTexture)
{
	CTextureSlotPool<> Pool;
	CHandleFactory Factory;
	ASSERT_TRUE(Pool.Reset(3));
	ASSERT_EQ(Pool.Allocate(), 0);
	ASSERT_EQ(Pool.Allocate(), 1);
	const int Id = Pool.Allocate();
	ASSERT_EQ(Id, 2);
	const auto WithoutTag = Factory.CreateTextureHandle(Id);
	ASSERT_TRUE(WithoutTag.IsValid());
	EXPECT_FALSE(Pool.IsAllocated(WithoutTag.Id(), WithoutTag.Generation()));
	EXPECT_TRUE(Pool.IsAllocated(Id, Pool.Generation(Id)));
}
