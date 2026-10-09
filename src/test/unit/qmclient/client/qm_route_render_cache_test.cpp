#include <game/client/components/qmclient/route_render_cache.h>

#include <gtest/gtest.h>

TEST(QmRouteRenderCache, UnchangedRouteIsBuiltOnceAcrossFrames)
{
	CQmRouteRenderCache Cache;
	CQmRouteRenderCache::SKey Key{7, 4, 2, 0, true, false};
	int Builds = 0;
	const auto Build = [&](std::vector<vec2> &vPoints) {
		++Builds;
		vPoints = {vec2(16.0f, 16.0f), vec2(48.0f, 16.0f)};
		return true;
	};
	for(int Frame = 0; Frame < 128; ++Frame)
		ASSERT_TRUE(Cache.Update(Key, Build));
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(Cache.Point(0), vec2(16.0f, 16.0f));
}

class QmRouteCacheInvalidation : public ::testing::TestWithParam<int>
{
};

TEST_P(QmRouteCacheInvalidation, ChangedRouteStateReplacesPreviousPoints)
{
	CQmRouteRenderCache Cache;
	CQmRouteRenderCache::SKey Key{7, 4, 2, 0, true, false};
	int Builds = 0;
	const auto Build = [&](std::vector<vec2> &vPoints) {
		vPoints.push_back(vec2(static_cast<float>(++Builds), 0.0f));
		return true;
	};
	ASSERT_TRUE(Cache.Update(Key, Build));
	switch(GetParam())
	{
	case 0: ++Key.m_Revision; break;
	case 1: ++Key.m_StartIndex; break;
	case 2: ++Key.m_ClientId; break;
	case 3: ++Key.m_Dummy; break;
	case 4: Key.m_DDrace = false; break;
	case 5: Key.m_Segment = true; break;
	}
	ASSERT_TRUE(Cache.Update(Key, Build));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Cache.Point(0), vec2(2.0f, 0.0f));
}

INSTANTIATE_TEST_SUITE_P(RouteDependencies, QmRouteCacheInvalidation, ::testing::Range(0, 6), ([](const ::testing::TestParamInfo<int> &Info) {
	const char *apNames[] = {"FieldRevision", "StartTile", "Client", "Dummy", "MapMode", "Segment"};
	return apNames[Info.param];
}));

TEST(QmRouteRenderCache, ResetRebuildsEvenWhenNewMapReusesTheSameKey)
{
	CQmRouteRenderCache Cache;
	CQmRouteRenderCache::SKey Key;
	int Builds = 0;
	const auto Build = [&](std::vector<vec2> &vPoints) {
		vPoints.push_back(vec2(static_cast<float>(++Builds), 0.0f));
		return true;
	};
	ASSERT_TRUE(Cache.Update(Key, Build));
	Cache.Invalidate();
	EXPECT_TRUE(Cache.QueryVisible({-10.0f, -10.0f, 10.0f, 10.0f}).empty());
	ASSERT_TRUE(Cache.Update(Key, Build));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Cache.Point(0), vec2(2.0f, 0.0f));
}

TEST(QmRouteRenderCache, FailedBuildHidesPartialPointsAndNewRevisionCanRecover)
{
	CQmRouteRenderCache Cache;
	CQmRouteRenderCache::SKey Key;
	int Builds = 0;
	const auto Build = [&](std::vector<vec2> &vPoints) {
		vPoints.push_back(vec2(0.0f, 0.0f));
		return ++Builds > 1;
	};
	EXPECT_FALSE(Cache.Update(Key, Build));
	EXPECT_TRUE(Cache.QueryVisible({-10.0f, -10.0f, 10.0f, 10.0f}).empty());
	EXPECT_FALSE(Cache.Update(Key, Build));
	EXPECT_EQ(Builds, 1);
	++Key.m_Revision;
	ASSERT_TRUE(Cache.Update(Key, Build));
	EXPECT_EQ(Cache.QueryVisible({-10.0f, -10.0f, 10.0f, 10.0f}), (std::vector<size_t>{0}));
}

TEST(QmRouteRenderCache, ViewIncludesBoundaryPointsInOriginalRouteOrder)
{
	CQmRouteRenderCache Cache;
	ASSERT_TRUE(Cache.Update({}, [](std::vector<vec2> &vPoints) {
		vPoints = {vec2(-48.0f, -48.0f), vec2(100000.0f, 0.0f), vec2(48.0f, 48.0f), vec2(-49.0f, 0.0f), vec2(0.0f, 0.0f)};
		return true;
	}));
	EXPECT_EQ(Cache.QueryVisible({-48.0f, -48.0f, 48.0f, 48.0f}), (std::vector<size_t>{0, 2, 4}));
}

TEST(QmRouteRenderCache, NarrowViewSkipsMostOfALongRouteAndIdenticalViewDoesNoWork)
{
	CQmRouteRenderCache Cache;
	ASSERT_TRUE(Cache.Update({}, [](std::vector<vec2> &vPoints) {
		for(int Index = 0; Index < 16384; ++Index)
			vPoints.emplace_back(Index * 32.0f, 0.0f);
		return true;
	}));
	const CQmRouteRenderCache::SView View{8192 * 32.0f, -1.0f, 8195 * 32.0f, 1.0f};
	size_t TestedPoints = 0;
	EXPECT_EQ(Cache.QueryVisible(View, &TestedPoints), (std::vector<size_t>{8192, 8193, 8194, 8195}));
	EXPECT_GT(TestedPoints, 0u);
	EXPECT_LE(TestedPoints, 128u);
	EXPECT_EQ(Cache.QueryVisible(View, &TestedPoints).size(), 4u);
	EXPECT_EQ(TestedPoints, 0u);
}

TEST(QmRouteRenderCache, PanningAndZoomingChangeVisiblePointsWithoutRebuildingRoute)
{
	CQmRouteRenderCache Cache;
	int Builds = 0;
	ASSERT_TRUE(Cache.Update({}, [&](std::vector<vec2> &vPoints) {
		++Builds;
		vPoints = {vec2(-64.0f, 0.0f), vec2(0.0f, 0.0f), vec2(64.0f, 0.0f)};
		return true;
	}));
	EXPECT_EQ(Cache.QueryVisible({-1.0f, -1.0f, 1.0f, 1.0f}), (std::vector<size_t>{1}));
	EXPECT_EQ(Cache.QueryVisible({63.0f, -1.0f, 65.0f, 1.0f}), (std::vector<size_t>{2}));
	EXPECT_EQ(Cache.QueryVisible({-64.0f, -1.0f, 64.0f, 1.0f}), (std::vector<size_t>{0, 1, 2}));
	EXPECT_EQ(Builds, 1);
}

TEST(QmRouteRenderCache, ReplacedRouteRefreshesAnUnchangedViewport)
{
	CQmRouteRenderCache Cache;
	CQmRouteRenderCache::SKey Key;
	const CQmRouteRenderCache::SView View{-1.0f, -1.0f, 1.0f, 1.0f};
	ASSERT_TRUE(Cache.Update(Key, [](std::vector<vec2> &vPoints) {
		vPoints = {vec2(0.0f, 0.0f)};
		return true;
	}));
	ASSERT_EQ(Cache.QueryVisible(View).size(), 1u);
	++Key.m_Revision;
	ASSERT_TRUE(Cache.Update(Key, [](std::vector<vec2> &vPoints) {
		vPoints = {vec2(1024.0f, 0.0f)};
		return true;
	}));
	EXPECT_TRUE(Cache.QueryVisible(View).empty());
}
