#include <game/client/components/tclient/pet.h>

#include <gtest/gtest.h>

TEST(QmPetFrame, CompletePreparationPrecedesDrawingAndRetainsTheFollowedClient)
{
	int Followed = 7;
	bool Prepared = false, Drawn = false;
	EXPECT_TRUE(QmTClientPetRenderFrame(Followed, [&] { Prepared = true; }, [&] {
		EXPECT_TRUE(Prepared);
		Drawn = true; }));
	EXPECT_TRUE(Drawn);
	EXPECT_EQ(Followed, 7);
}

TEST(QmPetFrame, PreparationAllocationFailureHidesThePetAndDoesNotDraw)
{
	int Followed = 7;
	bool Drawn = false;
	EXPECT_FALSE(QmTClientPetRenderFrame(Followed, [] { throw std::bad_alloc(); }, [&] { Drawn = true; }));
	EXPECT_EQ(Followed, -1);
	EXPECT_FALSE(Drawn);
}

TEST(QmPetFrame, PreparationLengthFailureHidesThePetAndDoesNotDraw)
{
	int Followed = 7;
	bool Drawn = false;
	EXPECT_FALSE(QmTClientPetRenderFrame(Followed, [] { throw std::length_error("skin preparation"); }, [&] { Drawn = true; }));
	EXPECT_EQ(Followed, -1);
	EXPECT_FALSE(Drawn);
}

TEST(QmPetFrame, ASubsequentFrameCanDrawForTheSelectedClientAfterPreparationFailure)
{
	int Followed = 7;
	ASSERT_FALSE(QmTClientPetRenderFrame(Followed, [] { throw std::bad_alloc(); }, [] {}));
	ASSERT_EQ(Followed, -1);
	// 组件每帧重新选择当前本地角色，不把一次准备失败缓存为永久失败。
	Followed = 8;
	bool Drawn = false;
	EXPECT_TRUE(QmTClientPetRenderFrame(Followed, [] {}, [&] { Drawn = true; }));
	EXPECT_EQ(Followed, 8);
	EXPECT_TRUE(Drawn);
}

TEST(QmPetFrame, DrawingAllocationFailureStillPropagates)
{
	int Followed = 7;
	EXPECT_THROW(QmTClientPetRenderFrame(Followed, [] {}, [] { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_EQ(Followed, 7);
}

TEST(QmPetFrame, UnexpectedPreparationFailureStillPropagates)
{
	int Followed = 7;
	bool Drawn = false;
	EXPECT_THROW(QmTClientPetRenderFrame(Followed, [] { throw std::logic_error("unrelated failure"); }, [&] { Drawn = true; }), std::logic_error);
	EXPECT_EQ(Followed, 7);
	EXPECT_FALSE(Drawn);
}

TEST(QmPetSpring, RestingAtTheTargetDoesNotMove)
{
	vec2 Position(100, 200), Velocity(0, 0);
	QmTClientPetAdvanceSpring(Position, Velocity, vec2(100, 200), 0.016f);
	EXPECT_EQ(Position, vec2(100, 200));
	EXPECT_EQ(Velocity, vec2(0, 0));
}

TEST(QmPetSpring, ZeroElapsedTimeRetainsPositionAndVelocity)
{
	vec2 Position(100, 200), Velocity(3, -4);
	QmTClientPetAdvanceSpring(Position, Velocity, vec2(-20, 5), 0);
	EXPECT_EQ(Position, vec2(100, 200));
	EXPECT_EQ(Velocity, vec2(3, -4));
}
