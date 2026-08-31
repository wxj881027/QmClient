#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_CRYPTO_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_CRYPTO_H

#include <cstddef>
#include <string>

// Spotify TOTP(open.spotify.com/api/token)所需的 SHA1 / HMAC-SHA1。
// 仅用于 TOTP 挑战,不用于任何安全敏感场景。
namespace QmSpotifyCrypto
{
	// 计算 HMAC-SHA1,返回 20 字节原始摘要。
	std::string HmacSha1(const unsigned char *pKey, size_t KeyLen, const unsigned char *pData, size_t DataLen);
} // namespace QmSpotifyCrypto

#endif
