#include "friend_last_seen_data.h"

#include <base/system.h>

#include <engine/friends.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <algorithm>
#include <charconv>
#include <memory>
#include <utility>

namespace
{
	bool ValidName(const char *pName)
	{
		return pName && pName[0] != '\0' && str_length(pName) < MAX_NAME_LENGTH && str_utf8_check(pName);
	}

	bool ValidDate(std::string_view Date)
	{
		if(Date.size() != 10 || Date[4] != '-' || Date[7] != '-')
			return false;
		for(size_t i = 0; i < Date.size(); ++i)
			if(i != 4 && i != 7 && (Date[i] < '0' || Date[i] > '9'))
				return false;
		const int Year = (Date[0] - '0') * 1000 + (Date[1] - '0') * 100 + (Date[2] - '0') * 10 + Date[3] - '0';
		const int Month = (Date[5] - '0') * 10 + Date[6] - '0';
		const int Day = (Date[8] - '0') * 10 + Date[9] - '0';
		if(Year < 1970 || Month < 1 || Month > 12)
			return false;
		constexpr int s_aDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		const bool LeapYear = Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0);
		return Day >= 1 && Day <= s_aDays[Month - 1] + (Month == 2 && LeapYear ? 1 : 0);
	}

	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;
}

std::optional<std::string> QmParseDdStatsLastSeen(std::string_view Body, const char *pPlayerName)
{
	if(!ValidName(pPlayerName) || Body.empty() || Body.size() > 8 * 1024 * 1024)
		return std::nullopt;
	const TJson pRoot(JsonParse(Body.data(), Body.size()), json_value_free);
	if(!pRoot || pRoot->type != json_object)
		return std::nullopt;
	const json_value *pInfo = json_object_get(pRoot.get(), "recent_player_info");
	if(pInfo->type != json_array)
		return std::nullopt;
	std::string LatestDate;
	for(unsigned i = 0; i < pInfo->u.array.length; ++i)
	{
		const json_value *pEntry = pInfo->u.array.values[i];
		if(pEntry->type != json_object)
			return std::nullopt;
		const json_value *pName = json_object_get(pEntry, "name");
		const json_value *pDate = json_object_get(pEntry, "last_seen");
		if(pName->type != json_string || pName->u.string.length != static_cast<unsigned>(str_length(pPlayerName)) || str_comp(json_string_get(pName), pPlayerName) != 0 || pDate->type != json_string || !ValidDate(std::string_view(json_string_get(pDate), pDate->u.string.length)))
			return std::nullopt;
		// 同一名字可能有多套皮肤或战队记录，不能依赖返回顺序。
		if(LatestDate < json_string_get(pDate))
			LatestDate = json_string_get(pDate);
	}
	return LatestDate;
}

const SQmFriendLastSeenEntry *CQmFriendLastSeenCache::Find(const char *pName) const
{
	const auto It = m_Entries.find(pName ? pName : "");
	return It == m_Entries.end() ? nullptr : &It->second;
}

const SQmFriendLastSeenEntry *CQmFriendLastSeenCache::Get(const char *pName, int64_t Now)
{
	if(!ValidName(pName))
		return nullptr;
	auto It = m_Entries.find(pName);
	if(It == m_Entries.end())
	{
		if(m_Entries.size() >= IFriends::MAX_FRIENDS)
			return nullptr;
		It = m_Entries.emplace(pName, SQmFriendLastSeenEntry{}).first;
	}
	auto &Entry = It->second;
	Entry.m_VisibleAt = Now;
	const bool Fresh = Entry.m_CheckedAt > 0 && Now >= Entry.m_CheckedAt && Now - Entry.m_CheckedAt < CACHE_SECONDS;
	if(!Fresh && Now >= Entry.m_RetryAt && !Entry.m_Queued && (!m_ActiveRequest || m_ActiveRequest->m_Name != pName))
	{
		m_Queue.emplace_back(pName);
		Entry.m_Queued = true;
	}
	return &Entry;
}

std::optional<SQmFriendLastSeenRequest> CQmFriendLastSeenCache::NextRequest(int64_t Now)
{
	if(m_ActiveRequest || Now < m_NextRequestAt)
		return std::nullopt;
	while(!m_Queue.empty())
	{
		std::string Name = std::move(m_Queue.front());
		m_Queue.pop_front();
		const auto It = m_Entries.find(Name);
		if(It == m_Entries.end())
			continue;
		It->second.m_Queued = false;
		// 离开好友页或滚出视口后，不继续下载先前排队的玩家档案。
		if(Now - It->second.m_VisibleAt > 2)
			continue;
		m_NextRequestAt = Now + 2;
		m_ActiveRequest = SQmFriendLastSeenRequest{++m_RequestId, std::move(Name)};
		return m_ActiveRequest;
	}
	return std::nullopt;
}

bool CQmFriendLastSeenCache::IsCurrent(uint64_t RequestId) const
{
	return m_ActiveRequest && m_ActiveRequest->m_Id == RequestId;
}

void CQmFriendLastSeenCache::Complete(uint64_t RequestId, std::optional<std::string> Date, int64_t Now, int64_t RetryAfterSeconds)
{
	if(!IsCurrent(RequestId))
		return;
	auto &Entry = m_Entries.at(m_ActiveRequest->m_Name);
	if(Date && (Date->empty() || ValidDate(*Date)))
	{
		Entry.m_Date = std::move(*Date);
		Entry.m_Status = Entry.m_Date.empty() ? EQmFriendLastSeenStatus::NO_DATA : EQmFriendLastSeenStatus::READY;
		Entry.m_CheckedAt = Now;
		Entry.m_RetryAt = 0;
		m_Dirty = true;
	}
	else
	{
		Entry.m_Status = EQmFriendLastSeenStatus::ERROR;
		Entry.m_RetryAt = Now + RETRY_SECONDS;
		// 站点失败时暂停整个队列，并尊重服务端的 Retry-After。
		m_NextRequestAt = Now + std::clamp<int64_t>(RetryAfterSeconds, 60, CACHE_SECONDS);
	}
	m_ActiveRequest.reset();
}

void CQmFriendLastSeenCache::CancelRequests()
{
	for(const auto &Name : m_Queue)
	{
		const auto It = m_Entries.find(Name);
		if(It != m_Entries.end())
			It->second.m_Queued = false;
	}
	m_Queue.clear();
	m_ActiveRequest.reset();
}

void CQmFriendLastSeenCache::RetainNames(const std::unordered_set<std::string> &Names)
{
	for(auto It = m_Entries.begin(); It != m_Entries.end();)
	{
		if(Names.find(It->first) == Names.end())
		{
			if(m_ActiveRequest && m_ActiveRequest->m_Name == It->first)
				m_ActiveRequest.reset();
			It = m_Entries.erase(It);
			m_Dirty = true;
		}
		else
			++It;
	}
	m_Queue.erase(std::remove_if(m_Queue.begin(), m_Queue.end(), [&](const std::string &Name) { return Names.find(Name) == Names.end(); }), m_Queue.end());
}

std::string CQmFriendLastSeenCache::Serialize() const
{
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("version");
	Writer.WriteIntValue(1);
	Writer.WriteAttribute("players");
	Writer.BeginArray();
	for(const auto &[Name, Entry] : m_Entries)
	{
		if(Entry.m_CheckedAt <= 0)
			continue;
		Writer.BeginObject();
		Writer.WriteAttribute("name");
		Writer.WriteStrValue(Name.c_str());
		Writer.WriteAttribute("date");
		Writer.WriteStrValue(Entry.m_Date.c_str());
		Writer.WriteAttribute("checked_at");
		Writer.WriteStrValue(std::to_string(Entry.m_CheckedAt).c_str());
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.EndObject();
	return Writer.GetOutputString();
}

bool CQmFriendLastSeenCache::Load(std::string_view Body, int64_t Now)
{
	if(Body.empty() || Body.size() > 1024 * 1024)
		return false;
	const TJson pRoot(JsonParse(Body.data(), Body.size()), json_value_free);
	if(!pRoot || pRoot->type != json_object)
		return false;
	const json_value *pVersion = json_object_get(pRoot.get(), "version");
	if(pVersion->type != json_integer || pVersion->u.integer != 1)
		return false;
	const json_value *pPlayers = json_object_get(pRoot.get(), "players");
	if(pPlayers->type != json_array || pPlayers->u.array.length > IFriends::MAX_FRIENDS)
		return false;
	decltype(m_Entries) Entries;
	for(unsigned i = 0; i < pPlayers->u.array.length; ++i)
	{
		const json_value *pPlayer = pPlayers->u.array.values[i];
		if(pPlayer->type != json_object)
			return false;
		const json_value *pName = json_object_get(pPlayer, "name");
		const json_value *pDate = json_object_get(pPlayer, "date");
		const json_value *pChecked = json_object_get(pPlayer, "checked_at");
		if(pName->type != json_string || !ValidName(json_string_get(pName)) || pName->u.string.length != static_cast<unsigned>(str_length(json_string_get(pName))) || pDate->type != json_string || pChecked->type != json_string)
			return false;
		SQmFriendLastSeenEntry Entry;
		Entry.m_Date = json_string_get(pDate);
		if(pDate->u.string.length != Entry.m_Date.size() || (!Entry.m_Date.empty() && !ValidDate(Entry.m_Date)))
			return false;
		const char *pCheckedAt = json_string_get(pChecked);
		const auto Parsed = std::from_chars(pCheckedAt, pCheckedAt + pChecked->u.string.length, Entry.m_CheckedAt);
		if(Parsed.ec != std::errc{} || Parsed.ptr != pCheckedAt + pChecked->u.string.length || Entry.m_CheckedAt <= 0 || Entry.m_CheckedAt > Now)
			return false;
		Entry.m_Status = Entry.m_Date.empty() ? EQmFriendLastSeenStatus::NO_DATA : EQmFriendLastSeenStatus::READY;
		if(!Entries.emplace(json_string_get(pName), std::move(Entry)).second)
			return false;
	}
	CancelRequests();
	m_Entries = std::move(Entries);
	m_Dirty = false;
	return true;
}
