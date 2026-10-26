#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_USERS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_USERS_H

#include "qmclient_utils.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <utility>

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

	// 合并分配失败保留旧名单并请求全量；同修订全量可恢复需要同步的状态。
	EApplyResult Apply(const json_value *pData, const char *pExpectedServer);
	// 发布器失败前不得修改外部状态；接收成功后才提交内部修订和名单。
	template<typename TPublish>
	EApplyResult ApplyAndPublish(const json_value *pData, const char *pExpectedServer, TPublish &&Publish)
	{
		try
		{
			return ApplyCandidate(pData, pExpectedServer, FPublish(std::forward<TPublish>(Publish)));
		}
		catch(const std::bad_alloc &)
		{
			m_NeedsFull = true;
			return EApplyResult::RESYNC;
		}
	}
	SQmClientUsersParseResult Result() const;
	void Reset();
	bool NeedsFull() const { return m_NeedsFull; }
	int LeaseSeconds() const { return m_LeaseSeconds; }
	int64_t Revision() const { return m_Revision; }

private:
	using FPublish = std::function<bool(SQmClientUsersParseResult, int)>;
	EApplyResult ApplyCandidate(const json_value *pData, const char *pExpectedServer, const FPublish &Publish);
	static SQmClientUsersParseResult BuildResult(const std::map<std::string, SQmClientServerDistribution> &Servers, const std::map<std::string, SQmClientRecognitionMark> &Players, bool Parsed);

	std::map<std::string, SQmClientServerDistribution> m_Servers;
	std::map<std::string, SQmClientRecognitionMark> m_Players;
	std::string m_ServerAddress;
	int64_t m_Revision = 0;
	int m_LeaseSeconds = 0;
	bool m_NeedsFull = false;
};

#endif
