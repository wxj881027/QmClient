#include <game/client/components/qmclient/music_lyrics/music_lyrics_qrc.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/qrc_test_fixture.h>

#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace
{
	struct SDecodeObservation
	{
		bool m_Succeeded = false;
		bool m_BadAllocation = false;
		bool m_Triggered = false;
		size_t m_Allocations = 0;
	};

	SDecodeObservation DecodeWithFailure(std::string_view Input, std::string &Output, size_t BeforeFailure)
	{
		SDecodeObservation Observation;
		CPredictionAllocationFailure Failure(BeforeFailure);
		try
		{
			Observation.m_Succeeded = QmMusicLyrics::DecryptQrc(Input, &Output);
		}
		catch(const std::bad_alloc &)
		{
			Observation.m_BadAllocation = true;
		}
		Observation.m_Triggered = Failure.Triggered();
		Observation.m_Allocations = Failure.Attempts();
		return Observation;
	}
}

TEST(MusicLyricsQrcAllocation, NativePreparationFailurePropagatesAndFreshDecodeRecovers)
{
	const auto Data = QrcTestFixture::FromHex(QrcTestFixture::ENCRYPTED_RLRC_HEX);
	const std::string_view Input(reinterpret_cast<const char *>(Data.data()), Data.size());
	std::string Expected;
	ASSERT_TRUE(QmMusicLyrics::DecryptQrc(Input, &Expected));
	ASSERT_NE(Expected.find("停(0,274)下(274,274)"), std::string::npos);
	std::string TraceOutput = "stale";
	const auto Trace = DecodeWithFailure(Input, TraceOutput, std::numeric_limits<size_t>::max());
	ASSERT_TRUE(Trace.m_Succeeded);
	ASSERT_FALSE(Trace.m_BadAllocation);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 64u);
	EXPECT_EQ(TraceOutput, Expected);
	// 这里只拒收 C++ 容器分配，不把 native hook 次数当作 zlib malloc/free 证据。
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		std::string Output = "stale";
		const auto Refused = DecodeWithFailure(Input, Output, Budget);
		EXPECT_TRUE(Refused.m_Triggered);
		EXPECT_TRUE(Refused.m_BadAllocation);
		EXPECT_FALSE(Refused.m_Succeeded);
		EXPECT_TRUE(Output.empty());
		ASSERT_TRUE(QmMusicLyrics::DecryptQrc(Input, &Output));
		EXPECT_EQ(Output, Expected);
	}
}
