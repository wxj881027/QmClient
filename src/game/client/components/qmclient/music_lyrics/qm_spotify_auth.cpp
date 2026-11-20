#include "qm_spotify_auth.h"

#include "qm_spotify_parser.h"

#include <base/system.h>

#include <engine/http.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
	constexpr const char *SPOTIFY_USER_AGENT =
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
	constexpr const char *LRCLIB_USER_AGENT = "QmClient/1.0 (+https://github.com/Q1menG/QmClient)";
	constexpr const char *SERVER_TIME_URL = "https://open.spotify.com/api/server-time";
	constexpr const char *SECRET_MIRROR_URLS[3] = {
		"https://code.thetadev.de/ThetaDev/spotify-secrets/raw/branch/main/secrets/secretDict.json",
		"https://raw.githubusercontent.com/Thereallo1026/spotify-secrets/refs/heads/main/secrets/secretDict.json",
		"https://raw.githubusercontent.com/xyloflake/spot-secrets-go/main/secrets/secretDict.json",
	};
	constexpr int64_t TOKEN_REFRESH_SKEW_MS = 60 * 1000;
	constexpr int64_t RETRY_DELAY_MS = 5000;
	constexpr int64_t TOKEN_FAILURE_DELAY_MS = 30 * 1000;
	constexpr int64_t MAX_RESPONSE_SIZE = 2 << 20;
}

CSpotifyAuthPipeline::CSpotifyAuthPipeline(TRequestFactory CreateRequest) :
	m_CreateRequest(CreateRequest != nullptr ? CreateRequest : HttpGet) {}

void CSpotifyAuthPipeline::AbortRequests()
{
	auto pSecretRequest = std::move(m_pSecretRequest);
	auto pServerTimeRequest = std::move(m_pServerTimeRequest);
	auto pTokenRequest = std::move(m_pTokenRequest);
	if(pSecretRequest)
		pSecretRequest->Abort();
	if(pServerTimeRequest)
		pServerTimeRequest->Abort();
	if(pTokenRequest)
		pTokenRequest->Abort();
}

void CSpotifyAuthPipeline::Reset()
{
	AbortRequests();
	m_TokenState = ETokenState::None;
	m_SecretMirrorIndex = 0;
	m_Secret = {};
	m_AccessToken.clear();
	m_TokenExpirationMs = 0;
	m_TokenLegacyAttempted = false;
	m_NextTokenRetryTick = 0;
}

void CSpotifyAuthPipeline::RequestRefresh()
{
	// 旧歌词响应也可能请求刷新；已有鉴权继续完成当前阶段。
	if(m_TokenState == ETokenState::FetchSecret || m_TokenState == ETokenState::FetchServerTime || m_TokenState == ETokenState::FetchToken)
		return;
	m_TokenState = ETokenState::None;
	m_TokenLegacyAttempted = false;
}

bool CSpotifyAuthPipeline::HasToken() const
{
	return m_TokenState == ETokenState::Valid && !m_AccessToken.empty();
}

void CSpotifyAuthPipeline::FailAfter(int64_t MonotonicNowMs, int64_t DelayMs)
{
	m_TokenState = ETokenState::Failed;
	m_NextTokenRetryTick = MonotonicNowMs > std::numeric_limits<int64_t>::max() - DelayMs ? std::numeric_limits<int64_t>::max() : MonotonicNowMs + DelayMs;
	AbortRequests();
}

void CSpotifyAuthPipeline::Poll(IHttp *pHttp, const std::string &SpDc, int64_t MonotonicNowMs, int64_t UnixNowMs)
try
{
	if(pHttp == nullptr)
		return;

	// 已有有效 token 且未临近过期 → 直接可用。
	if(m_TokenState == ETokenState::Valid)
	{
		// 响应到期值是正 Unix 毫秒，提前量先相减，不和单调时钟混用。
		if(UnixNowMs < m_TokenExpirationMs - TOKEN_REFRESH_SKEW_MS)
			return;
		m_TokenState = ETokenState::None;
		m_TokenLegacyAttempted = false;
	}
	if(m_NextTokenRetryTick > MonotonicNowMs)
		return;
	// 退避到期:Failed → None 重新走完整链路。
	if(m_TokenState == ETokenState::Failed)
		m_TokenState = ETokenState::None;

	auto FinishTokenFailure = [&](int64_t DelayMs) {
		FailAfter(MonotonicNowMs, DelayMs);
	};

	// 阶段 1:secret 获取(镜像轮询,全部失败用内置兜底)。
	if(m_TokenState == ETokenState::None)
	{
		// 已有 secret(token 过期刷新场景)直接进入 server-time 阶段。
		if(!m_Secret.m_Secret.empty())
		{
			m_TokenState = ETokenState::FetchServerTime;
			return;
		}
		if(m_SecretMirrorIndex < 3)
		{
			m_TokenState = ETokenState::FetchSecret;
			auto pRequest = std::shared_ptr<IHttpRequest>(m_CreateRequest(SECRET_MIRROR_URLS[m_SecretMirrorIndex]));
			pRequest->LogProgress(HTTPLOG::FAILURE);
			pRequest->FailOnErrorStatus(false);
			pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
			pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
			pRequest->HeaderString("User-Agent", LRCLIB_USER_AGENT);
			m_pSecretRequest = pRequest;
			pHttp->Run(pRequest);
			return;
		}
		// 内置兜底(同步)。
		m_SecretMirrorIndex = 3;
		QmSpotifyToken::STotpSecret Secret;
		if(!QmSpotifyToken::ParseSecretPayload(QmSpotifyToken::BundledSecretPayload(), &Secret))
		{
			FinishTokenFailure(TOKEN_FAILURE_DELAY_MS);
			return;
		}
		m_Secret = std::move(Secret);
		m_TokenState = ETokenState::FetchServerTime;
	}
	if(m_TokenState == ETokenState::FetchSecret)
	{
		std::shared_ptr<IHttpRequest> pRequest = m_pSecretRequest;
		if(pRequest == nullptr || !pRequest->Done())
			return;
		m_pSecretRequest.reset();
		bool Ok = false;
		if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataLength = 0;
			pRequest->Result(&pData, &DataLength);
			if(pData != nullptr)
			{
				const std::string_view Json((const char *)pData, DataLength);
				QmSpotifyToken::STotpSecret Secret;
				Ok = QmSpotifyToken::ParseSecretPayload(Json, &Secret);
				if(Ok)
					m_Secret = std::move(Secret);
			}
		}
		if(Ok)
		{
			m_TokenState = ETokenState::FetchServerTime;
		}
		else
		{
			++m_SecretMirrorIndex;
			m_TokenState = ETokenState::None;
		}
		return;
	}
	// 阶段 2:服务器时间。
	if(m_TokenState == ETokenState::FetchServerTime)
	{
		auto pRequest = std::shared_ptr<IHttpRequest>(m_CreateRequest(SERVER_TIME_URL));
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		m_pServerTimeRequest = pRequest;
		m_TokenState = ETokenState::FetchToken;
		m_pTokenRequest = nullptr;
		pHttp->Run(pRequest);
		return;
	}
	// 阶段 3:token。
	// 请求已发出时直接交给完成阶段，不再等待已经释放的 server-time 请求。
	if(m_TokenState == ETokenState::FetchToken && m_pTokenRequest == nullptr)
	{
		std::shared_ptr<IHttpRequest> pTimeRequest = m_pServerTimeRequest;
		if(pTimeRequest == nullptr)
		{
			FinishTokenFailure(RETRY_DELAY_MS);
			return;
		}
		if(!pTimeRequest->Done())
			return;
		m_pServerTimeRequest.reset();
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
		const std::string TokenUrl = QmSpotifyToken::BuildTokenUrl(m_Secret, ServerTime, m_TokenLegacyAttempted);
		auto pRequest = std::shared_ptr<IHttpRequest>(m_CreateRequest(TokenUrl.c_str()));
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{5000, 10000, 500, 10});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("User-Agent", SPOTIFY_USER_AGENT);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("App-Platform", "WebPlayer");
		pRequest->HeaderString("Origin", "https://open.spotify.com");
		pRequest->HeaderString("Referer", "https://open.spotify.com/");
		const std::string Cookie = "sp_dc=" + SpDc;
		pRequest->HeaderString("Cookie", Cookie.c_str());
		m_pTokenRequest = pRequest;
		pHttp->Run(pRequest);
		return;
	}
	// 阶段 4:token 完成。
	if(m_pTokenRequest != nullptr && m_pTokenRequest->Done())
	{
		std::shared_ptr<IHttpRequest> pRequest = m_pTokenRequest;
		m_pTokenRequest.reset();
		// 网络失败时终态非 DONE，读取 StatusCode 会触发断言；以 -1 走通用失败路径
		const int Status = pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : -1;
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
				m_AccessToken = std::move(AccessToken);
				m_TokenExpirationMs = ExpirationMs;
				m_TokenState = ETokenState::Valid;
				return;
			}
		}
		if(Status == 400 && !m_TokenLegacyAttempted)
		{
			// 新参数被拒 → 降级旧参数重试一次。
			m_TokenLegacyAttempted = true;
			m_TokenState = ETokenState::FetchServerTime;
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
catch(const std::bad_alloc &)
{
	FailAfter(MonotonicNowMs, RETRY_DELAY_MS);
}
catch(const std::length_error &)
{
	FailAfter(MonotonicNowMs, RETRY_DELAY_MS);
}
