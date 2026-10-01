#include <game/client/components/qmclient/pie_menu_points.h>

#include <gtest/gtest.h>

TEST(PieMenuPoints, CachedScoreIsReportedWithoutClampingOrRaceTimeFormatting)
{
	qm_pie_menu::CPointsRequest Request;
	Request.Start("player", 0.0);
	const auto Notification = Request.Poll("player", {EPointsStatus::READY, 123456}, 0.0);
	ASSERT_TRUE(Notification.has_value());
	EXPECT_EQ(Notification->m_Name, "player");
	EXPECT_EQ(Notification->m_Result.m_Status, EPointsStatus::READY);
	EXPECT_EQ(Notification->m_Result.m_Points, 123456);
	EXPECT_TRUE(Request.Name().empty());
	EXPECT_FALSE(Request.Poll("player", {EPointsStatus::READY, 123456}, 1.0).has_value());
}

TEST(PieMenuPoints, QueuedAndFetchingResultsWaitUntilReady)
{
	qm_pie_menu::CPointsRequest Request;
	Request.Start("player", 0.0);
	EXPECT_FALSE(Request.Poll("player", {EPointsStatus::NOT_REQUESTED, 0}, 1.0).has_value());
	EXPECT_FALSE(Request.Poll("player", {EPointsStatus::FETCHING, 0}, 10.0).has_value());
	const auto Notification = Request.Poll("player", {EPointsStatus::READY, 0}, 20.0);
	ASSERT_TRUE(Notification.has_value());
	EXPECT_EQ(Notification->m_Result.m_Status, EPointsStatus::READY);
	EXPECT_EQ(Notification->m_Result.m_Points, 0);
}

TEST(PieMenuPoints, RetargetingIgnoresThePreviousPlayersResult)
{
	qm_pie_menu::CPointsRequest Request;
	Request.Start("first", 0.0);
	Request.Start("second", 2.0);
	EXPECT_FALSE(Request.Poll("first", {EPointsStatus::READY, 100}, 3.0).has_value());
	EXPECT_EQ(Request.Name(), "second");
	const auto Notification = Request.Poll("second", {EPointsStatus::READY, 200}, 5.0);
	ASSERT_TRUE(Notification.has_value());
	EXPECT_EQ(Notification->m_Name, "second");
	EXPECT_EQ(Notification->m_Result.m_Points, 200);
}

TEST(PieMenuPoints, FailureIsReportedOnceAndCanBeRequestedAgain)
{
	qm_pie_menu::CPointsRequest Request;
	Request.Start("player", 0.0);
	const auto Notification = Request.Poll("player", {EPointsStatus::FAILED, 0}, 10.0);
	ASSERT_TRUE(Notification.has_value());
	EXPECT_EQ(Notification->m_Result.m_Status, EPointsStatus::FAILED);
	EXPECT_FALSE(Request.Poll("player", {EPointsStatus::FAILED, 0}, 11.0).has_value());
	Request.Start("player", 40.0);
	EXPECT_FALSE(Request.Poll("player", {EPointsStatus::FETCHING, 0}, 41.0).has_value());
	EXPECT_TRUE(Request.Poll("player", {EPointsStatus::READY, 300}, 45.0).has_value());
}

TEST(PieMenuPoints, BusyOrUnresponsiveServiceHasABoundedWait)
{
	qm_pie_menu::CPointsRequest Request;
	Request.Start("player", 5.0);
	EXPECT_FALSE(Request.Poll("player", {EPointsStatus::NOT_REQUESTED, 0}, 64.0).has_value());
	const auto Notification = Request.Poll("player", {EPointsStatus::FETCHING, 0}, 65.0);
	ASSERT_TRUE(Notification.has_value());
	EXPECT_EQ(Notification->m_Result.m_Status, EPointsStatus::FAILED);
	EXPECT_TRUE(Request.Name().empty());
}
