#include <game/client/components/qmclient/dummy_distance_link.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	using qm_dummy_link::MakeLink;
	using qm_dummy_link::SEndpoint;

	const SEndpoint MAIN{vec2(0, 0), false};
}

TEST(QmDummyDistanceLink, FallbackSnapshotAllowsZeroOrATickWithinThreeSeconds)
{
	EXPECT_TRUE(qm_dummy_link::CanUseFallbackSnapshot(0, 500, 50));
	EXPECT_TRUE(qm_dummy_link::CanUseFallbackSnapshot(500, 500, 50));
	EXPECT_TRUE(qm_dummy_link::CanUseFallbackSnapshot(350, 500, 50));
	EXPECT_FALSE(qm_dummy_link::CanUseFallbackSnapshot(349, 500, 50));
	EXPECT_FALSE(qm_dummy_link::CanUseFallbackSnapshot(501, 500, 50));
}

TEST(QmDummyDistanceLink, FallbackSnapshotRejectsAgeOutsideSignedTickRange)
{
	EXPECT_FALSE(qm_dummy_link::CanUseFallbackSnapshot(std::numeric_limits<int>::min(), 500, 50));
}

TEST(QmDummyDistanceLink, MissingEitherEndpointHidesLinkUntilBothArePresent)
{
	const SEndpoint Dummy{vec2(100, 0), false};
	EXPECT_FALSE(MakeLink(MAIN, std::nullopt));
	EXPECT_FALSE(MakeLink(std::nullopt, Dummy));
	ASSERT_TRUE(MakeLink(MAIN, Dummy));
	EXPECT_EQ(MakeLink(MAIN, Dummy)->m_Dummy, Dummy.m_Pos);
}

TEST(QmDummyDistanceLink, BothTeesFrozenHideLinkUntilEitherWakes)
{
	SEndpoint Main = MAIN;
	SEndpoint Dummy{vec2(100, 0), false};
	Main.m_Frozen = true;
	Dummy.m_Frozen = true;
	EXPECT_FALSE(MakeLink(Main, Dummy));
	Main.m_Frozen = false;
	EXPECT_TRUE(MakeLink(Main, Dummy));
	Main.m_Frozen = true;
	Dummy.m_Frozen = false;
	EXPECT_TRUE(MakeLink(Main, Dummy));
	Main.m_Frozen = false;
	EXPECT_TRUE(MakeLink(Main, Dummy));
}

TEST(QmDummyDistanceLink, OpacityStaysConstantThroughFifteenTiles)
{
	const auto Near = MakeLink(MAIN, SEndpoint{vec2(32.0f, 0.0f), false});
	const auto AtFadeStart = MakeLink(MAIN, SEndpoint{vec2(288.0f, 384.0f), false});
	ASSERT_TRUE(Near && AtFadeStart);
	EXPECT_FLOAT_EQ(Near->m_Alpha, 0.9f);
	EXPECT_FLOAT_EQ(AtFadeStart->m_Alpha, Near->m_Alpha);
}

TEST(QmDummyDistanceLink, OpacityFadesLinearlyAfterFifteenTiles)
{
	const auto Midway = MakeLink(MAIN, SEndpoint{vec2(720.0f, 0.0f), false});
	const auto BeforeFadeEnd = MakeLink(MAIN, SEndpoint{vec2(959.0f, 0.0f), false});
	ASSERT_TRUE(Midway && BeforeFadeEnd);
	EXPECT_FLOAT_EQ(Midway->m_Alpha, 0.45f);
	EXPECT_GT(BeforeFadeEnd->m_Alpha, 0.0f);
	EXPECT_LT(BeforeFadeEnd->m_Alpha, Midway->m_Alpha);
}

TEST(QmDummyDistanceLink, LinkIsHiddenAtThirtyTilesAndBeyond)
{
	EXPECT_FALSE(MakeLink(MAIN, SEndpoint{vec2(960.0f, 0.0f), false}));
	EXPECT_FALSE(MakeLink(MAIN, SEndpoint{vec2(961.0f, 0.0f), false}));
}

TEST(QmDummyDistanceLink, LineConnectsOpposingTipsAndLengthensAsDropsRecede)
{
	const SEndpoint Main{vec2(42, 10), false};
	const SEndpoint Dummy{vec2(42, -200), false};
	auto Link = MakeLink(Main, Dummy);
	ASSERT_TRUE(Link);
	qm_dummy_link::CDropletMotion Motion;
	Link->m_Shape = Motion.Advance(Link->m_Distance, 1.0f, 0);
	const vec2 Start = Link->LineStart(), End = Link->LineEnd();
	EXPECT_LT(Start.y, Main.m_Pos.y);
	EXPECT_GT(End.y, Dummy.m_Pos.y);
	EXPECT_GT(Start.y, End.y);
	EXPECT_FLOAT_EQ(Main.m_Pos.y - Start.y, End.y - Dummy.m_Pos.y);
	Link->m_Shape = Motion.Advance(0.0f, 1.0f, 0);
	EXPECT_EQ(Link->LineStart(), Main.m_Pos);
	EXPECT_EQ(Link->LineEnd(), Dummy.m_Pos);
}

TEST(QmDummyDistanceLink, DistantDropsHaveLongerNarrowerNoses)
{
	qm_dummy_link::CDropletMotion Motion;
	const auto Near = Motion.Advance(110.0f, 0.0f, 0);
	const auto Far = Motion.Advance(700.0f, 0.0f, 0);
	EXPECT_GT(Far.Offset(vec2(1, 0)).x, Near.Offset(vec2(1, 0)).x);
	EXPECT_LT(Far.Offset(vec2(0.8f, 0.6f)).y, Near.Offset(vec2(0.8f, 0.6f)).y);
}

TEST(QmDummyDistanceLink, ReversingMidRetractionResumesFromTheVisibleSize)
{
	qm_dummy_link::CDropletMotion Motion;
	const auto Open = Motion.Advance(100.0f, 1.0f, 2);
	const auto Closing = Motion.Advance(60.0f, 0.1f, 2);
	const auto Reversed = Motion.Advance(100.0f, 0.0f, 2);
	EXPECT_GT(Closing.m_Radius, 0.0f);
	EXPECT_LT(Closing.m_Radius, Open.m_Radius);
	EXPECT_FLOAT_EQ(Closing.m_Radius, Reversed.m_Radius);
	EXPECT_GT(Motion.Advance(250.0f, 0.7f, 2).m_Radius, Closing.m_Radius);
}

TEST(QmDummyDistanceLink, GrowFromCenterOnlyAfterLeavingHammerReach)
{
	qm_dummy_link::CDropletMotion Motion;
	EXPECT_FLOAT_EQ(Motion.Advance(qm_dummy_link::HAMMER_REACH, 1.0f, 2).m_Radius, 0.0f);
	const float First = Motion.Advance(200.0f, 0.016f, 2).m_Radius;
	EXPECT_GT(First, 0.0f);
	EXPECT_LT(First, Motion.Advance(200.0f, 0.2f, 2).m_Radius);
	Motion.Advance(50.0f, 2.0f, 2);
	EXPECT_FLOAT_EQ(Motion.Advance(50.0f, 0.016f, 2).m_Radius, 0.0f);
	EXPECT_FLOAT_EQ(Motion.Advance(0.0f, 0.016f, 2).m_Radius, 0.0f);
}
