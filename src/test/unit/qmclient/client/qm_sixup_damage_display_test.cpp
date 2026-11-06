#include <game/client/components/qmclient/sixup_damage_display.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

TEST(QmSixupDamageDisplay, FirstHitAfterGapKeepsOriginalSpreadAndResetsGroup)
{
	int DamageTaken = 9;
	float PreviousTime = 0;
	std::vector<int> Angles;
	QmForEachSixupDamageAngle(2, DamageTaken, PreviousTime, 1.0f, [&](int Angle) { Angles.push_back(Angle); });
	EXPECT_EQ(Angles, (std::vector<int>{1072, 1206}));
	EXPECT_EQ(DamageTaken, 0);
	EXPECT_FLOAT_EQ(PreviousTime, 1.0f);
}

TEST(QmSixupDamageDisplay, ConsecutiveHitKeepsQuarterRadianGroupOffset)
{
	int DamageTaken = 0;
	float PreviousTime = 1.0f;
	std::vector<int> Angles;
	QmForEachSixupDamageAngle(2, DamageTaken, PreviousTime, 1.1f, [&](int Angle) { Angles.push_back(Angle); });
	EXPECT_EQ(Angles, (std::vector<int>{1136, 1270}));
	EXPECT_EQ(DamageTaken, 1);
	EXPECT_FLOAT_EQ(PreviousTime, 1.1f);
}

TEST(QmSixupDamageDisplay, HalfSecondBoundaryPreservesStrictGrouping)
{
	const struct
	{
		float m_Time;
		int m_Angle;
		int m_Count;
	} aCases[] = {{std::nextafter(1.5f, 1.0f), 1181, 1}, {1.5f, 1117, 0}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Time);
		int DamageTaken = 0;
		float PreviousTime = 1.0f;
		std::vector<int> Angles;
		QmForEachSixupDamageAngle(1, DamageTaken, PreviousTime, Case.m_Time, [&](int Angle) { Angles.push_back(Angle); });
		EXPECT_EQ(Angles, (std::vector<int>{Case.m_Angle}));
		EXPECT_EQ(DamageTaken, Case.m_Count);
		EXPECT_FLOAT_EQ(PreviousTime, Case.m_Time);
	}
}

TEST(QmSixupDamageDisplay, LargeRepresentableGroupStillProducesEveryIndicator)
{
	int DamageTaken = 1 << 24;
	float PreviousTime = 1.0f;
	std::vector<int> Angles;
	QmForEachSixupDamageAngle(18, DamageTaken, PreviousTime, 1.1f, [&](int Angle) { Angles.push_back(Angle); });
	ASSERT_EQ(Angles.size(), 18u);
	EXPECT_GT(Angles.front(), 1000000000);
	EXPECT_EQ(DamageTaken, (1 << 24) + 1);
}

TEST(QmSixupDamageDisplay, PositiveOverflowIsSkippedAndOriginalGapRestoresDrawing)
{
	int DamageTaken = std::numeric_limits<int>::max();
	float PreviousTime = 1.0f;
	std::vector<int> Angles;
	auto Emit = [&](int Angle) { Angles.push_back(Angle); };
	QmForEachSixupDamageAngle(1, DamageTaken, PreviousTime, 1.1f, Emit);
	EXPECT_TRUE(Angles.empty());
	EXPECT_EQ(DamageTaken, std::numeric_limits<int>::max());
	EXPECT_FLOAT_EQ(PreviousTime, 1.1f);
	QmForEachSixupDamageAngle(1, DamageTaken, PreviousTime, 1.2f, Emit);
	EXPECT_TRUE(Angles.empty());
	EXPECT_EQ(DamageTaken, std::numeric_limits<int>::max());
	QmForEachSixupDamageAngle(1, DamageTaken, PreviousTime, 2.0f, Emit);
	EXPECT_EQ(Angles, (std::vector<int>{1117}));
	EXPECT_EQ(DamageTaken, 0);
	EXPECT_FLOAT_EQ(PreviousTime, 2.0f);
}

TEST(QmSixupDamageDisplay, NegativeOutOfRangeAngleIsNotConverted)
{
	int DamageTaken = std::numeric_limits<int>::min();
	float PreviousTime = 1.0f;
	int Emitted = 0;
	QmForEachSixupDamageAngle(1, DamageTaken, PreviousTime, 1.1f, [&](int) { ++Emitted; });
	EXPECT_EQ(Emitted, 0);
	EXPECT_EQ(DamageTaken, std::numeric_limits<int>::min() + 1);
}

TEST(QmSixupDamageDisplay, AmountOutsideValidatedDamageBudgetLeavesGroupUntouched)
{
	for(int Amount : {-1, 0, 19, std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Amount);
		int DamageTaken = 7;
		float PreviousTime = 1.0f;
		int Emitted = 0;
		QmForEachSixupDamageAngle(Amount, DamageTaken, PreviousTime, 1.1f, [&](int) { ++Emitted; });
		EXPECT_EQ(Emitted, 0);
		EXPECT_EQ(DamageTaken, 7);
		EXPECT_FLOAT_EQ(PreviousTime, 1.0f);
	}
}
