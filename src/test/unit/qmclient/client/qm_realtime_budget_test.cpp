#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_realtime.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	bool Parse(const std::string &Json, SQmRealtimeMessage &Message)
	{
		return ParseQmRealtimeMessage(Json.data(), Json.size(), Message);
	}

	std::string Array(const std::string &Value, size_t Count)
	{
		std::string Json = R"({"type":"sponsors","data":{"unknown":[)";
		Json.reserve(Json.size() + (Value.size() + 1) * Count + 3);
		for(size_t Index = 0; Index < Count; ++Index)
		{
			if(Index)
				Json += ',';
			Json += Value;
		}
		return Json + "]}}";
	}

	void ExpectEmpty(const SQmRealtimeMessage &Message)
	{
		EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::INVALID);
		EXPECT_TRUE(Message.m_Type.empty());
		EXPECT_FALSE(Message.m_HasRealtimeData);
		EXPECT_EQ(Message.m_pPayload, nullptr);
		EXPECT_EQ(Message.m_pTitlePayload, nullptr);
	}
}

TEST(QmRealtimeBudget, InputAtExistingTransportByteLimitRemainsAccepted)
{
	std::string Json = R"({"type":"ping"})";
	Json.resize(QMCLIENT_REALTIME_MAX_BYTES, ' ');
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(Json, Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::PING);
}

TEST(QmRealtimeBudget, OversizedInputRejectsAndReleasesPreviousOutput)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"sponsors","data":{}})", Message));
	std::weak_ptr<const json_value> Previous = Message.m_pPayload;
	ASSERT_FALSE(Previous.expired());
	std::string Json = R"({"type":"ping"})";
	Json.resize(QMCLIENT_REALTIME_MAX_BYTES + 1, ' ');
	EXPECT_FALSE(Parse(Json, Message));
	ExpectEmpty(Message);
	EXPECT_TRUE(Previous.expired());
}

TEST(QmRealtimeBudget, LargeOpaqueStringFitsExistingInputAndNewTreeBudgets)
{
	const std::string Prefix = R"({"type":"sponsors","data":{"opaque":")", Suffix = R"("}})";
	const size_t TextSize = QMCLIENT_REALTIME_MAX_BYTES - Prefix.size() - Suffix.size();
	SQmRealtimeMessage Message;
	{
		const std::string Json = Prefix + std::string(TextSize, 'x') + Suffix;
		ASSERT_EQ(Json.size(), QMCLIENT_REALTIME_MAX_BYTES);
		ASSERT_TRUE(Parse(Json, Message));
	}
	ASSERT_TRUE(Message.m_HasRealtimeData);
	ASSERT_NE(Message.m_pPayload, nullptr);
	const json_value *pText = json_object_get(Message.m_pPayload.get(), "opaque");
	ASSERT_EQ(pText->type, json_string);
	EXPECT_EQ(pText->u.string.length, TextSize);
	EXPECT_EQ(pText->u.string.ptr[TextSize - 1], 'x');
}

TEST(QmRealtimeBudget, DenseValuesUnderByteLimitCannotExceedTreeBudget)
{
	// 单个值至少占 sizeof(json_value)，使节点本体已超限，不依赖固定 ABI 大小。
	const size_t Count = QMCLIENT_REALTIME_JSON_MEMORY_BUDGET / sizeof(json_value) + 1;
	const auto Json = Array("0", Count);
	ASSERT_LE(Json.size(), QMCLIENT_REALTIME_MAX_BYTES);
	SQmRealtimeMessage Message;
	EXPECT_FALSE(Parse(Json, Message));
	ExpectEmpty(Message);
}

TEST(QmRealtimeBudget, OrdinaryDenseUnknownArrayRemainsAvailableToConsumer)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(Array("0", 2048), Message));
	ASSERT_TRUE(Message.m_HasRealtimeData);
	ASSERT_NE(Message.m_pPayload, nullptr);
	const json_value *pArray = json_object_get(Message.m_pPayload.get(), "unknown");
	ASSERT_EQ(pArray->type, json_array);
	EXPECT_EQ(pArray->u.array.length, 2048u);
}

TEST(QmRealtimeBudget, ObjectMemberStorageCountsTowardSameTreeBudget)
{
	// 即使值节点本体未耗尽预算，成员条目也必须计入同一上限。
	const size_t Count = QMCLIENT_REALTIME_JSON_MEMORY_BUDGET / (sizeof(json_value) + sizeof(json_object_entry)) + 1;
	std::string Json = R"({"type":"sponsors","data":{"unknown":{)";
	Json.reserve(Json.size() + Count * 6 + 3);
	for(size_t Index = 0; Index < Count; ++Index)
	{
		if(Index)
			Json += ',';
		Json += R"("a":0)";
	}
	Json += "}}}";
	ASSERT_LE(Json.size(), QMCLIENT_REALTIME_MAX_BYTES);
	SQmRealtimeMessage Message;
	EXPECT_FALSE(Parse(Json, Message));
	ExpectEmpty(Message);
}

TEST(QmRealtimeBudget, StringAndArrayStorageShareNodeBudget)
{
	const size_t Count = QMCLIENT_REALTIME_JSON_MEMORY_BUDGET / (sizeof(json_value) + sizeof(json_value *) + 9) + 1;
	const auto Json = Array(R"("abcdefgh")", Count);
	ASSERT_LE(Json.size(), QMCLIENT_REALTIME_MAX_BYTES);
	SQmRealtimeMessage Message;
	EXPECT_FALSE(Parse(Json, Message));
	ExpectEmpty(Message);
}

TEST(QmRealtimeBudget, NormalMessageCanParseAfterTreeBudgetFailure)
{
	SQmRealtimeMessage Message;
	ASSERT_FALSE(Parse(Array("0", QMCLIENT_REALTIME_JSON_MEMORY_BUDGET / sizeof(json_value) + 1), Message));
	ExpectEmpty(Message);
	ASSERT_TRUE(Parse(R"({"type":"state","data":{"online_users":7}})", Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::STATE);
	EXPECT_TRUE(Message.m_HasOnlineUsers);
	EXPECT_EQ(Message.m_OnlineUsers, 7);
}
