#include <engine/client/qm_crash_report_file.h>

#include <gtest/gtest.h>
#include <test/support/qm_crash_report_file_memory.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

namespace
{
	// 只替换外部流读取；字节限制、字符串所有权及异常回退均来自生产reader。
	class CInput
	{
		std::string_view m_Bytes;

	public:
		explicit CInput(std::string_view Bytes) :
			m_Bytes(Bytes) {}
		unsigned m_Chunk = 4096;
		unsigned m_Calls = 0;
		unsigned m_ErrorAt = 0;
		unsigned m_ThrowAt = 0;
		bool m_Overreport = false;
		size_t m_Consumed = 0;
		unsigned m_PeakRequested = 0;
		unsigned Read(char *pOut, unsigned Requested)
		{
			++m_Calls;
			m_PeakRequested = std::max(m_PeakRequested, Requested);
			if(m_Calls == m_ThrowAt)
				throw std::bad_alloc();
			if(m_Overreport)
				return Requested + 1;
			const auto Size = std::min({static_cast<size_t>(Requested), static_cast<size_t>(m_Chunk), m_Bytes.size() - m_Consumed});
			if(Size)
				std::memcpy(pOut, m_Bytes.data() + m_Consumed, Size);
			m_Consumed += Size;
			return static_cast<unsigned>(Size);
		}
		bool Failed() const { return m_ErrorAt && m_Calls >= m_ErrorAt; }
	};
}

TEST(QmCrashReportTextRead, EmptyStreamIsAValidEmptyReport)
{
	CInput Input("");
	const auto Text = QmCrashReportFile::ReadText(Input, 8);
	ASSERT_TRUE(Text);
	EXPECT_TRUE(Text->empty());
	EXPECT_EQ(Input.m_Calls, 1U);
}

TEST(QmCrashReportTextRead, NormalTextKeepsNewlinesAndAllBytes)
{
	CInput Input("Report type: hang\r\nmap: test\n");
	const auto Text = QmCrashReportFile::ReadText(Input);
	ASSERT_TRUE(Text);
	EXPECT_STREQ(Text->c_str(), "Report type: hang\r\nmap: test\n");
}

TEST(QmCrashReportTextRead, NonzeroShortReadsContinueUntilEof)
{
	CInput Input("abcdef");
	Input.m_Chunk = 1;
	const auto Text = QmCrashReportFile::ReadText(Input, 8);
	ASSERT_TRUE(Text);
	EXPECT_STREQ(Text->c_str(), "abcdef");
	EXPECT_EQ(Input.m_Consumed, 6U);
	EXPECT_EQ(Input.m_Calls, 7U);
}

TEST(QmCrashReportTextRead, ExactLimitIsAcceptedAfterEofIsConfirmed)
{
	CInput Input("12345678");
	const auto Text = QmCrashReportFile::ReadText(Input, 8);
	ASSERT_TRUE(Text);
	EXPECT_STREQ(Text->c_str(), "12345678");
	EXPECT_EQ(Input.m_Consumed, 8U);
	EXPECT_EQ(Input.m_Calls, 2U);
}

TEST(QmCrashReportTextRead, OneExtraByteRejectsTheWholeReport)
{
	CInput Input("123456789");
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input, 8));
	EXPECT_EQ(Input.m_Consumed, 9U);
}

TEST(QmCrashReportTextRead, OversizedStreamIsNotReadBeyondTheSingleProbeByte)
{
	const std::string Bytes(65536, 'x');
	CInput Input(Bytes);
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input, 8));
	EXPECT_EQ(Input.m_Consumed, 9U);
	EXPECT_LE(Input.m_PeakRequested, 9U);
}

TEST(QmCrashReportTextRead, ZeroLimitAcceptsOnlyAnEmptyStream)
{
	CInput Input("");
	const auto Text = QmCrashReportFile::ReadText(Input, 0);
	ASSERT_TRUE(Text);
	EXPECT_TRUE(Text->empty());
}

TEST(QmCrashReportTextRead, ZeroLimitRejectsTheFirstByte)
{
	CInput Input("a");
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input, 0));
	EXPECT_EQ(Input.m_Consumed, 1U);
}

TEST(QmCrashReportTextRead, LimitAboveTheReportPolicyIsRejectedWithoutReading)
{
	CInput Input("a");
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input, QmCrashReportFile::MAX_REPORT_BYTES + 1));
	EXPECT_EQ(Input.m_Calls, 0U);
}

TEST(QmCrashReportTextRead, EmbeddedNullRejectsTheReport)
{
	const std::string Bytes("a\0b", 3);
	CInput Input(Bytes);
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input));
}

TEST(QmCrashReportTextRead, NullInALaterChunkDiscardsPreviouslyReadText)
{
	const std::string Bytes("good\0tail", 9);
	CInput Input(Bytes);
	Input.m_Chunk = 4;
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input));
	EXPECT_EQ(Input.m_Calls, 2U);
}

TEST(QmCrashReportTextRead, InvalidUtf8IsKeptForTheLenientDisplayConversion)
{
	const std::string Bytes("ok\xff\xe4", 4);
	CInput Input(Bytes);
	const auto Text = QmCrashReportFile::ReadText(Input);
	ASSERT_TRUE(Text);
	EXPECT_EQ(std::string_view(Text->data(), Text->size()), Bytes);
}

TEST(QmCrashReportTextRead, ReadErrorDiscardsBytesFromTheFailedRead)
{
	CInput Input("partial");
	Input.m_ErrorAt = 1;
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input));
}

TEST(QmCrashReportTextRead, ErrorAtEofDoesNotPublishEarlierBytes)
{
	CInput Input("data");
	Input.m_Chunk = 4;
	Input.m_ErrorAt = 2;
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input));
	EXPECT_EQ(Input.m_Calls, 2U);
}

TEST(QmCrashReportTextRead, InvalidReadCountIsRejectedBeforeInspectingTheBuffer)
{
	CInput Input("");
	Input.m_Overreport = true;
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input));
}

TEST(QmCrashReportTextRead, InputExceptionReleasesTheAccumulatedText)
{
	const std::string Bytes(16384, 'x');
	CInput Input(Bytes);
	Input.m_ThrowAt = 2;
	CCrashReportFileMemory Memory;
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input, QmCrashReportFile::MAX_REPORT_BYTES, &Memory));
	EXPECT_GT(Memory.m_Calls, 0U);
	EXPECT_EQ(Memory.m_Live, 0U);
}

TEST(QmCrashReportTextRead, InitialTextAllocationFailureDoesNotEscape)
{
	const std::string Bytes(16384, 'x');
	CInput Input(Bytes);
	CCrashReportFileMemory Memory;
	Memory.m_FailAt = 1;
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input, QmCrashReportFile::MAX_REPORT_BYTES, &Memory));
	EXPECT_EQ(Memory.m_Calls, 1U);
	EXPECT_EQ(Memory.m_Live, 0U);
}

TEST(QmCrashReportTextRead, TextGrowthFailureReleasesTheExistingAllocation)
{
	const std::string Bytes(65536, 'x');
	CInput Input(Bytes);
	CCrashReportFileMemory Memory;
	Memory.m_FailAt = 2;
	EXPECT_FALSE(QmCrashReportFile::ReadText(Input, QmCrashReportFile::MAX_REPORT_BYTES, &Memory));
	EXPECT_EQ(Memory.m_Calls, 2U);
	EXPECT_EQ(Memory.m_Live, 0U);
}

TEST(QmCrashReportTextRead, SuccessfulTextOwnsItsAllocationUntilReleased)
{
	const std::string Bytes(8192, 'x');
	CInput Input(Bytes);
	CCrashReportFileMemory Memory;
	auto Text = QmCrashReportFile::ReadText(Input, QmCrashReportFile::MAX_REPORT_BYTES, &Memory);
	ASSERT_TRUE(Text);
	EXPECT_EQ(Text->size(), Bytes.size());
	EXPECT_GT(Memory.m_Live, 0U);
	Text.reset();
	EXPECT_EQ(Memory.m_Live, 0U);
}

TEST(QmCrashReportTextRead, ANewAttemptCanSucceedAfterAllocationFailure)
{
	const std::string Bytes(8192, 'x');
	CCrashReportFileMemory Memory;
	Memory.m_FailAt = 1;
	CInput First(Bytes);
	EXPECT_FALSE(QmCrashReportFile::ReadText(First, QmCrashReportFile::MAX_REPORT_BYTES, &Memory));
	Memory.m_FailAt = 0;
	CInput Next(Bytes);
	const auto Text = QmCrashReportFile::ReadText(Next, QmCrashReportFile::MAX_REPORT_BYTES, &Memory);
	ASSERT_TRUE(Text);
	EXPECT_EQ(std::string_view(Text->data(), Text->size()), Bytes);
}
