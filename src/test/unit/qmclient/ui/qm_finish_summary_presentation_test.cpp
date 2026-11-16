#include <game/client/QmUi/QmFinishSummaryPresentation.h>

#include <gtest/gtest.h>

namespace
{
	using namespace qm_finish_summary;

	TNumberTargets Targets()
	{
		TNumberTargets Result;
		Result.fill(-1.0);
		Result[static_cast<size_t>(ENumber::TIME)] = 6000.0;
		Result[static_cast<size_t>(ENumber::POINTS)] = 100.0;
		return Result;
	}
}

TEST(QmFinishSummaryPresentation, WaitsForExpandedShellBeforeRevealingDetails)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 10.0, false, 2, Values);
	Presentation.Update(1, 20.0, false, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.TimePlacement(), 0.0f);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::MAP), 0.0f);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::TIME), 0.0);
	EXPECT_FALSE(Presentation.Complete());
	Presentation.Update(1, 20.0, true, 2, Values);
	Presentation.Update(1, 20.3, true, 2, Values);
	EXPECT_GT(Presentation.Number(ENumber::TIME), 0.0);
}

TEST(QmFinishSummaryPresentation, TimeCountsAtCenterBeforeMovingToTopLeft)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 0.3, true, 2, Values);
	EXPECT_GT(Presentation.Number(ENumber::TIME), 0.0);
	EXPECT_LT(Presentation.Number(ENumber::TIME), 6000.0);
	EXPECT_FLOAT_EQ(Presentation.TimePlacement(), 0.0f);
	Presentation.Update(1, 0.75, true, 2, Values);
	EXPECT_GT(Presentation.TimePlacement(), 0.0f);
	EXPECT_LT(Presentation.TimePlacement(), 1.0f);
	Presentation.Update(1, 1.0, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.TimePlacement(), 1.0f);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::TIME), 6000.0);
}

TEST(QmFinishSummaryPresentation, MetadataAppearsInFieldOrder)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 1.04, true, 2, Values);
	EXPECT_GT(Presentation.FieldAlpha(EField::MAP), 0.0f);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::MAPPER), 0.0f);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::POINTS), 0.0f);
	Presentation.Update(1, 1.2, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::MAP), 1.0f);
	EXPECT_GT(Presentation.FieldAlpha(EField::MAPPER), 0.0f);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::LOCAL_RANK), 0.0f);
}

TEST(QmFinishSummaryPresentation, PointsWaitForTheirFieldThenCountFromZero)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 1.8, true, 2, Values);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::POINTS), 0.0);
	Presentation.Update(1, 2.3, true, 2, Values);
	EXPECT_GT(Presentation.Number(ENumber::POINTS), 0.0);
	EXPECT_LT(Presentation.Number(ENumber::POINTS), 100.0);
	EXPECT_FALSE(Presentation.Complete());
	Presentation.Update(1, 2.7, true, 2, Values);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::POINTS), 100.0);
	EXPECT_TRUE(Presentation.Complete());
}

TEST(QmFinishSummaryPresentation, RevisedTargetContinuesFromCurrentNumber)
{
	CPresentation Presentation;
	auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 0.25, true, 2, Values);
	const double Before = Presentation.Number(ENumber::TIME);
	Values[static_cast<size_t>(ENumber::TIME)] = 8000.0;
	Presentation.Update(1, 0.25, true, 2, Values);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::TIME), Before);
	Presentation.Update(1, 0.5, true, 2, Values);
	EXPECT_GT(Presentation.Number(ENumber::TIME), Before);
	EXPECT_LT(Presentation.Number(ENumber::TIME), 8000.0);
	Presentation.Update(1, 1.0, true, 2, Values);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::TIME), 8000.0);
}

TEST(QmFinishSummaryPresentation, AsyncUpdateKeepsCompletedEntrance)
{
	CPresentation Presentation;
	auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 3.0, true, 2, Values);
	Values[static_cast<size_t>(ENumber::POINTS)] = 150.0;
	Presentation.Update(1, 4.0, true, 2, Values);
	EXPECT_TRUE(Presentation.Complete());
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::MAP), 1.0f);
	EXPECT_FLOAT_EQ(Presentation.TimePlacement(), 1.0f);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::POINTS), 100.0);
	Presentation.Update(1, 4.7, true, 2, Values);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::POINTS), 150.0);
}

TEST(QmFinishSummaryPresentation, DisablingMotionSettlesAllFieldsAndNumbers)
{
	CPresentation Presentation;
	auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 0.2, true, 0, Values);
	EXPECT_TRUE(Presentation.Complete());
	EXPECT_FLOAT_EQ(Presentation.TimePlacement(), 1.0f);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::POINTS), 1.0f);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::POINTS), 100.0);
	Values[static_cast<size_t>(ENumber::POINTS)] = 150.0;
	Presentation.Update(1, 0.3, true, 0, Values);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::POINTS), 150.0);
	Presentation.Update(1, 0.4, true, 2, Values);
	EXPECT_TRUE(Presentation.Complete());
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::POINTS), 150.0);
}

TEST(QmFinishSummaryPresentation, MotionDisabledStartsInFinalState)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 0.0, false, 0, Values);
	EXPECT_TRUE(Presentation.Complete());
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::TIME), 6000.0);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::MAP), 1.0f);
}

TEST(QmFinishSummaryPresentation, NewFinishStartsItsOwnPresentation)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 3.0, true, 2, Values);
	Presentation.Update(2, 4.0, true, 2, Values);
	EXPECT_FALSE(Presentation.Complete());
	EXPECT_FLOAT_EQ(Presentation.TimePlacement(), 0.0f);
	EXPECT_FLOAT_EQ(Presentation.FieldAlpha(EField::MAP), 0.0f);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::TIME), 0.0);
}

TEST(QmFinishSummaryPresentation, RepeatedFrameDoesNotAdvanceOrRestart)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 0.0, true, 2, Values);
	Presentation.Update(1, 0.3, true, 2, Values);
	const double Before = Presentation.Number(ENumber::TIME);
	Presentation.Update(1, 0.3, true, 2, Values);
	EXPECT_DOUBLE_EQ(Presentation.Number(ENumber::TIME), Before);
}

TEST(QmFinishSummaryPresentation, NumberColorProgressFollowsCountAndKeepsFinalGradient)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 10.0, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::TIME), 0.0f);
	Presentation.Update(1, 10.25, true, 2, Values);
	EXPECT_GT(Presentation.NumberColorProgress(ENumber::TIME), 0.0f);
	EXPECT_LT(Presentation.NumberColorProgress(ENumber::TIME), 1.0f);
	Presentation.Update(1, 12.0, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::TIME), 1.0f);
	Presentation.Update(1, 14.0, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::TIME), 1.0f);
}

TEST(QmFinishSummaryPresentation, RetargetKeepsCurrentNumberColor)
{
	CPresentation Presentation;
	auto Values = Targets();
	Presentation.Update(1, 10.0, true, 2, Values);
	Presentation.Update(1, 10.25, true, 2, Values);
	const float Before = Presentation.NumberColorProgress(ENumber::TIME);
	Values[static_cast<size_t>(ENumber::TIME)] = 12000.0;
	Presentation.Update(1, 10.25, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::TIME), Before);
	Presentation.Update(1, 10.5, true, 2, Values);
	EXPECT_GT(Presentation.NumberColorProgress(ENumber::TIME), Before);
}

TEST(QmFinishSummaryPresentation, DisabledMotionSettlesNumberColorImmediately)
{
	CPresentation Presentation;
	const auto Values = Targets();
	Presentation.Update(1, 10.0, true, 0, Values);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::TIME), 1.0f);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::POINTS), 1.0f);
	Presentation.Update(1, 10.1, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::TIME), 1.0f);
	Presentation.Update(2, 11.0, true, 2, Values);
	EXPECT_FLOAT_EQ(Presentation.NumberColorProgress(ENumber::TIME), 0.0f);
}
