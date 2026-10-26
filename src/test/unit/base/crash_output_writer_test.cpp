#include <base/crash_output.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>

namespace
{
	struct SFile
	{
		std::array<char, 128> m_aBytes{};
		size_t m_Size = 0, m_Chunk = 128;
		int m_Writes = 0, m_Flushes = 0, m_Closes = 0;
		int m_FailWrite = 0, m_ZeroWrite = 0, m_Overreport = 0;
		bool m_FlushResult = true, m_CloseResult = true;
		bool Write(const void *pData, size_t Size, size_t &Written) noexcept
		{
			++m_Writes;
			if(m_Writes == m_FailWrite)
				return false;
			if(m_Writes == m_ZeroWrite || m_Writes == m_Overreport)
			{
				Written = m_Writes == m_ZeroWrite ? 0 : Size + 1;
				return true;
			}
			Written = Size < m_Chunk ? Size : m_Chunk;
			if(Written > m_aBytes.size() - m_Size)
				return false;
			std::memcpy(m_aBytes.data() + m_Size, pData, Written);
			m_Size += Written;
			return true;
		}
		bool Flush() noexcept
		{
			++m_Flushes;
			return m_FlushResult;
		}
		bool Close() noexcept
		{
			++m_Closes;
			return m_CloseResult;
		}
		std::string Bytes() const { return {m_aBytes.data(), m_Size}; }
	};
}

TEST(CrashOutputWriter, FullBinaryOutputCompletesOnlyAfterFlushAndClose)
{
	SFile File;
	crash_output::CWriter Output(File);
	const std::array<char, 4> aData{'a', '\0', 'b', 'c'};
	ASSERT_TRUE(Output.Write(aData.data(), aData.size()));
	EXPECT_EQ(File.Bytes(), std::string(aData.data(), aData.size()));
	EXPECT_EQ(File.m_Flushes, 0);
	EXPECT_EQ(File.m_Closes, 0);
	EXPECT_TRUE(Output.Finish());
	EXPECT_EQ(File.m_Flushes, 1);
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, ShortWritesProduceTheCompleteOriginalBytes)
{
	SFile File;
	File.m_Chunk = 2;
	crash_output::CWriter Output(File);
	ASSERT_TRUE(Output.WriteText("abcde"));
	EXPECT_EQ(File.Bytes(), "abcde");
	EXPECT_TRUE(Output.Finish());
}

TEST(CrashOutputWriter, ZeroProgressStopsAndNeverPublishesCompletion)
{
	SFile File;
	File.m_ZeroWrite = 1;
	crash_output::CWriter Output(File);
	EXPECT_FALSE(Output.WriteText("data"));
	EXPECT_FALSE(Output.WriteText("later"));
	EXPECT_EQ(File.m_Writes, 1);
	EXPECT_FALSE(Output.Finish());
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, WriteErrorRemainsFailedEvenWhenFlushAndCloseSucceed)
{
	SFile File;
	File.m_FailWrite = 1;
	crash_output::CWriter Output(File);
	EXPECT_FALSE(Output.WriteText("data"));
	EXPECT_FALSE(Output.Finish());
	EXPECT_EQ(File.m_Flushes, 1);
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, ErrorAfterPartialWriteRetainsPrefixAndStopsFurtherWrites)
{
	SFile File;
	File.m_Chunk = 2;
	File.m_FailWrite = 2;
	crash_output::CWriter Output(File);
	EXPECT_FALSE(Output.WriteText("abcde"));
	EXPECT_EQ(File.Bytes(), "ab");
	EXPECT_FALSE(Output.WriteText("later"));
	EXPECT_EQ(File.m_Writes, 2);
	EXPECT_FALSE(Output.Finish());
}

TEST(CrashOutputWriter, NullNonemptyWriteFailsBeforeCallingFile)
{
	SFile File;
	crash_output::CWriter Output(File);
	EXPECT_FALSE(Output.Write(nullptr, 1));
	EXPECT_EQ(File.m_Writes, 0);
	EXPECT_FALSE(Output.Finish());
}

TEST(CrashOutputWriter, EmptyAndNullTextDoNotCallFile)
{
	SFile File;
	crash_output::CWriter Output(File);
	EXPECT_TRUE(Output.Write(nullptr, 0));
	EXPECT_TRUE(Output.WriteText(nullptr));
	EXPECT_TRUE(Output.WriteText(""));
	EXPECT_EQ(File.m_Writes, 0);
	EXPECT_TRUE(Output.Finish());
}

TEST(CrashOutputWriter, FlushFailureStillClosesAndCannotComplete)
{
	SFile File;
	File.m_FlushResult = false;
	crash_output::CWriter Output(File);
	ASSERT_TRUE(Output.WriteText("data"));
	EXPECT_FALSE(Output.Finish());
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, CloseFailureCannotCompleteOrCloseAgain)
{
	SFile File;
	File.m_CloseResult = false;
	{
		crash_output::CWriter Output(File);
		ASSERT_TRUE(Output.WriteText("data"));
		EXPECT_FALSE(Output.Finish());
		EXPECT_FALSE(Output.Finish());
	}
	EXPECT_EQ(File.m_Flushes, 1);
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, FailedProducerStillFlushesAndClosesButCannotComplete)
{
	SFile File;
	crash_output::CWriter Output(File);
	ASSERT_TRUE(Output.WriteText("partial producer output"));
	EXPECT_FALSE(Output.Finish(false));
	EXPECT_EQ(File.m_Flushes, 1);
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, AbandonedWriterClosesExactlyOnceWithoutFlushing)
{
	SFile File;
	{
		crash_output::CWriter Output(File);
		ASSERT_TRUE(Output.WriteText("partial"));
	}
	EXPECT_EQ(File.m_Flushes, 0);
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, FinishedWriterRejectsNewWritesAndRetainsTerminalResult)
{
	SFile File;
	{
		crash_output::CWriter Output(File);
		ASSERT_TRUE(Output.WriteText("complete"));
		ASSERT_TRUE(Output.Finish());
		EXPECT_FALSE(Output.WriteText("later"));
		EXPECT_TRUE(Output.Finish());
		EXPECT_EQ(File.Bytes(), "complete");
	}
	EXPECT_EQ(File.m_Flushes, 1);
	EXPECT_EQ(File.m_Closes, 1);
}

TEST(CrashOutputWriter, ImpossibleReportedByteCountIsRejected)
{
	SFile File;
	File.m_Overreport = 1;
	crash_output::CWriter Output(File);
	EXPECT_FALSE(Output.WriteText("data"));
	EXPECT_FALSE(Output.Finish());
	EXPECT_EQ(File.m_Writes, 1);
}
