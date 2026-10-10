#include <game/client/components/qmclient/nameplate_density.h>

#include <gtest/gtest.h>

#include <limits>
#include <set>

namespace
{
	constexpr float GRID = 0.14384104f;
	struct CNameplateDensityTest : testing::Test
	{
		CQmNameplateDensity m_Density;
		int m_Budget = 16;
		bool Update(float Ratio, bool Zooming = false, int Cost = 6, float Pixels = 1.0f)
		{
			return m_Density.Update(Ratio, Pixels, GRID, Zooming, Cost, 16, m_Budget);
		}
	};
}

TEST_F(CNameplateDensityTest, FirstContentAppearsEvenWithExhaustedBudget)
{
	m_Budget = 0;
	ASSERT_TRUE(Update(1.0f, true));
	EXPECT_NEAR(m_Density.Ratio(), 1.1547f, 0.0001f);
	EXPECT_EQ(m_Budget, 0);
	EXPECT_EQ(m_Density.Revision(), 1u);
}

TEST_F(CNameplateDensityTest, AnimationWithinSamplingHeadroomRetainsAllPartsAndStopCommitsOneRevision)
{
	ASSERT_TRUE(Update(1.0f));
	const float Before = m_Density.Ratio();
	for(float Ratio : {1.1f, 0.9f, 0.8f, 1.1f})
		EXPECT_FALSE(Update(Ratio, true));
	EXPECT_EQ(m_Density.Ratio(), Before);
	EXPECT_EQ(m_Density.Revision(), 1u);
	m_Budget = 16;
	ASSERT_TRUE(Update(1.2f));
	EXPECT_EQ(m_Density.Revision(), 2u);
	EXPECT_EQ(m_Budget, 10);
	EXPECT_GT(m_Density.Ratio() / 1.2f, 1.05f);
	EXPECT_LT(m_Density.Ratio() / 1.2f, 1.27f);
	EXPECT_FALSE(Update(1.2f));
}

TEST_F(CNameplateDensityTest, InsufficientBudgetDefersWholeNameplate)
{
	ASSERT_TRUE(Update(1.0f));
	m_Budget = 5;
	EXPECT_FALSE(Update(1.3f));
	EXPECT_EQ(m_Density.Revision(), 1u);
	EXPECT_EQ(m_Budget, 5);
	m_Budget = 6;
	EXPECT_TRUE(Update(1.3f));
	EXPECT_EQ(m_Budget, 0);
	EXPECT_EQ(m_Density.Revision(), 2u);
}

TEST_F(CNameplateDensityTest, LargeNameplateWaitsForWholeFrameWithoutStarvation)
{
	ASSERT_TRUE(Update(1.0f));
	m_Budget = 15;
	EXPECT_FALSE(Update(1.3f, false, 20));
	m_Budget = 16;
	EXPECT_TRUE(Update(1.3f, false, 20));
	EXPECT_EQ(m_Budget, 0);
}

TEST_F(CNameplateDensityTest, SmallDriftAndReverseWithinDeadbandDoNotRebuild)
{
	ASSERT_TRUE(Update(1.0f));
	for(float Ratio : {1.01f, 0.99f, 1.04f, 0.96f, 1.0f})
		EXPECT_FALSE(Update(Ratio));
	EXPECT_EQ(m_Density.Revision(), 1u);
	m_Budget = 16;
	EXPECT_TRUE(Update(0.85f));
	EXPECT_FALSE(Update(0.86f));
}

TEST_F(CNameplateDensityTest, WindowOrDpiChangeRebuildsEvenAtSameZoom)
{
	ASSERT_TRUE(Update(1.0f));
	m_Budget = 16;
	ASSERT_TRUE(Update(1.0f, false, 6, 2.0f));
	EXPECT_EQ(m_Density.Revision(), 2u);
	EXPECT_FALSE(Update(1.0f, false, 6, 2.0f));
}

TEST_F(CNameplateDensityTest, InvalidInputsPreserveStateAndCanRecover)
{
	for(float Value : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		EXPECT_FALSE(Update(Value));
		EXPECT_FALSE(Update(1.0f, false, 6, Value));
	}
	EXPECT_FALSE(Update(1.0f, false, -1));
	EXPECT_EQ(m_Budget, 16);
	EXPECT_EQ(m_Density.Revision(), 0u);
	EXPECT_TRUE(Update(1.0f));
	m_Density.Reset();
	EXPECT_EQ(m_Density.Revision(), 0u);
	EXPECT_EQ(m_Density.Ratio(), 0.0f);
	EXPECT_TRUE(Update(0.9f));
}

TEST_F(CNameplateDensityTest, EmptyOrHiddenPartsDoNotConsumeOrBlockBudget)
{
	m_Budget = 0;
	ASSERT_TRUE(Update(1.0f, false, 0));
	EXPECT_EQ(m_Budget, 0);
	ASSERT_TRUE(Update(1.3f, false, 0));
	EXPECT_EQ(m_Density.Revision(), 2u);
	EXPECT_EQ(m_Budget, 0);
}

TEST_F(CNameplateDensityTest, BoundaryJitterKeepsExistingRasterBucket)
{
	ASSERT_TRUE(Update(1.0f));
	EXPECT_FALSE(Update(1.07f));
	m_Budget = 16;
	ASSERT_TRUE(Update(1.11f));
	const float Selected = m_Density.Ratio();
	for(float Ratio : {1.08f, 1.07f, 1.08f, 1.075f})
		EXPECT_FALSE(Update(Ratio));
	EXPECT_EQ(m_Density.Ratio(), Selected);
}

TEST_F(CNameplateDensityTest, ResizeAndZoomShareTheSamePhysicalRasterBucket)
{
	ASSERT_TRUE(Update(1.3f, false, 6, 1.0f));
	const float Physical = m_Density.Ratio();
	m_Budget = 16;
	ASSERT_TRUE(Update(0.65f, false, 6, 2.0f));
	EXPECT_NEAR(m_Density.Ratio() * 2.0f, Physical, 0.000001f);
}

TEST_F(CNameplateDensityTest, RepeatedTraversalUsesAFiniteSetOfRasterBuckets)
{
	std::set<float> First, Second;
	for(int Round = 0; Round < 2; ++Round)
		for(int Step = 0; Step <= 80; ++Step)
		{
			m_Budget = 16;
			Update(0.5f + Step / 80.0f);
			(Round == 0 ? First : Second).insert(m_Density.Ratio());
		}
	EXPECT_EQ(First, Second);
	EXPECT_LE(First.size(), 9u);
}

TEST(QmNameplateSmallTextSampling, OnlySmallPhysicalTextGetsBoundedExtraSampling)
{
	EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(24.0f, 0.5f), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(24.0f, 1.0f), 1.0f);
	const float Mid = QmNameplateSmallTextSamplingScale(20.0f, 0.5f);
	EXPECT_GT(Mid, 1.0f);
	EXPECT_LT(Mid, 1.25f);
	EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(16.0f, 0.5f), 1.25f);
	EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(1.0f, 0.1f), 1.25f);
}

TEST(QmNameplateSmallTextSampling, ResolutionAndUserFontSizeDeterminePhysicalThreshold)
{
	const float LowResolution = QmNameplateSmallTextSamplingScale(20.0f, 0.5f);
	EXPECT_FLOAT_EQ(LowResolution, QmNameplateSmallTextSamplingScale(10.0f, 1.0f));
	EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(20.0f, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(28.0f, 0.5f), 1.0f);
}

TEST(QmNameplateSmallTextSampling, InvalidInputsLeaveSamplingUnchanged)
{
	for(float Value : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(Value, 1.0f), 1.0f);
		EXPECT_FLOAT_EQ(QmNameplateSmallTextSamplingScale(20.0f, Value), 1.0f);
	}
}

TEST_F(CNameplateDensityTest, SmallTextCompensationRetainsAnimationCacheAndBudgetedRecovery)
{
	ASSERT_TRUE(Update(1.0f));
	const float Original = m_Density.Ratio();
	const float SmallRatio = 0.3f * QmNameplateSmallTextSamplingScale(24.0f, 0.3f);
	EXPECT_FALSE(Update(SmallRatio, true));
	EXPECT_EQ(m_Density.Ratio(), Original);
	m_Budget = 5;
	EXPECT_FALSE(Update(SmallRatio));
	m_Budget = 16;
	ASSERT_TRUE(Update(SmallRatio));
	EXPECT_GT(m_Density.Ratio(), 0.3f);
	EXPECT_EQ(m_Budget, 10);
	EXPECT_FALSE(Update(SmallRatio));
	m_Budget = 16;
	ASSERT_TRUE(Update(1.0f));
	EXPECT_FLOAT_EQ(m_Density.Ratio(), Original);
}

TEST_F(CNameplateDensityTest, ZoomFromTinyRasterRestoresNativeSamplingBeforeAnimationStops)
{
	const float TinyRatio = 0.1f;
	ASSERT_TRUE(Update(TinyRatio * QmNameplateSmallTextSamplingScale(24.0f, TinyRatio)));
	const auto TinyRevision = m_Density.Revision();
	m_Budget = 5;
	EXPECT_FALSE(Update(1.0f, true));
	EXPECT_EQ(m_Density.Revision(), TinyRevision);
	EXPECT_EQ(m_Budget, 5);
	m_Budget = 16;
	ASSERT_TRUE(Update(1.0f, true));
	EXPECT_GE(m_Density.Ratio(), 1.0f);
	EXPECT_LT(m_Density.Ratio(), 1.27f);
	EXPECT_EQ(m_Density.Revision(), TinyRevision + 1);
	EXPECT_EQ(m_Budget, 10);
	EXPECT_FALSE(Update(1.0f, true));
	EXPECT_FALSE(Update(1.0f));
}

TEST_F(CNameplateDensityTest, SamplingFloorOverridesDeadbandWithoutUnboundedOversampling)
{
	ASSERT_TRUE(Update(1.0f));
	const float Before = m_Density.Ratio();
	m_Budget = 16;
	ASSERT_TRUE(Update(Before * 1.001f));
	EXPECT_GE(m_Density.Ratio(), Before * 1.001f);
	EXPECT_LT(m_Density.Ratio(), Before * 1.27f);
	EXPECT_EQ(m_Budget, 10);
}

TEST_F(CNameplateDensityTest, ExtraSmallTextSamplingWaitsDuringAnimationWhenNativeFloorIsSatisfied)
{
	ASSERT_TRUE(Update(1.0f));
	const float Before = m_Density.Ratio();
	m_Budget = 16;
	EXPECT_FALSE(m_Density.Update(1.25f, 1.0f, GRID, true, 6, 16, m_Budget, 1.0f));
	EXPECT_EQ(m_Density.Ratio(), Before);
	EXPECT_EQ(m_Budget, 16);
	ASSERT_TRUE(m_Density.Update(1.25f, 1.0f, GRID, false, 6, 16, m_Budget, 1.0f));
	EXPECT_GE(m_Density.Ratio(), 1.25f);
}

TEST_F(CNameplateDensityTest, DpiIncreaseDuringZoomRestoresPhysicalSamplingWithWholeCardBudget)
{
	ASSERT_TRUE(Update(1.0f));
	m_Budget = 5;
	EXPECT_FALSE(Update(1.0f, true, 6, 2.0f));
	m_Budget = 16;
	ASSERT_TRUE(Update(1.0f, true, 6, 2.0f));
	EXPECT_GE(m_Density.Ratio() * 2.0f, 2.0f);
	EXPECT_EQ(m_Budget, 10);
}

TEST_F(CNameplateDensityTest, RepeatedTinyViewAndNativeViewRecoveryDoesNotAccumulateSampling)
{
	float NativeRaster = 0.0f;
	for(int Cycle = 0; Cycle < 8; ++Cycle)
	{
		m_Budget = 16;
		ASSERT_TRUE(Update(0.1f));
		const auto TinyRevision = m_Density.Revision();
		m_Budget = 0;
		EXPECT_FALSE(Update(1.0f, true));
		EXPECT_EQ(m_Density.Revision(), TinyRevision);
		m_Budget = 16;
		ASSERT_TRUE(Update(1.0f, true));
		EXPECT_EQ(m_Density.Revision(), TinyRevision + 1);
		if(Cycle == 0)
			NativeRaster = m_Density.Ratio();
		EXPECT_FLOAT_EQ(m_Density.Ratio(), NativeRaster);
		EXPECT_GE(m_Density.Ratio(), 1.0f);
		EXPECT_LT(m_Density.Ratio(), 1.27f);
		EXPECT_EQ(m_Budget, 10);
		EXPECT_FALSE(Update(1.0f));
	}
}

TEST_F(CNameplateDensityTest, InvalidSamplingFloorCannotConsumeBudgetOrChangeRevision)
{
	ASSERT_TRUE(Update(1.0f));
	const auto Revision = m_Density.Revision();
	const float Before = m_Density.Ratio();
	m_Budget = 16;
	for(float Minimum : {-1.0f, 2.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		EXPECT_FALSE(m_Density.Update(1.3f, 1.0f, GRID, true, 6, 16, m_Budget, Minimum));
		EXPECT_EQ(m_Density.Revision(), Revision);
		EXPECT_EQ(m_Density.Ratio(), Before);
		EXPECT_EQ(m_Budget, 16);
	}
	EXPECT_TRUE(m_Density.Update(1.3f, 1.0f, GRID, true, 6, 16, m_Budget, 1.3f));
}
