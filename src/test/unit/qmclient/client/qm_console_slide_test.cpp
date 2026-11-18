#include <game/client/components/qmclient/console_slide.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	float SpringDampingRatio(const SUiSpringConfig &Spring)
	{
		return Spring.m_Damping / (2.0f * std::sqrt(Spring.m_Stiffness));
	}
}

TEST(QmConsoleSlide, HeightScaleRejectsOvershootAndNegativeValues)
{
	// 面板下沿不能越过展开高度或缩到负高度，否则会盖住/闪回聊天与下层 HUD。
	EXPECT_FLOAT_EQ(QmConsoleSlideHeightScale(1.12f), 1.0f);
	EXPECT_FLOAT_EQ(QmConsoleSlideHeightScale(-0.2f), 0.0f);
	EXPECT_FLOAT_EQ(QmConsoleSlideHeightScale(0.5f), 0.5f);
	EXPECT_FLOAT_EQ(QmConsoleSlideHeightScale(1.0f), 1.0f);
}

TEST(QmConsoleSlide, NonFiniteHeightScaleCollapsesInsteadOfCoveringScreen)
{
	// 坏值不能把整块面板铺满屏幕；按完全收起处理，下一帧由有效弹簧值接管。
	EXPECT_FLOAT_EQ(QmConsoleSlideHeightScale(std::numeric_limits<float>::quiet_NaN()), 0.0f);
	EXPECT_FLOAT_EQ(QmConsoleSlideHeightScale(std::numeric_limits<float>::infinity()), 0.0f);
}

TEST(QmConsoleSlide, EveryFeelLevelUsesCriticalDamping)
{
	for(const bool Opening : {true, false})
	{
		for(int Feel = 0; Feel <= 2; ++Feel)
		{
			const SUiSpringConfig Spring = QmConsoleSlideSpring(Opening, Feel);
			EXPECT_NEAR(SpringDampingRatio(Spring), 1.0f, 0.0001f);
		}
	}
}

TEST(QmConsoleSlide, FeelChangesResponseWithoutReintroducingOvershoot)
{
	const SQmConsoleSlideSpringSpec Soft = QmConsoleSlideSpringSpec(true, 0);
	const SQmConsoleSlideSpringSpec Standard = QmConsoleSlideSpringSpec(true, 1);
	const SQmConsoleSlideSpringSpec Bouncy = QmConsoleSlideSpringSpec(true, 2);
	EXPECT_GT(Soft.m_Response, Standard.m_Response);
	EXPECT_GT(Standard.m_Response, Bouncy.m_Response);
	EXPECT_GE(Soft.m_DampingRatio, 1.0f);
	EXPECT_GE(Standard.m_DampingRatio, 1.0f);
	EXPECT_GE(Bouncy.m_DampingRatio, 1.0f);
}

TEST(QmConsoleSlide, ClosingIsFasterThanOpeningForEveryFeel)
{
	for(int Feel = 0; Feel <= 2; ++Feel)
	{
		const SQmConsoleSlideSpringSpec Open = QmConsoleSlideSpringSpec(true, Feel);
		const SQmConsoleSlideSpringSpec Close = QmConsoleSlideSpringSpec(false, Feel);
		EXPECT_LT(Close.m_Response, Open.m_Response);
		EXPECT_GE(Close.m_DampingRatio, 1.0f);
	}
}

TEST(QmConsoleSlide, SpringHeightStaysMonotonicAndInsideUnitRange)
{
	constexpr float DT = 1.0f / 120.0f;
	for(int Feel = 0; Feel <= 2; ++Feel)
	{
		float Value = 0.0f;
		float Velocity = 0.0f;
		float Previous = 0.0f;
		const SUiSpringConfig Opening = QmConsoleSlideSpring(true, Feel);
		for(int Frame = 0; Frame < 240; ++Frame)
		{
			QmSpringAdvance(Value, Velocity, 1.0f, Opening, DT);
			EXPECT_LE(Value, 1.0f + 0.0001f);
			EXPECT_GE(Value, Previous - 0.00001f);
			Previous = Value;
		}
		EXPECT_NEAR(Value, 1.0f, Opening.m_RestEpsilon * 2.0f);

		Value = 1.0f;
		Velocity = 0.0f;
		Previous = 1.0f;
		const SUiSpringConfig Closing = QmConsoleSlideSpring(false, Feel);
		for(int Frame = 0; Frame < 240; ++Frame)
		{
			QmSpringAdvance(Value, Velocity, 0.0f, Closing, DT);
			EXPECT_GE(Value, -0.0001f);
			EXPECT_LE(Value, Previous + 0.00001f);
			Previous = Value;
		}
		EXPECT_NEAR(Value, 0.0f, Closing.m_RestEpsilon * 2.0f);
	}
}
