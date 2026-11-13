#include "music_lyrics_community.h"

#include "music_lyrics_community_format.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>

#include <memory>
#include <utility>

namespace QmMusicLyrics
{
	namespace
	{
		constexpr int64_t RETRY_MS = 60 * 1000;
		constexpr int64_t NOT_FOUND_RETRY_MS = 10 * 60 * 1000;

		class CLyricsJob final : public IJob
		{
			IStorage *m_pStorage;
			std::string m_Path;
			std::string m_Text;

			void Run() override
			{
				if(!m_Downloaded)
				{
					const std::unique_ptr<void, decltype(&io_close)> File(m_pStorage->OpenFile(m_Path.c_str(), IOFLAG_READ, IStorage::TYPE_SAVE), io_close);
					if(!File)
						return;
					const int64_t Length = io_length(File.get());
					if(Length > 0 && static_cast<uint64_t>(Length) <= COMMUNITY_LYRICS_MAX_BYTES)
					{
						m_Text.resize(static_cast<size_t>(Length));
						if(io_read(File.get(), m_Text.data(), static_cast<unsigned>(Length)) != static_cast<unsigned>(Length))
							m_Text.clear();
					}
				}
				m_Parsed = ParseCommunityLyrics(m_Text, &m_Data);
				if(!m_Parsed || !m_Downloaded)
					return;

				// 校验后再写同目录临时文件，失败时保留原缓存。
				m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
				m_pStorage->CreateFolder("qmclient/lyrics", IStorage::TYPE_SAVE);
				const auto Folder = m_Path.substr(0, m_Path.rfind('/'));
				m_pStorage->CreateFolder(Folder.c_str(), IStorage::TYPE_SAVE);
				char aTemporary[IO_MAX_PATH_LENGTH];
				IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), m_Path.c_str());
				const IOHANDLE File = m_pStorage->OpenFile(aTemporary, IOFLAG_WRITE, IStorage::TYPE_SAVE);
				if(!File)
					return;
				const bool Written = io_write(File, m_Text.data(), static_cast<unsigned>(m_Text.size())) == m_Text.size();
				const bool Closed = io_close(File) == 0;
				m_Saved = Written && Closed && m_pStorage->RenameFile(aTemporary, m_Path.c_str(), IStorage::TYPE_SAVE, false);
			}

		public:
			uint64_t m_Epoch;
			bool m_Downloaded;
			bool m_Parsed = false;
			bool m_Saved = false;
			SLyricsData m_Data;

			CLyricsJob(IStorage *pStorage, std::string Path, uint64_t Epoch, bool Downloaded = false, std::string Text = {}) :
				m_pStorage(pStorage), m_Path(std::move(Path)), m_Text(std::move(Text)), m_Epoch(Epoch), m_Downloaded(Downloaded) {}
		};
	}

	struct CCommunityLyrics::SImpl
	{
		TRequestFactory m_CreateRequest;
		int m_SourceId = 0;
		uint64_t m_SongId = 0;
		uint64_t m_Generation = 0;
		uint32_t m_Pid = 0;
		std::string m_CatalogId;
		SCommunitySong m_Song;
		uint64_t m_Epoch = 0;
		int64_t m_NextAttemptMs = 0;
		bool m_CacheRead = false;
		std::shared_ptr<IHttpRequest> m_pRequest;
		std::shared_ptr<CLyricsJob> m_pJob;
		SLyricsData m_Data;

		bool Matches(int SourceId, const SSourceSnapshot &Source) const
		{
			return SourceId == m_SourceId && Source.m_SongId == m_SongId &&
			       Source.m_Generation == m_Generation && Source.m_ProducerPid == m_Pid && Source.m_CatalogId == m_CatalogId;
		}
	};

	CCommunityLyrics::CCommunityLyrics(TRequestFactory CreateRequest) :
		m_pImpl(std::make_unique<SImpl>())
	{
		m_pImpl->m_CreateRequest = CreateRequest;
	}

	CCommunityLyrics::~CCommunityLyrics() { Reset(); }

	void CCommunityLyrics::Reset()
	{
		if(m_pImpl->m_pRequest)
			m_pImpl->m_pRequest->Abort();
		m_pImpl->m_pRequest.reset();
		m_pImpl->m_SourceId = 0;
		m_pImpl->m_SongId = 0;
		m_pImpl->m_CatalogId.clear();
		m_pImpl->m_Song = {};
		m_pImpl->m_Data = {};
		m_pImpl->m_CacheRead = false;
		m_pImpl->m_NextAttemptMs = 0;
		++m_pImpl->m_Epoch;
		// 旧磁盘任务完成后再接下一个，避免同名临时缓存并发写入。
	}

	void CCommunityLyrics::Update(int SourceId, const SSourceSnapshot &Source, bool Enabled, int64_t NowMs,
		IStorage *pStorage, IEngine *pEngine, IHttp *pHttp)
	{
		if(!Enabled || Source.m_SongId == 0 || Source.m_HasWordTiming ||
			(SourceId != 1 && SourceId != 4 && SourceId != 5))
		{
			if(m_pImpl->m_SourceId != 0)
				Reset();
			return;
		}
		if(!m_pImpl->Matches(SourceId, Source))
		{
			Reset();
			m_pImpl->m_SourceId = SourceId;
			m_pImpl->m_SongId = Source.m_SongId;
			m_pImpl->m_Generation = Source.m_Generation;
			m_pImpl->m_Pid = Source.m_ProducerPid;
			m_pImpl->m_CatalogId = Source.m_CatalogId;
			m_pImpl->m_Song = CommunitySong(SourceId, Source.m_SongId, Source.m_CatalogId);
			// 允许原来源的解析任务先完成；缓存读取无需等待网络。
			m_pImpl->m_NextAttemptMs = NowMs + 500;
		}
		if(m_pImpl->m_Song.m_Source == ECommunitySource::NONE || !pStorage || !pEngine || !pHttp)
			return;
		if(m_pImpl->m_pJob)
		{
			if(!m_pImpl->m_pJob->Finished())
				return;
			auto pJob = std::move(m_pImpl->m_pJob);
			if(pJob->State() == IJob::STATE_DONE && pJob->m_Epoch == m_pImpl->m_Epoch)
			{
				if(pJob->m_Parsed)
					m_pImpl->m_Data = std::move(pJob->m_Data);
				if(pJob->m_Downloaded && pJob->m_Parsed && !pJob->m_Saved)
					log_warn("lyrics", "AMLL lyrics loaded, but the local cache could not be saved");
			}
		}
		if(m_pImpl->m_Data.HasLyrics())
			return;
		if(!m_pImpl->m_CacheRead)
		{
			m_pImpl->m_CacheRead = true;
			m_pImpl->m_pJob = std::make_shared<CLyricsJob>(pStorage, CommunityCachePath(m_pImpl->m_Song), m_pImpl->m_Epoch);
			pEngine->AddJob(m_pImpl->m_pJob);
			return;
		}
		if(m_pImpl->m_pRequest)
		{
			if(!m_pImpl->m_pRequest->Done())
				return;
			auto pRequest = std::move(m_pImpl->m_pRequest);
			const int Status = pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : -1;
			m_pImpl->m_NextAttemptMs = NowMs + (Status == 404 ? NOT_FOUND_RETRY_MS : RETRY_MS);
			if(Status == 200)
			{
				unsigned char *pData = nullptr;
				size_t Size = 0;
				pRequest->Result(&pData, &Size);
				if(pData && Size > 0 && Size <= COMMUNITY_LYRICS_MAX_BYTES)
				{
					m_pImpl->m_pJob = std::make_shared<CLyricsJob>(pStorage, CommunityCachePath(m_pImpl->m_Song),
						m_pImpl->m_Epoch, true, std::string(reinterpret_cast<const char *>(pData), Size));
					pEngine->AddJob(m_pImpl->m_pJob);
				}
			}
			return;
		}
		if(NowMs < m_pImpl->m_NextAttemptMs)
			return;
		const auto Url = CommunityUrl(m_pImpl->m_Song);
		std::shared_ptr<IHttpRequest> pRequest = m_pImpl->m_CreateRequest ? m_pImpl->m_CreateRequest(Url.c_str()) : HttpGet(Url.c_str());
		pRequest->Timeout(CTimeout{4000, 10000, 500, 5});
		pRequest->MaxResponseSize(COMMUNITY_LYRICS_MAX_BYTES);
		pRequest->FailOnErrorStatus(false);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		m_pImpl->m_pRequest = pRequest;
		pHttp->Run(std::move(pRequest));
	}

	SSourceSnapshot CCommunityLyrics::Apply(int SourceId, const SSourceSnapshot &Source) const
	{
		if(Source.m_HasWordTiming || !m_pImpl->Matches(SourceId, Source) || !m_pImpl->m_Data.HasLyrics() || Source.m_PositionMs < 0)
			return Source;
		const auto Selected = NeteaseLyrics::SelectCurrentLine(m_pImpl->m_Data.m_Timeline, Source.m_PositionMs);
		SSourceSnapshot Result = Source;
		Result.m_Active = true;
		Result.m_pData = &m_pImpl->m_Data;
		Result.m_HasWordTiming = true;
		Result.m_Original = Selected.m_pLine ? std::string_view(Selected.m_pLine->m_Text) : std::string_view();
		// 两个来源当前句文字相同才保留原译文，不能给社区句子配上另一句翻译。
		Result.m_Translation = Result.m_Original == Source.m_Original ? Source.m_Translation : std::string_view();
		Result.m_LineStartMs = Selected.m_pLine ? Selected.m_pLine->m_StartMs : -1;
		Result.m_LineEndMs = Selected.m_pLine ? Selected.m_pLine->m_EndMs : -1;
		return Result;
	}
}
