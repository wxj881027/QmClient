// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <gtest/gtest.h>
#include <test/test.h>

TEST(Os, VersionStr)
{
	char aVersion[128];
	ASSERT_TRUE(os_version_str(aVersion, sizeof(aVersion)));
	EXPECT_STRNE(aVersion, "");
}

TEST(Os, LocaleStr)
{
	char aLocale[128];
	os_locale_str(aLocale, sizeof(aLocale));
	EXPECT_STRNE(aLocale, "");
}

TEST(Os, LocaleZeroCapacityDoesNotWrite)
{
	char Guard = '!';
	os_locale_str(&Guard, 0);
	os_locale_str(nullptr, 0);
	EXPECT_EQ(Guard, '!');
}

TEST(Os, LocaleSingleByteBufferPreservesGuard)
{
	char aOutput[] = {'!', '?'};
	os_locale_str(aOutput, 1);
	EXPECT_EQ(aOutput[0], '\0');
	EXPECT_EQ(aOutput[1], '?');
}
