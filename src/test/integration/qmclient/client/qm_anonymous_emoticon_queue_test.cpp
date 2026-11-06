#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <string>
#include <utility>

namespace
{
	std::string Frame(const std::string &Address, int Sequence)
	{
		return R"({"type":"emoticon","data":{"client_id":"other","player_name":"Name","player_id":3,"emoticon":5,"launch_mode":true,"server_address":")" +
		       Address + R"(","sequence":)" + std::to_string(Sequence) + "}}";
	}
}

TEST(QmAnonymousEmoticonQueue, LongWhitespaceAddressPassesRealParserNormalizerAndQueue)
{
	std::deque<SQmRealtimeMessage> Events;
	{
		const std::string Json = Frame(std::string(96 * 1024, ' ') + "EXAMPLE.COM:08303/", 1);
		ASSERT_LE(Json.size(), 128u * 1024u);
		SQmRealtimeMessage Message;
		ASSERT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Message));
		ASSERT_TRUE(Message.m_HasEmoticon);
		EXPECT_EQ(NormalizeQmServerAddress(Message.m_EmoticonServerAddress.c_str()), "example.com:8303");
		ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Message), 64));
	}
	ASSERT_EQ(Events.size(), 1u);
	EXPECT_EQ(Events.front().m_EmoticonPlayerName, "Name");
	EXPECT_EQ(Events.front().m_EmoticonSequence, 1u);
	ASSERT_NE(Events.front().m_pPayload, nullptr);
	EXPECT_STREQ(json_object_get(Events.front().m_pPayload.get(), "player_name")->u.string.ptr, "Name");
}

TEST(QmAnonymousEmoticonQueue, SuccessfulOverflowReleasesOnlyOldestParsedPayload)
{
	std::deque<SQmRealtimeMessage> Events;
	std::weak_ptr<const json_value> First;
	for(int Sequence = 1; Sequence <= 64; ++Sequence)
	{
		const auto Json = Frame("one:8303", Sequence);
		SQmRealtimeMessage Message;
		ASSERT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Message));
		if(Sequence == 1)
			First = Message.m_pPayload;
		ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Message), 64));
	}
	ASSERT_FALSE(First.expired());
	const auto Json = Frame("one:8303", 65);
	SQmRealtimeMessage Message;
	ASSERT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Message));
	ASSERT_TRUE(TryQueueQmRealtimeMessage(Events, std::move(Message), 64));
	EXPECT_TRUE(First.expired());
	ASSERT_EQ(Events.size(), 64u);
	EXPECT_EQ(Events.front().m_EmoticonSequence, 2u);
	EXPECT_EQ(Events.back().m_EmoticonSequence, 65u);
}
