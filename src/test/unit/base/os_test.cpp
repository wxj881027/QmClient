// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/os.h>

#include <gtest/gtest.h>
#include <test/test.h>

#if defined(CONF_FAMILY_WINDOWS)
TEST(Os, CmdlineOwnerReleasesStorageAfterAnArgumentIsReplaced)
{
	int Argc = 1;
	const char *apOriginal[] = {"original", nullptr};
	const char **ppArgv = apOriginal;
	const CCmdlineFix Cmdline(&Argc, &ppArgv);
	ASSERT_GT(Argc, 0);
	ASSERT_NE(ppArgv, apOriginal);
	EXPECT_EQ(ppArgv[Argc], nullptr);

	ppArgv[0] = apOriginal[0];
}

TEST(Os, OpenLinkRejectsInvalidUtf8)
{
	EXPECT_EQ(os_open_link("\xF0"), 0);
}
#endif

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
