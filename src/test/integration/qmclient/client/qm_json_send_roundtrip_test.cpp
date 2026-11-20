#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_json_send.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	using TJsonOwner = std::unique_ptr<json_value, decltype(&json_value_free)>;
}

TEST(QmJsonSendRoundtrip, EscapedUnicodeTextArrivesAsCompleteJson)
{
	const std::string Text = "玩家\"\\\n\r\t";
	std::string Sent;
	ASSERT_TRUE(TrySendQmJsonMessage([&](CJsonStringWriter &Writer) {
		Writer.BeginObject();
		Writer.WriteAttribute("text");
		Writer.WriteStrValue(Text.c_str());
		Writer.EndObject(); }, [&](const char *pData, size_t Size) { Sent.assign(pData, Size); return true; }));
	TJsonOwner Root(JsonParse(Sent.data(), Sent.size()), json_value_free);
	ASSERT_NE(Root, nullptr);
	const json_value *pText = json_object_get(Root.get(), "text");
	ASSERT_EQ(pText->type, json_string);
	EXPECT_EQ(std::string(pText->u.string.ptr, pText->u.string.length), Text);
}

TEST(QmJsonSendRoundtrip, NestedArraysIntegersAndBooleansKeepTheirValues)
{
	std::string Sent;
	ASSERT_TRUE(TrySendQmJsonMessage([](CJsonStringWriter &Writer) {
		Writer.BeginObject();
		Writer.WriteAttribute("players");
		Writer.BeginArray();
		Writer.BeginObject();
		Writer.WriteAttribute("id");
		Writer.WriteIntValue(63);
		Writer.WriteAttribute("dummy");
		Writer.WriteBoolValue(false);
		Writer.EndObject();
		Writer.EndArray();
		Writer.EndObject(); }, [&](const char *pData, size_t Size) { Sent.assign(pData, Size); return true; }));
	TJsonOwner Root(JsonParse(Sent.data(), Sent.size()), json_value_free);
	ASSERT_NE(Root, nullptr);
	const json_value *pPlayers = json_object_get(Root.get(), "players");
	ASSERT_EQ(json_array_length(pPlayers), 1);
	const json_value *pPlayer = json_array_get(pPlayers, 0);
	EXPECT_EQ(json_int_get(json_object_get(pPlayer, "id")), 63);
	const json_value *pDummy = json_object_get(pPlayer, "dummy");
	ASSERT_EQ(pDummy->type, json_boolean);
	EXPECT_FALSE(json_boolean_get(pDummy));
}

TEST(QmJsonSendRoundtrip, LongEscapedPayloadIsNotTruncatedAtAdmission)
{
	const std::string Text(32768, '\\');
	std::string Sent;
	ASSERT_TRUE(TrySendQmJsonMessage([&](CJsonStringWriter &Writer) { Writer.WriteStrValue(Text.c_str()); }, [&](const char *pData, size_t Size) { Sent.assign(pData, Size); return true; }));
	TJsonOwner Root(JsonParse(Sent.data(), Sent.size()), json_value_free);
	ASSERT_NE(Root, nullptr);
	ASSERT_EQ(Root->type, json_string);
	EXPECT_EQ(std::string(Root->u.string.ptr, Root->u.string.length), Text);
}
