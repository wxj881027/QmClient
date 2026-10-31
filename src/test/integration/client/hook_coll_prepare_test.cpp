#include <game/client/components/qmclient/qm_hook_coll_candidates.h>
#include <game/client/components/qmclient/qm_hook_coll_geometry.h>
#include <game/client/components/qmclient/qm_hook_coll_spatial_index.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>

namespace
{
	struct SScenario
	{
		CQmHookCollCandidates m_Candidates;
		CQmHookCollSpatialIndex m_Index;
		SQmHookCollLineScratch m_Geometry;
		std::array<vec2, MAX_CLIENTS> m_aPositions;
		std::array<bool, MAX_CLIENTS> m_aEligible{};
		int m_PositionSamples = 0;
		SQmHookCollSegment m_Segment{vec2(0, 0), vec2(80, 0), 30};

		SScenario()
		{
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				m_aPositions[Id] = vec2(Id * 10.0f, 0);
				m_aEligible[Id] = Id > 0 && Id <= 3;
			}
		}

		const std::vector<int> &Allowed(int Own = 0)
		{
			return m_Candidates.Get(Own, [&](int Id) { return m_aEligible[Id]; });
		}

		bool Prepare(int Own = 0)
		{
			return m_Geometry.TryPrepare([&] {
				const auto &vNearby = m_Index.GetCandidates(m_Segment, Allowed(Own), [&](int Id) {
					++m_PositionSamples;
					return m_aPositions[Id]; }, [&](int Id) { return m_aEligible[Id]; });
				vec2 Hit;
				if(QmIntersectHookCollTargets(m_Segment, Hit, vNearby, [&](int Id) { return m_aPositions[Id]; }, 30) == -1)
					return false;
				// 合成一条绘制计划；资格、空间筛选、求交和几何提交均使用生产模块。
				m_Geometry.m_vLineSegments.emplace_back(m_Segment.m_Start, Hit);
				m_Geometry.m_TipLine = IGraphics::CLineItem(Hit, Hit + vec2(4, 0));
				m_Geometry.PrepareQuads(vec2(0, 1), 1, true);
				return true;
			});
		}

		void WarmEmptySpatialQuery()
		{
			m_aEligible.fill(true);
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
				m_aPositions[Id] = vec2(Id * 100.0f, 0);
			const auto &vAllowed = Allowed();
			const SQmHookCollSegment Empty(vec2(-500, 0), vec2(-400, 0), 30);
			for(int Query = 0; Query < 40; ++Query)
				m_Index.GetCandidates(Empty, vAllowed, [&](int Id) {
					++m_PositionSamples;
					return m_aPositions[Id]; }, [](int) { return true; });
			m_Segment = SQmHookCollSegment(vec2(100, 0), vec2(120, 0), 30);
		}
	};

	struct SFailureResult
	{
		bool m_Accepted = false;
		bool m_Triggered = false;
	};

	SFailureResult FailPreparation(SScenario &Scenario, std::size_t BeforeFailure = 0)
	{
		SFailureResult Result;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			Result.m_Accepted = Scenario.Prepare();
			Result.m_Triggered = Failure.Triggered();
		}
		return Result;
	}

	void ExpectNoGeometry(const SScenario &Scenario)
	{
		EXPECT_TRUE(Scenario.m_Geometry.m_vLineSegments.empty());
		EXPECT_TRUE(Scenario.m_Geometry.m_vLineQuadSegments.empty());
		EXPECT_FALSE(Scenario.m_Geometry.m_TipLine);
		EXPECT_FALSE(Scenario.m_Geometry.m_TipQuad);
	}
}

TEST(HookCollPrepareIntegration, EveryPreparationAllocationFailureDropsPrefixAndCanRetry)
{
	bool ReachedSuccess = false;
	for(int FailureIndex = 0; FailureIndex < MAX_CLIENTS + 8; ++FailureIndex)
	{
		SCOPED_TRACE(FailureIndex);
		SScenario Scenario;
		const auto Result = FailPreparation(Scenario, FailureIndex);
		if(!Result.m_Triggered)
		{
			ASSERT_TRUE(Result.m_Accepted);
			ReachedSuccess = true;
			break;
		}
		EXPECT_FALSE(Result.m_Accepted);
		ExpectNoGeometry(Scenario);
		ASSERT_TRUE(Scenario.Prepare());
		EXPECT_EQ(Scenario.Allowed(), (std::vector<int>{1, 2, 3}));
		ASSERT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.size(), 1u);
		EXPECT_FLOAT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.front().m_X0, 10);
		ASSERT_TRUE(Scenario.m_Geometry.m_TipQuad);
		EXPECT_FLOAT_EQ(Scenario.m_Geometry.m_TipQuad->m_X0, 14);
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST(HookCollPrepareIntegration, NearbyAllocationFailureKeepsPreparedSpatialIndexForRetry)
{
	SScenario Scenario;
	Scenario.WarmEmptySpatialQuery();
	const int Samples = Scenario.m_PositionSamples;
	ASSERT_EQ(Samples, MAX_CLIENTS);
	const auto Result = FailPreparation(Scenario);
	ASSERT_TRUE(Result.m_Triggered);
	EXPECT_FALSE(Result.m_Accepted);
	ExpectNoGeometry(Scenario);
	ASSERT_TRUE(Scenario.Prepare());
	EXPECT_EQ(Scenario.m_PositionSamples, Samples);
	ASSERT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.size(), 1u);
	EXPECT_FLOAT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.front().m_X0, 100);
}

TEST(HookCollPrepareIntegration, QuadAllocationFailureKeepsPublishedEligibilityAndRetriesGeometry)
{
	SScenario Scenario;
	Scenario.m_aEligible.fill(false);
	Scenario.m_aEligible[1] = true;
	ASSERT_EQ(Scenario.Allowed(), (std::vector<int>{1}));
	Scenario.m_Geometry.m_vLineSegments.reserve(1);
	const auto Result = FailPreparation(Scenario);
	ASSERT_TRUE(Result.m_Triggered);
	EXPECT_FALSE(Result.m_Accepted);
	ExpectNoGeometry(Scenario);
	EXPECT_EQ(Scenario.Allowed(), (std::vector<int>{1}));
	ASSERT_TRUE(Scenario.Prepare());
	ASSERT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.size(), 1u);
	EXPECT_FLOAT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.front().m_X0, 10);
	EXPECT_TRUE(Scenario.m_Geometry.m_TipQuad);
}

TEST(HookCollPrepareIntegration, OneOwnersFailureDoesNotBlockAnotherOwnerOrLaterRetry)
{
	SScenario Scenario;
	const auto Result = FailPreparation(Scenario);
	ASSERT_TRUE(Result.m_Triggered);
	ExpectNoGeometry(Scenario);
	ASSERT_TRUE(Scenario.Prepare(1));
	ASSERT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.size(), 1u);
	EXPECT_FLOAT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.front().m_X0, 20);
	ASSERT_TRUE(Scenario.Prepare(0));
	ASSERT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.size(), 1u);
	EXPECT_FLOAT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.front().m_X0, 10);
}

TEST(HookCollPrepareIntegration, NextFrameResetAfterFailureUsesNewPositionsAndEligibility)
{
	SScenario Scenario;
	Scenario.WarmEmptySpatialQuery();
	ASSERT_TRUE(FailPreparation(Scenario).m_Triggered);
	Scenario.m_aEligible[1] = false;
	Scenario.m_aPositions[2] = vec2(110, 0);
	Scenario.m_Candidates.Reset();
	Scenario.m_Index.Reset();
	ASSERT_TRUE(Scenario.Prepare());
	ASSERT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.size(), 1u);
	EXPECT_FLOAT_EQ(Scenario.m_Geometry.m_vLineQuadSegments.front().m_X0, 110);
}
