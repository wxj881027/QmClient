#include <base/system.h>

#include <game/client/components/countryflags.h>

#include <gtest/gtest.h>

namespace
{
	constexpr int64_t kFreq = 1000; // 1 tick = 1ms，便于按阈值换算
	constexpr int64_t kGapTicks = (int64_t)(COUNTRY_FLAG_ANIM_REAPPEAR_GAP * kFreq);
	constexpr int64_t kNow = 1'000'000;
	constexpr int64_t kLoaded = kNow - 100'000;
} // namespace

TEST(CountryFlagAnimStartTime, DisabledAnimReturnsZero)
{
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(false, false, 0, kLoaded, 0, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), 0);
}

TEST(CountryFlagAnimStartTime, MeasurePassReturnsZero)
{
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, true, 0, kLoaded, 0, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), 0);
}

TEST(CountryFlagAnimStartTime, CustomStartTimeWins)
{
	const int64_t Custom = kNow - 5;
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, false, Custom, kLoaded, 0, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), Custom);
}

TEST(CountryFlagAnimStartTime, FirstRenderStartsFromNow)
{
	// 从未真实渲染过（首次显示）：动画从当前时间起播，而不是纹理加载时刻。
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, false, 0, kLoaded, 0, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), kNow);
}

TEST(CountryFlagAnimStartTime, ContinuouslyVisibleKeepsLoadedTime)
{
	// 持续可见（距上次渲染远小于阈值）：不重放，维持加载时刻（Elapsed 已超时长，表现为静止）。
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, false, 0, kLoaded, kNow - 1, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), kLoaded);
}

TEST(CountryFlagAnimStartTime, ReappearAfterGapRestartsFromNow)
{
	// 超过阈值后再次渲染：从当前时间重放入场动画。
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, false, 0, kLoaded, kNow - kGapTicks - 1, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), kNow);
}

TEST(CountryFlagAnimStartTime, GapThresholdIsInclusive)
{
	// 恰好达到阈值也算重新出现。
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, false, 0, kLoaded, kNow - kGapTicks, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), kNow);
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, false, 0, kLoaded, kNow - kGapTicks + 1, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), kLoaded);
}

TEST(CountryFlagAnimStartTime, FutureLastRenderDoesNotRestart)
{
	// 上次渲染时刻异常在未来（gap 为负）：不重放。
	ASSERT_EQ(ComputeCountryFlagAnimStartTime(true, false, 0, kLoaded, kNow + 100, kNow, COUNTRY_FLAG_ANIM_REAPPEAR_GAP, kFreq), kLoaded);
}
