#include <game/client/components/qmclient/qm_json_fields.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	json_value String(std::string &Text)
	{
		json_value Value;
		Value.type = json_string;
		Value.u.string.ptr = Text.data();
		Value.u.string.length = static_cast<unsigned>(Text.size());
		return Value;
	}
}

TEST(QmUsersJson, AcceptsCompleteAsciiAndMultibyteText)
{
	for(std::string Text : {"ASCII", "中文😀", "one:8303", ""})
	{
		SCOPED_TRACE(Text);
		const auto Value = String(Text);
		EXPECT_TRUE(qm_json::Text(&Value));
	}
}

TEST(QmUsersJson, EmptyPolicyDoesNotRejectNonemptyText)
{
	std::string Empty, Name = "Name";
	const auto EmptyValue = String(Empty), NameValue = String(Name);
	EXPECT_TRUE(qm_json::Text(&EmptyValue, true));
	EXPECT_FALSE(qm_json::Text(&EmptyValue, false));
	EXPECT_TRUE(qm_json::Text(&NameValue, false));
}

TEST(QmUsersJson, EmbeddedNullAtAnyPositionIsRejected)
{
	for(std::string Text : {std::string("\0name", 5), std::string("na\0me", 5), std::string("name\0", 5)})
	{
		const auto Value = String(Text);
		EXPECT_FALSE(qm_json::Text(&Value));
	}
}

TEST(QmUsersJson, InvalidUtf8IncludingTruncationOverlongAndSurrogatesIsRejected)
{
	for(std::string Text : {std::string("\x80"), std::string("\xe4\xbd"), std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80")})
	{
		const auto Value = String(Text);
		EXPECT_FALSE(qm_json::Text(&Value));
	}
}

TEST(QmUsersJson, MissingWrongTypeAndNullStringStorageAreRejected)
{
	json_value Value;
	Value.type = json_integer;
	EXPECT_FALSE(qm_json::Text(nullptr));
	EXPECT_FALSE(qm_json::Text(&Value));
	Value.type = json_string;
	EXPECT_FALSE(qm_json::Text(&Value));
}

TEST(QmUsersJson, FieldLookupRejectsNonObjectsAndReturnsNoneForMissingKeys)
{
	json_value Value;
	Value.type = json_array;
	EXPECT_EQ(qm_json::Field(nullptr, "users"), &json_value_none);
	EXPECT_EQ(qm_json::Field(&Value, "users"), &json_value_none);
	Value.type = json_object;
	EXPECT_EQ(qm_json::Field(&Value, "users"), &json_value_none);
}

TEST(QmUsersJson, UnknownNullSuffixedMemberCannotShadowExactMember)
{
	char aUnknown[] = "users\0extra", aExact[] = "users";
	json_value Wrong, Right, Object;
	json_object_entry aMembers[2]{};
	aMembers[0].name = aUnknown;
	aMembers[0].name_length = sizeof(aUnknown) - 1;
	aMembers[0].value = &Wrong;
	aMembers[1].name = aExact;
	aMembers[1].name_length = sizeof(aExact) - 1;
	aMembers[1].value = &Right;
	Object.type = json_object;
	Object.u.object.values = aMembers;
	Object.u.object.length = 2;
	EXPECT_EQ(qm_json::Field(&Object, "users"), &Right);
	Object.u.object.length = 1;
	EXPECT_EQ(qm_json::Field(&Object, "users"), &json_value_none);
}

TEST(QmUsersJson, ExactDuplicateMembersKeepExistingFirstMatchRule)
{
	char aName[] = "users";
	json_value First, Second, Object;
	json_object_entry aMembers[2]{};
	for(auto &Member : aMembers)
	{
		Member.name = aName;
		Member.name_length = sizeof(aName) - 1;
	}
	aMembers[0].value = &First;
	aMembers[1].value = &Second;
	Object.type = json_object;
	Object.u.object.values = aMembers;
	Object.u.object.length = 2;
	EXPECT_EQ(qm_json::Field(&Object, "users"), &First);
}
