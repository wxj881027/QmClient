#include <base/system.h>

#include <game/client/components/countryflags.h>

#include <gtest/gtest.h>

namespace
{
	constexpr int64_t kFreq = 1000;
	constexpr int64_t kNow = 1'000'000;
	constexpr int64_t kGapTicks = (int64_t)(COUNTRY_FLAG_ANIM_REAPPEAR_GAP * kFreq);
	using EPass = EQmCountryFlagRenderPass;

	class CountryFlagEntryLifecycle : public ::testing::Test
	{
	protected:
		CCountryFlags::CCountryFlag m_Flag{};

		SCountryFlagEntryVisual Frame(EPass Pass, int64_t Now, int64_t CustomStart = 0, int MotionLevel = 2)
		{
			return m_Flag.ResolveEntryAnimation(Pass, MotionLevel, CustomStart, Now, kFreq);
		}

		static void ExpectSettled(const SCountryFlagEntryVisual &Visual)
		{
			EXPECT_TRUE(Visual.m_Visible);
			EXPECT_FLOAT_EQ(Visual.m_Scale, 1.0f);
			EXPECT_FLOAT_EQ(Visual.m_Alpha, 1.0f);
		}
	};
}

TEST_F(CountryFlagEntryLifecycle, LongBackgroundRenderingDoesNotReplayOnConsoleClose)
{
	Frame(EPass::INTERACTIVE, kNow);
	for(int64_t Elapsed = 100; Elapsed <= 2000; Elapsed += 100)
		Frame(EPass::BACKGROUND, kNow + Elapsed);
	ExpectSettled(Frame(EPass::INTERACTIVE, kNow + 2010));
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
}

TEST_F(CountryFlagEntryLifecycle, BackgroundPreservesEntranceProgress)
{
	Frame(EPass::INTERACTIVE, kNow);
	const auto Before = Frame(EPass::INTERACTIVE, kNow + 10);
	const auto Background = Frame(EPass::BACKGROUND, kNow + 30);
	const auto Resumed = Frame(EPass::INTERACTIVE, kNow + 30);
	EXPECT_GT(Background.m_Alpha, Before.m_Alpha);
	EXPECT_LT(Background.m_Alpha, 1.0f);
	EXPECT_FLOAT_EQ(Background.m_Scale, Resumed.m_Scale);
	EXPECT_FLOAT_EQ(Background.m_Alpha, Resumed.m_Alpha);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
}

TEST_F(CountryFlagEntryLifecycle, FirstBackgroundDisplayStartsEntrance)
{
	const auto Visual = Frame(EPass::BACKGROUND, kNow);
	EXPECT_TRUE(Visual.m_Visible);
	EXPECT_FLOAT_EQ(Visual.m_Scale, 0.0f);
	EXPECT_FLOAT_EQ(Visual.m_Alpha, 0.0f);
	EXPECT_GT(Frame(EPass::INTERACTIVE, kNow + 20).m_Alpha, 0.0f);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
}

TEST_F(CountryFlagEntryLifecycle, RealAbsenceRestartsAtInclusiveGap)
{
	Frame(EPass::INTERACTIVE, kNow);
	Frame(EPass::BACKGROUND, kNow + 200);
	const auto Visual = Frame(EPass::INTERACTIVE, kNow + 200 + kGapTicks);
	EXPECT_FLOAT_EQ(Visual.m_Scale, 0.0f);
	EXPECT_FLOAT_EQ(Visual.m_Alpha, 0.0f);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow + 200 + kGapTicks);
}

TEST_F(CountryFlagEntryLifecycle, ShortAbsenceDoesNotRestart)
{
	Frame(EPass::INTERACTIVE, kNow);
	ExpectSettled(Frame(EPass::INTERACTIVE, kNow + kGapTicks - 1));
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
}

TEST_F(CountryFlagEntryLifecycle, MeasurementDoesNotInitializeEntrance)
{
	ExpectSettled(Frame(EPass::MEASUREMENT, kNow, kNow + 500));
	EXPECT_EQ(m_Flag.m_LastRenderTimestamp, 0);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, 0);
	EXPECT_FLOAT_EQ(Frame(EPass::INTERACTIVE, kNow + 10).m_Alpha, 0.0f);
}

TEST_F(CountryFlagEntryLifecycle, MeasurementDoesNotCancelActiveEntrance)
{
	Frame(EPass::INTERACTIVE, kNow);
	const auto Before = Frame(EPass::BACKGROUND, kNow + 20);
	ExpectSettled(Frame(EPass::MEASUREMENT, kNow + 25, 0, 0));
	EXPECT_EQ(m_Flag.m_LastRenderTimestamp, kNow + 20);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
	const auto After = Frame(EPass::INTERACTIVE, kNow + 30);
	EXPECT_GT(After.m_Alpha, Before.m_Alpha);
	EXPECT_LT(After.m_Alpha, 1.0f);
}

TEST_F(CountryFlagEntryLifecycle, MeasurementDoesNotKeepAbsentFlagAlive)
{
	Frame(EPass::INTERACTIVE, kNow);
	for(int64_t Elapsed = 100; Elapsed <= 1000; Elapsed += 100)
		ExpectSettled(Frame(EPass::MEASUREMENT, kNow + Elapsed));
	EXPECT_FLOAT_EQ(Frame(EPass::INTERACTIVE, kNow + 1010).m_Alpha, 0.0f);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow + 1010);
}

TEST_F(CountryFlagEntryLifecycle, ZeroMotionCancelsEntranceWithoutReplayOnReenable)
{
	Frame(EPass::INTERACTIVE, kNow);
	ExpectSettled(Frame(EPass::BACKGROUND, kNow + 20, kNow + 1000, 0));
	ExpectSettled(Frame(EPass::INTERACTIVE, kNow + 30));
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, 0);
}

TEST_F(CountryFlagEntryLifecycle, CustomFutureStartDelaysBackgroundAndSurvivesMeasurement)
{
	const int64_t Start = kNow + 100;
	EXPECT_FALSE(Frame(EPass::INTERACTIVE, kNow, Start).m_Visible);
	EXPECT_FALSE(Frame(EPass::BACKGROUND, kNow + 30, Start).m_Visible);
	ExpectSettled(Frame(EPass::MEASUREMENT, kNow + 50, kNow + 500));
	EXPECT_EQ(m_Flag.m_LastRenderTimestamp, kNow + 30);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, 0);
	EXPECT_FALSE(Frame(EPass::BACKGROUND, Start - 1, Start).m_Visible);
	EXPECT_FLOAT_EQ(Frame(EPass::BACKGROUND, Start, Start).m_Alpha, 0.0f);
	EXPECT_GT(Frame(EPass::INTERACTIVE, Start + 20, Start).m_Alpha, 0.0f);
}

TEST_F(CountryFlagEntryLifecycle, CustomToAutomaticRestoresAutomaticProgress)
{
	Frame(EPass::INTERACTIVE, kNow);
	const auto Before = Frame(EPass::INTERACTIVE, kNow + 20);
	EXPECT_FALSE(Frame(EPass::BACKGROUND, kNow + 25, kNow + 1000).m_Visible);
	const auto Automatic = Frame(EPass::INTERACTIVE, kNow + 30);
	EXPECT_TRUE(Automatic.m_Visible);
	EXPECT_GT(Automatic.m_Alpha, Before.m_Alpha);
	EXPECT_GT(Automatic.m_Alpha, 0.0f);
	EXPECT_LT(Automatic.m_Alpha, 1.0f);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
}

TEST_F(CountryFlagEntryLifecycle, CustomOnlyDisplayDoesNotInitializeAutomaticEntrance)
{
	EXPECT_FALSE(Frame(EPass::BACKGROUND, kNow, kNow + 1000).m_Visible);
	ExpectSettled(Frame(EPass::INTERACTIVE, kNow + 20));
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, 0);
}

TEST_F(CountryFlagEntryLifecycle, ExpiredCustomStartDoesNotReplaceAutomaticProgress)
{
	Frame(EPass::INTERACTIVE, kNow);
	ExpectSettled(Frame(EPass::BACKGROUND, kNow + 10, kNow - 1000));
	const auto Automatic = Frame(EPass::INTERACTIVE, kNow + 20);
	EXPECT_GT(Automatic.m_Alpha, 0.0f);
	EXPECT_LT(Automatic.m_Alpha, 1.0f);
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
}

TEST_F(CountryFlagEntryLifecycle, ExplicitCustomStartCanReplayContinuouslyVisibleFlag)
{
	Frame(EPass::INTERACTIVE, kNow);
	Frame(EPass::BACKGROUND, kNow + 200);
	ExpectSettled(Frame(EPass::BACKGROUND, kNow + 300));
	EXPECT_FLOAT_EQ(Frame(EPass::INTERACTIVE, kNow + 310, kNow + 310).m_Alpha, 0.0f);
	EXPECT_GT(Frame(EPass::BACKGROUND, kNow + 330, kNow + 310).m_Alpha, 0.0f);
}

TEST_F(CountryFlagEntryLifecycle, ReducedMotionCompletesEarlierWithoutRestarting)
{
	Frame(EPass::INTERACTIVE, kNow, 0, 1);
	Frame(EPass::BACKGROUND, kNow + 100, 0, 1);
	ExpectSettled(Frame(EPass::BACKGROUND, kNow + 160, 0, 1));
	ExpectSettled(Frame(EPass::INTERACTIVE, kNow + 170, 0, 1));
	EXPECT_EQ(m_Flag.m_AnimStartTimestamp, kNow);
}

TEST_F(CountryFlagEntryLifecycle, NonpositiveFrequencyUsesMinimumTickFrequency)
{
	for(const int64_t Frequency : {int64_t(-1), int64_t(0), int64_t(1)})
	{
		SCOPED_TRACE(Frequency);
		const auto Visual = m_Flag.ResolveEntryAnimation(EPass::BACKGROUND, 2, kNow, kNow, Frequency);
		EXPECT_TRUE(Visual.m_Visible);
		EXPECT_FLOAT_EQ(Visual.m_Scale, 0.0f);
		EXPECT_FLOAT_EQ(Visual.m_Alpha, 0.0f);
		ExpectSettled(m_Flag.ResolveEntryAnimation(EPass::INTERACTIVE, 2, kNow, kNow + 1, Frequency));
	}
}
