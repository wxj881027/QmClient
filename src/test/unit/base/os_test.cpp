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
