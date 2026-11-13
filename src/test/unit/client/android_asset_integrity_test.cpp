#include <android/asset_integrity.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

namespace
{
	const std::string HASH(64, '0');
}

TEST(AndroidAssetIntegrityLine, HeaderHasNoFilenameAndPreservesDigest)
{
	android_assets::CIntegrityFileLine Line;
	ASSERT_TRUE(android_assets::ParseIntegrityLine(HASH, true, Line));
	EXPECT_STREQ(Line.m_aFilename, "");
	EXPECT_EQ(Line.m_Sha256, SHA256_DIGEST{});
}

TEST(AndroidAssetIntegrityLine, BodyPreservesUnicodeAndSpacesInFilename)
{
	android_assets::CIntegrityFileLine Line;
	ASSERT_TRUE(android_assets::ParseIntegrityLine("data/测试/a file .png " + HASH, false, Line));
	EXPECT_STREQ(Line.m_aFilename, "data/测试/a file .png");
}

TEST(AndroidAssetIntegrityLine, AbsoluteOutsideParentAndAmbiguousSegmentsAreRejected)
{
	for(const char *pPath : {"/data/file", "../user/settings.cfg", "user/settings.cfg", "data/../user/settings.cfg", "data/a/../../user/settings.cfg", "data/./file", "data//file", "data/", "data/a/", "C:/data/file", "\\data\\file"})
	{
		SCOPED_TRACE(pPath);
		android_assets::CIntegrityFileLine Line;
		EXPECT_FALSE(android_assets::ParseIntegrityLine(std::string(pPath) + " " + HASH, false, Line));
	}
}

TEST(AndroidAssetIntegrityLine, PosixFilenameCharactersRemainLiteral)
{
	android_assets::CIntegrityFileLine Line;
	ASSERT_TRUE(android_assets::ParseIntegrityLine("data/a\\b: c.png " + HASH, false, Line));
	EXPECT_STREQ(Line.m_aFilename, "data/a\\b: c.png");
}

TEST(AndroidAssetIntegrityLine, HashMustBeCompleteAndExactLength)
{
	for(const std::string &Hash : {HASH.substr(1), HASH + "0", HASH + "garbage", std::string(64, 'z')})
	{
		SCOPED_TRACE(Hash);
		android_assets::CIntegrityFileLine Line;
		EXPECT_FALSE(android_assets::ParseIntegrityLine(Hash, true, Line));
		EXPECT_FALSE(android_assets::ParseIntegrityLine("data/file " + Hash, false, Line));
	}
}

TEST(AndroidAssetIntegrityLine, UppercaseDigestRetainsExistingAcceptance)
{
	android_assets::CIntegrityFileLine Line;
	ASSERT_TRUE(android_assets::ParseIntegrityLine("data/file " + std::string(64, 'A'), false, Line));
	for(const auto Byte : Line.m_Sha256.data)
		EXPECT_EQ(Byte, 0xaa);
}

TEST(AndroidAssetIntegrityLine, FilenameCapacityIncludesTerminatorAndDoesNotTruncate)
{
	const std::string Path = "data/" + std::string(IO_MAX_PATH_LENGTH - 6, 'a');
	android_assets::CIntegrityFileLine Line;
	ASSERT_TRUE(android_assets::ParseIntegrityLine(Path + " " + HASH, false, Line));
	EXPECT_STREQ(Line.m_aFilename, Path.c_str());
	EXPECT_FALSE(android_assets::ParseIntegrityLine(Path + "a " + HASH, false, Line));
	EXPECT_STREQ(Line.m_aFilename, Path.c_str());
}

TEST(AndroidAssetIntegrityLine, InvalidLineLeavesPreviousOutputUntouched)
{
	android_assets::CIntegrityFileLine Line;
	ASSERT_TRUE(android_assets::ParseIntegrityLine("data/previous " + HASH, false, Line));
	const auto Before = Line;
	EXPECT_FALSE(android_assets::ParseIntegrityLine("../outside " + HASH, false, Line));
	EXPECT_STREQ(Line.m_aFilename, Before.m_aFilename);
	EXPECT_EQ(Line.m_Sha256, Before.m_Sha256);
}

TEST(AndroidAssetIntegrityLine, EmbeddedNullAndLineBreaksCannotHideFilenameSuffix)
{
	android_assets::CIntegrityFileLine Line;
	EXPECT_FALSE(android_assets::ParseIntegrityLine(std::string("data/a\0x", 8) + " " + HASH, false, Line));
	EXPECT_FALSE(android_assets::ParseIntegrityLine("data/a\nb " + HASH, false, Line));
	EXPECT_FALSE(android_assets::ParseIntegrityLine("data/a\rb " + HASH, false, Line));
}

TEST(AndroidAssetIntegrityLine, InvalidUtf8FilenameIsRejected)
{
	android_assets::CIntegrityFileLine Line;
	EXPECT_FALSE(android_assets::ParseIntegrityLine(std::string("data/") + char(0xff) + " " + HASH, false, Line));
}

TEST(AndroidAssetIntegrityLine, FilenameIsNotAllowedInAggregateHeader)
{
	android_assets::CIntegrityFileLine Line;
	EXPECT_FALSE(android_assets::ParseIntegrityLine("data/file " + HASH, true, Line));
}

TEST(AndroidAssetIntegrityLine, BodyRequiresFilenameAndDigest)
{
	android_assets::CIntegrityFileLine Line;
	EXPECT_FALSE(android_assets::ParseIntegrityLine(HASH, false, Line));
	EXPECT_FALSE(android_assets::ParseIntegrityLine("data/file", false, Line));
}
