#include <game/client/components/qmclient/qm_sidebar_holiday.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmSidebarHoliday, SpringFestivalIncludesEveAndEndsAfterTheSeventhDay)
{
	EXPECT_EQ(QmSidebarHoliday(2026, 2, 15), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2026, 2, 16), EQmSidebarHoliday::SPRING_FESTIVAL);
	EXPECT_EQ(QmSidebarHoliday(2026, 2, 17), EQmSidebarHoliday::SPRING_FESTIVAL);
	EXPECT_EQ(QmSidebarHoliday(2026, 2, 23), EQmSidebarHoliday::SPRING_FESTIVAL);
	EXPECT_EQ(QmSidebarHoliday(2026, 2, 24), EQmSidebarHoliday::NONE);
}

TEST(QmSidebarHoliday, LunarDatesChangeWithTheYear)
{
	EXPECT_EQ(QmSidebarHoliday(2027, 2, 6), EQmSidebarHoliday::SPRING_FESTIVAL);
	EXPECT_EQ(QmSidebarHoliday(2027, 2, 17), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2026, 6, 19), EQmSidebarHoliday::DRAGON_BOAT);
	EXPECT_EQ(QmSidebarHoliday(2027, 6, 9), EQmSidebarHoliday::DRAGON_BOAT);
	EXPECT_EQ(QmSidebarHoliday(2026, 9, 25), EQmSidebarHoliday::MID_AUTUMN);
	EXPECT_EQ(QmSidebarHoliday(2027, 9, 15), EQmSidebarHoliday::MID_AUTUMN);
}

TEST(QmSidebarHoliday, LeapMonthDoesNotRepeatDragonBoatFestival)
{
	// 2028 年闰五月；只在正常五月初五触发。
	EXPECT_EQ(QmSidebarHoliday(2028, 5, 28), EQmSidebarHoliday::DRAGON_BOAT);
	EXPECT_EQ(QmSidebarHoliday(2028, 6, 27), EQmSidebarHoliday::NONE);
}

TEST(QmSidebarHoliday, QingmingFollowsSolarTermIncludingLeapYears)
{
	EXPECT_EQ(QmSidebarHoliday(2026, 4, 4), EQmSidebarHoliday::QINGMING);
	EXPECT_EQ(QmSidebarHoliday(2026, 4, 6), EQmSidebarHoliday::QINGMING);
	EXPECT_EQ(QmSidebarHoliday(2026, 4, 7), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2028, 4, 4), EQmSidebarHoliday::QINGMING);
	EXPECT_EQ(QmSidebarHoliday(2028, 4, 6), EQmSidebarHoliday::NONE);
}

TEST(QmSidebarHoliday, MidAutumnTakesPriorityDuringNationalDayAndThenReturns)
{
	EXPECT_EQ(QmSidebarHoliday(2025, 10, 4), EQmSidebarHoliday::NATIONAL_DAY);
	EXPECT_EQ(QmSidebarHoliday(2025, 10, 5), EQmSidebarHoliday::MID_AUTUMN);
	EXPECT_EQ(QmSidebarHoliday(2025, 10, 7), EQmSidebarHoliday::MID_AUTUMN);
	EXPECT_EQ(QmSidebarHoliday(2025, 10, 8), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2031, 10, 2), EQmSidebarHoliday::MID_AUTUMN);
	EXPECT_EQ(QmSidebarHoliday(2031, 10, 3), EQmSidebarHoliday::NATIONAL_DAY);
}

TEST(QmSidebarHoliday, LabourAndNationalDayUseTheirOwnDateWindows)
{
	EXPECT_EQ(QmSidebarHoliday(2026, 5, 1), EQmSidebarHoliday::LABOUR_DAY);
	EXPECT_EQ(QmSidebarHoliday(2026, 5, 5), EQmSidebarHoliday::LABOUR_DAY);
	EXPECT_EQ(QmSidebarHoliday(2026, 5, 6), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2026, 10, 1), EQmSidebarHoliday::NATIONAL_DAY);
	EXPECT_EQ(QmSidebarHoliday(2026, 10, 7), EQmSidebarHoliday::NATIONAL_DAY);
	EXPECT_EQ(QmSidebarHoliday(2026, 10, 8), EQmSidebarHoliday::NONE);
}

TEST(QmSidebarHoliday, HalloweenAndChristmasEndWithoutSpillingIntoTheNextMonth)
{
	EXPECT_EQ(QmSidebarHoliday(2026, 10, 23), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2026, 10, 24), EQmSidebarHoliday::HALLOWEEN);
	EXPECT_EQ(QmSidebarHoliday(2026, 10, 31), EQmSidebarHoliday::HALLOWEEN);
	EXPECT_EQ(QmSidebarHoliday(2026, 11, 1), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2026, 12, 19), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2026, 12, 20), EQmSidebarHoliday::CHRISTMAS);
	EXPECT_EQ(QmSidebarHoliday(2026, 12, 26), EQmSidebarHoliday::CHRISTMAS);
	EXPECT_EQ(QmSidebarHoliday(2026, 12, 27), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2027, 1, 1), EQmSidebarHoliday::NONE);
}

TEST(QmSidebarHoliday, InvalidDatesDoNotActivateDecorations)
{
	for(const auto Date : {vec3(2026, 2, 29), vec3(2026, 4, 31), vec3(2026, 10, 32), vec3(2026, 0, 1), vec3(2026, 13, 1), vec3(2026, 5, 0), vec3(0, 5, 1)})
		EXPECT_EQ(QmSidebarHoliday(static_cast<int>(Date.x), static_cast<int>(Date.y), static_cast<int>(Date.z)), EQmSidebarHoliday::NONE);
}

TEST(QmSidebarHoliday, UnsupportedCalendarYearsKeepFixedHolidaysWithoutInventingLunarDates)
{
	EXPECT_EQ(QmSidebarHoliday(2101, 2, 17), EQmSidebarHoliday::NONE);
	EXPECT_EQ(QmSidebarHoliday(2101, 5, 1), EQmSidebarHoliday::LABOUR_DAY);
	EXPECT_EQ(QmSidebarHoliday(2101, 10, 31), EQmSidebarHoliday::HALLOWEEN);
	EXPECT_EQ(QmSidebarHoliday(2101, 12, 25), EQmSidebarHoliday::CHRISTMAS);
}

TEST(QmSidebarHoliday, ParticlesStayWithinTheSideTracks)
{
	for(const auto Holiday : {EQmSidebarHoliday::SPRING_FESTIVAL, EQmSidebarHoliday::QINGMING, EQmSidebarHoliday::LABOUR_DAY, EQmSidebarHoliday::DRAGON_BOAT, EQmSidebarHoliday::MID_AUTUMN, EQmSidebarHoliday::NATIONAL_DAY, EQmSidebarHoliday::HALLOWEEN, EQmSidebarHoliday::CHRISTMAS})
		for(const double Time : {0.0, 1.0, 11.999, 1000000000.0})
			for(int Index = 0; Index < QM_SIDEBAR_HOLIDAY_PARTICLES; ++Index)
			{
				SCOPED_TRACE(::testing::Message() << static_cast<int>(Holiday) << ":" << Time << ":" << Index);
				const auto Particle = QmSidebarHolidayParticle(Holiday, Index, Time);
				EXPECT_TRUE(Particle.m_Position.x < 0.1f || Particle.m_Position.x > 0.9f);
				EXPECT_GE(Particle.m_Position.y, 0.06f);
				EXPECT_LE(Particle.m_Position.y, 0.94f);
				EXPECT_GT(Particle.m_Alpha, 0.0f);
				EXPECT_LE(Particle.m_Alpha, 1.0f);
			}
}

TEST(QmSidebarHoliday, ParticleAnimationRepeatsContinuouslyWithoutAccumulatedState)
{
	for(int Index = 0; Index < QM_SIDEBAR_HOLIDAY_PARTICLES; ++Index)
	{
		const auto First = QmSidebarHolidayParticle(EQmSidebarHoliday::HALLOWEEN, Index, 0.25);
		const auto Later = QmSidebarHolidayParticle(EQmSidebarHoliday::HALLOWEEN, Index, 12000000.25);
		EXPECT_EQ(First.m_Position, Later.m_Position);
		EXPECT_FLOAT_EQ(First.m_Alpha, Later.m_Alpha);
	}
}

TEST(QmSidebarHoliday, InvalidAnimationTimeKeepsAVisibleFiniteDecoration)
{
	const auto Result = QmSidebarHolidayParticle(EQmSidebarHoliday::CHRISTMAS, 0, std::numeric_limits<double>::quiet_NaN());
	EXPECT_GE(Result.m_Position.y, 0.0f);
	EXPECT_LE(Result.m_Position.y, 1.0f);
	EXPECT_GT(Result.m_Alpha, 0.0f);
}
