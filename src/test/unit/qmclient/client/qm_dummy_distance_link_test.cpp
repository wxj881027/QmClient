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

TEST(QmDummyDistanceLink, HookLengthBoundaryChangesOnlyTheHighlight)
{
	const SEndpoint Dummy{vec2(-30, 40), false};
	const auto Inside = MakeLink(MAIN, Dummy, 50);
	const auto Outside = MakeLink(MAIN, Dummy, 49);
	ASSERT_TRUE(Inside);
	ASSERT_TRUE(Outside);
	EXPECT_TRUE(Inside->m_InHookRange);
	EXPECT_FALSE(Outside->m_InHookRange);
	EXPECT_FLOAT_EQ(Inside->m_Alpha, Outside->m_Alpha);
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
			EXPECT_TRUE(Link->LineStart(false));
			EXPECT_TRUE(Link->LineStart(true));
		}
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
	EXPECT_TRUE(Farther->LineStart(false));
}

TEST(QmDummyDistanceLink, DropletLineStartsAtTipFacingDummy)
{
	const SEndpoint Main{vec2(42, 10), false};
	const auto Link = MakeLink(Main, SEndpoint{vec2(42, -200), false}, 380);
	ASSERT_TRUE(Link);
	EXPECT_EQ(Link->LineStart(false), Main.m_Pos);
	ASSERT_TRUE(Link->LineStart(true));
	EXPECT_EQ(*Link->LineStart(true), vec2(42, 10 - qm_dummy_link::TIP_REACH));
}

TEST(QmDummyDistanceLink, DummyInsideHammerReachKeepsDirectionWithoutExtension)
{
	const auto Link = MakeLink(MAIN, SEndpoint{vec2(0, 20), false}, 380);
	ASSERT_TRUE(Link);
	EXPECT_EQ(Link->m_Direction, vec2(0, 1));
	EXPECT_FALSE(Link->LineStart(true));
	EXPECT_TRUE(Link->LineStart(false));
}

TEST(QmDummyDistanceLink, CoincidentTeesHaveNoDirectionOrExtension)
{
	const auto Link = MakeLink(MAIN, MAIN, 380);
	ASSERT_TRUE(Link);
	EXPECT_EQ(Link->m_Direction, vec2(0, 0));
	EXPECT_FALSE(Link->LineStart(true));
	EXPECT_FALSE(Link->LineStart(false));
}
