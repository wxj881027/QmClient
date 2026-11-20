#include <game/client/components/system_media_controls.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace
{
	using namespace std::chrono_literals;
	using SystemMediaControls::EMediaRequestStatus;

	struct SMediaPoll
	{
		std::chrono::steady_clock::time_point m_Time{};
		int m_Polls = 0;
		EMediaRequestStatus m_Status = EMediaRequestStatus::PENDING;
		bool m_Stop = false;

		bool Wait()
		{
			return SystemMediaControls::WaitForMediaRequest(
				[&] { return m_Status; },
				[&] { return m_Stop || m_Polls >= 250; },
				[&] { return m_Time; },
				[&](std::chrono::steady_clock::duration Slice) {
					++m_Polls;
					m_Time += Slice;
				});
		}
	};
}

TEST(QmMediaSmtcRecovery, PendingRequestReturnsWhenTheBudgetExpires)
{
	SMediaPoll Poll;
	const auto Start = Poll.m_Time;
	EXPECT_FALSE(Poll.Wait());
	EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(Poll.m_Time - Start).count(), 3000);
	EXPECT_LT(Poll.m_Polls, 250);
}

TEST(QmMediaSmtcRecovery, TerminalRequestsAndShutdownNeedNoSleep)
{
	SMediaPoll Poll;
	Poll.m_Status = EMediaRequestStatus::COMPLETE;
	EXPECT_TRUE(Poll.Wait());
	EXPECT_EQ(Poll.m_Polls, 0);

	Poll.m_Status = EMediaRequestStatus::FAILED;
	EXPECT_FALSE(Poll.Wait());
	EXPECT_EQ(Poll.m_Polls, 0);

	Poll.m_Status = EMediaRequestStatus::PENDING;
	Poll.m_Stop = true;
	EXPECT_FALSE(Poll.Wait());
	EXPECT_EQ(Poll.m_Polls, 0);
}

TEST(QmMediaSmtcRecovery, CompletionDuringPollingReturnsBeforeTheDeadline)
{
	std::chrono::steady_clock::time_point Time{};
	int Polls = 0;
	EXPECT_TRUE(SystemMediaControls::WaitForMediaRequest(
		[&] { return Polls >= 2 ? EMediaRequestStatus::COMPLETE : EMediaRequestStatus::PENDING; },
		[&] { return Polls >= 4; },
		[&] { return Time; },
		[&](std::chrono::steady_clock::duration Slice) {
			++Polls;
			Time += Slice;
		}));
	EXPECT_EQ(Polls, 2);
	EXPECT_LT(Time.time_since_epoch(), 1s);
}

TEST(QmMediaSmtcRecovery, PreservesStaleSelectionAcrossTheFullNativeTimelineRange)
{
	constexpr int64_t NOW = 100'000'000'000LL;
	SystemMediaControls::STimelineProperties Timeline;
	Timeline.m_Start100ns = std::numeric_limits<int64_t>::min();
	Timeline.m_End100ns = std::numeric_limits<int64_t>::max();
	Timeline.m_LastUpdatedUtc100ns = NOW - 6 * SystemMediaControls::HUNDRED_NS_PER_SECOND;
	EXPECT_TRUE(SystemMediaControls::MediaTimelineLooksStale(Timeline, NOW));

	std::swap(Timeline.m_Start100ns, Timeline.m_End100ns);
	EXPECT_FALSE(SystemMediaControls::MediaTimelineLooksStale(Timeline, NOW));
}

TEST(QmMediaSmtcRecovery, FutureUpdateCannotBeAnOldPlayingSession)
{
	EXPECT_FALSE(SystemMediaControls::MediaTimelineLooksStale(true, 10'000, 1, std::numeric_limits<int64_t>::min()));
}

TEST(QmMediaSmtcRecovery, FailedCoverPreparationCanRetryTheSameTrack)
{
	std::string PreparedKey = "previous";
	int Reads = 0;
	auto Prepare = [&] { return ++Reads == 2; };

	SystemMediaControls::RefreshAlbumArt(PreparedKey, "next", false, Prepare);
	EXPECT_TRUE(PreparedKey.empty());
	EXPECT_EQ(Reads, 1);

	SystemMediaControls::RefreshAlbumArt(PreparedKey, "next", false, Prepare);
	EXPECT_EQ(PreparedKey, "next");
	EXPECT_EQ(Reads, 2);

	SystemMediaControls::RefreshAlbumArt(PreparedKey, "next", false, Prepare);
	EXPECT_EQ(PreparedKey, "next");
	EXPECT_EQ(Reads, 2);
}

TEST(QmMediaSmtcRecovery, FailedUploadReloadsThePreviouslyPreparedCover)
{
	std::string PreparedKey = "same track";
	int Reads = 0;
	SystemMediaControls::RefreshAlbumArt(PreparedKey, "same track", true, [&] {
		++Reads;
		return true;
	});
	EXPECT_EQ(PreparedKey, "same track");
	EXPECT_EQ(Reads, 1);
}
