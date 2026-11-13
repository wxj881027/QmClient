#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_AUTH_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_AUTH_H

#include "qm_spotify_token.h"

#include <cstdint>
#include <memory>
#include <string>

class IHttp;
class IHttpRequest;

// 拥有完整鉴权阶段及其请求；HTTP 工厂只替换网络边界，轮询仍执行同一份生产逻辑。
class CSpotifyAuthPipeline
{
public:
	using TRequestFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl);
	explicit CSpotifyAuthPipeline(TRequestFactory CreateRequest = nullptr);
	CSpotifyAuthPipeline(const CSpotifyAuthPipeline &) = delete;
	CSpotifyAuthPipeline &operator=(const CSpotifyAuthPipeline &) = delete;

	void Poll(IHttp *pHttp, const std::string &SpDc, int64_t MonotonicNowMs, int64_t UnixNowMs);
	void AbortRequests();
	void Reset();
	void RequestRefresh();
	bool HasToken() const;
	const std::string &AccessToken() const { return m_AccessToken; }

private:
	void FailAfter(int64_t MonotonicNowMs, int64_t DelayMs);

	enum class ETokenState : uint8_t
	{
		None,
		FetchSecret,
		FetchServerTime,
		FetchToken,
		Valid,
		Failed,
	};
	TRequestFactory m_CreateRequest;
	ETokenState m_TokenState = ETokenState::None;
	int m_SecretMirrorIndex = 0;
	QmSpotifyToken::STotpSecret m_Secret;
	std::string m_AccessToken;
	int64_t m_TokenExpirationMs = 0;
	bool m_TokenLegacyAttempted = false;
	std::shared_ptr<IHttpRequest> m_pSecretRequest;
	std::shared_ptr<IHttpRequest> m_pServerTimeRequest;
	std::shared_ptr<IHttpRequest> m_pTokenRequest;
	int64_t m_NextTokenRetryTick = 0;
};

#endif
