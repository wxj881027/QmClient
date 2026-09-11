// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_UTILS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_UTILS_H

#include <base/color.h>

#include <engine/shared/client_brand.h>

#include <cstdint>
#include <string>
#include <vector>

typedef struct _json_value json_value;

struct SQmTitlePresence
{
	int m_PlayerId = -1;
	std::string m_PlayerName;
	std::string m_Title;
	int64_t m_RemainingSeconds = 0;
};

std::vector<SQmTitlePresence> ParseQmTitlePresences(const json_value *pRoot, const char *pServerAddress);
bool IsValidQmTitle(const char *pTitle);

struct SQmClientServerDistribution
{
	std::string m_ServerAddress;
	int m_UserCount = 0;
	int m_DummyCount = 0;
};

struct SQmClientRecognitionMark
{
	std::string m_Name;
	bool m_FootParticlesEnabled = false;
	bool m_RemoteParticlesEnabled = false;
	bool m_VoiceSupported = false;
	EClientBrand m_ClientBrand = EClientBrand::QM;
	std::string m_Qid;
};

struct SQmClientUsersParseResult
{
	bool m_Parsed = false;
	std::vector<SQmClientServerDistribution> m_vServerDistribution;
	std::vector<SQmClientRecognitionMark> m_vLocalServerMarks;
	int m_OnlineUserCount = 0;
	int m_OnlineDummyCount = 0;
};

bool ParseQmClientUsersJson(const json_value *pRoot, const char *pServerAddress, SQmClientUsersParseResult &OutResult);

enum class EQmDeveloperBadgeStyle
{
	BLACK,
	RAINBOW,
};

struct SQmDeveloperPresence
{
	std::string m_DeveloperId;
	std::string m_ServerAddress;
	int m_PlayerId = -1;
	std::string m_PlayerName;
	bool m_Dummy = false;
	int64_t m_IssuedAt = 0;
	int64_t m_ExpiresAt = 0;
	int m_StyleBucket = 0;
};

struct SQmDeveloperPresenceParseResult
{
	bool m_Parsed = false;
	int64_t m_ServerTime = 0;
	std::vector<SQmDeveloperPresence> m_vPresences;
};

bool ParseQmDeveloperPresencesJson(const json_value *pRoot, const char *pServerAddress, SQmDeveloperPresenceParseResult &OutResult);
const SQmDeveloperPresence *FindQmDeveloperPresence(const std::vector<SQmDeveloperPresence> &vPresences, const char *pServerAddress, int PlayerId, const char *pPlayerName, int64_t Now);
EQmDeveloperBadgeStyle QmDeveloperBadgeStyleFromBucket(int StyleBucket);
bool ShouldShowQmDeveloperBadge(bool Authenticated, bool ShowName, bool HideIdentity);
bool IsQmDeveloperMarkCurrent(bool Active, const char *pMarkedName, const char *pCurrentName, int64_t ExpireTick, int64_t NowTick);

// [] 内头衔的本地配色方式。默认档保持既有表现：聊天沿用玩家名色，名牌沿用服务器下发的彩虹样式。
enum class EQmTitleColorMode
{
	FOLLOW_SERVER = 0,
	SINGLE = 1,
	RAINBOW = 2,
};

struct SQmTitleColorStyle
{
	EQmTitleColorMode m_Mode = EQmTitleColorMode::FOLLOW_SERVER;
	// 最终是否按彩虹渲染：彩虹档恒为 true；跟随服务器档取决于服务器样式。
	bool m_Rainbow = false;
	// 单色档的填充色，透明度已并入；跟随服务器档不使用该字段。
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	// 本地配置的透明度，仅单色/彩虹档生效。
	float m_Alpha = 1.0f;

	bool operator==(const SQmTitleColorStyle &Other) const
	{
		return m_Mode == Other.m_Mode && m_Rainbow == Other.m_Rainbow && m_Color == Other.m_Color && m_Alpha == Other.m_Alpha;
	}
	bool operator!=(const SQmTitleColorStyle &Other) const { return !(*this == Other); }
};

SQmTitleColorStyle ResolveQmTitleColorStyle(int Mode, unsigned int PackedColor, int Opacity, bool ServerRainbow);
ColorRGBA QmTitleRainbowColor(int CharIndex, int CharCount, float Alpha);

// 与 qm_title_color / qm_title_opacity 的配置默认值保持一致，供设置页重置按钮使用。
inline constexpr unsigned int QM_TITLE_COLOR_DEFAULT = 0xFFFFFF;
inline constexpr int QM_TITLE_OPACITY_DEFAULT = 100;

#endif
