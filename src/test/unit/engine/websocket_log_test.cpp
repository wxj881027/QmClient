#include <engine/shared/websocket_log.h>

#include <gtest/gtest.h>

TEST(WebsocketLogLine, NullAndEmptyInputBecomeEmptyText)
{
	char aBuffer[16] = "old";
	CopyWebsocketLogLine(aBuffer, nullptr);
	EXPECT_STREQ(aBuffer, "");
	CopyWebsocketLogLine(aBuffer, "");
	EXPECT_STREQ(aBuffer, "");
}

TEST(WebsocketLogLine, TimestampWithEmptyBodyDoesNotReadBeforeBuffer)
{
	for(const char *pLine : {"[time] ", "[time] \n", "[time] \r\n"})
	{
		SCOPED_TRACE(pLine);
		char aBuffer[16];
		CopyWebsocketLogLine(aBuffer, pLine);
		EXPECT_STREQ(aBuffer, "");
	}
}

TEST(WebsocketLogLine, SingleCharacterBodySurvivesOptionalNewline)
{
	for(const char *pLine : {"[time] x", "[time] x\n", "[time] x\r\n"})
	{
		SCOPED_TRACE(pLine);
		char aBuffer[16];
		CopyWebsocketLogLine(aBuffer, pLine);
		EXPECT_STREQ(aBuffer, "x");
	}
}

TEST(WebsocketLogLine, CommonLineEndingsAreRemovedAtTheTail)
{
	for(const char *pLine : {"[time] hello\n", "[time] hello\r\n", "[time] hello\r"})
	{
		SCOPED_TRACE(pLine);
		char aBuffer[16];
		CopyWebsocketLogLine(aBuffer, pLine);
		EXPECT_STREQ(aBuffer, "hello");
	}
}

TEST(WebsocketLogLine, MissingTimestampKeepsMessageInsteadOfAsserting)
{
	char aBuffer[32];
	CopyWebsocketLogLine(aBuffer, "plain message\n");
	EXPECT_STREQ(aBuffer, "plain message");
}

TEST(WebsocketLogLine, EmbeddedCarriageReturnIsNotMistakenForTrailingCrLf)
{
	char aBuffer[16];
	CopyWebsocketLogLine(aBuffer, "[time] a\rb");
	EXPECT_STREQ(aBuffer, "a\rb");
}

TEST(WebsocketLogLine, TruncationKeepsTerminatorAndNeighborBytes)
{
	struct SGuarded
	{
		char m_Before = 'L';
		char m_aText[4];
		char m_After = 'R';
	} Guarded;
	CopyWebsocketLogLine(Guarded.m_aText, "[time] abcdef\n");
	EXPECT_STREQ(Guarded.m_aText, "abc");
	EXPECT_EQ(Guarded.m_Before, 'L');
	EXPECT_EQ(Guarded.m_After, 'R');
}

TEST(WebsocketLogLine, SingleByteOutputIsTerminated)
{
	char aBuffer[1] = {'x'};
	CopyWebsocketLogLine(aBuffer, "[time] message\r\n");
	EXPECT_EQ(aBuffer[0], '\0');
}
