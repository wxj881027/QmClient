#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_POINTS_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_POINTS_STATE_H

#include <cstdint>
#include <map>
#include <string>

enum class EPointsStatus
{
	NOT_REQUESTED,
	FETCHING,
	READY,
	FAILED
};

struct SPlayerPointsResult
{
	EPointsStatus m_Status;
	int m_Points;
};

class CQmPlayerPointsCache
{
public:
	struct SRequestToken
	{
		uint64_t m_Generation = 0;
		uint64_t m_RequestId = 0;
	};

private:
	struct SEntry
	{
		int m_Points = 0;
		EPointsStatus m_Status = EPointsStatus::NOT_REQUESTED;
		int64_t m_LastSuccessTick = 0;
		int64_t m_LastFailureTick = 0;
		uint64_t m_PendingRequestId = 0;
	};
	std::map<std::string, SEntry> m_Entries;
	uint64_t m_Generation = 1;
	uint64_t m_RequestId = 0;

	static bool WithinWindow(int64_t Timestamp, int64_t Now, int64_t Frequency, int64_t WindowMs)
	{
		return Timestamp > 0 && Frequency > 0 && Now >= Timestamp && Now - Timestamp < WindowMs * Frequency / 1000;
	}

	bool IsCurrent(const SEntry &Entry, SRequestToken Token) const
	{
		return Token.m_Generation == m_Generation && Token.m_RequestId != 0 && Entry.m_PendingRequestId == Token.m_RequestId;
	}

public:
	void Load(const std::string &Name, int Points)
	{
		SEntry &Entry = m_Entries[Name];
		Entry = {};
		Entry.m_Points = Points;
		Entry.m_Status = EPointsStatus::READY;
	}

	SPlayerPointsResult Get(const std::string &Name) const
	{
		const auto It = m_Entries.find(Name);
		return It == m_Entries.end() ? SPlayerPointsResult{EPointsStatus::NOT_REQUESTED, 0} : SPlayerPointsResult{It->second.m_Status, It->second.m_Points};
	}

	bool ShouldQuery(const std::string &Name, int64_t Now, int64_t Frequency) const
	{
		const auto It = m_Entries.find(Name);
		if(It == m_Entries.end())
			return true;
		const SEntry &Entry = It->second;
		if(Entry.m_PendingRequestId != 0 || WithinWindow(Entry.m_LastFailureTick, Now, Frequency, 30 * 1000))
			return false;
		return Entry.m_Status != EPointsStatus::READY || !WithinWindow(Entry.m_LastSuccessTick, Now, Frequency, 2 * 60 * 60 * 1000);
	}

	SRequestToken BeginRequest(const std::string &Name)
	{
		SEntry &Entry = m_Entries[Name];
		if(Entry.m_Status != EPointsStatus::READY)
			Entry.m_Status = EPointsStatus::FETCHING;
		Entry.m_PendingRequestId = ++m_RequestId;
		return {m_Generation, Entry.m_PendingRequestId};
	}

	bool CompleteSuccess(const std::string &Name, SRequestToken Token, int Points, int64_t Now)
	{
		auto It = m_Entries.find(Name);
		if(It == m_Entries.end() || !IsCurrent(It->second, Token))
			return false;
		SEntry &Entry = It->second;
		Entry.m_Points = Points;
		Entry.m_Status = EPointsStatus::READY;
		Entry.m_LastSuccessTick = Now;
		Entry.m_LastFailureTick = 0;
		Entry.m_PendingRequestId = 0;
		return true;
	}

	bool CompleteFailure(const std::string &Name, SRequestToken Token, int64_t Now)
	{
		auto It = m_Entries.find(Name);
		if(It == m_Entries.end() || !IsCurrent(It->second, Token))
			return false;
		SEntry &Entry = It->second;
		if(Entry.m_Status != EPointsStatus::READY)
			Entry.m_Status = EPointsStatus::FAILED;
		Entry.m_LastFailureTick = Now;
		Entry.m_PendingRequestId = 0;
		return true;
	}

	void CancelPendingRequests()
	{
		++m_Generation;
		for(auto &[Name, Entry] : m_Entries)
		{
			Entry.m_PendingRequestId = 0;
			if(Entry.m_Status == EPointsStatus::FETCHING)
				Entry.m_Status = EPointsStatus::NOT_REQUESTED;
		}
	}

	void BeginServerSession()
	{
		CancelPendingRequests();
		// 新入服必须刷新，最近成功数据仍供记分板显示。
		for(auto &[Name, Entry] : m_Entries)
		{
			Entry.m_LastSuccessTick = 0;
			Entry.m_LastFailureTick = 0;
		}
	}

	void Clear()
	{
		CancelPendingRequests();
		m_Entries.clear();
	}
};

#endif
