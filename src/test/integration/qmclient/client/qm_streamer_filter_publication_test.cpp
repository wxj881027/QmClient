#include <game/client/components/qmclient/streamer_word_filter.h>
#include <game/client/components/qmclient/word_filter_list.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>

namespace
{
	class QmStreamerFilePublication : public testing::Test
	{
	protected:
		void SetUp() override { ASSERT_TRUE(QmStreamerWordFilter::SetAdditionalWords({})); }
		void TearDown() override { ASSERT_TRUE(QmStreamerWordFilter::SetAdditionalWords({})); }

		void ExpectRules(const char *pExpected)
		{
			std::string Text = "qzhealtholdtoken qzhealthnewtoken qzhealthlatesttoken 习近平";
			QmStreamerWordFilter::Filter(Text, true);
			EXPECT_EQ(Text, pExpected);
		}
	};
}

TEST_F(QmStreamerFilePublication, FailedPublicationRetriesWithoutAFileRevisionChange)
{
	QmWordFilterList::CFileList File;
	ASSERT_TRUE(File.UpdateFromText("qzhealtholdtoken"));
	ASSERT_TRUE(File.PublishStreamerWords());
	ASSERT_TRUE(File.UpdateFromText("qzhealthnewtoken"));
	const uint64_t FileRevision = File.Revision();
	const uint64_t FilterRevision = QmStreamerWordFilter::Revision();
	bool Published, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Published = File.PublishStreamerWords();
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Published);
	EXPECT_EQ(File.Revision(), FileRevision);
	EXPECT_EQ(QmStreamerWordFilter::Revision(), FilterRevision);
	ExpectRules("*** qzhealthnewtoken qzhealthlatesttoken ***");
	ASSERT_TRUE(File.PublishStreamerWords());
	EXPECT_EQ(File.Revision(), FileRevision);
	EXPECT_EQ(QmStreamerWordFilter::Revision(), FilterRevision + 1);
	ExpectRules("qzhealtholdtoken *** qzhealthlatesttoken ***");
	{
		CPredictionAllocationFailure Failure;
		Published = File.PublishStreamerWords();
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Published);
	EXPECT_FALSE(Triggered);
}

TEST_F(QmStreamerFilePublication, ANewerFileSupersedesAPreviouslyFailedPublication)
{
	QmWordFilterList::CFileList File;
	ASSERT_TRUE(File.UpdateFromText("qzhealtholdtoken"));
	ASSERT_TRUE(File.PublishStreamerWords());
	ASSERT_TRUE(File.UpdateFromText("qzhealthnewtoken"));
	bool Published;
	{
		CPredictionAllocationFailure Failure;
		Published = File.PublishStreamerWords();
	}
	EXPECT_FALSE(Published);
	ASSERT_TRUE(File.UpdateFromText("qzhealthlatesttoken"));
	ASSERT_TRUE(File.PublishStreamerWords());
	ExpectRules("qzhealtholdtoken qzhealthnewtoken *** ***");
}

TEST_F(QmStreamerFilePublication, FailedClearRetainsRulesUntilAnOrdinaryRetrySucceeds)
{
	QmWordFilterList::CFileList File;
	ASSERT_TRUE(File.UpdateFromText("qzhealtholdtoken"));
	ASSERT_TRUE(File.PublishStreamerWords());
	ASSERT_TRUE(File.UpdateFromText(""));
	bool Published;
	{
		CPredictionAllocationFailure Failure;
		Published = File.PublishStreamerWords();
	}
	EXPECT_FALSE(Published);
	ExpectRules("*** qzhealthnewtoken qzhealthlatesttoken ***");
	ASSERT_TRUE(File.PublishStreamerWords());
	ExpectRules("qzhealtholdtoken qzhealthnewtoken qzhealthlatesttoken ***");
}

TEST_F(QmStreamerFilePublication, FirstFilePublicationFailureRemainsPending)
{
	QmWordFilterList::CFileList File;
	ASSERT_TRUE(File.UpdateFromText("qzhealthnewtoken"));
	bool Published;
	{
		CPredictionAllocationFailure Failure;
		Published = File.PublishStreamerWords();
	}
	EXPECT_FALSE(Published);
	ExpectRules("qzhealtholdtoken qzhealthnewtoken qzhealthlatesttoken ***");
	ASSERT_TRUE(File.PublishStreamerWords());
	ExpectRules("qzhealtholdtoken *** qzhealthlatesttoken ***");
}

TEST_F(QmStreamerFilePublication, ForceRepublishesAnUnchangedFileRevision)
{
	QmWordFilterList::CFileList File;
	ASSERT_TRUE(File.UpdateFromText("qzhealthnewtoken"));
	ASSERT_TRUE(File.PublishStreamerWords());
	ASSERT_TRUE(QmStreamerWordFilter::SetAdditionalWords({"qzhealtholdtoken"}));
	const uint64_t Revision = File.Revision();
	ASSERT_TRUE(File.PublishStreamerWords());
	ExpectRules("*** qzhealthnewtoken qzhealthlatesttoken ***");
	ASSERT_TRUE(File.PublishStreamerWords(true));
	EXPECT_EQ(File.Revision(), Revision);
	ExpectRules("qzhealtholdtoken *** qzhealthlatesttoken ***");
}
