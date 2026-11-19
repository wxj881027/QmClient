#include <game/client/components/qmclient/qm_hook_coll_geometry.h>
#include <game/client/components/qmclient/qm_hook_coll_spatial_index.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <new>

namespace
{
	void ExpectEmpty(const SQmHookCollLineScratch &Scratch)
	{
		EXPECT_TRUE(Scratch.m_vLineSegments.empty());
		EXPECT_TRUE(Scratch.m_vLineQuadSegments.empty());
		EXPECT_FALSE(Scratch.m_TipLine);
		EXPECT_FALSE(Scratch.m_TipQuad);
	}
}

TEST(QmHookCollGeometryPreparation, ThinPreparationDropsPreviousWideGeometry)
{
	SQmHookCollLineScratch Scratch;
	Scratch.AppendQuad({vec2(1, 2), vec2(5, 2)}, vec2(0, 1), 1);
	Scratch.m_TipLine = IGraphics::CLineItem(vec2(8, 0), vec2(9, 0));
	Scratch.m_TipQuad = Scratch.m_vLineQuadSegments.front();
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(10, 20), vec2(30, 20));
		return true;
	}));
	ASSERT_EQ(Scratch.m_vLineSegments.size(), 1u);
	EXPECT_FLOAT_EQ(Scratch.m_vLineSegments.front().m_X0, 10);
	EXPECT_TRUE(Scratch.m_vLineQuadSegments.empty());
	EXPECT_FALSE(Scratch.m_TipLine);
	EXPECT_FALSE(Scratch.m_TipQuad);
}

TEST(QmHookCollGeometryPreparation, DeclinedPreparationDropsEveryPartialResult)
{
	SQmHookCollLineScratch Scratch;
	EXPECT_FALSE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(1, 2), vec2(5, 2));
		Scratch.m_TipLine = IGraphics::CLineItem(vec2(8, 0), vec2(9, 0));
		Scratch.PrepareQuads(vec2(0, 1), 1, true);
		return false;
	}));
	ExpectEmpty(Scratch);
}

TEST(QmHookCollGeometryPreparation, ExceptionDropsPartialResultAndNextAttemptCanSucceed)
{
	SQmHookCollLineScratch Scratch;
	EXPECT_FALSE(Scratch.TryPrepare([&]() -> bool {
		Scratch.m_vLineSegments.emplace_back(vec2(1, 2), vec2(5, 2));
		Scratch.m_TipLine = IGraphics::CLineItem(vec2(8, 0), vec2(9, 0));
		throw std::bad_alloc();
	}));
	ExpectEmpty(Scratch);
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(20, 30), vec2(40, 30));
		return true;
	}));
	ASSERT_EQ(Scratch.m_vLineSegments.size(), 1u);
	EXPECT_FLOAT_EQ(Scratch.m_vLineSegments.front().m_X0, 20);
	EXPECT_FALSE(Scratch.m_TipLine);
}

TEST(QmHookCollGeometryPreparation, WideGeometryKeepsMainOrderAndSeparateTip)
{
	SQmHookCollLineScratch Scratch;
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(1, 2), vec2(5, 2));
		Scratch.m_vLineSegments.emplace_back(vec2(20, 3), vec2(24, 3));
		Scratch.m_TipLine = IGraphics::CLineItem(vec2(8, 0), vec2(9, 0));
		Scratch.PrepareQuads(vec2(0, 1), 1, true);
		return true;
	}));
	EXPECT_TRUE(Scratch.m_vLineSegments.empty());
	ASSERT_EQ(Scratch.m_vLineQuadSegments.size(), 2u);
	EXPECT_FLOAT_EQ(Scratch.m_vLineQuadSegments[0].m_X0, 5);
	EXPECT_FLOAT_EQ(Scratch.m_vLineQuadSegments[0].m_Y0, 1);
	EXPECT_FLOAT_EQ(Scratch.m_vLineQuadSegments[1].m_X0, 24);
	ASSERT_TRUE(Scratch.m_TipQuad);
	EXPECT_FLOAT_EQ(Scratch.m_TipQuad->m_X0, 9);
	EXPECT_FLOAT_EQ(Scratch.m_TipQuad->m_Y0, -1);
}

TEST(QmHookCollGeometryPreparation, HiddenTipDoesNotEnterWideDrawData)
{
	SQmHookCollLineScratch Scratch;
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(1, 2), vec2(5, 2));
		Scratch.m_TipLine = IGraphics::CLineItem(vec2(8, 0), vec2(9, 0));
		Scratch.PrepareQuads(vec2(0, 1), 1, false);
		return true;
	}));
	ASSERT_EQ(Scratch.m_vLineQuadSegments.size(), 1u);
	EXPECT_TRUE(Scratch.m_TipLine);
	EXPECT_FALSE(Scratch.m_TipQuad);
}

TEST(QmHookCollGeometryPreparation, ResetClearsVisibleGeometryAndRetainsReusableCapacity)
{
	SQmHookCollLineScratch Scratch;
	Scratch.m_vLineSegments.reserve(2);
	Scratch.AppendQuad({vec2(1, 2), vec2(5, 2)}, vec2(0, 1), 1);
	Scratch.m_TipLine = IGraphics::CLineItem(vec2(8, 0), vec2(9, 0));
	Scratch.m_TipQuad = Scratch.m_vLineQuadSegments.front();
	const auto LineCapacity = Scratch.m_vLineSegments.capacity();
	const auto QuadCapacity = Scratch.m_vLineQuadSegments.capacity();
	Scratch.Reset();
	ExpectEmpty(Scratch);
	EXPECT_EQ(Scratch.m_vLineSegments.capacity(), LineCapacity);
	EXPECT_EQ(Scratch.m_vLineQuadSegments.capacity(), QuadCapacity);
}

TEST(QmHookCollGeometryPreparation, SpatialIndexExcludesNonfiniteCoordinatesBeforeSorting)
{
	std::array<vec2, MAX_CLIENTS> aPositions;
	std::vector<int> vAllowed;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		aPositions[Id] = vec2(Id * 100.0f, 0);
		vAllowed.push_back(Id);
	}
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();
	aPositions[0] = vec2(Nan, 0);
	aPositions[1] = vec2(0, Nan);
	aPositions[2] = vec2(Inf, 0);
	aPositions[3] = vec2(0, -Inf);
	aPositions[4] = vec2(40, 0);
	CQmHookCollSpatialIndex Index;
	const SQmHookCollSegment Segment(vec2(0, 0), vec2(80, 0), 30);
	const auto PositionOf = [&](int Id) { return aPositions[Id]; };
	for(int Query = 0; Query < 40; ++Query)
		Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; });
	const auto &vNearby = Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; });
	EXPECT_EQ(vNearby, (std::vector<int>{4}));
}

TEST(QmHookCollGeometryPreparation, NextFrameCanRestorePreviouslyNonfiniteTarget)
{
	std::array<vec2, MAX_CLIENTS> aPositions;
	std::vector<int> vAllowed;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		aPositions[Id] = vec2(1000 + Id * 100.0f, 0);
		vAllowed.push_back(Id);
	}
	aPositions[0] = vec2(std::numeric_limits<float>::quiet_NaN(), 0);
	CQmHookCollSpatialIndex Index;
	const SQmHookCollSegment Segment(vec2(0, 0), vec2(80, 0), 30);
	const auto PositionOf = [&](int Id) { return aPositions[Id]; };
	for(int Query = 0; Query < 40; ++Query)
		Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; });
	EXPECT_TRUE(Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; }).empty());
	aPositions[0] = vec2(40, 0);
	Index.Reset();
	for(int Query = 0; Query < 40; ++Query)
		Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; });
	EXPECT_EQ(Index.GetCandidates(Segment, vAllowed, PositionOf, [](int) { return true; }), (std::vector<int>{0}));
}
