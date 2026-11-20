#include <game/client/components/tclient/qm_tee_trail.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(QmTeeTrailTime, TickConversionPreservesOrdinaryTruncation)
{
	EXPECT_EQ(qm_tee_trail::TickFromTime(0), 0);
	EXPECT_EQ(qm_tee_trail::TickFromTime(1.99), 1);
	EXPECT_EQ(qm_tee_trail::TickFromTime(10.5), 10);
}

TEST(QmTeeTrailTime, TickConversionRetainsValuesBeyondTheOldIntegerDomain)
{
	EXPECT_EQ(qm_tee_trail::TickFromTime(2147483648.25), INT64_C(2147483648));
	EXPECT_EQ(qm_tee_trail::TickFromTime(2147483647.75), INT64_C(2147483647));
}

TEST(QmTeeTrailTime, InvalidTimeHasNoIntegerResult)
{
	EXPECT_FALSE(qm_tee_trail::TickFromTime(-1));
	EXPECT_FALSE(qm_tee_trail::TickFromTime(std::numeric_limits<double>::infinity()));
	EXPECT_FALSE(qm_tee_trail::TickFromTime(std::numeric_limits<double>::quiet_NaN()));
}

TEST(QmTeeTrailTime, LastRepresentableDoubleBelowTheSignedLimitIsAccepted)
{
	const double Upper = 9223372036854775808.0;
	EXPECT_EQ(qm_tee_trail::TickFromTime(std::nextafter(Upper, 0.0)), INT64_C(9223372036854774784));
}

TEST(QmTeeTrailTime, SignedLimitAndLargerFiniteTimeAreRejectedBeforeConversion)
{
	EXPECT_FALSE(qm_tee_trail::TickFromTime(9223372036854775808.0));
	EXPECT_FALSE(qm_tee_trail::TickFromTime(std::numeric_limits<double>::max()));
}

TEST(QmTeeTrailTime, NewHeadKeepsItsWideTickAndFractionalBirthTime)
{
	qm_tee_trail::CTrailState State;
	State.Update(vec2(0, 0), 2147483648.25, 12, 100);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	ASSERT_EQ(vTrail.size(), 1u);
	EXPECT_EQ(vTrail[0].m_Tick, INT64_C(2147483648));
	EXPECT_DOUBLE_EQ(vTrail[0].m_Time, 2147483648.25);
	EXPECT_EQ(static_cast<unsigned>(vTrail[0].m_Tick), 0x80000000u);
}

TEST(QmTeeTrailTime, InterpolationAcrossTheOldLimitKeepsHeadAndSampleTicks)
{
	qm_tee_trail::CTrailState State;
	State.Update(vec2(0, 0), 2147483647.5, 12, 100);
	State.Update(vec2(12, 0), 2147483648.5, 12, 100);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	ASSERT_EQ(vTrail.size(), 3u);
	EXPECT_EQ(vTrail[0].m_Tick, INT64_C(2147483648));
	EXPECT_DOUBLE_EQ(vTrail[0].m_Time, 2147483648.5);
	EXPECT_EQ(vTrail[1].m_Pos, vec2(6, 0));
	EXPECT_EQ(vTrail[1].m_Tick, INT64_C(2147483648));
	EXPECT_DOUBLE_EQ(vTrail[1].m_Time, 2147483648.0);
	EXPECT_EQ(vTrail[2].m_Tick, INT64_C(2147483647));
}

TEST(QmTeeTrailTime, UnrepresentableTimeResetsHistoryAndAllowsANormalUpdate)
{
	qm_tee_trail::CTrailState State;
	State.Update(vec2(0, 0), 10, 12, 100);
	State.Update(vec2(12, 0), 9223372036854775808.0, 12, 100);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	EXPECT_TRUE(vTrail.empty());
	State.Update(vec2(0, 0), 30, 12, 100);
	State.Export(vTrail);
	ASSERT_EQ(vTrail.size(), 1u);
	EXPECT_EQ(vTrail[0].m_Tick, 30);
}

TEST(QmTeeTrailTime, FiniteCoordinatesWithOverflowingDistanceResetTheTrail)
{
	qm_tee_trail::CTrailState State;
	State.Update(vec2(0, 0), 10, 12, 100);
	State.Update(vec2(std::numeric_limits<float>::max(), 0), 11, 12, 100);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	EXPECT_TRUE(vTrail.empty());
}

TEST(QmTeeTrailTime, ResetStartsANewHeadWithoutUsingTheDiscardedPosition)
{
	qm_tee_trail::CTrailState State;
	State.Update(vec2(1.0e19f, 0), 10, 1, 100);
	State.Reset();
	State.Update(vec2(-1.0e19f, 0), 11, 1, 100);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	ASSERT_EQ(vTrail.size(), 1u);
	EXPECT_EQ(vTrail[0].m_Pos, vec2(-1.0e19f, 0));
	EXPECT_EQ(vTrail[0].m_Tick, 11);
}

TEST(QmTeeTrailTime, DiscardedHistoryDoesNotRejectANewFiniteHead)
{
	struct SCase
	{
		double m_Time;
		qm_tee_trail::EUpdateMode m_Mode;
	};
	const SCase aCases[] = {{11, qm_tee_trail::EUpdateMode::RESET}, {30, qm_tee_trail::EUpdateMode::NORMAL}, {5, qm_tee_trail::EUpdateMode::NORMAL}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Time);
		qm_tee_trail::CTrailState State;
		State.Update(vec2(1.0e19f, 0), 10, 1, 100);
		State.Update(vec2(-1.0e19f, 0), Case.m_Time, 1, 100, Case.m_Mode);
		std::vector<CTrailPart> vTrail;
		State.Export(vTrail);
		ASSERT_EQ(vTrail.size(), 1u);
		EXPECT_EQ(vTrail[0].m_Pos, vec2(-1.0e19f, 0));
		EXPECT_DOUBLE_EQ(vTrail[0].m_Time, Case.m_Time);
	}
}

TEST(QmTeeTrailTime, SamplingStepThatCannotAdvanceResetsAndRemainsRetryable)
{
	qm_tee_trail::CTrailState State;
	constexpr float Huge = 72057594037927936.0f;
	State.Update(vec2(0, 0), 10, Huge, 100);
	State.Update(vec2(Huge, 0), 11, Huge, 100);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	EXPECT_TRUE(vTrail.empty());
	State.Update(vec2(6, 0), 12, 2, 30);
	State.Export(vTrail);
	ASSERT_EQ(vTrail.size(), 1u);
	EXPECT_EQ(vTrail[0].m_Pos, vec2(6, 0));
}

TEST(QmTeeTrailTime, LegacySpeedKeepsOrdinaryAndEqualTickRules)
{
	EXPECT_FLOAT_EQ(qm_tee_trail::LegacySpeed(12, 100, 96), 3);
	EXPECT_FLOAT_EQ(qm_tee_trail::LegacySpeed(12, 96, 100), 3);
	EXPECT_FLOAT_EQ(qm_tee_trail::LegacySpeed(12, 100, 100), 1200);
}

TEST(QmTeeTrailTime, LegacySpeedUsesTheCompleteOldSignedTickDistance)
{
	EXPECT_FLOAT_EQ(qm_tee_trail::LegacySpeed(4294967296.0f, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()), 1);
}

TEST(QmTeeTrailTime, LegacySpeedUsesTheCompleteWideSignedTickDistance)
{
	EXPECT_FLOAT_EQ(qm_tee_trail::LegacySpeed(18446744073709551616.0f, std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min()), 1);
}

TEST(QmTeeTrailTime, LegacyNegativeSentinelKeepsItsOriginalDistance)
{
	EXPECT_FLOAT_EQ(qm_tee_trail::LegacySpeed(6, 0, -1), 6);
}

TEST(QmTeeTrailTime, ConnectionClockCanCrossTheOldLimitWithoutChangingItsTimeline)
{
	qm_tee_trail::CTrailClock Clock;
	qm_tee_trail::CTrailState State;
	const double First = Clock.Update(2147483647.5, 0.25, false);
	State.Update(vec2(0, 0), First, 12, 100);
	const double Switched = Clock.Update(1000, 0.75, true);
	EXPECT_DOUBLE_EQ(Switched, 2147483648.25);
	State.Update(vec2(6, 0), Switched, 12, 100, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	ASSERT_GE(vTrail.size(), 2u);
	EXPECT_EQ(vTrail.front().m_Tick, INT64_C(2147483648));
	EXPECT_DOUBLE_EQ(vTrail.back().m_Time, First);
}

TEST(QmTeeTrailTime, WideSamplesRemainUsableByTheProductionGeometryBuilder)
{
	qm_tee_trail::CTrailState State;
	State.Update(vec2(0, 0), 2147483647.5, 12, 100);
	State.Update(vec2(12, 0), 2147483648.5, 12, 100);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_ORIGINAL, false, 2147483648.5, 4, 7, vQuads);
	ASSERT_FALSE(vQuads.empty());
	for(const auto &Quad : vQuads)
		for(int i = 0; i < 4; ++i)
		{
			EXPECT_TRUE(std::isfinite(Quad.m_aPos[i].x));
			EXPECT_TRUE(std::isfinite(Quad.m_aPos[i].y));
			EXPECT_TRUE(std::isfinite(Quad.m_aColor[i].a));
		}
}
