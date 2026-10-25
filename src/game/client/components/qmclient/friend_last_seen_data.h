#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_LAST_SEEN_DATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_LAST_SEEN_DATA_H

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

enum class EQmFriendLastSeenStatus
{
	LOADING,
	READY,
	NO_DATA,
	ERROR,
};

struct SQmFriendLastSeenEntry
{
	std::string m_Date;
	EQmFriendLastSeenStatus m_Status = EQmFriendLastSeenStatus::LOADING;
	int64_t m_CheckedAt = 0;
	int64_t m_RetryAt = 0;
	int64_t m_VisibleAt = 0;
	bool m_Queued = false;
};

struct SQmFriendLastSeenRequest
{
	uint64_t m_Id;
	std::string m_Name;
};

// 空字符串表示没有历史记录，nullopt 表示响应无效；日期保持 DDStats 的 UTC 日精度。
std::optional<std::string> QmParseDdStatsLastSeen(std::string_view Body, const char *pPlayerName);

class CQmFriendLastSeenCache
{
	std::map<std::string, SQmFriendLastSeenEntry, std::less<>> m_Entries;
	std::deque<std::string> m_Queue;
	std::optional<SQmFriendLastSeenRequest> m_ActiveRequest;
	uint64_t m_RequestId = 0;
	int64_t m_NextRequestAt = 0;
	bool m_Dirty = false;

public:
	static constexpr int64_t CACHE_SECONDS = 24 * 60 * 60;
	static constexpr int64_t RETRY_SECONDS = 15 * 60;

	const SQmFriendLastSeenEntry *Get(const char *pName, int64_t Now);
	const SQmFriendLastSeenEntry *Find(const char *pName) const;
	std::optional<SQmFriendLastSeenRequest> NextRequest(int64_t Now);
	bool IsCurrent(uint64_t RequestId) const;
	void Complete(uint64_t RequestId, std::optional<std::string> Date, int64_t Now, int64_t RetryAfterSeconds = 0);
	void CancelRequests();
	void RetainNames(const std::unordered_set<std::string> &Names);
	std::string Serialize() const;
	bool Load(std::string_view Body, int64_t Now);
	bool Dirty() const { return m_Dirty; }
};

#endif
