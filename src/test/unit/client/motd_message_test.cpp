#include <game/client/components/motd_message.h>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace
{
	std::vector<std::string> Lines(std::string_view Message)
	{
		std::vector<std::string> vLines;
		ForEachMotdLine(Message, [&](std::string_view Line) { vLines.emplace_back(Line); });
		return vLines;
	}
}

TEST(MotdMessage, EmptyMessageTerminatesBufferAndHasNoLogLines)
{
	char aBuffer[8] = "stale";
	const auto Message = DecodeMotdMessage("", aBuffer, sizeof(aBuffer));
	EXPECT_TRUE(Message.empty());
	EXPECT_EQ(aBuffer[0], '\0');
	EXPECT_TRUE(Lines(Message).empty());
}

TEST(MotdMessage, PlainMessageFillsUsableCapacityAndKeepsTerminator)
{
	char aBuffer[5];
	const auto Message = DecodeMotdMessage("abcd", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Message, "abcd");
	EXPECT_EQ(aBuffer[4], '\0');
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"abcd"}));
}

TEST(MotdMessage, EscapedNewlineIsDecodedBeforeLogging)
{
	char aBuffer[32];
	const auto Message = DecodeMotdMessage("first\\nsecond", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Message, "first\nsecond");
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"first", "second"}));
}

TEST(MotdMessage, ActualNewlineUsesTheSameLineBoundaries)
{
	char aBuffer[32];
	const auto Message = DecodeMotdMessage("first\nsecond", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"first", "second"}));
}

TEST(MotdMessage, LeadingAndConsecutiveEmptyLinesArePreserved)
{
	char aBuffer[32];
	const auto Message = DecodeMotdMessage("\\nfirst\\n\\nlast", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"", "first", "", "last"}));
}

TEST(MotdMessage, TrailingNewlineDoesNotCreateAnExtraEmptyTail)
{
	char aBuffer[32];
	const auto Message = DecodeMotdMessage("line\\n", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"line"}));
}

TEST(MotdMessage, TrailingBackslashRemainsLiteral)
{
	char aBuffer[32];
	const auto Message = DecodeMotdMessage("line\\", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Message, "line\\");
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"line\\"}));
}

TEST(MotdMessage, RepeatedBackslashesKeepTheExistingBytewiseConversion)
{
	char aBuffer[32];
	const auto Message = DecodeMotdMessage("\\\\n", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Message, "\\\n");
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"\\"}));
}

TEST(MotdMessage, SingleByteDestinationOnlyStoresTerminator)
{
	char aBuffer[1] = {'x'};
	const auto Message = DecodeMotdMessage("abc\\n", aBuffer, sizeof(aBuffer));
	EXPECT_TRUE(Message.empty());
	EXPECT_EQ(aBuffer[0], '\0');
	EXPECT_TRUE(Lines(Message).empty());
}

TEST(MotdMessage, ZeroCapacityDoesNotAccessDestination)
{
	EXPECT_TRUE(DecodeMotdMessage("text", nullptr, 0).empty());
}

TEST(MotdMessage, LongMessageStopsBeforeTheTerminatorSlot)
{
	std::array<char, 7> aGuarded;
	aGuarded.fill('#');
	const auto Message = DecodeMotdMessage("abcdefghi", aGuarded.data() + 1, 5);
	EXPECT_EQ(Message, "abcd");
	EXPECT_EQ(aGuarded[5], '\0');
	EXPECT_EQ(aGuarded.front(), '#');
	EXPECT_EQ(aGuarded.back(), '#');
}

TEST(MotdMessage, NewlineAtFormerOverflowSlotCannotCreateAnOutOfBoundsTail)
{
	std::array<char, 7> aGuarded;
	aGuarded.fill('#');
	const auto Message = DecodeMotdMessage("abcd\\n", aGuarded.data() + 1, 5);
	EXPECT_EQ(Message, "abcd");
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"abcd"}));
	EXPECT_EQ(aGuarded[5], '\0');
	EXPECT_EQ(aGuarded.back(), '#');
}

TEST(MotdMessage, LastUsableByteMayBeNewlineWithoutReadingPastTheTerminator)
{
	char aBuffer[6];
	const auto Message = DecodeMotdMessage("abcd\\ntruncated", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Message, "abcd\n");
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"abcd"}));
	EXPECT_EQ(aBuffer[5], '\0');
}

TEST(MotdMessage, ShorterReplacementDoesNotLogPreviousBufferContents)
{
	char aBuffer[32];
	DecodeMotdMessage("old\\nlonger message", aBuffer, sizeof(aBuffer));
	const auto Message = DecodeMotdMessage("new", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Message, "new");
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"new"}));
	EXPECT_STREQ(aBuffer, "new");
}

TEST(MotdMessage, LineIterationUsesExplicitLengthWithoutRequiringNullTermination)
{
	const char aBytes[] = {'a', '\n', 'b', 'c'};
	EXPECT_EQ(Lines(std::string_view(aBytes, sizeof(aBytes))), (std::vector<std::string>{"a", "bc"}));
}

TEST(MotdMessage, MultibyteTextIsPreservedWhenItFits)
{
	char aBuffer[64];
	const auto Message = DecodeMotdMessage("欢迎\\n玩家", aBuffer, sizeof(aBuffer));
	EXPECT_EQ(Message, "欢迎\n玩家");
	EXPECT_EQ(Lines(Message), (std::vector<std::string>{"欢迎", "玩家"}));
}
