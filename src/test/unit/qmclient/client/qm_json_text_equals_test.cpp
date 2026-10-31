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

TEST(QmJsonTextEquals, CompleteAsciiUnicodeAndEmptyStringsCanMatch)
{
	for(std::string Text : {"one:8303", "中文😀", ""})
	{
		SCOPED_TRACE(Text);
		const auto Value = String(Text);
		EXPECT_TRUE(qm_json::TextEquals(&Value, Text.c_str()));
	}
}

TEST(QmJsonTextEquals, NullSuffixCannotMatchPrefix)
{
	std::string Text("one\0extra", 9);
	const auto Value = String(Text);
	EXPECT_FALSE(qm_json::TextEquals(&Value, "one"));
	EXPECT_FALSE(qm_json::TextEquals(&Value, Text.c_str()));
}

TEST(QmJsonTextEquals, DifferentLengthCaseAndWhitespaceDoNotMatch)
{
	std::string Text = "one:8303";
	const auto Value = String(Text);
	for(const char *pExpected : {"one", "one:8303/", "ONE:8303", " one:8303", "one:8303 "})
	{
		SCOPED_TRACE(pExpected);
		EXPECT_FALSE(qm_json::TextEquals(&Value, pExpected));
	}
}

TEST(QmJsonTextEquals, InvalidUtf8CannotMatchEvenIdenticalBytes)
{
	for(std::string Text : {"\x80", "\xe4\xbd", "\xc0\xaf", "\xed\xa0\x80"})
	{
		SCOPED_TRACE(::testing::PrintToString(Text));
		const auto Value = String(Text);
		EXPECT_FALSE(qm_json::TextEquals(&Value, Text.c_str()));
	}
}

TEST(QmJsonTextEquals, MissingWrongTypeOrNullStorageDoesNotMatch)
{
	EXPECT_FALSE(qm_json::TextEquals(nullptr, ""));
	json_value Value;
	Value.type = json_integer;
	EXPECT_FALSE(qm_json::TextEquals(&Value, ""));
	Value.type = json_string;
	EXPECT_FALSE(qm_json::TextEquals(&Value, ""));
}

TEST(QmJsonTextEquals, NullExpectedStringDoesNotMatch)
{
	std::string Text;
	const auto Value = String(Text);
	EXPECT_FALSE(qm_json::TextEquals(&Value, nullptr));
}
