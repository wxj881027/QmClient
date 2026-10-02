#include <game/client/components/qmclient/voice/voice_core.h>

#include <gtest/gtest.h>

TEST(VoiceSequence, DeltaNormal)
{
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(10, 5), 5);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(100, 0), 100);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(5, 10), -5);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(0, 100), -100);
}

TEST(VoiceSequence, DeltaSame)
{
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(42, 42), 0);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(0, 0), 0);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(65535, 65535), 0);
}

TEST(VoiceSequence, DeltaWrapForward)
{
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(5, 65530), 11);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(0, 65535), 1);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(100, 65436), 200);
}

TEST(VoiceSequence, DeltaWrapBackward)
{
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(65530, 5), -11);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(65535, 0), -1);
	EXPECT_EQ(VoiceUtils::VoiceSeqDelta(65436, 100), -200);
}

TEST(VoiceSequence, LessNormal)
{
	EXPECT_TRUE(VoiceUtils::VoiceSeqLess(5, 10));
	EXPECT_TRUE(VoiceUtils::VoiceSeqLess(0, 1));
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(10, 5));
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(1, 0));
}

TEST(VoiceSequence, LessEqual)
{
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(42, 42));
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(0, 0));
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(65535, 65535));
}

TEST(VoiceSequence, LessWrap)
{
	EXPECT_TRUE(VoiceUtils::VoiceSeqLess(65530, 5));
	EXPECT_TRUE(VoiceUtils::VoiceSeqLess(65535, 1));
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(5, 65530));
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(1, 65535));
}

TEST(VoiceSequence, LessHalfWrap)
{
	EXPECT_TRUE(VoiceUtils::VoiceSeqLess(0, 32768));
	EXPECT_TRUE(VoiceUtils::VoiceSeqLess(32768, 0));
	EXPECT_FALSE(VoiceUtils::VoiceSeqLess(0, 32769));
	EXPECT_TRUE(VoiceUtils::VoiceSeqLess(32769, 0));
}
