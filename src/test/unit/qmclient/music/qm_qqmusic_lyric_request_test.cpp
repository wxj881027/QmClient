#include <gtest/gtest.h>
#include <qm/music/qm_qqmusic_lyric_request.h>

#include <exception>
#include <future>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace
{
	using QmMusicHook::QQMusic::CLyricRequest;
	using QmMusicHook::QQMusic::SLyricFetchResult;
	using TLyricFuture = std::future<SLyricFetchResult>;

	TLyricFuture Ready(SLyricFetchResult Result)
	{
		std::promise<SLyricFetchResult> Promise;
		Promise.set_value(std::move(Result));
		return Promise.get_future();
	}

	TLyricFuture Failed(const std::exception_ptr &Error)
	{
		std::promise<SLyricFetchResult> Promise;
		Promise.set_exception(Error);
		return Promise.get_future();
	}
}

TEST(QmQQMusicLyricRequest, AcceptedWorkerResourceFailureUsesExistingBackoffAndAttemptLimit)
{
	for(const auto &Error : {std::make_exception_ptr(std::bad_alloc()), std::make_exception_ptr(std::length_error("lyric result"))})
	{
		CLyricRequest Request;
		Request.Reset(100);
		int Starts = 0;
		const auto Start = [&] {
			++Starts;
			return Failed(Error);
		};

		Request.Update(100, true, Start);
		EXPECT_EQ(Starts, 1);
		Request.Update(200, true, Start);
		EXPECT_TRUE(Request.Loading());
		Request.Update(2199, true, Start);
		EXPECT_EQ(Starts, 1);

		Request.Update(2200, true, Start);
		EXPECT_EQ(Starts, 2);
		Request.Update(2300, true, Start);
		Request.Update(6299, true, Start);
		EXPECT_EQ(Starts, 2);

		Request.Update(6300, true, Start);
		EXPECT_EQ(Starts, 3);
		EXPECT_TRUE(Request.Loading());
		Request.Update(6400, true, Start);
		EXPECT_FALSE(Request.Loading());
		EXPECT_TRUE(Request.Exhausted());
		EXPECT_TRUE(Request.Lyrics().m_Content.empty());

		Request.Update(20000, true, Start);
		EXPECT_EQ(Starts, 3);
	}
}

TEST(QmQQMusicLyricRequest, RejectedLaunchDoesNotConsumeTheAttemptBudget)
{
	for(const auto &Error : {std::make_exception_ptr(std::bad_alloc()),
		    std::make_exception_ptr(std::length_error("lyric launch")),
		    std::make_exception_ptr(std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)))})
	{
		CLyricRequest Request;
		Request.Reset(100);
		int Starts = 0;
		const auto Start = [&]() -> TLyricFuture {
			if(++Starts <= 3)
				std::rethrow_exception(Error);
			return Ready({true, {"lrc", "[00:01.00]accepted", {}}});
		};

		for(int Now : {100, 300, 500})
		{
			Request.Update(Now, true, Start);
			EXPECT_TRUE(Request.Loading());
			EXPECT_FALSE(Request.Exhausted());
		}
		Request.Update(700, true, Start);
		EXPECT_EQ(Starts, 4);
		Request.Update(900, true, Start);
		EXPECT_FALSE(Request.Loading());
		EXPECT_EQ(Request.Lyrics().m_Content, "[00:01.00]accepted");
	}
}

TEST(QmQQMusicLyricRequest, InvalidFutureIsNotAnAcceptedRequest)
{
	CLyricRequest Request;
	Request.Reset(100);
	int Starts = 0;
	const auto Start = [&]() -> TLyricFuture {
		++Starts;
		return {};
	};
	for(int Now : {100, 300, 500, 700})
		Request.Update(Now, true, Start);
	EXPECT_EQ(Starts, 4);
	EXPECT_TRUE(Request.Loading());
	EXPECT_FALSE(Request.Exhausted());
}

TEST(QmQQMusicLyricRequest, ChangedSongReclaimsOldCompletionWithoutApplyingIt)
{
	for(bool OldFails : {false, true})
	{
		SCOPED_TRACE(OldFails);
		CLyricRequest Request;
		std::promise<SLyricFetchResult> Old;
		Request.Reset(100);
		Request.Update(100, true, [&] { return Old.get_future(); });

		Request.Reset(200);
		int Starts = 0;
		const auto Start = [&] {
			++Starts;
			return Ready({true, {"lrc", "[00:01.00]new", "[00:01.00]translation"}});
		};
		Request.Update(200, true, Start);
		EXPECT_EQ(Starts, 0);

		if(OldFails)
			Old.set_exception(std::make_exception_ptr(std::logic_error("stale lyric result")));
		else
			Old.set_value({true, {"lrc", "[00:01.00]old", {}}});
		Request.Update(300, true, Start);
		EXPECT_EQ(Starts, 1);
		EXPECT_TRUE(Request.Lyrics().m_Content.empty());

		Request.Update(400, true, Start);
		EXPECT_EQ(Starts, 1);
		EXPECT_EQ(Request.Lyrics().m_Content, "[00:01.00]new");
		EXPECT_EQ(Request.Lyrics().m_Translation, "[00:01.00]translation");
	}
}

TEST(QmQQMusicLyricRequest, SuccessfulEmptyLyricsFinishLoadingWithoutRetry)
{
	CLyricRequest Request;
	Request.Reset(100);
	int Starts = 0;
	const auto Start = [&] {
		++Starts;
		return Ready({true, {}});
	};
	Request.Update(100, true, Start);
	Request.Update(200, true, Start);
	Request.Update(20000, true, Start);
	EXPECT_EQ(Starts, 1);
	EXPECT_FALSE(Request.Loading());
	EXPECT_FALSE(Request.Exhausted());
	EXPECT_TRUE(Request.Lyrics().m_Content.empty());
}
