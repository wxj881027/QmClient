#include <game/client/components/qmclient/map_tags.h>

#include <gtest/gtest.h>
#include <test/support/map_http_recorder.h>
#include <test/support/prediction_allocation_failure.h>

TEST(QmMapTagsWriteCompletionAllocation, FailedReceiptParsingReleasesWriteAndKeepsSnapshot)
{
	MapHttpTest::CHttpRecorder Http;
	const auto Write = [](const char *pUrl, const char *) { return MapHttpTest::CreateResponse(pUrl); };
	QmMapTags::CClient Client(MapHttpTest::CreateResponse, Write);
	Client.SetContext("ddnet", "Lighthouse", "Alice");
	Client.Update(&Http, 1.0);
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Http.m_vRequests.back()->Finish(R"({"ok":true,"community":"ddnet","map":"Lighthouse","player":"Alice","eligible":true,"eligibility_error":"","tags":[]})");
	Client.Poll();
	ASSERT_TRUE(Client.Ready());
	ASSERT_TRUE(Client.Submit(&Http, "Hook"));
	ASSERT_EQ(Http.m_vRequests.size(), 2u);
	Http.m_vRequests.back()->Finish(R"({"ok":true,"pending":true})");

	bool Triggered;
	{
		CPredictionAllocationFailure Failure;
		Client.Poll();
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Client.Busy());
	EXPECT_TRUE(Client.Ready());
	EXPECT_TRUE(Client.Snapshot().m_Eligible);
	EXPECT_STREQ(Client.Status(), "Cannot submit this tag. Please retry later.");
	Client.Update(&Http, 2.0);
	EXPECT_EQ(Http.m_vRequests.size(), 2u);

	ASSERT_TRUE(Client.Submit(&Http, "Fly"));
	ASSERT_EQ(Http.m_vRequests.size(), 3u);
	Http.m_vRequests.back()->Finish(R"({"ok":true,"pending":true})");
	Client.Poll();
	EXPECT_FALSE(Client.Busy());
	EXPECT_STREQ(Client.Status(), "Tag submitted for review.");
}
