#include <game/client/components/qmclient/dummy_distance_link.h>

#include <gtest/gtest.h>

namespace
{
	using qm_dummy_link::MakeLink;
	using qm_dummy_link::SEndpoint;

	const SEndpoint MAIN{vec2(0, 0), false};
}

TEST(QmDummyDistanceLink, MissingEitherEndpointHidesLinkUntilBothArePresent)
{
	const SEndpoint Dummy{vec2(100, 0), false};
	EXPECT_FALSE(MakeLink(MAIN, std::nullopt, 380));
	EXPECT_FALSE(MakeLink(std::nullopt, Dummy, 380));
	ASSERT_TRUE(MakeLink(MAIN, Dummy, 380));
	EXPECT_EQ(MakeLink(MAIN, Dummy, 380)->m_Dummy, Dummy.m_Pos);
}

TEST(QmDummyDistanceLink, HookCanCatchTheNearEdgeOutsideCenterRange)
{
	const SEndpoint Dummy{vec2(409, 0), false};
	const auto Inside = MakeLink(MAIN, Dummy, 380);
	const auto Outside = MakeLink(MAIN, Dummy, 379);
	ASSERT_TRUE(Inside);
	ASSERT_TRUE(Outside);
	EXPECT_TRUE(Inside->m_InHookRange);
	EXPECT_FALSE(Outside->m_InHookRange);
	EXPECT_FLOAT_EQ(Inside->m_Alpha, Outside->m_Alpha);
}

TEST(QmDummyDistanceLink, ControlledTeeAimsAtSnapshotPositionButLineJoinsRenderedCenters)
{
	const SEndpoint Dummy{vec2(415, 0), false, vec2(407, 0)};
	const auto FromMain = MakeLink(MAIN, Dummy, 380);
	const auto FromDummy = MakeLink(MAIN, Dummy, 380, true);
	ASSERT_TRUE(FromMain && FromDummy);
	EXPECT_TRUE(FromMain->m_InHookRange);
	EXPECT_FALSE(FromDummy->m_InHookRange);
	EXPECT_EQ(FromMain->LineEnd(), Dummy.m_Pos);
}

TEST(QmDummyDistanceLink, SoloAtEitherEndSuppressesHighlightWithoutHidingLink)
{
	for(const bool MainSolo : {false, true})
	{
		for(const bool DummySolo : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << "main solo=" << MainSolo << ", dummy solo=" << DummySolo);
			const auto Link = MakeLink(SEndpoint{vec2(0, 0), MainSolo}, SEndpoint{vec2(100, 0), DummySolo}, 380);
			ASSERT_TRUE(Link);
			EXPECT_EQ(Link->m_InHookRange, !MainSolo && !DummySolo);
			EXPECT_FLOAT_EQ(Link->m_Distance, 100.0f);
		}
	}
}

TEST(QmDummyDistanceLink, BothTeesFrozenHideLinkUntilEitherWakes)
{
	SEndpoint Main = MAIN;
	SEndpoint Dummy{vec2(100, 0), false};
	for(const bool ControlDummy : {false, true})
	{
		SCOPED_TRACE(ControlDummy);
		Main.m_Frozen = true;
		Dummy.m_Frozen = true;
		EXPECT_FALSE(MakeLink(Main, Dummy, 380, ControlDummy));
		Main.m_Frozen = false;
		EXPECT_TRUE(MakeLink(Main, Dummy, 380, ControlDummy));
		Main.m_Frozen = true;
		Dummy.m_Frozen = false;
		EXPECT_TRUE(MakeLink(Main, Dummy, 380, ControlDummy));
		Main.m_Frozen = false;
		EXPECT_TRUE(MakeLink(Main, Dummy, 380, ControlDummy));
	}
}

TEST(QmDummyDistanceLink, MoreDistantTeesFadeButNeverDisappear)
{
	const auto Near = MakeLink(MAIN, SEndpoint{vec2(12, 0), false}, 380);
	const auto Far = MakeLink(MAIN, SEndpoint{vec2(1800, 0), false}, 380);
	const auto Farther = MakeLink(MAIN, SEndpoint{vec2(65536, 0), false}, 380);
	ASSERT_TRUE(Near && Far && Farther);
	EXPECT_GT(Near->m_Alpha, Far->m_Alpha);
	EXPECT_GT(Far->m_Alpha, Farther->m_Alpha);
	EXPECT_GT(Farther->m_Alpha, 0.0f);
	EXPECT_EQ(Farther->LineEnd(), Farther->m_Dummy);
}

TEST(QmDummyDistanceLink, LineConnectsOpposingTipsAndLengthensAsDropsRecede)
{
	const SEndpoint Main{vec2(42, 10), false};
	const SEndpoint Dummy{vec2(42, -200), false};
	auto Link = MakeLink(Main, Dummy, 380);
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
