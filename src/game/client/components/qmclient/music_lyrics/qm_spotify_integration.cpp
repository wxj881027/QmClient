#include "qm_spotify_integration.h"

#include "qm_spotify_parser.h"
#include "qm_spotify_token.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/http.h>

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
	constexpr const char *SERVER_TIME_URL = "https://open.spotify.com/api/server-time";
	constexpr const char *PATHFINDER_URL_BASE = "https://api-partner.spotify.com/pathfinder/v1/query";
	constexpr const char *LYRICS_URL_BASE = "https://spclient.wg.spotify.com/color-lyrics/v2/track/";
	constexpr const char *LRCLIB_GET_URL = "https://lrclib.net/api/get";
	constexpr const char *LRCLIB_SEARCH_URL = "https://lrclib.net/api/search";
	// pathfinder searchDesktop 的 persisted query(两个 hash 轮换,坏一个换一个)。
	constexpr const char *SEARCH_HASHES[2] = {
		"0dff51c99e552b992377a2a6f40d213dc42b62db86ca0bcf16cf3934aec1aae6",
		"75bbf6bfcfdf85b8fc828417bfad92b7cd66bf7f556d85670f4da8292373ebec",
	};
	// open.spotify.com/api/token 的 TOTP secret 镜像(社区维护,按序轮询)。
	constexpr const char *SECRET_MIRROR_URLS[3] = {
		"https://code.thetadev.de/ThetaDev/spotify-secrets/raw/branch/main/secrets/secretDict.json",
		"https://raw.githubusercontent.com/Thereallo1026/spotify-secrets/refs/heads/main/secrets/secretDict.json",
		"https://raw.githubusercontent.com/xyloflake/spot-secrets-go/main/secrets/secretDict.json",
	};
	constexpr int64_t TOKEN_REFRESH_SKEW_MS = 60 * 1000; // token 过期前 1 分钟视为需刷新
	constexpr int64_t RETRY_DELAY_MS = 5000;
	constexpr int64_t TOKEN_FAILURE_DELAY_MS = 30 * 1000; // sp_dc 无效等持久失败
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

	uint64_t MonotonicTickMs()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
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
	enum class ETokenState : uint8_t
	{
		None,
		FetchSecret,
		FetchServerTime,
		FetchToken,
		Valid,
		Failed,
	};

	// 配置。
	bool m_ConfigInitialized = false;
	bool m_LastEnabled = false;
	std::string m_LastSpDc;
	std::string m_SpDc;

	// token 状态。
	ETokenState m_TokenState = ETokenState::None;
	int m_SecretMirrorIndex = 0;
	bool m_SecretFromBundled = false;
	QmSpotifyToken::STotpSecret m_Secret;
	std::string m_AccessToken;
	int64_t m_TokenExpirationMs = 0;
	bool m_TokenLegacyAttempted = false;
	std::shared_ptr<CHttpRequest> m_pSecretRequest;
	std::shared_ptr<CHttpRequest> m_pServerTimeRequest;
	std::shared_ptr<CHttpRequest> m_pTokenRequest;
	int64_t m_NextTokenRetryTick = 0;

	// 歌曲身份。
	std::string m_IdentityTitle;
	std::string m_IdentityArtist;
	bool m_HasIdentity = false;
	uint64_t m_SongId = 0;

	// 歌曲数据请求。
	std::shared_ptr<CHttpRequest> m_pSearchRequest;
	int m_SearchAttempt = 0; // 0/1 = pathfinder 双 hash,2 = /v1/search
	bool m_SearchFailed = false;
	std::shared_ptr<CHttpRequest> m_pLyricsRequest;
	int m_RetryCount = 0; // 本首歌已消耗的 token 刷新重试次数(搜索/歌词共用)
	std::string m_PendingTrackId;
	std::string m_PendingIsrc;
	std::shared_ptr<CHttpRequest> m_pLrclibRequest;

	// 歌词数据。
	QmMusicLyrics::SLyricsData m_Lyrics;
	bool m_HasLyrics = false;
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
		if(m_pSecretRequest)
			m_pSecretRequest->Abort();
		if(m_pServerTimeRequest)
			m_pServerTimeRequest->Abort();
		if(m_pTokenRequest)
			m_pTokenRequest->Abort();
		m_pSecretRequest.reset();
		m_pServerTimeRequest.reset();
		m_pTokenRequest.reset();
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
		m_PendingIsrc.clear();
		m_Lyrics = {};
		m_HasLyrics = false;
		m_ActiveLyrics = false;
		m_PositionMs = 0;
		m_CurrentLyric.clear();
	}

	void ResetTokenState()
	{
		AbortTokenRequests();
		m_TokenState = ETokenState::None;
		m_SecretMirrorIndex = 0;
		m_SecretFromBundled = false;
		m_Secret = {};
		m_AccessToken.clear();
		m_TokenExpirationMs = 0;
		m_TokenLegacyAttempted = false;
		m_NextTokenRetryTick = 0;
	}
};

CSpotifyIntegration::CSpotifyIntegration() :
	m_pImpl(std::make_unique<SImpl>()) {}

CSpotifyIntegration::~CSpotifyIntegration() = default;

void CSpotifyIntegration::OnInit()
{
	m_pImpl->m_ConfigInitialized = false;
	m_pImpl->m_LastEnabled = false;
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
	const std::string SpDc = QmSpotifyToken::NormalizeSpDc(g_Config.m_QmSpotifySpDc);
	if(!m_pImpl->m_ConfigInitialized || Enabled != m_pImpl->m_LastEnabled || SpDc != m_pImpl->m_LastSpDc)
	{
		m_pImpl->m_ConfigInitialized = true;
		m_pImpl->m_LastEnabled = Enabled;
		m_pImpl->m_LastSpDc = SpDc;
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
		const int64_t ElapsedTicks = time() - MediaState.m_PositionUpdatedTick;
		const double Rate = MediaState.m_PlaybackRate > 0.0 ? MediaState.m_PlaybackRate : 1.0;
		if(ElapsedTicks > 0)
			PositionMs += (int64_t)(ElapsedTicks * Rate * 1000.0 / (double)time_freq());
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
	const int64_t NowMs = MonotonicTickMs();

	// 已有有效 token 且未临近过期 → 直接可用。
	if(m_pImpl->m_TokenState == SImpl::ETokenState::Valid)
	{
		if(m_pImpl->m_TokenExpirationMs > NowMs + TOKEN_REFRESH_SKEW_MS)
			return;
		m_pImpl->m_TokenState = SImpl::ETokenState::None;
		m_pImpl->m_TokenLegacyAttempted = false;
	}
	if(m_pImpl->m_NextTokenRetryTick > NowMs)
		return;
	// 退避到期:Failed → None 重新走完整链路。
	if(m_pImpl->m_TokenState == SImpl::ETokenState::Failed)
		m_pImpl->m_TokenState = SImpl::ETokenState::None;

	auto FinishTokenFailure = [&](int64_t DelayMs) {
		m_pImpl->m_TokenState = SImpl::ETokenState::Failed;
		m_pImpl->m_NextTokenRetryTick = MonotonicTickMs() + DelayMs;
		m_pImpl->AbortTokenRequests();
	};

	// 阶段 1:secret 获取(镜像轮询,全部失败用内置兜底)。
	if(m_pImpl->m_TokenState == SImpl::ETokenState::None)
	{
		// 已有 secret(token 过期刷新场景)直接进入 server-time 阶段。
		if(!m_pImpl->m_Secret.m_Secret.empty())
		{
			m_pImpl->m_TokenState = SImpl::ETokenState::FetchServerTime;
			return;
		}
		if(m_pImpl->m_SecretMirrorIndex < 3)
		{
			m_pImpl->m_TokenState = SImpl::ETokenState::FetchSecret;
			auto pRequest = std::make_shared<CHttpRequest>(SECRET_MIRROR_URLS[m_pImpl->m_SecretMirrorIndex]);
			pRequest->LogProgress(HTTPLOG::FAILURE);
			pRequest->FailOnErrorStatus(false);
			pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
			pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
			pRequest->HeaderString("User-Agent", LRCLIB_USER_AGENT);
			m_pImpl->m_pSecretRequest = pRequest;
			Http()->Run(pRequest);
			return;
		}
		// 内置兜底(同步)。
		m_pImpl->m_SecretFromBundled = true;
		m_pImpl->m_SecretMirrorIndex = 3;
		if(!QmSpotifyToken::ParseSecretPayload(QmSpotifyToken::BundledSecretPayload(), &m_pImpl->m_Secret))
		{
			FinishTokenFailure(TOKEN_FAILURE_DELAY_MS);
			return;
		}
		m_pImpl->m_TokenState = SImpl::ETokenState::FetchServerTime;
	}
	if(m_pImpl->m_TokenState == SImpl::ETokenState::FetchSecret)
	{
		std::shared_ptr<CHttpRequest> pRequest = m_pImpl->m_pSecretRequest;
		if(pRequest == nullptr || !pRequest->Done())
			return;
		m_pImpl->m_pSecretRequest.reset();
		bool Ok = false;
		if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataLength = 0;
			pRequest->Result(&pData, &DataLength);
			if(pData != nullptr)
			{
				const std::string_view Json((const char *)pData, DataLength);
				Ok = QmSpotifyToken::ParseSecretPayload(Json, &m_pImpl->m_Secret);
			}
		}
		if(Ok)
		{
			m_pImpl->m_TokenState = SImpl::ETokenState::FetchServerTime;
		}
		else
		{
			++m_pImpl->m_SecretMirrorIndex;
			m_pImpl->m_TokenState = SImpl::ETokenState::None;
		}
		return;
	}
	// 阶段 2:服务器时间。
	if(m_pImpl->m_TokenState == SImpl::ETokenState::FetchServerTime)
	{
		auto pRequest = std::make_shared<CHttpRequest>(SERVER_TIME_URL);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		m_pImpl->m_pServerTimeRequest = pRequest;
		m_pImpl->m_TokenState = SImpl::ETokenState::FetchToken;
		m_pImpl->m_pTokenRequest = nullptr;
		Http()->Run(pRequest);
		return;
	}
	// 阶段 3:token。
	if(m_pImpl->m_TokenState == SImpl::ETokenState::FetchToken)
	{
		std::shared_ptr<CHttpRequest> pTimeRequest = m_pImpl->m_pServerTimeRequest;
		if(pTimeRequest == nullptr)
		{
			// server-time 已完成并发出 token 请求时,等待其完成;否则视为状态异常。
			if(m_pImpl->m_pTokenRequest != nullptr)
				return;
			FinishTokenFailure(RETRY_DELAY_MS);
			return;
		}
		if(!pTimeRequest->Done())
			return;
		m_pImpl->m_pServerTimeRequest.reset();
		int64_t ServerTime = 0;
		if(pTimeRequest->State() == EHttpState::DONE && pTimeRequest->StatusCode() == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataLength = 0;
			pTimeRequest->Result(&pData, &DataLength);
			if(pData != nullptr)
				QmSpotify::ParseServerTime(std::string_view((const char *)pData, DataLength), &ServerTime);
		}
		if(ServerTime <= 0)
		{
			FinishTokenFailure(RETRY_DELAY_MS);
			return;
		}
		const std::string TokenUrl = QmSpotifyToken::BuildTokenUrl(m_pImpl->m_Secret, ServerTime, m_pImpl->m_TokenLegacyAttempted);
		auto pRequest = std::make_shared<CHttpRequest>(TokenUrl.c_str());
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("App-Platform", "WebPlayer");
		pRequest->HeaderString("Origin", "https://open.spotify.com");
		pRequest->HeaderString("Referer", "https://open.spotify.com/");
		const std::string Cookie = "sp_dc=" + m_pImpl->m_SpDc;
		pRequest->HeaderString("Cookie", Cookie.c_str());
		m_pImpl->m_pTokenRequest = pRequest;
		Http()->Run(pRequest);
		return;
	}
	// 阶段 4:token 完成。
	if(m_pImpl->m_pTokenRequest != nullptr && m_pImpl->m_pTokenRequest->Done())
	{
		std::shared_ptr<CHttpRequest> pRequest = m_pImpl->m_pTokenRequest;
		m_pImpl->m_pTokenRequest.reset();
		const int Status = pRequest->StatusCode();
		if(pRequest->State() == EHttpState::DONE && Status == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataLength = 0;
			pRequest->Result(&pData, &DataLength);
			std::string AccessToken;
			int64_t ExpirationMs = 0;
			if(pData != nullptr &&
				QmSpotify::ParseTokenResponse(std::string_view((const char *)pData, DataLength), &AccessToken, &ExpirationMs))
			{
				m_pImpl->m_AccessToken = std::move(AccessToken);
				m_pImpl->m_TokenExpirationMs = ExpirationMs;
				m_pImpl->m_TokenState = SImpl::ETokenState::Valid;
				return;
			}
		}
		if(Status == 400 && !m_pImpl->m_TokenLegacyAttempted)
		{
			// 新参数被拒 → 降级旧参数重试一次。
			m_pImpl->m_TokenLegacyAttempted = true;
			m_pImpl->m_TokenState = SImpl::ETokenState::FetchServerTime;
			return;
		}
		if(Status == 401 || Status == 403)
		{
			dbg_msg("spotify", "sp_dc cookie rejected (HTTP %d), check qm_spotify_sp_dc", Status);
			FinishTokenFailure(TOKEN_FAILURE_DELAY_MS);
			return;
		}
		FinishTokenFailure(RETRY_DELAY_MS);
	}
}

void CSpotifyIntegration::PollSongPipeline()
{
	// 需要 token 才能搜索/取歌词;失败时不打扰已有歌词显示。
	if(m_pImpl->m_TokenState != SImpl::ETokenState::Valid || m_pImpl->m_AccessToken.empty())
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
		auto pRequest = std::make_shared<CHttpRequest>(Url.c_str());
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("Authorization", ("Bearer " + m_pImpl->m_AccessToken).c_str());
		m_pImpl->m_pSearchRequest = pRequest;
		Http()->Run(pRequest);
		return;
	}
	if(!m_pImpl->m_pSearchRequest->Done())
		return;
	std::shared_ptr<CHttpRequest> pRequest = m_pImpl->m_pSearchRequest;
	m_pImpl->m_pSearchRequest.reset();
	const int Status = pRequest->StatusCode();
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
			m_pImpl->m_TokenState = SImpl::ETokenState::None;
			m_pImpl->m_TokenLegacyAttempted = false;
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
		if(m_pImpl->m_PendingTrackId.empty())
			return;
		const std::string Url = std::string(LYRICS_URL_BASE) + m_pImpl->m_PendingTrackId + "?format=json&market=from_token";
		auto pRequest = std::make_shared<CHttpRequest>(Url.c_str());
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("App-platform", "WebPlayer");
		pRequest->HeaderString("Authorization", ("Bearer " + m_pImpl->m_AccessToken).c_str());
		m_pImpl->m_pLyricsRequest = pRequest;
		Http()->Run(pRequest);
		return;
	}
	if(!m_pImpl->m_pLyricsRequest->Done())
		return;
	std::shared_ptr<CHttpRequest> pRequest = m_pImpl->m_pLyricsRequest;
	m_pImpl->m_pLyricsRequest.reset();
	const int Status = pRequest->StatusCode();
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
			m_pImpl->m_TokenState = SImpl::ETokenState::None;
			m_pImpl->m_TokenLegacyAttempted = false;
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
	auto pRequest = std::make_shared<CHttpRequest>(Url.c_str());
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
	std::shared_ptr<CHttpRequest> pRequest = m_pImpl->m_pLrclibRequest;
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
