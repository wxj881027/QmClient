#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

typedef struct _json_value json_value;

enum class EQmRealtimeEvent
{
	INVALID,
	UNKNOWN,
	PING,
	PONG,
	STATE,
	BROADCAST,
	SPONSORS,
	TITLES,
	USERS,
	DEVELOPERS,
	PLAYTIME,
	TIME,
	TITLE_PROFILE,
	TITLE_STATUS,
	EMOTICON,
	ERROR,
};

struct SQmRealtimeMessage
{
	EQmRealtimeEvent m_Event = EQmRealtimeEvent::INVALID;
	std::string m_Type;
	bool m_HasOnlineUsers = false;
	bool m_HasOnlineDummies = false;
	int m_OnlineUsers = 0;
	int m_OnlineDummies = 0;
	bool m_StatePayloadValid = false;
	bool m_HasBroadcast = false;
	std::string m_BroadcastMarkdown;
	int m_BroadcastVersion = 0;
	bool m_HasTitles = false;
	std::shared_ptr<const json_value> m_pTitlePayload;
	bool m_HasEmoticon = false;
	int m_Emoticon = -1;
	int m_PlayerId = -1;
	bool m_LaunchMode = false;
	bool m_SuperLaunch = false;
	// 旧消费者仍读取这些字段；共享接线迁移后可一并移除。
	bool m_EmoticonPayloadValid = false;
	int m_EmoticonPlayerId = -1;
	int m_EmoticonId = -1;
	bool m_EmoticonLaunch = false;
	bool m_EmoticonSuperLaunch = false;
	bool m_HasRealtimeData = false;
	bool m_HasServerTime = false;
	int64_t m_ServerTime = 0;
	bool m_HasPlaytimeSeconds = false;
	int64_t m_PlaytimeSeconds = -1;
	bool m_HasTitleProfile = false;
	bool m_HasTitleText = false;
	std::string m_TitleText;
	bool m_HasTitleBoundName = false;
	std::string m_TitleBoundName;
	bool m_HasTitleStyle = false;
	std::string m_TitleStyle;
	bool m_HasTitleAuthenticated = false;
	bool m_TitleAuthenticated = false;
	bool m_HasTitleStatusCode = false;
	int m_TitleStatusCode = 0;
	std::shared_ptr<const json_value> m_pPayload;
	uint64_t m_EmoticonSequence = 0;
	std::string m_EmoticonClientId;
	std::string m_EmoticonPlayerName;
	std::string m_EmoticonServerAddress;
};

bool ParseQmRealtimeMessage(const char *pData, size_t Size, SQmRealtimeMessage &OutMessage);

// 空配置使用中心服；只有固定加密入口允许发送认证凭据。
const char *QmRealtimeEffectiveUrl(const char *pConfiguredUrl);
bool QmRealtimeAllowsCredentials(const char *pUrl);

#endif
