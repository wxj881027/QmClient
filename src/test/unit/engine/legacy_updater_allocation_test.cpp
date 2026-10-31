#include <engine/client/updater_data.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>
#include <vector>

TEST(LegacyUpdaterAllocation, EachCppPreparationFailurePreservesThePreviousPlanAndAllowsRetry)
{
	const std::string Content = R"([{"version":"2","client":true,"download":["first.bin","second.bin"],"remove":["old.bin"]}])";
	const std::vector<std::pair<std::string, bool>> Expected{{"old.bin", false}, {"second.bin", true}, {"first.bin", true}};
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 128; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		QmLegacyUpdater::SUpdatePlan Plan;
		Plan.m_ServerUpdate = true;
		Plan.m_FileJobs.emplace_front("previous.bin", false);
		const auto *pPrevious = &Plan.m_FileJobs.front();
		bool Parsed, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Parsed = QmLegacyUpdater::ParseManifest(Content, "1", Plan);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Parsed);
			EXPECT_EQ((std::vector<std::pair<std::string, bool>>{Plan.m_FileJobs.begin(), Plan.m_FileJobs.end()}), Expected);
			ReachedSuccess = Parsed;
			break;
		}
		++Failures;
		EXPECT_FALSE(Parsed);
		ASSERT_FALSE(Plan.m_FileJobs.empty());
		EXPECT_EQ(&Plan.m_FileJobs.front(), pPrevious);
		EXPECT_TRUE(Plan.m_ServerUpdate);
		EXPECT_FALSE(Plan.m_ClientUpdate);
		EXPECT_EQ((std::vector<std::pair<std::string, bool>>{Plan.m_FileJobs.begin(), Plan.m_FileJobs.end()}), (std::vector<std::pair<std::string, bool>>{{"previous.bin", false}}));
		ASSERT_TRUE(QmLegacyUpdater::ParseManifest(Content, "1", Plan));
		EXPECT_TRUE(Plan.m_ClientUpdate);
		EXPECT_FALSE(Plan.m_ServerUpdate);
		EXPECT_EQ((std::vector<std::pair<std::string, bool>>{Plan.m_FileJobs.begin(), Plan.m_FileJobs.end()}), Expected);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(ReachedSuccess);
}
