#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_USERS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_USERS_H

#include "qmclient_utils.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

struct SQmRealtimeMessage;

inline constexpr const char *QMCLIENT_USERS_SYNC_CAPABILITY = "users-sync-zlib-v1";
inline constexpr size_t QMCLIENT_USERS_SYNC_MAX_BYTES = 2 * 1024 * 1024;

// 只接受带有 QMU1 标识的名单帧，不把任意二进制内容交给其他实时事件。
bool ParseQmCompressedUsersMessage(const char *pData, size_t Size, SQmRealtimeMessage &Out);

class CQmRealtimeUsersState
{
public:
	enum class EApplyResult
	{
		APPLIED,
		IGNORED,
		RESYNC,
	};

	EApplyResult Apply(const json_value *pData, const char *pExpectedServer);
	SQmClientUsersParseResult Result() const;
	void Reset();
	bool NeedsFull() const { return m_NeedsFull; }
	int LeaseSeconds() const { return m_LeaseSeconds; }
	int64_t Revision() const { return m_Revision; }

private:
	std::map<std::string, SQmClientServerDistribution> m_Servers;
	std::map<std::string, SQmClientRecognitionMark> m_Players;
	std::string m_ServerAddress;
	int64_t m_Revision = 0;
	int m_LeaseSeconds = 0;
	bool m_NeedsFull = false;
};

#endif
