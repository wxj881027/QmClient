#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_TOKEN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_TOKEN_H

#include <cstdint>
#include <string>
#include <string_view>

// Spotify Web Player token 链路(移植自 Lyricify-Lyrics-Helper 的公开实现):
// sp_dc cookie → open.spotify.com/api/token 的 TOTP 挑战参数。
// 本模块只做纯计算(可单测),不发起网络请求。
namespace QmSpotifyToken
{
	struct STotpSecret
	{
		std::string m_Secret;
		std::string m_Version;
	};

	// 内置兜底 secret 载荷(社区维护的 spotify-secrets 镜像不可用时的回退)。
	std::string BundledSecretPayload();

	// 解析 secret 载荷 {"59":[...],"60":[...]}:取数字 key 最大的一组,
	// 每个值按 value[i] ^ ((i % 33) + 9) 还原为十进制字符串后拼接。
	bool ParseSecretPayload(std::string_view Json, STotpSecret *pOut);

	// RFC 6238 TOTP(周期 30s、6 位、HMAC-SHA1)。
	std::string GenerateTotp(const std::string &Secret, int64_t ServerTimeSeconds);

	// 归一化用户粘贴的 sp_dc(容忍整段 cookie "sp_dc=...; ...")。
	std::string NormalizeSpDc(std::string_view Value);

	// 组装 open.spotify.com/api/token 完整 URL。
	// Legacy=true 时用旧参数(reason=transport + ts),用于服务端拒绝新参数后的降级。
	std::string BuildTokenUrl(const STotpSecret &Secret, int64_t ServerTimeSeconds, bool Legacy);
} // namespace QmSpotifyToken

#endif
