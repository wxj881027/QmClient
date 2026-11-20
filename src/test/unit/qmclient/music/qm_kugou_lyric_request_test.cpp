#include <gtest/gtest.h>
#include <qm/music/qm_kugou_lyric_request.h>
#include <test/support/prediction_allocation_failure.h>

#include <chrono>
#include <exception>
#include <future>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace
{
	using namespace std::chrono_literals;
	using QmMusicHook::CKugouLyricRequest;
	using QmMusicHook::SKugouLyricResult;
	using QmMusicHook::SPlayback;

	using TLyricFuture = std::future<SKugouLyricResult>;
	constexpr auto START = CKugouLyricRequest::TClock::time_point{} + 20s;

	SPlayback Song(const char *pId)
	{
		SPlayback Playback;
		Playback.m_HasSong = true;
		Playback.m_MediaId = pId;
		Playback.m_DurationMs = 60000;
		return Playback;
	}

	TLyricFuture Ready(SKugouLyricResult Result)
	{
		std::promise<SKugouLyricResult> Promise;
		Promise.set_value(std::move(Result));
		return Promise.get_future();
	}

	TLyricFuture Failed(const std::exception_ptr &Exception)
	{
		std::promise<SKugouLyricResult> Promise;
		Promise.set_exception(Exception);
		return Promise.get_future();
	}
}

TEST(QmKugouLyricRequest, FailedCurrentRequestRetriesAtTenSecondsAndPublishesLyrics)
{
	for(const auto &Error : {std::make_exception_ptr(std::bad_alloc()), std::make_exception_ptr(std::length_error("lyric result"))})
	{
		CKugouLyricRequest Request;
		auto Playback = Song("song-a");
		int Calls = 0;
		const auto Start = [&](const std::string &MediaId, int64_t DurationMs) {
			EXPECT_EQ(MediaId, "song-a");
			EXPECT_EQ(DurationMs, 60000);
			if(++Calls == 1)
				return Failed(Error);
			return Ready({"krc", "new lyrics", true});
		};
		Request.Update(Playback, START, Start);
		EXPECT_TRUE(Playback.m_Loading);
		EXPECT_TRUE(Playback.m_Error.empty());
		Playback = Song("song-a");
		Request.Update(Playback, START + 1s, Start);
		EXPECT_TRUE(Playback.m_Loading);
		EXPECT_FALSE(Playback.m_Error.empty());
		EXPECT_TRUE(Playback.m_LyricContent.empty());
		Playback = Song("song-a");
		Request.Update(Playback, START + 11s - 1ms, Start);
		EXPECT_EQ(Calls, 1);
		Request.Update(Playback, START + 11s, Start);
		EXPECT_EQ(Calls, 2);
		Playback = Song("song-a");
		Request.Update(Playback, START + 12s, Start);
		EXPECT_EQ(Calls, 2);
		EXPECT_FALSE(Playback.m_Loading);
		EXPECT_TRUE(Playback.m_Error.empty());
		EXPECT_EQ(Playback.m_LyricType, "krc");
		EXPECT_EQ(Playback.m_LyricContent, "new lyrics");
	}
}

TEST(QmKugouLyricRequest, OldRequestFailureDoesNotDelayOrReplaceTheNewSong)
{
	CKugouLyricRequest Request;
	std::promise<SKugouLyricResult> OldRequest;
	int Calls = 0;
	const auto Start = [&](const std::string &MediaId, int64_t) {
		if(++Calls == 1)
		{
			EXPECT_EQ(MediaId, "song-a");
			return OldRequest.get_future();
		}
		EXPECT_EQ(MediaId, "song-b");
		return Ready({"lrc", "second song", true});
	};
	auto Playback = Song("song-a");
	Request.Update(Playback, START, Start);
	Playback = Song("song-b");
	Request.Update(Playback, START + 200ms, Start);
	EXPECT_TRUE(Playback.m_Loading);
	EXPECT_EQ(Calls, 1);
	OldRequest.set_exception(std::make_exception_ptr(std::bad_alloc()));
	Playback = Song("song-b");
	Request.Update(Playback, START + 400ms, Start);
	EXPECT_EQ(Calls, 2);
	EXPECT_TRUE(Playback.m_Error.empty());
	EXPECT_TRUE(Playback.m_LyricContent.empty());
	Playback = Song("song-b");
	Request.Update(Playback, START + 600ms, Start);
	EXPECT_FALSE(Playback.m_Loading);
	EXPECT_EQ(Playback.m_LyricContent, "second song");
}

TEST(QmKugouLyricRequest, ReturningToTheSameSongDoesNotAcceptItsPreviousRequest)
{
	for(const bool OldSucceededWithoutLyrics : {false, true})
	{
		SCOPED_TRACE(OldSucceededWithoutLyrics);
		CKugouLyricRequest Request;
		std::promise<SKugouLyricResult> OldRequest;
		int Calls = 0;
		const auto Start = [&](const std::string &MediaId, int64_t) {
			EXPECT_EQ(MediaId, "song-a");
			if(++Calls == 1)
				return OldRequest.get_future();
			return Ready({"lrc", "current lyrics", true});
		};
		auto Playback = Song("song-a");
		Request.Update(Playback, START, Start);
		Playback = Song("song-b");
		Request.Update(Playback, START + 200ms, Start);
		Playback = Song("song-a");
		Request.Update(Playback, START + 400ms, Start);
		ASSERT_EQ(Calls, 1);

		OldRequest.set_value(SKugouLyricResult{"", "", OldSucceededWithoutLyrics});
		Request.Update(Playback, START + 600ms, Start);
		EXPECT_EQ(Calls, 2);
		EXPECT_TRUE(Playback.m_Loading);
		EXPECT_TRUE(Playback.m_Error.empty());

		Request.Update(Playback, START + 800ms, Start);
		EXPECT_FALSE(Playback.m_Loading);
		EXPECT_EQ(Playback.m_LyricContent, "current lyrics");
	}
}

TEST(QmKugouLyricRequest, ResourceFailureBeforeSubmissionCanRetryOnTheNextSample)
{
	for(const auto &Error : {std::make_exception_ptr(std::bad_alloc()), std::make_exception_ptr(std::length_error("request identity")),
		    std::make_exception_ptr(std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)))})
	{
		CKugouLyricRequest Request;
		auto Playback = Song("song-a");
		int Calls = 0;
		const auto Start = [&](const std::string &MediaId, int64_t) {
			EXPECT_EQ(MediaId, "song-a");
			if(++Calls == 1)
				std::rethrow_exception(Error);
			return Ready({"lrc", "retried", true});
		};
		Request.Update(Playback, START, Start);
		EXPECT_EQ(Calls, 1);
		EXPECT_TRUE(Playback.m_Loading);
		EXPECT_FALSE(Playback.m_Error.empty());
		Playback = Song("song-a");
		Request.Update(Playback, START + 200ms, Start);
		EXPECT_EQ(Calls, 2);
		Playback = Song("song-a");
		Request.Update(Playback, START + 400ms, Start);
		EXPECT_FALSE(Playback.m_Loading);
		EXPECT_EQ(Playback.m_LyricContent, "retried");
	}
}

TEST(QmKugouLyricRequest, SuccessWithoutLyricsFinishesLoadingWithoutAnotherFetch)
{
	CKugouLyricRequest Request;
	auto Playback = Song("song-a");
	int Calls = 0;
	const auto Start = [&](const std::string &, int64_t) {
		++Calls;
		return Ready({"", "", true});
	};
	Request.Update(Playback, START, Start);
	Playback = Song("song-a");
	Request.Update(Playback, START + 1s, Start);
	EXPECT_FALSE(Playback.m_Loading);
	EXPECT_TRUE(Playback.m_LyricContent.empty());
	EXPECT_TRUE(Playback.m_Error.empty());
	Playback = Song("song-a");
	Request.Update(Playback, START + 70s, Start);
	EXPECT_EQ(Calls, 1);
	EXPECT_FALSE(Playback.m_Loading);
}

TEST(QmKugouLyricRequest, FinishedLyricsSurviveFailedAllocationWhileCopyingToPlayback)
{
	CKugouLyricRequest Request;
	auto Playback = Song("song-a");
	const std::string Lyrics(1024, 'x');
	int Calls = 0;
	const auto Start = [&](const std::string &, int64_t) {
		++Calls;
		return Ready({"lrc", Lyrics, true});
	};
	Request.Update(Playback, START, Start);
	ASSERT_EQ(Calls, 1);
	Playback = Song("song-a");
	bool Threw = false;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure(0);
		try
		{
			Request.Update(Playback, START + 200ms, Start);
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_TRUE(Threw);
	EXPECT_EQ(Calls, 1);
	Playback = Song("song-a");
	Request.Update(Playback, START + 400ms, Start);
	EXPECT_EQ(Calls, 1);
	EXPECT_FALSE(Playback.m_Loading);
	EXPECT_EQ(Playback.m_LyricType, "lrc");
	EXPECT_EQ(Playback.m_LyricContent, Lyrics);
}

TEST(QmKugouLyricRequest, UnrelatedSystemErrorFromLauncherKeepsItsException)
{
	CKugouLyricRequest Request;
	auto Playback = Song("song-a");
	const auto Start = [](const std::string &, int64_t) -> TLyricFuture {
		throw std::system_error(std::make_error_code(std::errc::invalid_argument));
	};
	EXPECT_THROW(Request.Update(Playback, START, Start), std::system_error);
}
