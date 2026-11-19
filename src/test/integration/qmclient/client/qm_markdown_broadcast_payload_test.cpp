#include <game/client/components/qmclient/qm_markdown_broadcast_payload.h>
#include <game/client/components/qmclient/qm_realtime.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	using EResult = CQmMarkdownBroadcast::EApplyResult;

	qm_news::TPayload Json(std::string_view Text)
	{
		return qm_news::TPayload(json_parse(Text.data(), Text.size()));
	}

	qm_news::TPayload Cache(int Version, const std::string &Markdown)
	{
		return Json("{\"cache_version\":1,\"version\":" + std::to_string(Version) + ",\"markdown\":\"" + Markdown + "\"}");
	}
}

TEST(QmMarkdownBroadcastPayload, RealtimeParserTransfersOwnedMessageAfterDecode)
{
	CQmMarkdownBroadcast Broadcast;
	qm_news::CReceiver Receiver;
	const std::string Body(8192, 'x');
	const std::string Input = "{\"type\":\"broadcast\",\"data\":{\"version\":3,\"markdown\":\"" + Body + "\"}}";
	{
		SQmRealtimeMessage Message;
		ASSERT_TRUE(ParseQmRealtimeMessage(Input.data(), Input.size(), Message));
		ASSERT_TRUE(Message.m_HasBroadcast);
		EResult Result;
		bool Triggered;
		{
			CPredictionAllocationFailure Fault;
			Result = Receiver.ApplyOwned(Broadcast, std::move(Message.m_BroadcastMarkdown), Message.m_BroadcastVersion);
			Triggered = Fault.Triggered();
		}
		EXPECT_FALSE(Triggered);
		EXPECT_EQ(Result, EResult::APPLIED);
	}
	EXPECT_EQ(Broadcast.Markdown(), Body);
	EXPECT_EQ(Broadcast.Version(), 3);
}

TEST(QmMarkdownBroadcastPayload, ExactFieldsWinOverUnknownPrefixedMembers)
{
	auto Root = Json(R"({"markdown\u0000extra":"ignored","version\u0000extra":999,"markdown":"current","version":2})");
	ASSERT_NE(Root, nullptr);
	CQmMarkdownBroadcast Broadcast;
	qm_news::CReceiver Receiver;
	EXPECT_EQ(Receiver.ApplyResponse(Broadcast, Root.get()), EResult::APPLIED);
	EXPECT_STREQ(Broadcast.Markdown(), "current");
	EXPECT_EQ(Broadcast.Version(), 2);
}

TEST(QmMarkdownBroadcastPayload, PrefixedRequiredFieldsCannotSupplyMissingData)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("kept", 1));
	qm_news::CReceiver Receiver;
	for(const char *pJson : {
		    R"({"cache_version":1,"markdown\u0000extra":"bad","version":2})",
		    R"({"cache_version":1,"markdown":"bad","version\u0000extra":2})",
		    R"({"cache_version\u0000extra":1,"markdown":"bad","version":2})"})
	{
		SCOPED_TRACE(pJson);
		auto Root = Json(pJson);
		ASSERT_NE(Root, nullptr);
		EXPECT_EQ(Receiver.LoadCache(Broadcast, std::move(Root), 1), EResult::REJECTED);
		EXPECT_FALSE(Receiver.HasPendingCache());
		EXPECT_STREQ(Broadcast.Markdown(), "kept");
	}
}

TEST(QmMarkdownBroadcastPayload, PayloadVersionIsCheckedBeforeNarrowing)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("kept", 1));
	qm_news::CReceiver Receiver;
	for(const char *pVersion : {"-1", "2147483648", "4294967301", "9223372036854775807", "1.5", "null"})
	{
		SCOPED_TRACE(pVersion);
		auto Root = Json(std::string("{\"cache_version\":1,\"version\":") + pVersion + ",\"markdown\":\"bad\"}");
		ASSERT_NE(Root, nullptr);
		EXPECT_EQ(Receiver.LoadCache(Broadcast, std::move(Root), 1), EResult::REJECTED);
		EXPECT_EQ(Broadcast.Version(), 1);
		EXPECT_STREQ(Broadcast.Markdown(), "kept");
	}
	EXPECT_EQ(Receiver.LoadCache(Broadcast, Cache(std::numeric_limits<int>::max(), "valid"), 1), EResult::APPLIED);
	EXPECT_EQ(Broadcast.Version(), std::numeric_limits<int>::max());
}

TEST(QmMarkdownBroadcastPayload, InvalidTextKeepsPreviouslyPublishedContent)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("kept", 1));
	qm_news::CReceiver Receiver;
	const std::string InvalidUtf8 = std::string("{\"version\":2,\"markdown\":\"") + char(0xff) + "\"}";
	for(const std::string &Text : {std::string(R"({"version":2,"markdown":"hello\u0000tail"})"), InvalidUtf8})
	{
		auto Root = Json(Text);
		EXPECT_EQ(Receiver.ApplyResponse(Broadcast, Root.get()), EResult::REJECTED);
		EXPECT_STREQ(Broadcast.Markdown(), "kept");
		EXPECT_EQ(Broadcast.Version(), 1);
	}
}

TEST(QmMarkdownBroadcastPayload, ExistingByteBudgetAndEmptyUpdateRemainValid)
{
	CQmMarkdownBroadcast Broadcast;
	qm_news::CReceiver Receiver;
	const std::string Maximum(CQmMarkdownBroadcast::MAX_BYTES, 'x');
	EXPECT_EQ(Receiver.LoadCache(Broadcast, Cache(2, Maximum), 1), EResult::APPLIED);
	EXPECT_EQ(Broadcast.Markdown(), Maximum);
	EXPECT_EQ(Receiver.LoadCache(Broadcast, Cache(3, Maximum + 'x'), 1), EResult::REJECTED);
	EXPECT_EQ(Broadcast.Version(), 2);
	EXPECT_EQ(Receiver.LoadCache(Broadcast, Cache(3, ""), 1), EResult::APPLIED);
	EXPECT_FALSE(Broadcast.HasMarkdown());
	EXPECT_EQ(Broadcast.Version(), 3);
}

TEST(QmMarkdownBroadcastPayload, FailedCacheCopyRetainsRootAndRetriesWithoutReparse)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("previous", 1));
	qm_news::CReceiver Receiver;
	const std::string Text(8192, 'x');
	auto Root = Cache(2, Text);
	ASSERT_NE(Root, nullptr);
	EResult Result;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Result = Receiver.LoadCache(Broadcast, std::move(Root), 1);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Result, EResult::RETRY);
	EXPECT_EQ(Root, nullptr);
	EXPECT_TRUE(Receiver.HasPendingCache());
	EXPECT_STREQ(Broadcast.Markdown(), "previous");
	EXPECT_EQ(Receiver.RetryCache(Broadcast), EResult::APPLIED);
	EXPECT_FALSE(Receiver.HasPendingCache());
	EXPECT_EQ(Broadcast.Markdown(), Text);
}

TEST(QmMarkdownBroadcastPayload, FreshSameVersionMessageSupersedesFailedOldCache)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("previous", 1));
	qm_news::CReceiver Receiver;
	auto Root = Cache(2, std::string(8192, 'x'));
	ASSERT_NE(Root, nullptr);
	EResult Result;
	{
		CPredictionAllocationFailure Fault;
		Result = Receiver.LoadCache(Broadcast, std::move(Root), 1);
	}
	ASSERT_EQ(Result, EResult::RETRY);
	ASSERT_TRUE(Receiver.HasPendingCache());
	EXPECT_EQ(Receiver.ApplyOwned(Broadcast, std::string("fresh"), 2), EResult::APPLIED);
	EXPECT_FALSE(Receiver.HasPendingCache());
	EXPECT_EQ(Receiver.RetryCache(Broadcast), EResult::REJECTED);
	EXPECT_STREQ(Broadcast.Markdown(), "fresh");
}

TEST(QmMarkdownBroadcastPayload, UnchangedFreshMessageAlsoDiscardsFailedOldCache)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("current", 2));
	qm_news::CReceiver Receiver;
	auto Root = Cache(2, std::string(8192, 'x'));
	EResult Result;
	{
		CPredictionAllocationFailure Fault;
		Result = Receiver.LoadCache(Broadcast, std::move(Root), 1);
	}
	ASSERT_EQ(Result, EResult::RETRY);
	EXPECT_EQ(Receiver.ApplyOwned(Broadcast, std::string("current"), 2), EResult::UNCHANGED);
	EXPECT_FALSE(Receiver.HasPendingCache());
	EXPECT_STREQ(Broadcast.Markdown(), "current");
}

TEST(QmMarkdownBroadcastPayload, ResponseResourceFailureKeepsBorrowedRootForCallerRetry)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("previous", 1));
	qm_news::CReceiver Receiver;
	const std::string Text(8192, 'x');
	auto Root = Json("{\"version\":2,\"markdown\":\"" + Text + "\"}");
	ASSERT_NE(Root, nullptr);
	EResult Result;
	{
		CPredictionAllocationFailure Fault;
		Result = Receiver.ApplyResponse(Broadcast, Root.get());
	}
	EXPECT_EQ(Result, EResult::RETRY);
	EXPECT_STREQ(Broadcast.Markdown(), "previous");
	EXPECT_NE(Root, nullptr);
	EXPECT_EQ(Receiver.ApplyResponse(Broadcast, Root.get()), EResult::APPLIED);
	Root.reset();
	EXPECT_EQ(Broadcast.Markdown(), Text);
}

TEST(QmMarkdownBroadcastPayload, MalformedResponseDoesNotDiscardPendingValidCache)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("previous", 1));
	qm_news::CReceiver Receiver;
	const std::string Text(8192, 'x');
	auto Root = Cache(2, Text);
	EResult Result;
	{
		CPredictionAllocationFailure Fault;
		Result = Receiver.LoadCache(Broadcast, std::move(Root), 1);
	}
	ASSERT_EQ(Result, EResult::RETRY);
	auto Invalid = Json(R"({"version":3,"markdown":"bad\u0000tail"})");
	EXPECT_EQ(Receiver.ApplyResponse(Broadcast, Invalid.get()), EResult::REJECTED);
	EXPECT_TRUE(Receiver.HasPendingCache());
	EXPECT_EQ(Receiver.RetryCache(Broadcast), EResult::APPLIED);
	EXPECT_EQ(Broadcast.Markdown(), Text);
}

TEST(QmMarkdownBroadcastPayload, ValidResponseSupersedesFailedOldCache)
{
	CQmMarkdownBroadcast Broadcast;
	ASSERT_TRUE(Broadcast.Apply("previous", 1));
	qm_news::CReceiver Receiver;
	auto Root = Cache(2, std::string(8192, 'x'));
	EResult Result;
	{
		CPredictionAllocationFailure Fault;
		Result = Receiver.LoadCache(Broadcast, std::move(Root), 1);
	}
	ASSERT_EQ(Result, EResult::RETRY);
	auto Response = Json(R"({"version":2,"markdown":"fresh response"})");
	ASSERT_NE(Response, nullptr);
	EXPECT_EQ(Receiver.ApplyResponse(Broadcast, Response.get()), EResult::APPLIED);
	EXPECT_FALSE(Receiver.HasPendingCache());
	EXPECT_EQ(Receiver.RetryCache(Broadcast), EResult::REJECTED);
	EXPECT_STREQ(Broadcast.Markdown(), "fresh response");
}
