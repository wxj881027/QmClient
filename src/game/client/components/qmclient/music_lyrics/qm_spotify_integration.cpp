#include "qm_spotify_integration.h"

#include "music_lyrics_community_format.h"
#include "qm_spotify_auth.h"
#include "qm_spotify_parser.h"
#include "qm_spotify_progress.h"
#include "qm_spotify_token.h"

#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>
#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>
#include <game/client/components/system_media_controls.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace
{
	constexpr const char *SPOTIFY_USER_AGENT =
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
	constexpr const char *LRCLIB_USER_AGENT = "QmClient/1.0 (+https://github.com/Q1menG/QmClient)";
	constexpr const char *PATHFINDER_URL_BASE = "https://api-partner.spotify.com/pathfinder/v1/query";
	constexpr const char *LYRICS_URL_BASE = "https://spclient.wg.spotify.com/color-lyrics/v2/track/";
	constexpr const char *LRCLIB_GET_URL = "https://lrclib.net/api/get";
	constexpr const char *LRCLIB_SEARCH_URL = "https://lrclib.net/api/search";
	// pathfinder searchDesktop 的 persisted query(两个 hash 轮换,坏一个换一个)。
	constexpr const char *SEARCH_HASHES[2] = {
		"0dff51c99e552b992377a2a6f40d213dc42b62db86ca0bcf16cf3934aec1aae6",
		"75bbf6bfcfdf85b8fc828417bfad92b7cd66bf7f556d85670f4da8292373ebec",
	};
	constexpr int64_t MAX_RESPONSE_SIZE = 2 << 20;

	bool ContainsAsciiInsensitive(std::string_view Text, std::string_view Needle)
	{
		if(Needle.empty() || Text.size() < Needle.size())
			return false;
		for(size_t Offset = 0; Offset <= Text.size() - Needle.size(); ++Offset)
		{
			bool Match = true;
			for(size_t Index = 0; Index < Needle.size(); ++Index)
			{
				const char TextChar = Text[Offset + Index] >= 'A' && Text[Offset + Index] <= 'Z' ? (char)(Text[Offset + Index] - 'A' + 'a') : Text[Offset + Index];
				const char NeedleChar = Needle[Index] >= 'A' && Needle[Index] <= 'Z' ? (char)(Needle[Index] - 'A' + 'a') : Needle[Index];
				if(TextChar != NeedleChar)
				{
					Match = false;
					break;
				}
			}
			if(Match)
				return true;
		}
		return false;
	}

	bool IsSpotifySourceAppId(std::string_view SourceAppId)
	{
		return ContainsAsciiInsensitive(SourceAppId, "spotify");
	}

	int64_t MonotonicTickMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	int64_t UnixTickMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	// FNV-1a 64:歌曲身份(title+artist)的稳定哈希。
	uint64_t IdentityHash(std::string_view Title, std::string_view Artist)
	{
		uint64_t Hash = 1469598103934665603ull;
		auto Mix = [&Hash](char C) {
			Hash ^= (unsigned char)C;
			Hash *= 1099511628211ull;
		};
		for(char C : Title)
			Mix(C);
		Mix('\x1f');
		for(char C : Artist)
			Mix(C);
		return Hash;
	}

	// 简单 JSON 字符串转义(搜索词进入 variables JSON)。
	std::string JsonEscape(std::string_view Text)
	{
		std::string Result;
		Result.reserve(Text.size() + 8);
		for(char C : Text)
		{
			if(C == '"' || C == '\\')
			{
				Result += '\\';
				Result += C;
			}
			else if(C == '\n')
			{
				Result += "\\n";
			}
			else if(C == '\r')
			{
				Result += "\\r";
			}
			else if(C == '\t')
			{
				Result += "\\t";
			}
			else
			{
				Result += C;
			}
		}
		return Result;
	}

	// 百分号编码(保留 A-Za-z0-9-._~)。
	std::string UrlEncode(std::string_view Text)
	{
		constexpr const char HEX[] = "0123456789ABCDEF";
		std::string Result;
		Result.reserve(Text.size() * 3);
		for(unsigned char C : Text)
		{
			if((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '-' || C == '.' || C == '_' || C == '~')
			{
				Result += (char)C;
			}
			else
			{
				Result += '%';
				Result += HEX[C >> 4];
				Result += HEX[C & 0x0F];
			}
		}
		return Result;
	}
} // namespace

struct CSpotifyIntegration::SImpl
{
	// 配置。
	bool m_ConfigInitialized = false;
	bool m_LastEnabled = false;
	std::string m_LastRawSpDc;
	std::string m_LastSpDc;
	std::string m_SpDc;

	CSpotifyAuthPipeline m_Auth;

	// 歌曲身份。
	std::string m_IdentityTitle;
	std::string m_IdentityArtist;
	bool m_HasIdentity = false;
	uint64_t m_SongId = 0;

	// 歌曲数据请求。
	std::shared_ptr<IHttpRequest> m_pSearchRequest;
	int m_SearchAttempt = 0; // 0/1 = pathfinder 双 hash,2 = /v1/search
	bool m_SearchFailed = false;
	std::shared_ptr<IHttpRequest> m_pLyricsRequest;
	int m_RetryCount = 0; // 本首歌已消耗的 token 刷新重试次数(搜索/歌词共用)
	std::string m_PendingTrackId;
	std::string m_CatalogTrackId;
	std::string m_PendingIsrc;
	std::shared_ptr<IHttpRequest> m_pLrclibRequest;

	// 歌词数据。
	QmMusicLyrics::SLyricsData m_Lyrics;
	bool m_HasLyrics = false;
	bool m_HasWordTiming = false;
	bool m_ActiveLyrics = false;

	// 进度与当前句。
	int64_t m_PositionMs = 0;
	std::string m_CurrentLyric;

	void AbortSongRequests()
	{
		if(m_pSearchRequest)
			m_pSearchRequest->Abort();
		if(m_pLyricsRequest)
			m_pLyricsRequest->Abort();
		if(m_pLrclibRequest)
			m_pLrclibRequest->Abort();
		m_pSearchRequest.reset();
		m_pLyricsRequest.reset();
		m_pLrclibRequest.reset();
	}

	void AbortTokenRequests()
	{
		m_Auth.AbortRequests();
	}

	void ResetSongData()
	{
		AbortSongRequests();
		m_IdentityTitle.clear();
		m_IdentityArtist.clear();
		m_HasIdentity = false;
		m_SongId = 0;
		m_SearchAttempt = 0;
		m_SearchFailed = false;
		m_RetryCount = 0;
		m_PendingTrackId.clear();
		m_CatalogTrackId.clear();
		m_HasWordTiming = false;
		m_PendingIsrc.clear();
		m_Lyrics = {};
		m_HasLyrics = false;
		m_ActiveLyrics = false;
		m_PositionMs = 0;
		m_CurrentLyric.clear();
	}

	void ResetTokenState()
	{
		m_Auth.Reset();
	}
};

CSpotifyIntegration::CSpotifyIntegration() :
	m_pImpl(std::make_unique<SImpl>()) {}

CSpotifyIntegration::~CSpotifyIntegration() = default;

void CSpotifyIntegration::OnInit()
{
	m_pImpl->m_ConfigInitialized = false;
	m_pImpl->m_LastEnabled = false;
	m_pImpl->m_LastRawSpDc.clear();
	m_pImpl->m_LastSpDc.clear();
	m_pImpl->m_SpDc.clear();
	m_pImpl->ResetTokenState();
	m_pImpl->ResetSongData();
}

void CSpotifyIntegration::OnShutdown()
{
	m_pImpl->AbortTokenRequests();
	m_pImpl->ResetSongData();
}

void CSpotifyIntegration::OnReset()
{
	// 地图切换等重置:歌曲数据清空,token 保留避免重复鉴权。
	m_pImpl->ResetSongData();
}

void CSpotifyIntegration::OnUpdate()
{
	if(GameClient() == nullptr)
		return;

	// 配置同步:开关 / sp_dc 变化。
	const bool Enabled = g_Config.m_QmSpotifyEnable != 0;
	bool SpDcChanged = false;
	if(!m_pImpl->m_ConfigInitialized || m_pImpl->m_LastRawSpDc != g_Config.m_QmSpotifySpDc)
	{
		m_pImpl->m_LastRawSpDc = g_Config.m_QmSpotifySpDc;
		const std::string NormalizedSpDc = QmSpotifyToken::NormalizeSpDc(g_Config.m_QmSpotifySpDc);
		SpDcChanged = NormalizedSpDc != m_pImpl->m_LastSpDc;
		m_pImpl->m_LastSpDc = NormalizedSpDc;
	}
	if(!m_pImpl->m_ConfigInitialized || Enabled != m_pImpl->m_LastEnabled || SpDcChanged)
	{
		m_pImpl->m_ConfigInitialized = true;
		m_pImpl->m_LastEnabled = Enabled;
		const std::string &SpDc = m_pImpl->m_LastSpDc;
		if(Enabled && !SpDc.empty() && SpDc != m_pImpl->m_SpDc)
		{
			m_pImpl->m_SpDc = SpDc;
			m_pImpl->ResetTokenState();
			m_pImpl->ResetSongData();
		}
		else if(!Enabled || SpDc.empty())
		{
			m_pImpl->m_SpDc.clear();
			m_pImpl->ResetTokenState();
			m_pImpl->ResetSongData();
		}
	}
	if(!m_pImpl->m_LastEnabled || m_pImpl->m_SpDc.empty())
	{
		m_pImpl->ResetSongData();
		return;
	}

	// SMTC 是标准媒体状态的唯一权威来源。
	CSystemMediaControls::SState MediaState{};
	const bool HasMedia = GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState);
	if(!HasMedia || MediaState.m_PlaybackState == CSystemMediaControls::EPlaybackState::Stopped)
	{
		m_pImpl->ResetSongData();
		return;
	}
	if(MediaState.m_aSourceAppId[0] != '\0' && !IsSpotifySourceAppId(MediaState.m_aSourceAppId))
	{
		m_pImpl->ResetSongData();
		return;
	}
	const std::string_view Title(MediaState.m_aTitle);
	const std::string_view Artist(MediaState.m_aArtist);
	if(Title.empty() && Artist.empty())
	{
		m_pImpl->ResetSongData();
		return;
	}

	// 切歌检测:标题/歌手变化 → 重建歌曲数据链路。
	if(!m_pImpl->m_HasIdentity ||
		std::string_view(m_pImpl->m_IdentityTitle) != Title ||
		std::string_view(m_pImpl->m_IdentityArtist) != Artist)
	{
		m_pImpl->ResetSongData();
		m_pImpl->m_HasIdentity = true;
		m_pImpl->m_IdentityTitle.assign(Title);
		m_pImpl->m_IdentityArtist.assign(Artist);
		m_pImpl->m_SongId = IdentityHash(Title, Artist);
	}

	// 播放状态与进度。
	int64_t PositionMs = MediaState.m_PositionMs;
	if(MediaState.m_Playing && MediaState.m_PositionUpdatedTick > 0)
	{
		// SMTC 进度约 1s 更新一次,用本地时钟外推平滑选句。
		PositionMs = QmSpotify::ExtrapolatePositionMs(PositionMs, MediaState.m_PositionUpdatedTick, time(), time_freq(), MediaState.m_PlaybackRate);
	}
	m_pImpl->m_PositionMs = std::max<int64_t>(0, PositionMs);

	// token 链路(失败有退避,不影响已有歌词显示)。
	PollTokenPipeline();

	// 歌曲数据链路(搜索 → 歌词 → LRCLIB 兜底)。
	PollSongPipeline();
	PollSongPipelineLyrics();
	PollSongPipelineLrclib();

	// 当前句选择。
	if(m_pImpl->m_HasLyrics)
	{
		const NeteaseLyrics::SSelectedLine Selected = NeteaseLyrics::SelectCurrentLine(m_pImpl->m_Lyrics.m_Timeline, m_pImpl->m_PositionMs);
		if(Selected.m_pLine == nullptr)
			m_pImpl->m_CurrentLyric.clear();
		else
			m_pImpl->m_CurrentLyric = Selected.m_pLine->m_Text;
		m_pImpl->m_ActiveLyrics = true;
	}
	else
	{
		m_pImpl->m_CurrentLyric.clear();
		m_pImpl->m_ActiveLyrics = false;
	}
}

void CSpotifyIntegration::PollTokenPipeline()
{
	m_pImpl->m_Auth.Poll(Http(), m_pImpl->m_SpDc, MonotonicTickMs(), UnixTickMs());
}

void CSpotifyIntegration::PollSongPipeline()
{
	// 需要 token 才能搜索/取歌词;失败时不打扰已有歌词显示。
	if(!m_pImpl->m_Auth.HasToken())
		return;
	if(!m_pImpl->m_HasIdentity || m_pImpl->m_HasLyrics)
		return;

	// 搜索(未开始或请求完成)。
	if(m_pImpl->m_pSearchRequest == nullptr)
	{
		if(m_pImpl->m_SearchFailed || m_pImpl->m_SearchAttempt > 2)
			return;
		const std::string Term = m_pImpl->m_IdentityTitle + " " + m_pImpl->m_IdentityArtist;
		std::string Url;
		if(m_pImpl->m_SearchAttempt < 2)
		{
			const std::string Variables = "{\"searchTerm\":\"" + JsonEscape(Term) + "\",\"offset\":0,\"limit\":10,\"numberOfTopResults\":5}";
			const std::string Extensions = "{\"persistedQuery\":{\"version\":1,\"sha256Hash\":\"" + std::string(SEARCH_HASHES[m_pImpl->m_SearchAttempt]) + "\"}}";
			Url = std::string(PATHFINDER_URL_BASE) + "?operationName=searchDesktop&variables=" +
			      UrlEncode(Variables) + "&extensions=" + UrlEncode(Extensions);
		}
		else
		{
			Url = "https://api.spotify.com/v1/search?q=" + UrlEncode(Term) + "&type=track&limit=10&market=from_token";
		}
		auto pRequest = std::shared_ptr<IHttpRequest>(HttpGet(Url.c_str()));
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("Authorization", ("Bearer " + m_pImpl->m_Auth.AccessToken()).c_str());
		m_pImpl->m_pSearchRequest = pRequest;
		Http()->Run(pRequest);
		return;
	}
	if(!m_pImpl->m_pSearchRequest->Done())
		return;
	std::shared_ptr<IHttpRequest> pRequest = m_pImpl->m_pSearchRequest;
	m_pImpl->m_pSearchRequest.reset();
	// 网络失败时终态非 DONE，读取 StatusCode 会触发断言；以 -1 走通用失败路径
	const int Status = pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : -1;
	if(pRequest->State() == EHttpState::DONE && Status == 200)
	{
		unsigned char *pData = nullptr;
		size_t DataLength = 0;
		pRequest->Result(&pData, &DataLength);
		std::vector<QmSpotify::STrackCandidate> Candidates;
		if(pData != nullptr &&
			QmSpotify::ParseSearchResponse(std::string_view((const char *)pData, DataLength), &Candidates) &&
			!Candidates.empty())
		{
			// 取第一个非空候选(Spotify 按相关度排序)。
			const QmSpotify::STrackCandidate &Best = Candidates[0];
			m_pImpl->m_PendingTrackId = Best.m_Id;
			m_pImpl->m_CatalogTrackId = Best.m_Id;
			m_pImpl->m_PendingIsrc = Best.m_Isrc;
			m_pImpl->m_RetryCount = 0;
			m_pImpl->m_SearchAttempt = 0;
			return;
		}
	}
	if(Status == 401 || Status == 403)
	{
		// token 失效:强制刷新后重试同一次搜索。
		if(m_pImpl->m_RetryCount < 1)
		{
			m_pImpl->m_RetryCount = 1;
			m_pImpl->m_Auth.RequestRefresh();
			return;
		}
	}
	++m_pImpl->m_SearchAttempt;
	if(m_pImpl->m_SearchAttempt > 2)
		m_pImpl->m_SearchFailed = true;
}

void CSpotifyIntegration::PollSongPipelineLyrics()
{
	// LRCLIB 兜底进行中时不再重复请求 color-lyrics。
	if(m_pImpl->m_pLrclibRequest != nullptr)
		return;
	// 歌词(未开始或完成)。
	if(m_pImpl->m_pLyricsRequest == nullptr)
	{
		if(!m_pImpl->m_Auth.HasToken() || m_pImpl->m_PendingTrackId.empty())
			return;
		const std::string Url = std::string(LYRICS_URL_BASE) + m_pImpl->m_PendingTrackId + "?format=json&market=from_token";
		auto pRequest = std::shared_ptr<IHttpRequest>(HttpGet(Url.c_str()));
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("App-platform", "WebPlayer");
		pRequest->HeaderString("Authorization", ("Bearer " + m_pImpl->m_Auth.AccessToken()).c_str());
		m_pImpl->m_pLyricsRequest = pRequest;
		Http()->Run(pRequest);
		return;
	}
	if(!m_pImpl->m_pLyricsRequest->Done())
		return;
	std::shared_ptr<IHttpRequest> pRequest = m_pImpl->m_pLyricsRequest;
	m_pImpl->m_pLyricsRequest.reset();
	// 网络失败时终态非 DONE，读取 StatusCode 会触发断言；以 -1 走通用失败路径
	const int Status = pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : -1;
	if(pRequest->State() == EHttpState::DONE && Status == 200)
	{
		unsigned char *pData = nullptr;
		size_t DataLength = 0;
		pRequest->Result(&pData, &DataLength);
		QmMusicLyrics::SLyricsData Lyrics;
		if(pData != nullptr &&
			QmSpotify::ParseColorLyrics(std::string_view((const char *)pData, DataLength), &Lyrics) &&
			Lyrics.HasLyrics())
		{
			if(Lyrics.m_Song.m_Title.empty())
				Lyrics.m_Song.m_Title = m_pImpl->m_IdentityTitle;
			if(Lyrics.m_Song.m_Artist.empty())
				Lyrics.m_Song.m_Artist = m_pImpl->m_IdentityArtist;
			m_pImpl->m_Lyrics = std::move(Lyrics);
			m_pImpl->m_HasWordTiming = QmMusicLyrics::HasWordTiming(m_pImpl->m_Lyrics);
			m_pImpl->m_HasLyrics = true;
			m_pImpl->m_PendingTrackId.clear();
			m_pImpl->m_PendingIsrc.clear();
			return;
		}
	}
	if(Status == 401 || Status == 403)
	{
		if(m_pImpl->m_RetryCount < 2)
		{
			m_pImpl->m_RetryCount = 2;
			m_pImpl->m_Auth.RequestRefresh();
			return;
		}
	}
	// color-lyrics 未收录或无时间轴 → LRCLIB 兜底。
	StartLrclibFallback();
}

void CSpotifyIntegration::StartLrclibFallback()
{
	if(m_pImpl->m_pLrclibRequest != nullptr)
		return;
	if(m_pImpl->m_PendingTrackId.empty())
		return;
	std::string Url;
	if(!m_pImpl->m_PendingIsrc.empty())
	{
		Url = std::string(LRCLIB_GET_URL) + "?isrc=" + m_pImpl->m_PendingIsrc;
	}
	else
	{
		Url = std::string(LRCLIB_SEARCH_URL) + "?track_name=" + UrlEncode(m_pImpl->m_IdentityTitle) +
		      "&artist_name=" + UrlEncode(m_pImpl->m_IdentityArtist);
	}
	auto pRequest = std::shared_ptr<IHttpRequest>(HttpGet(Url.c_str()));
	pRequest->LogProgress(HTTPLOG::FAILURE);
	pRequest->FailOnErrorStatus(false);
	pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
	pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
	pRequest->HeaderString("User-Agent", LRCLIB_USER_AGENT);
	m_pImpl->m_pLrclibRequest = pRequest;
	Http()->Run(pRequest);
}

void CSpotifyIntegration::PollSongPipelineLrclib()
{
	if(m_pImpl->m_pLrclibRequest == nullptr)
		return;
	if(!m_pImpl->m_pLrclibRequest->Done())
		return;
	std::shared_ptr<IHttpRequest> pRequest = m_pImpl->m_pLrclibRequest;
	m_pImpl->m_pLrclibRequest.reset();
	if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200)
	{
		unsigned char *pData = nullptr;
		size_t DataLength = 0;
		pRequest->Result(&pData, &DataLength);
		QmMusicLyrics::SLyricsData Lyrics;
		if(pData != nullptr &&
			QmSpotify::ParseLrclib(std::string_view((const char *)pData, DataLength), &Lyrics) &&
			Lyrics.HasLyrics())
		{
			if(Lyrics.m_Song.m_Title.empty())
				Lyrics.m_Song.m_Title = m_pImpl->m_IdentityTitle;
			if(Lyrics.m_Song.m_Artist.empty())
				Lyrics.m_Song.m_Artist = m_pImpl->m_IdentityArtist;
			m_pImpl->m_Lyrics = std::move(Lyrics);
			m_pImpl->m_HasWordTiming = QmMusicLyrics::HasWordTiming(m_pImpl->m_Lyrics);
			m_pImpl->m_HasLyrics = true;
		}
	}
	m_pImpl->m_PendingTrackId.clear();
	m_pImpl->m_PendingIsrc.clear();
}

bool CSpotifyIntegration::GetCurrentLyric(char *pBuffer, size_t BufferSize) const
{
	if(pBuffer == nullptr || BufferSize == 0)
		return false;
	pBuffer[0] = '\0';
	if(!g_Config.m_QmLyrics || !g_Config.m_QmLyricsInMediaIsland)
		return false;
	if(!m_pImpl->m_HasIdentity || !m_pImpl->m_HasLyrics || m_pImpl->m_CurrentLyric.empty())
		return false;
	const std::string Truncated = NeteaseLyrics::TruncateUtf8(m_pImpl->m_CurrentLyric, BufferSize - 1);
	str_copy(pBuffer, Truncated.c_str(), BufferSize);
	return pBuffer[0] != '\0';
}

bool CSpotifyIntegration::HasCurrentLyric() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 &&
	       m_pImpl->m_HasIdentity && m_pImpl->m_HasLyrics && !m_pImpl->m_CurrentLyric.empty();
}

bool CSpotifyIntegration::HasActiveLyrics() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 &&
	       m_pImpl->m_HasIdentity && m_pImpl->m_ActiveLyrics;
}

uint64_t CSpotifyIntegration::CurrentSongId() const
{
	return m_pImpl->m_SongId;
}

QmMusicLyrics::SSourceSnapshot CSpotifyIntegration::LyricsSnapshot() const
{
	if(!m_pImpl->m_HasIdentity)
		return {};
	if(!m_pImpl->m_HasLyrics)
		return {m_pImpl->m_SongId, 0, 0, false, nullptr, {}, {}, -1, -1, m_pImpl->m_PositionMs, m_pImpl->m_CatalogTrackId, false};
	const auto Selected = NeteaseLyrics::SelectCurrentLine(m_pImpl->m_Lyrics.m_Timeline, m_pImpl->m_PositionMs);
	return {m_pImpl->m_SongId, 0, 0, m_pImpl->m_ActiveLyrics, &m_pImpl->m_Lyrics,
		m_pImpl->m_CurrentLyric, QmMusicLyrics::TranslationAt(m_pImpl->m_Lyrics, Selected.m_Index),
		Selected.m_pLine != nullptr ? Selected.m_pLine->m_StartMs : -1,
		Selected.m_pLine != nullptr ? Selected.m_pLine->m_EndMs : -1, m_pImpl->m_PositionMs, m_pImpl->m_CatalogTrackId, m_pImpl->m_HasWordTiming};
}
