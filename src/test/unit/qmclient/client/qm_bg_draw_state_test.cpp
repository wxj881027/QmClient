#include <game/client/components/qmclient/bg_draw_state.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	CBgDrawItemDataPoint Point(float X, float Y, float Width = 2.0f)
	{
		return {X, Y, Width, 1.0f, 0.5f, 0.0f, 1.0f};
	}
}

TEST(QmBgDrawStroke, LoadingPreservesBothEndpointsAndSavedWidths)
{
	const CQmBgDrawStroke Stroke(CBgDrawItemData{Point(10, 20, 3), Point(100, -30, 8)});
	ASSERT_EQ(Stroke.Data().size(), 2u);
	EXPECT_FLOAT_EQ(Stroke.Data().back().x, 100);
	EXPECT_FLOAT_EQ(Stroke.Data().back().y, -30);
	EXPECT_FLOAT_EQ(Stroke.Data().back().w, 8);
	EXPECT_EQ(Stroke.Min(), vec2(7, -38));
	EXPECT_EQ(Stroke.Max(), vec2(108, 23));
}

TEST(QmBgDrawStroke, LoadingRetainsIntermediatePoints)
{
	const CQmBgDrawStroke Stroke(CBgDrawItemData{Point(0, 0), Point(1, 1), Point(2, 2)});
	ASSERT_EQ(Stroke.Data().size(), 3u);
	EXPECT_FLOAT_EQ(Stroke.Data()[1].x, 1);
	EXPECT_FLOAT_EQ(Stroke.Data()[2].x, 2);
}

TEST(QmBgDrawStroke, MovingExpandsVisibleBounds)
{
	CQmBgDrawStroke Stroke(Point(10, 20));
	ASSERT_TRUE(Stroke.MoveTo(Point(200, -100, 5)));
	EXPECT_EQ(Stroke.Min(), vec2(8, -105));
	EXPECT_EQ(Stroke.Max(), vec2(205, 22));
}

TEST(QmBgDrawStroke, UnchangedPointDoesNotRequestGeometryRebuild)
{
	CQmBgDrawStroke Stroke(Point(10, 20));
	EXPECT_FALSE(Stroke.MoveTo(Point(10, 20)));
	EXPECT_TRUE(Stroke.MoveTo(Point(10, 20, 4)));
}

TEST(QmBgDrawStroke, PenUpAndLoadedStrokesRejectFurtherDrawing)
{
	CQmBgDrawStroke Stroke(Point(10, 20));
	Stroke.PenUp();
	Stroke.PenUp();
	EXPECT_FALSE(Stroke.MoveTo(Point(100, 200)));
	CQmBgDrawStroke Loaded(CBgDrawItemData{Point(0, 0)});
	EXPECT_FALSE(Loaded.MoveTo(Point(100, 200)));
}

TEST(QmBgDrawStroke, StopsAtPointLimit)
{
	CQmBgDrawStroke Stroke(Point(0, 0));
	for(size_t i = 1; i < CQmBgDrawStroke::MAX_POINTS; ++i)
		ASSERT_TRUE(Stroke.MoveTo(Point(static_cast<float>(i) * 10.0f, 0)));
	EXPECT_FALSE(Stroke.MoveTo(Point(20000, 0)));
	EXPECT_EQ(Stroke.Data().size(), CQmBgDrawStroke::MAX_POINTS);
}

TEST(QmBgDrawStroke, InvalidPointKeepsDataBoundsAndTheAbilityToResume)
{
	for(float Bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Bad);
		for(int Field = 0; Field < 7; ++Field)
		{
			SCOPED_TRACE(Field);
			CQmBgDrawStroke Stroke(Point(10, 20));
			auto Invalid = Point(100, 200);
			float *apFields[]{&Invalid.x, &Invalid.y, &Invalid.w, &Invalid.r, &Invalid.g, &Invalid.b, &Invalid.a};
			*apFields[Field] = Bad;
			EXPECT_FALSE(Stroke.MoveTo(Invalid));
			ASSERT_EQ(Stroke.Data().size(), 1u);
			EXPECT_EQ(Stroke.Data().front().Pos(), vec2(10, 20));
			EXPECT_EQ(Stroke.Min(), vec2(8, 18));
			EXPECT_EQ(Stroke.Max(), vec2(12, 22));
			EXPECT_TRUE(Stroke.MoveTo(Point(100, 200)));
		}
	}
}

TEST(QmBgDrawStroke, InvalidColorDoesNotOverwriteTheLastShortSegment)
{
	CQmBgDrawStroke Stroke(Point(0, 0, 10));
	ASSERT_TRUE(Stroke.MoveTo(Point(1, 0, 10)));
	auto Invalid = Point(2, 1, 10);
	Invalid.r = std::numeric_limits<float>::quiet_NaN();
	EXPECT_FALSE(Stroke.MoveTo(Invalid));
	ASSERT_EQ(Stroke.Data().size(), 2u);
	EXPECT_EQ(Stroke.Data().back().Pos(), vec2(1, 0));
	EXPECT_TRUE(Stroke.MoveTo(Point(2, 1, 10)));
	ASSERT_EQ(Stroke.Data().size(), 2u);
	EXPECT_EQ(Stroke.Data().back().Pos(), vec2(2, 1));
}

TEST(QmBgDrawStroke, FiniteSavedWidthsKeepTheirExistingValues)
{
	const CQmBgDrawStroke Stroke(CBgDrawItemData{Point(10, 20, -4), Point(30, 40, 0)});
	ASSERT_EQ(Stroke.Data().size(), 2u);
	EXPECT_TRUE(QmBgDrawPointIsFinite(Stroke.Data()[0]));
	EXPECT_TRUE(QmBgDrawPointIsFinite(Stroke.Data()[1]));
	EXPECT_FLOAT_EQ(Stroke.Data()[0].w, -4);
	EXPECT_FLOAT_EQ(Stroke.Data()[1].w, 0);
}

TEST(QmBgDrawStore, ZeroCapacityReleasesEveryActiveBorrow)
{
	std::list<int> Items{10, 20};
	std::array<int *, 2> apActive{&Items.front(), &Items.back()};
	EXPECT_TRUE(QmBgDrawMakeSpace(Items, apActive, 0, 1));
	EXPECT_TRUE(Items.empty());
	EXPECT_EQ(apActive[0], nullptr);
	EXPECT_EQ(apActive[1], nullptr);
	EXPECT_FALSE(QmBgDrawMakeSpace(Items, apActive, 0, 1));
}

TEST(QmBgDrawStore, WholeBatchReplacementReleasesEveryActiveBorrow)
{
	std::list<int> Items{10, 20};
	std::array<int *, 2> apActive{&Items.front(), &Items.back()};
	EXPECT_TRUE(QmBgDrawMakeSpace(Items, apActive, 2, 2));
	EXPECT_TRUE(Items.empty());
	EXPECT_EQ(apActive[0], nullptr);
	EXPECT_EQ(apActive[1], nullptr);
}

TEST(QmBgDrawStore, TrimmingPreservesBorrowOfRetainedStroke)
{
	std::list<int> Items{10, 20, 30};
	int *pRetained = &Items.back();
	std::array<int *, 2> apActive{&Items.front(), pRetained};
	EXPECT_TRUE(QmBgDrawMakeSpace(Items, apActive, 2, 1));
	ASSERT_EQ(Items.size(), 1u);
	EXPECT_EQ(Items.front(), 30);
	EXPECT_EQ(apActive[0], nullptr);
	EXPECT_EQ(apActive[1], pRetained);
}
