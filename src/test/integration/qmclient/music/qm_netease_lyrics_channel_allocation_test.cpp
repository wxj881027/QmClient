#include <game/client/components/qmclient/netease_hook/qm_netease_lyrics_channel.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/prediction_allocation_failure.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <limits>
#include <memory>
#include <string>

namespace
{
	std::wstring NextEndpoint()
	{
		static std::atomic_uint s_Next{0};
		return L"Local\\QmClient.Tests.LyricsAllocation." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++s_Next);
	}

	struct SReadObservation
	{
		size_t m_Attempts = 0;
		bool m_Triggered = false;
		bool m_Available = false;
		bool m_Escaped = false;
	};

	class CNeteaseLyricsChannelAllocationTest : public ::testing::Test
	{
	protected:
		const std::wstring m_Endpoint = NextEndpoint();
		QmNeteaseHook::CLyricsChannel m_Writer{m_Endpoint};
		QmNeteaseHook::CLyricsChannel m_Reader{m_Endpoint};
		NeteaseLyrics::SRawLyrics m_Old;
		NeteaseLyrics::SRawLyrics m_New;
		NeteaseLyrics::SRawLyrics m_Received;
		uint64_t m_Revision = 0;

		void SetUp() override
		{
			m_Old.m_Lrc = "[00:01]old original";
			m_Old.m_Yrc = "[1000,1000](1000,1000,0)old original";
			m_Old.m_TranslationLrc = "[00:01]old translation";
			m_New.m_Lrc = "[00:01]" + std::string(256, 'l');
			m_New.m_Yrc = "[1000,1000](1000,1000,0)" + std::string(384, 'y');
			m_New.m_TranslationLrc = "[00:01]" + std::string(512, 't');
		}
		void PrepareChangedPayload()
		{
			ASSERT_TRUE(m_Writer.Publish(100, 42, 7, m_Old));
			ASSERT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, m_Received));
			ASSERT_TRUE(m_Writer.Publish(100, 42, 7, m_New));
		}
		SReadObservation ReadWithFailure(size_t Budget = std::numeric_limits<size_t>::max())
		{
			CPredictionAllocationFailure Failure(Budget);
			SReadObservation Result;
			try
			{
				Result.m_Available = m_Reader.Read(100, 42, 7, m_Revision, m_Received);
			}
			catch(...)
			{
				Result.m_Escaped = true;
			}
			Result.m_Attempts = Failure.Attempts();
			Result.m_Triggered = Failure.Triggered();
			return Result;
		}
		void ExpectPayload(const NeteaseLyrics::SRawLyrics &Received, const NeteaseLyrics::SRawLyrics &Expected)
		{
			EXPECT_EQ(Received.m_Lrc, Expected.m_Lrc);
			EXPECT_EQ(Received.m_Yrc, Expected.m_Yrc);
			EXPECT_EQ(Received.m_TranslationLrc, Expected.m_TranslationLrc);
		}
		void ReadFromOtherThread(uint64_t &Revision)
		{
			struct SReadResult
			{
				std::wstring m_Endpoint;
				NeteaseLyrics::SRawLyrics m_Received;
				uint64_t m_Revision = 0;
				bool m_Available = false;
			};
			auto pResult = std::make_shared<SReadResult>();
			pResult->m_Endpoint = m_Endpoint;
			// 独立线程排除同线程递归获得 mutex；超时后仍由回调拥有全部上下文。
			ASSERT_TRUE(WaitForAsyncIo([pResult] {
				QmNeteaseHook::CLyricsChannel Channel(pResult->m_Endpoint);
				pResult->m_Available = Channel.Read(100, 42, 7, pResult->m_Revision, pResult->m_Received);
			}));
			ASSERT_TRUE(pResult->m_Available);
			Revision = pResult->m_Revision;
			ExpectPayload(pResult->m_Received, m_New);
		}
	};
}

TEST_F(CNeteaseLyricsChannelAllocationTest, EveryNativeReadRefusalPreservesPayloadRevisionAndReleasesTheMutex)
{
	ASSERT_NO_FATAL_FAILURE(PrepareChangedPayload());
	const auto Count = ReadWithFailure();
	ASSERT_FALSE(Count.m_Escaped);
	ASSERT_TRUE(Count.m_Available);
	ASSERT_GT(Count.m_Attempts, 0u);
	ASSERT_LE(Count.m_Attempts, 8u);
	for(size_t Budget = 0; Budget < Count.m_Attempts; ++Budget)
	{
		SCOPED_TRACE(Budget);
		ASSERT_NO_FATAL_FAILURE(PrepareChangedPayload());
		const uint64_t PreviousRevision = m_Revision;
		const auto Refused = ReadWithFailure(Budget);
		EXPECT_TRUE(Refused.m_Triggered);
		EXPECT_FALSE(Refused.m_Escaped);
		EXPECT_FALSE(Refused.m_Available);
		EXPECT_EQ(m_Revision, PreviousRevision);
		ExpectPayload(m_Received, m_Old);
		uint64_t OtherRevision = 0;
		ASSERT_NO_FATAL_FAILURE(ReadFromOtherThread(OtherRevision));
		EXPECT_NE(OtherRevision, PreviousRevision);
		ASSERT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, m_Received));
		EXPECT_EQ(m_Revision, OtherRevision);
		ExpectPayload(m_Received, m_New);
		const auto Unchanged = ReadWithFailure(0);
		EXPECT_FALSE(Unchanged.m_Escaped);
		EXPECT_FALSE(Unchanged.m_Available);
		EXPECT_FALSE(Unchanged.m_Triggered);
		EXPECT_EQ(Unchanged.m_Attempts, 0u);
		EXPECT_EQ(m_Revision, OtherRevision);
		ExpectPayload(m_Received, m_New);
	}
}

#endif
