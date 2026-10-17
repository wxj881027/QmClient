#include <game/client/components/qmclient/browser_map_preview.h>
#include <game/client/components/qmclient/browser_map_preview_loader.h>

#include <gtest/gtest.h>

#include <array>

TEST(QmBrowserMapPreviewView, StartFocusUsesReadableViewAndSurvivesViewportResize)
{
	CQmBrowserMapPreviewView View;
	View.Configure({32000, 18000}, {1500, 844});
	View.FocusStart({160, 320});
	EXPECT_EQ(View.Center(), vec2(160, 320));
	EXPECT_FLOAT_EQ(View.Zoom(), 1.0f);
	View.Configure({32000, 18000}, {1600, 900});
	EXPECT_EQ(View.Center(), vec2(160, 320));
	EXPECT_LT(View.ViewSize().x, 2000);
}

TEST(QmBrowserMapPreviewStart, NeutralSpawnTakesPriorityOverEarlierStartLineAndTeamSpawn)
{
	std::array<CTile, 8> aTiles{};
	aTiles[0].m_Index = TILE_START;
	aTiles[1].m_Index = ENTITY_OFFSET + ENTITY_SPAWN_RED;
	aTiles[6].m_Index = ENTITY_OFFSET + ENTITY_SPAWN;
	ASSERT_TRUE(QmBrowserMapPreviewFindStart(aTiles, 4, 2));
	EXPECT_EQ(*QmBrowserMapPreviewFindStart(aTiles, 4, 2), vec2(80, 48));
}

TEST(QmBrowserMapPreviewStart, StartLineIsUsedWhenThereAreNoSpawnTiles)
{
	std::array<CTile, 4> aTiles{};
	aTiles[3].m_Index = TILE_START;
	EXPECT_EQ(*QmBrowserMapPreviewFindStart(aTiles, 2, 2), vec2(48, 48));
}

TEST(QmBrowserMapPreviewStart, MissingOrTruncatedTilesDoNotInventAStart)
{
	std::array<CTile, 4> aTiles{};
	EXPECT_FALSE(QmBrowserMapPreviewFindStart(aTiles, 2, 2));
	aTiles[0].m_Index = ENTITY_OFFSET + ENTITY_SPAWN;
	EXPECT_FALSE(QmBrowserMapPreviewFindStart(aTiles, 3, 2));
	EXPECT_FALSE(QmBrowserMapPreviewFindStart(aTiles, 0, 2));
}

TEST(QmBrowserMapPreviewView, OpeningFitsBothDimensionsOfWideAndTallMaps)
{
	for(const vec2 World : {vec2(32000, 640), vec2(640, 32000), vec2(640, 640)})
	{
		SCOPED_TRACE(::testing::Message() << World.x << "x" << World.y);
		CQmBrowserMapPreviewView View;
		View.Configure(World, {1500, 844});
		EXPECT_GE(View.ViewSize().x + 0.01f, World.x);
		EXPECT_GE(View.ViewSize().y + 0.01f, World.y);
		EXPECT_EQ(View.Center(), World * 0.5f);
		EXPECT_FLOAT_EQ(View.ZoomFactor(), 1.0f);
	}
}

TEST(QmBrowserMapPreviewView, ZoomKeepsTheWorldPointUnderThePointer)
{
	CQmBrowserMapPreviewView View;
	View.Configure({32000, 18000}, {1500, 844});
	View.ZoomAt(8, {0.5f, 0.5f});
	const vec2 Anchor(0.75f, 0.3f);
	const vec2 Before = View.Center() + View.ViewSize() * (Anchor - vec2(0.5f, 0.5f));
	View.ZoomAt(1, Anchor);
	const vec2 After = View.Center() + View.ViewSize() * (Anchor - vec2(0.5f, 0.5f));
	EXPECT_NEAR(Before.x, After.x, 0.01f);
	EXPECT_NEAR(Before.y, After.y, 0.01f);
}

TEST(QmBrowserMapPreviewView, WholeMapCanBeDraggedBeforeZooming)
{
	CQmBrowserMapPreviewView View;
	View.Configure({32000, 18000}, {1500, 844});
	const vec2 Before = View.Center();
	View.BeginDrag({400, 200});
	View.DragTo({450, 220}, {800, 450});
	EXPECT_LT(View.Center().x, Before.x);
	EXPECT_LT(View.Center().y, Before.y);
	EXPECT_FLOAT_EQ(View.ZoomFactor(), 1.0f);
}

TEST(QmBrowserMapPreviewView, LargeMapsCanZoomBeyondEightTimesToSeeIndividualTiles)
{
	CQmBrowserMapPreviewView View;
	View.Configure({320000, 180000}, {1500, 844});
	for(int i = 0; i < 80; ++i)
		View.ZoomAt(1, {0.5f, 0.5f});
	EXPECT_GT(View.ZoomFactor(), 64.0f);
	EXPECT_LE(View.ViewSize().x, 400.0f);
	EXPECT_GE(View.Zoom(), 0.25f);
}

TEST(QmBrowserMapPreviewView, DragMovesTheMapWithThePointerAndReverseAtBoundaryRespondsImmediately)
{
	CQmBrowserMapPreviewView View;
	View.Configure({32000, 18000}, {1500, 844});
	View.ZoomAt(8, {0.5f, 0.5f});
	const vec2 Before = View.Center();
	View.BeginDrag({400, 200});
	View.DragTo({450, 220}, {800, 450});
	EXPECT_LT(View.Center().x, Before.x);
	EXPECT_LT(View.Center().y, Before.y);
	View.DragTo({100000, 220}, {800, 450});
	const vec2 AtEdge = View.Center();
	View.DragTo({99999, 220}, {800, 450});
	EXPECT_GT(View.Center().x, AtEdge.x);
}

TEST(QmBrowserMapPreviewView, ZoomingBackOutRestoresTheWholeMapAndCenter)
{
	CQmBrowserMapPreviewView View;
	View.Configure({32000, 18000}, {1500, 844});
	View.ZoomAt(8, {0.5f, 0.5f});
	View.BeginDrag({0, 0});
	View.DragTo({400, 250}, {800, 450});
	View.ZoomAt(-32, {0.1f, 0.9f});
	EXPECT_FLOAT_EQ(View.ZoomFactor(), 1.0f);
	EXPECT_EQ(View.Center(), vec2(16000, 9000));
}

TEST(QmBrowserMapPreviewView, ResizingPreservesZoomButChangingMapsResetsInteraction)
{
	CQmBrowserMapPreviewView View;
	View.Configure({32000, 18000}, {1500, 844});
	View.ZoomAt(8, {0.5f, 0.5f});
	const float Factor = View.ZoomFactor();
	View.Configure({32000, 18000}, {1000, 1000});
	EXPECT_FLOAT_EQ(View.ZoomFactor(), Factor);
	View.BeginDrag({0, 0});
	View.Configure({640, 640}, {1000, 1000});
	EXPECT_FALSE(View.Dragging());
	EXPECT_FLOAT_EQ(View.ZoomFactor(), 1.0f);
	EXPECT_EQ(View.Center(), vec2(320, 320));
}

TEST(QmBrowserMapPreviewView, DragAfterReleaseAndEmptyViewportLeaveCameraUnchanged)
{
	CQmBrowserMapPreviewView View;
	View.Configure({32000, 18000}, {1500, 844});
	View.ZoomAt(8, {0.5f, 0.5f});
	const vec2 Before = View.Center();
	View.BeginDrag({0, 0});
	View.DragTo({400, 200}, {0, 0});
	EXPECT_EQ(View.Center(), Before);
	View.EndDrag();
	View.DragTo({400, 200}, {800, 450});
	EXPECT_EQ(View.Center(), Before);
}
