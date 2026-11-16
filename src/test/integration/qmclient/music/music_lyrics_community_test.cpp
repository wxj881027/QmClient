#include <base/mem.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/music_lyrics/music_lyrics_community.h>
#include <game/client/components/qmclient/music_lyrics/music_lyrics_community_format.h>
#include <game/client/components/qmclient/music_lyrics/music_world_lyrics_state.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace QmMusicLyrics;

namespace
{
	// 自编短句；仅用受控 HTTP 响应，不访问真实网络或用户缓存。
	constexpr std::string_view LYRICS =
		"[1000,2000](1000,500,0)春(1500,1500,0)风\n"
		"[4000,2000](4000,1000,0)秋(5000,1000,0)雨";
	constexpr std::string_view OTHER_LYRICS = "[1000,2000](1000,1000,0)星(2000,1000,0)河";

	class CResponse final : public IHttpRequest
	{
	public:
		bool m_Aborted = false;
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) { WriteToMemory(); }
		void Header(const char *) override {}
		void Fail() { m_State = EHttpState::ERROR; }
		void Abort() override
		{
			m_Aborted = true;
			IHttpRequest::Abort();
		}
		void Finish(int Status, std::string_view Text = {})
		{
			free(m_pBuffer);
			m_ResponseLength = m_BufferSize = Text.size();
			m_pBuffer = static_cast<unsigned char *>(malloc(Text.size() + 1));
			ASSERT_NE(m_pBuffer, nullptr);
			if(!Text.empty())
				mem_copy(m_pBuffer, Text.data(), Text.size());
			m_pBuffer[Text.size()] = 0;
			m_StatusCode = Status;
			m_State = EHttpState::DONE;
		}
	};

	std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		return std::make_unique<CResponse>(pUrl);
	}

	class CHttpRecorder final : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_vRequests.push_back(std::static_pointer_cast<CResponse>(std::move(pRequest)));
		}
		bool HasIpresolveBug() const override { return false; }
	};

	class CDeferredEngine final : public IEngine
	{
	public:
		std::vector<std::shared_ptr<IJob>> m_vJobs;
		void Init() override {}
		void AddJob(std::shared_ptr<IJob> pJob) override { m_vJobs.push_back(std::move(pJob)); }
		void ShutdownJobs() override {}
		void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
		void CompleteJobs()
		{
			if(m_vJobs.empty())
				return;
			CJobPool Pool;
			Pool.Init(1);
			for(const auto &pJob : m_vJobs)
				Pool.Add(pJob);
			for(const auto &pJob : m_vJobs)
				EXPECT_TRUE(WaitForJobDone(*pJob));
			Pool.Shutdown();
			m_vJobs.clear();
		}
	};

	class CommunityLyrics : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CHttpRecorder m_Http;
		CDeferredEngine m_Engine;
		CCommunityLyrics m_Lyrics{CreateResponse};
		int m_SourceId = 1;
		SSourceSnapshot m_Source;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Source.m_SongId = 42;
			m_Source.m_Generation = 1;
			m_Source.m_ProducerPid = 100;
			m_Source.m_Active = true;
			m_Source.m_Original = "播放器原句";
			m_Source.m_Translation = "播放器译文";
			m_Source.m_LineStartMs = 1000;
			m_Source.m_LineEndMs = 3000;
			m_Source.m_PositionMs = 1500;
		}
		void TearDown() override
		{
			m_Lyrics.Reset();
			// 所有磁盘任务在隔离存储销毁前完成，未提交结果不能泄露到下一项测试。
			m_Engine.CompleteJobs();
		}
		void Update(int64_t NowMs = 1000, bool Enabled = true)
		{
			m_Lyrics.Update(m_SourceId, m_Source, Enabled, NowMs, m_pStorage.get(), &m_Engine, &m_Http);
		}
		SSourceSnapshot Apply() const { return m_Lyrics.Apply(m_SourceId, m_Source); }
		std::string CachePath() const
		{
			return CommunityCachePath(CommunitySong(m_SourceId, m_Source.m_SongId, m_Source.m_CatalogId));
		}
		void WriteCache(std::string_view Text)
		{
			const auto Path = CachePath();
			ASSERT_FALSE(Path.empty());
			for(size_t Slash = Path.find('/'); Slash != std::string::npos; Slash = Path.find('/', Slash + 1))
				m_pStorage->CreateFolder(Path.substr(0, Slash).c_str(), IStorage::TYPE_SAVE);
			const IOHANDLE File = m_pStorage->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Text.data(), Text.size()), Text.size());
			EXPECT_EQ(io_close(File), 0);
		}
		std::string ReadCache() const
		{
			char *pText = m_pStorage->ReadFileStr(CachePath().c_str(), IStorage::TYPE_SAVE);
			if(!pText)
				return {};
			const std::string Text(pText);
			free(pText);
			return Text;
		}
		void BeginDownload(int64_t NowMs = 1000)
		{
			const size_t RequestsBefore = m_Http.m_vRequests.size();
			Update(NowMs);
			EXPECT_EQ(m_Http.m_vRequests.size(), RequestsBefore);
			m_Engine.CompleteJobs();
			Update(NowMs + 500);
			ASSERT_EQ(m_Http.m_vRequests.size(), RequestsBefore + 1);
		}
		void FinishDownload(std::string_view Text = LYRICS, int64_t NowMs = 1600)
		{
			ASSERT_FALSE(m_Http.m_vRequests.empty());
			ASSERT_NO_FATAL_FAILURE(m_Http.m_vRequests.back()->Finish(200, Text));
			Update(NowMs);
			m_Engine.CompleteJobs();
			Update(NowMs + 1);
		}
	};
}

TEST_F(CommunityLyrics, ValidCacheIsReadBeforeAnyNetworkRequest)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	ASSERT_EQ(m_Engine.m_vJobs.size(), 1u);
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	m_Engine.CompleteJobs();
	Update(10000);
	const auto Result = Apply();
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	EXPECT_TRUE(Result.m_HasWordTiming);
	EXPECT_EQ(Result.m_Original, "春风");
	ASSERT_NE(Result.m_pData, nullptr);
	ASSERT_EQ(Result.m_pData->m_Timeline.m_vLines[0].m_vWords.size(), 2u);
	EXPECT_EQ(Result.m_pData->m_Timeline.m_vLines[0].m_vWords[1].m_StartMs, 1500);
}

TEST_F(CommunityLyrics, DownloadedCacheSurvivesServiceRecreation)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(FinishDownload());
	ASSERT_EQ(Apply().m_Original, "春风");
	m_Lyrics.Reset();
	CCommunityLyrics Restored(CreateResponse);
	Restored.Update(m_SourceId, m_Source, true, 5000, m_pStorage.get(), &m_Engine, &m_Http);
	m_Engine.CompleteJobs();
	Restored.Update(m_SourceId, m_Source, true, 6000, m_pStorage.get(), &m_Engine, &m_Http);
	EXPECT_EQ(Restored.Apply(m_SourceId, m_Source).m_Original, "春风");
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(CommunityLyrics, DownloadIsPublishedAndSavedOnlyAfterValidationCompletes)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(m_Http.m_vRequests.back()->Finish(200, LYRICS));
	Update(1600);
	ASSERT_EQ(m_Engine.m_vJobs.size(), 1u);
	EXPECT_FALSE(m_pStorage->FileExists(CachePath().c_str(), IStorage::TYPE_SAVE));
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	m_Engine.CompleteJobs();
	Update(1601);
	EXPECT_EQ(ReadCache(), LYRICS);
	EXPECT_EQ(Apply().m_Original, "春风");
}

TEST_F(CommunityLyrics, InvalidDownloadDoesNotReplaceTheOriginalOrWriteACache)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(FinishDownload("<html>temporary failure</html>"));
	EXPECT_FALSE(m_pStorage->FileExists(CachePath().c_str(), IStorage::TYPE_SAVE));
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	EXPECT_EQ(Apply().m_Translation, m_Source.m_Translation);
	EXPECT_FALSE(Apply().m_HasWordTiming);
	Update(2000);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(CommunityLyrics, CorruptCacheFallsBackToDownloadAndIsReplacedAfterValidation)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache("damaged cache"));
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(m_Http.m_vRequests.back()->Finish(200, LYRICS));
	Update(1600);
	EXPECT_EQ(ReadCache(), "damaged cache");
	m_Engine.CompleteJobs();
	Update(1601);
	EXPECT_EQ(ReadCache(), LYRICS);
	EXPECT_EQ(Apply().m_Original, "春风");
}

TEST_F(CommunityLyrics, NotFoundKeepsTheOriginalAndRetriesOnlyAfterTenMinutes)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(m_Http.m_vRequests.back()->Finish(404));
	Update(2000);
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	EXPECT_FALSE(Apply().m_HasWordTiming);
	Update(601999);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	Update(602000);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
	EXPECT_FALSE(m_pStorage->FileExists(CachePath().c_str(), IStorage::TYPE_SAVE));
}

TEST_F(CommunityLyrics, NetworkFailureBacksOffAndCanRecoverWithoutChangingSongs)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	m_Http.m_vRequests.back()->Fail();
	Update(2000);
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	Update(61999);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	Update(62000);
	ASSERT_EQ(m_Http.m_vRequests.size(), 2u);
	ASSERT_NO_FATAL_FAILURE(FinishDownload(LYRICS, 62100));
	EXPECT_EQ(Apply().m_Original, "春风");
	EXPECT_EQ(ReadCache(), LYRICS);
}

TEST_F(CommunityLyrics, IdentityChangesCancelPendingRequestsAndRejectTheirLateResults)
{
	const auto Original = m_Source;
	for(int Field = 0; Field < 3; ++Field)
	{
		SCOPED_TRACE(Field);
		m_Lyrics.Reset();
		m_Source = Original;
		ASSERT_NO_FATAL_FAILURE(BeginDownload());
		const auto pOldRequest = m_Http.m_vRequests.back();
		if(Field == 0)
			++m_Source.m_SongId;
		else if(Field == 1)
			++m_Source.m_Generation;
		else
			++m_Source.m_ProducerPid;
		Update(2000);
		EXPECT_TRUE(pOldRequest->m_Aborted);
		ASSERT_NO_FATAL_FAILURE(pOldRequest->Finish(200, LYRICS));
		m_Engine.CompleteJobs();
		Update(2500);
		EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
		EXPECT_FALSE(Apply().m_HasWordTiming);
	}
}

TEST_F(CommunityLyrics, SwitchingSongsWhileDownloadedLyricsAreBeingParsedCannotPublishTheOldSong)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(m_Http.m_vRequests.back()->Finish(200, LYRICS));
	Update(1600);
	ASSERT_EQ(m_Engine.m_vJobs.size(), 1u);
	++m_Source.m_SongId;
	Update(1700);
	m_Engine.CompleteJobs();
	Update(1800);
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	EXPECT_FALSE(Apply().m_HasWordTiming);
	m_Engine.CompleteJobs();
	Update(2200);
	ASSERT_NO_FATAL_FAILURE(FinishDownload(OTHER_LYRICS, 2300));
	EXPECT_EQ(Apply().m_Original, "星河");
}

TEST_F(CommunityLyrics, SwitchingSongsWhileReadingTheCacheDiscardsTheOldResult)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	++m_Source.m_SongId;
	Update(1100);
	m_Engine.CompleteJobs();
	Update(1200);
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	EXPECT_FALSE(Apply().m_HasWordTiming);
	m_Engine.CompleteJobs();
	Update(1600);
	ASSERT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(CommunityLyrics, DisablingCancelsTheDownloadAndReenablingCanLoadAgain)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	const auto pOldRequest = m_Http.m_vRequests.back();
	Update(1600, false);
	EXPECT_TRUE(pOldRequest->m_Aborted);
	ASSERT_NO_FATAL_FAILURE(pOldRequest->Finish(200, LYRICS));
	Update(2000, false);
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
	ASSERT_NO_FATAL_FAILURE(BeginDownload(3000));
	ASSERT_NO_FATAL_FAILURE(FinishDownload(LYRICS, 3600));
	EXPECT_EQ(Apply().m_Original, "春风");
}

TEST_F(CommunityLyrics, ResetCancelsTheRequestAndDoesNotReuseItsLateResult)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	const auto pOldRequest = m_Http.m_vRequests.back();
	m_Lyrics.Reset();
	m_Lyrics.Reset();
	EXPECT_TRUE(pOldRequest->m_Aborted);
	ASSERT_NO_FATAL_FAILURE(pOldRequest->Finish(200, LYRICS));
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	ASSERT_NO_FATAL_FAILURE(BeginDownload(3000));
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	ASSERT_NO_FATAL_FAILURE(FinishDownload(OTHER_LYRICS, 3600));
	EXPECT_EQ(Apply().m_Original, "星河");
}

TEST_F(CommunityLyrics, NativeWordTimingAvoidsCacheAndNetworkWork)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	m_Source.m_HasWordTiming = true;
	Update();
	Update(10000);
	EXPECT_TRUE(m_Engine.m_vJobs.empty());
	EXPECT_TRUE(m_Http.m_vRequests.empty());
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
}

TEST_F(CommunityLyrics, LateNativeWordTimingCancelsAPendingCommunityRequest)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	const auto pRequest = m_Http.m_vRequests.back();
	m_Source.m_HasWordTiming = true;
	Update(1600);
	EXPECT_TRUE(pRequest->m_Aborted);
	ASSERT_NO_FATAL_FAILURE(pRequest->Finish(200, LYRICS));
	Update(1700);
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	EXPECT_TRUE(Apply().m_HasWordTiming);
	EXPECT_EQ(Apply().m_pData, m_Source.m_pData);
}

TEST_F(CommunityLyrics, NativeWordTimingReplacesAlreadyLoadedCommunityLyrics)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(FinishDownload());
	ASSERT_EQ(Apply().m_Original, "春风");
	m_Source.m_HasWordTiming = true;
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	Update(2000);
	EXPECT_EQ(Apply().m_pData, m_Source.m_pData);
	EXPECT_EQ(m_Http.m_vRequests.size(), 1u);
}

TEST_F(CommunityLyrics, TheSameNumericIdUsesSeparateNeteaseAndQqMusicCaches)
{
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(FinishDownload());
	const auto NeteasePath = CachePath();
	m_SourceId = 4;
	m_Source.m_CatalogId = "qqmusic:id:42";
	EXPECT_NE(CachePath(), NeteasePath);
	ASSERT_NO_FATAL_FAILURE(BeginDownload(3000));
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	ASSERT_NO_FATAL_FAILURE(FinishDownload(OTHER_LYRICS, 3600));
	EXPECT_EQ(Apply().m_Original, "星河");
	EXPECT_EQ(ReadCache(), OTHER_LYRICS);
	m_SourceId = 1;
	m_Source.m_CatalogId = {};
	Update(4000);
	m_Engine.CompleteJobs();
	Update(5000);
	EXPECT_EQ(Apply().m_Original, "春风");
	EXPECT_EQ(ReadCache(), LYRICS);
	EXPECT_EQ(m_Http.m_vRequests.size(), 2u);
}

TEST_F(CommunityLyrics, SongIdentityAndPlaybackPositionCanLoadLyricsWithoutAnOriginalLine)
{
	m_Source.m_Active = false;
	m_Source.m_Original = {};
	m_Source.m_Translation = {};
	m_Source.m_LineStartMs = m_Source.m_LineEndMs = -1;
	ASSERT_NO_FATAL_FAILURE(BeginDownload());
	ASSERT_NO_FATAL_FAILURE(FinishDownload());
	const auto Result = Apply();
	EXPECT_TRUE(Result.m_Active);
	EXPECT_TRUE(Result.m_HasWordTiming);
	EXPECT_EQ(Result.m_Original, "春风");
	EXPECT_EQ(Result.m_LineStartMs, 1000);
	EXPECT_EQ(Result.m_PositionMs, m_Source.m_PositionMs);
}

TEST_F(CommunityLyrics, PausedPlaybackDoesNotAdvanceWhenWallTimePasses)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	m_Engine.CompleteJobs();
	Update(2000);
	const auto Paused = Apply();
	Update(100000);
	const auto StillPaused = Apply();
	EXPECT_EQ(StillPaused.m_PositionMs, Paused.m_PositionMs);
	EXPECT_EQ(StillPaused.m_Original, Paused.m_Original);
	EXPECT_EQ(StillPaused.m_LineStartMs, Paused.m_LineStartMs);
	EXPECT_TRUE(m_Http.m_vRequests.empty());
}

TEST_F(CommunityLyrics, SeekingUsesThePlayersPositionInBothDirectionsAndPreservesInstrumentalGaps)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	m_Engine.CompleteJobs();
	Update(2000);
	m_Source.m_PositionMs = 4500;
	EXPECT_EQ(Apply().m_Original, "秋雨");
	EXPECT_EQ(Apply().m_PositionMs, 4500);
	m_Source.m_PositionMs = 3500;
	EXPECT_TRUE(Apply().m_Original.empty());
	m_Source.m_PositionMs = 1500;
	EXPECT_EQ(Apply().m_Original, "春风");
	EXPECT_EQ(Apply().m_PositionMs, 1500);
	EXPECT_TRUE(m_Http.m_vRequests.empty());
}

TEST_F(CommunityLyrics, MissingPlaybackPositionKeepsTheOriginalSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	m_Engine.CompleteJobs();
	Update(2000);
	m_Source.m_PositionMs = -1;
	EXPECT_EQ(Apply().m_Original, m_Source.m_Original);
	EXPECT_EQ(Apply().m_PositionMs, -1);
	EXPECT_FALSE(Apply().m_HasWordTiming);
}

TEST_F(CommunityLyrics, DifferentCommunityTextDoesNotBorrowThePlayersTranslation)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	m_Engine.CompleteJobs();
	Update(2000);
	EXPECT_EQ(Apply().m_Original, "春风");
	EXPECT_TRUE(Apply().m_Translation.empty());
}

TEST_F(CommunityLyrics, MatchingCommunityTextKeepsThePlayersTranslation)
{
	m_Source.m_Original = "春风";
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	m_Engine.CompleteJobs();
	Update(2000);
	EXPECT_EQ(Apply().m_Translation, m_Source.m_Translation);
}

TEST_F(CommunityLyrics, CommunityChineseWordsReachTheWorldLyricsTimeline)
{
	ASSERT_NO_FATAL_FAILURE(WriteCache(LYRICS));
	Update();
	m_Engine.CompleteJobs();
	Update(2000);
	CWorldLyricsState World;
	// 原来源的句起点不同，显示必须采用社区句起点和播放器位置。
	m_Source.m_LineStartMs = 500;
	m_Source.m_PositionMs = 1250;
	World.Update(true, m_SourceId, Apply(), 0, vec2(0, 0), 1.0);
	EXPECT_EQ(World.Current().m_aRows[0], "春风");
	EXPECT_EQ(WorldLyricVisibleCharacters(World.Current(), 0), 1);
	m_Source.m_PositionMs = 1600;
	World.Update(true, m_SourceId, Apply(), 0, vec2(0, 0), 2.0);
	EXPECT_EQ(WorldLyricVisibleCharacters(World.Current(), 0), 2);
	m_Source.m_PositionMs = 1250;
	World.Update(true, m_SourceId, Apply(), 0, vec2(0, 0), 3.0);
	EXPECT_EQ(WorldLyricVisibleCharacters(World.Current(), 0), 1);
}
