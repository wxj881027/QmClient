#include <gtest/gtest.h>
#include <qm/setup/setup_options.h>

TEST(QmSetupResult, FileErrorAbortIsNotReportedAsUserCancellation)
{
	EXPECT_FALSE(QmSetup::IsCancelled(5, false));
	EXPECT_TRUE(QmSetup::IsCancelled(5, true));
	EXPECT_TRUE(QmSetup::IsCancelled(2, false));
	EXPECT_FALSE(QmSetup::IsCancelled(0, true));
	EXPECT_FALSE(QmSetup::IsCancelled(4, true));
}

TEST(QmSetupOptions, InteractiveInstallStartsWithNativeUi)
{
	const auto Options = QmSetup::ParseOptions({});
	EXPECT_TRUE(Options.m_UseNativeUi);
	EXPECT_TRUE(Options.m_Directory.empty());
}

TEST(QmSetupOptions, DirectoryOverrideRemainsEditableAndUsesLastArgument)
{
	const auto Options = QmSetup::ParseOptions({L"/DIR=C:\\old", L"/Dir=D:\\游戏 客户端", L"/NORESTART", L"/NOICONS"});
	EXPECT_TRUE(Options.m_UseNativeUi);
	EXPECT_EQ(Options.m_Directory, L"D:\\游戏 客户端");
}

TEST(QmSetupOptions, AutomaticUpdatesPreserveAllOriginalParameters)
{
	const std::vector<std::wstring> vArguments{L"/SILENT", L"/SUPPRESSMSGBOXES", L"/NORESTART", L"/CLOSEAPPLICATIONS", L"/NORESTARTAPPLICATIONS", L"/DIR=D:\\已有 客户端"};
	const auto Options = QmSetup::ParseOptions(vArguments);
	EXPECT_FALSE(Options.m_UseNativeUi);
	EXPECT_EQ(Options.m_vArguments, vArguments);
}

TEST(QmSetupOptions, AdvancedAndUnattendedCommandsUseTheOriginalEngine)
{
	for(const auto *pArgument : {L"/VERYSILENT", L"/silent", L"/HELP", L"/?", L"/LOADINF=previous.ini", L"/SAVEINF=next.ini", L"/TASKS=desktopicon", L"/LANG=en", L"/LOG", L"/future-option"})
	{
		SCOPED_TRACE(QmSetup::Lower(pArgument));
		const auto Options = QmSetup::ParseOptions({pArgument});
		EXPECT_FALSE(Options.m_UseNativeUi);
		EXPECT_EQ(Options.m_vArguments, std::vector<std::wstring>{pArgument});
	}
}

TEST(QmSetupOptions, EditedPathAndUncheckedDesktopOverridePreviousChoices)
{
	const auto Options = QmSetup::ParseOptions({L"/DIR=C:\\old", L"/NOICONS", L"/LOG=D:\\日志 文件.log"});
	const auto Arguments = QmSetup::NativeArguments(Options, L"D:\\新的 安装", false);
	const std::vector<std::wstring> Expected{L"/NOICONS", L"/LOG=D:\\日志 文件.log", L"/VERYSILENT", L"/SUPPRESSMSGBOXES", L"/SP-", L"/NORESTART", L"/NORESTARTAPPLICATIONS", L"/DIR=D:\\新的 安装", L"/TASKS="};
	EXPECT_EQ(Arguments, Expected);
	EXPECT_EQ(Options.m_Directory, L"C:\\old");
}

TEST(QmSetupOptions, CheckedDesktopRequestsTheExistingInnoTask)
{
	const auto Arguments = QmSetup::NativeArguments({}, L"C:\\QmClient", true);
	EXPECT_EQ(Arguments.back(), L"/TASKS=desktopicon");
}

TEST(QmSetupArguments, QuotesWhitespaceAndTrailingDirectorySeparator)
{
	EXPECT_EQ(QmSetup::QuoteArgument(L"D:\\游戏 客户端\\"), L"\"D:\\游戏 客户端\\\\\"");
	EXPECT_EQ(QmSetup::QuoteArgument(L""), L"\"\"");
}

TEST(QmSetupArguments, EscapesEmbeddedQuotesWithoutChangingOrdinarySlashes)
{
	EXPECT_EQ(QmSetup::QuoteArgument(LR"(a\"b)"), LR"("a\\\"b")");
	EXPECT_EQ(QmSetup::CommandLine({L"/DIR=D:\\游戏 客户端", L"/NORESTART"}), L"\"/DIR=D:\\游戏 客户端\" \"/NORESTART\"");
}
