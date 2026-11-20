#include <engine/client/qm_festive_report_text.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace
{
	struct SMemoryField
	{
		const wchar_t *m_pPrefix;
		const wchar_t *m_pLabel;
	};
	constexpr std::array<SMemoryField, 4> gs_aMemoryFields{{
		{L"Instruction bytes near RIP:", L"RIP 附近指令字节："},
		{L"Stack bytes near RSP:", L"RSP 附近栈字节："},
		{L"Instruction bytes near EIP:", L"EIP 附近指令字节："},
		{L"Stack bytes near ESP:", L"ESP 附近栈字节："},
	}};

	std::wstring Details(const std::wstring &Body)
	{
		return QmFestiveReportText::LocalizeCrashReport(L"QmClient Crash Report", Body).m_Details;
	}
}

TEST(QmFestiveReportText, MemoryRangesKeepTheirLeadingZeroAndCompleteAddress)
{
	for(const auto &Field : gs_aMemoryFields)
	{
		SCOPED_TRACE(::testing::PrintToString(Field.m_pPrefix));
		const std::wstring Line = std::wstring(Field.m_pPrefix) + L" 0x0000000000001234-0x0000000000001280";
		const std::wstring Result = Details(Line);
		EXPECT_NE(Result.find(std::wstring(Field.m_pLabel) + L"0x0000000000001234-0x0000000000001280\r\n"), std::wstring::npos);
		EXPECT_NE(Result.find(Line), std::wstring::npos);
	}
}

TEST(QmFestiveReportText, UnavailableMemoryKeepsTheCompleteStatusWord)
{
	for(const auto &Field : gs_aMemoryFields)
	{
		SCOPED_TRACE(::testing::PrintToString(Field.m_pPrefix));
		const std::wstring Result = Details(std::wstring(Field.m_pPrefix) + L" unavailable");
		EXPECT_NE(Result.find(std::wstring(Field.m_pLabel) + L"unavailable\r\n"), std::wstring::npos);
	}
}

TEST(QmFestiveReportText, ReportTruncatedAtMemoryPrefixStillProducesDetails)
{
	for(const auto &Field : gs_aMemoryFields)
	{
		SCOPED_TRACE(::testing::PrintToString(Field.m_pPrefix));
		std::wstring Result;
		ASSERT_NO_THROW(Result = Details(Field.m_pPrefix));
		EXPECT_NE(Result.find(std::wstring(Field.m_pLabel) + L"\r\n"), std::wstring::npos);
		EXPECT_NE(Result.find(Field.m_pPrefix), std::wstring::npos);
	}
}

TEST(QmFestiveReportText, MemoryValueWithoutSeparatingSpaceIsNotTruncated)
{
	for(const auto &Field : gs_aMemoryFields)
	{
		SCOPED_TRACE(::testing::PrintToString(Field.m_pPrefix));
		const std::wstring Result = Details(std::wstring(Field.m_pPrefix) + L"0x12");
		EXPECT_NE(Result.find(std::wstring(Field.m_pLabel) + L"0x12\r\n"), std::wstring::npos);
	}
}

TEST(QmFestiveReportText, AShorterUnknownPrefixRemainsOriginalText)
{
	const std::wstring Result = Details(L"Instruction bytes near RI");
	EXPECT_NE(Result.find(L"【中文化报告】\r\nInstruction bytes near RI\r\n"), std::wstring::npos);
}

TEST(QmFestiveReportText, MixedNewlinesPreserveOriginalLinesAndBlankLine)
{
	const std::wstring Result = Details(L"first\n\nsecond\r\nthird");
	EXPECT_NE(Result.find(L"【原始诊断信息（反馈时请保留）】\r\nfirst\r\n\r\nsecond\r\nthird"), std::wstring::npos);
	EXPECT_EQ(Result.find(L"\r\r\n"), std::wstring::npos);
}

TEST(QmFestiveReportText, UnknownUnicodeDetailsRemainAvailableForFeedback)
{
	const std::wstring Result = Details(L"地图：测试🚀\nopaque: 0123");
	EXPECT_NE(Result.find(L"【中文化报告】\r\n地图：测试🚀\r\nopaque: 0123\r\n"), std::wstring::npos);
	EXPECT_NE(Result.find(L"【原始诊断信息（反馈时请保留）】\r\n地图：测试🚀\r\nopaque: 0123"), std::wstring::npos);
}

TEST(QmFestiveReportText, ExplicitFatalTypeTakesPriorityOverGraphicsAdvice)
{
	const auto Report = QmFestiveReportText::LocalizeCrashReport(L"Graphics Error", L"Report type: fatal-crash\nReason: Fatal signal");
	EXPECT_EQ(Report.m_Title, L"客户端发生致命崩溃，好运还在");
	EXPECT_NE(Report.m_Details.find(L"原因：进程收到致命信号"), std::wstring::npos);
}

TEST(QmFestiveReportText, ExplicitHangTypeTakesPriorityOverAssertionTitle)
{
	const auto Report = QmFestiveReportText::LocalizeCrashReport(L"Assertion Error", L"Report type: hang\nClient state: offline (0)");
	EXPECT_EQ(Report.m_Title, L"客户端卡住了，好运还在");
	EXPECT_NE(Report.m_Details.find(L"报告类型：客户端无响应"), std::wstring::npos);
}

TEST(QmFestiveReportText, AssertionTitleRetainsAssertionAdvice)
{
	const auto Report = QmFestiveReportText::LocalizeCrashReport(L"Assertion Error", L"assertion error occurred");
	EXPECT_EQ(Report.m_Title, L"程序遇到断言，好运还在");
	EXPECT_NE(Report.m_Details.find(L"具体文件、行号和断言内容"), std::wstring::npos);
}

TEST(QmFestiveReportText, LegacyFatalReasonStillSelectsFatalReport)
{
	const auto Report = QmFestiveReportText::LocalizeCrashReport(L"", L"Reason: Unhandled C++ exception (std::terminate)");
	EXPECT_EQ(Report.m_Title, L"C++ 异常没被接住，好运还在");
	EXPECT_NE(Report.m_Details.find(L"原因：未捕获的 C++ 异常（std::terminate）"), std::wstring::npos);
}

TEST(QmFestiveReportText, EmptyReportStillHasACompleteDetailsShell)
{
	const auto Report = QmFestiveReportText::LocalizeCrashReport(L"", L"");
	EXPECT_FALSE(Report.m_Title.empty());
	EXPECT_FALSE(Report.m_Subtitle.empty());
	EXPECT_NE(Report.m_Details.find(L"【问题判断】"), std::wstring::npos);
	EXPECT_NE(Report.m_Details.find(L"【建议操作】"), std::wstring::npos);
	EXPECT_NE(Report.m_Details.find(L"【中文化报告】"), std::wstring::npos);
	EXPECT_NE(Report.m_Details.find(L"【原始诊断信息（反馈时请保留）】"), std::wstring::npos);
}
