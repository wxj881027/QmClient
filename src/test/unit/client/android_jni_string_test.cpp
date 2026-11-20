#include <android/jni_calls.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

TEST(AndroidJniString, EmptyTextHasNoCodeUnits)
{
	EXPECT_TRUE(android_jni::Utf16("").empty());
}

TEST(AndroidJniString, AsciiAndBmpTextRetainCodepoints)
{
	EXPECT_EQ(android_jni::Utf16("A中"), (std::vector<std::uint16_t>{0x41, 0x4e2d}));
}

TEST(AndroidJniString, SupplementaryCharacterUsesSurrogatePair)
{
	EXPECT_EQ(android_jni::Utf16("x\xf0\x9f\x98\x80"
				     "y"),
		(std::vector<std::uint16_t>{'x', 0xd83d, 0xde00, 'y'}));
}

TEST(AndroidJniString, HighestUnicodeScalarHasCompletePair)
{
	EXPECT_EQ(android_jni::Utf16("\xf4\x8f\xbf\xbf"), (std::vector<std::uint16_t>{0xdbff, 0xdfff}));
}

TEST(AndroidJniString, InvalidByteUsesExistingMediaReplacementPolicy)
{
	EXPECT_EQ(android_jni::Utf16("\xff"), (std::vector<std::uint16_t>{0xfffd}));
}

TEST(AndroidJniString, CStringTerminatorDoesNotBecomeJavaPayload)
{
	EXPECT_EQ(android_jni::Utf16("a\0b"), (std::vector<std::uint16_t>{'a'}));
}
