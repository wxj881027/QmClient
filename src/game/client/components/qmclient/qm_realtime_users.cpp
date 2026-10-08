#include "qm_realtime_users.h"

#include "qm_realtime.h"

#include <engine/shared/json.h>

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace
{
	constexpr size_t MAX_SERVERS = 8192;
	constexpr size_t MAX_PLAYERS = 512;
	constexpr int64_t MAX_REVISION = 9007199254740991LL;
	constexpr int64_t MAX_USERS = 1000000;

	const json_value *Field(const json_value *pObject, const char *pName)
	{
		return pObject && pObject->type == json_object ? json_object_get(pObject, pName) : nullptr;
	}

	bool Text(const json_value *pValue, std::string &Out, size_t Max, bool Empty = false)
	{
		if(!pValue || pValue->type != json_string || pValue->u.string.length > Max || (!Empty && !pValue->u.string.length))
			return false;
		for(unsigned i = 0; i < pValue->u.string.length; ++i)
			if(static_cast<unsigned char>(pValue->u.string.ptr[i]) < 0x20 || pValue->u.string.ptr[i] == 0x7f)
				return false;
		Out.assign(pValue->u.string.ptr, pValue->u.string.length);
		return true;
	}

	bool Integer(const json_value *pValue, int64_t &Out, int64_t Min, int64_t Max)
	{
		if(!pValue || pValue->type != json_integer || pValue->u.integer < Min || pValue->u.integer > Max)
			return false;
		Out = pValue->u.integer;
		return true;
	}

	bool Array(const json_value *pValue, size_t Max)
	{
		return pValue && pValue->type == json_array && pValue->u.array.length <= Max;
	}
}

bool ParseQmCompressedUsersMessage(const char *pData, size_t Size, SQmRealtimeMessage &Out)
{
	Out = {};
	if(!pData || Size < 9 || Size > QMCLIENT_USERS_SYNC_MAX_BYTES + 65536 || std::memcmp(pData, "QMU1", 4) != 0)
		return false;
	const auto *pBytes = reinterpret_cast<const unsigned char *>(pData);
	const uint32_t DecodedSize = (uint32_t(pBytes[4]) << 24) | (uint32_t(pBytes[5]) << 16) | (uint32_t(pBytes[6]) << 8) | pBytes[7];
	if(DecodedSize == 0 || DecodedSize > QMCLIENT_USERS_SYNC_MAX_BYTES)
		return false;
	std::string Json(DecodedSize, '\0');
	z_stream Stream{};
	Stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(pData + 8));
	Stream.avail_in = static_cast<uInt>(Size - 8);
	Stream.next_out = reinterpret_cast<Bytef *>(Json.data());
	Stream.avail_out = DecodedSize;
	if(inflateInit(&Stream) != Z_OK)
		return false;
	const int Status = inflate(&Stream, Z_FINISH);
	const bool Complete = Status == Z_STREAM_END && Stream.total_in == Size - 8 && Stream.total_out == DecodedSize;
	inflateEnd(&Stream);
	if(!Complete || !ParseQmRealtimeMessage(Json.data(), Json.size(), Out) || Out.m_Event != EQmRealtimeEvent::USERS_SYNC)
	{
		Out = {};
		return false;
	}
	return true;
}

CQmRealtimeUsersState::EApplyResult CQmRealtimeUsersState::Apply(const json_value *pData, const char *pExpectedServer)
{
	auto Invalid = [this]() {
		m_NeedsFull = true;
		return EApplyResult::RESYNC;
	};
	std::string Address;
	if(!Text(Field(pData, "server_address"), Address, 128, true))
		return Invalid();
	if(Address != (pExpectedServer ? pExpectedServer : ""))
		return EApplyResult::IGNORED;
	int64_t Revision, Base, Lease;
	const json_value *pFull = Field(pData, "full");
	if(!Integer(Field(pData, "revision"), Revision, 1, MAX_REVISION) ||
		!Integer(Field(pData, "base_revision"), Base, 0, MAX_REVISION) ||
		!Integer(Field(pData, "lease_seconds"), Lease, 1, 20) || !pFull || pFull->type != json_boolean)
		return Invalid();
	const bool Full = pFull->u.boolean;
	if(Full)
	{
		if(Base != 0)
			return Invalid();
		if(m_ServerAddress == Address && Revision <= m_Revision)
			return EApplyResult::IGNORED;
	}
	else if(m_Revision == 0 || m_ServerAddress != Address || m_NeedsFull || Base != m_Revision || (Revision != Base && Revision != Base + 1))
		return Invalid();

	const json_value *pServers = Field(pData, "servers");
	const json_value *pPlayers = Field(pData, "players");
	const json_value *pRemovedServers = Field(pData, "removed_servers");
	const json_value *pRemovedPlayers = Field(pData, "removed_players");
	if(!Array(pServers, MAX_SERVERS) || !Array(pPlayers, MAX_PLAYERS) || !Array(pRemovedServers, MAX_SERVERS) || !Array(pRemovedPlayers, MAX_PLAYERS))
		return Invalid();
	const bool HasChanges = pServers->u.array.length || pPlayers->u.array.length || pRemovedServers->u.array.length || pRemovedPlayers->u.array.length;
	if((!Full && Revision == Base && HasChanges) || (Full && (pRemovedServers->u.array.length || pRemovedPlayers->u.array.length)) || (Address.empty() && pPlayers->u.array.length))
		return Invalid();
	if(!Full && !HasChanges)
	{
		m_Revision = Revision;
		m_LeaseSeconds = static_cast<int>(Lease);
		return EApplyResult::APPLIED;
	}

	// 先在副本上验证整批变更，任一字段失败都不能留下半份名单。
	auto Servers = Full ? decltype(m_Servers){} : m_Servers;
	auto Players = Full ? decltype(m_Players){} : m_Players;
	std::set<std::string> ServerKeys, PlayerKeys;
	for(unsigned i = 0; i < pRemovedServers->u.array.length; ++i)
	{
		std::string Key;
		if(!Text(pRemovedServers->u.array.values[i], Key, 128, true) || !ServerKeys.insert(Key).second)
			return Invalid();
		Servers.erase(Key);
	}
	for(unsigned i = 0; i < pRemovedPlayers->u.array.length; ++i)
	{
		std::string Key;
		if(!Text(pRemovedPlayers->u.array.values[i], Key, 256) || !PlayerKeys.insert(Key).second)
			return Invalid();
		Players.erase(Key);
	}
	for(unsigned i = 0; i < pServers->u.array.length; ++i)
	{
		const json_value *pRow = pServers->u.array.values[i];
		std::string Key;
		int64_t Users, Dummies;
		if(!Array(pRow, 3) || pRow->u.array.length != 3 || !Text(pRow->u.array.values[0], Key, 128, true) ||
			!Integer(pRow->u.array.values[1], Users, 0, MAX_USERS) || !Integer(pRow->u.array.values[2], Dummies, 0, MAX_USERS) ||
			!ServerKeys.insert(Key).second)
			return Invalid();
		Servers[Key] = {Key, static_cast<int>(Users), static_cast<int>(Dummies)};
	}
	for(unsigned i = 0; i < pPlayers->u.array.length; ++i)
	{
		const json_value *pRow = pPlayers->u.array.values[i];
		std::string Key, Type;
		SQmClientRecognitionMark Mark;
		if(!Array(pRow, 5) || pRow->u.array.length != 5 || !Text(pRow->u.array.values[0], Key, 256) ||
			!Text(pRow->u.array.values[1], Mark.m_Name, 63) || !Text(pRow->u.array.values[2], Type, 8) ||
			(Type != "qm" && Type != "arg") || !Text(pRow->u.array.values[3], Mark.m_Qid, 128, true) ||
			pRow->u.array.values[4]->type != json_boolean || !PlayerKeys.insert(Key).second)
			return Invalid();
		Mark.m_ClientBrand = Type == "arg" ? EClientBrand::ARG : EClientBrand::QM;
		Mark.m_VoiceSupported = pRow->u.array.values[4]->u.boolean;
		Players[Key] = std::move(Mark);
	}
	if(Servers.size() > MAX_SERVERS || Players.size() > MAX_PLAYERS)
		return Invalid();
	int64_t Total = 0;
	for(const auto &[Key, Server] : Servers)
	{
		Total += Server.m_UserCount + static_cast<int64_t>(Server.m_DummyCount);
		if(Total > MAX_USERS)
			return Invalid();
	}
	m_Servers = std::move(Servers);
	m_Players = std::move(Players);
	m_ServerAddress = std::move(Address);
	m_Revision = Revision;
	m_LeaseSeconds = static_cast<int>(Lease);
	m_NeedsFull = false;
	return EApplyResult::APPLIED;
}

SQmClientUsersParseResult CQmRealtimeUsersState::Result() const
{
	SQmClientUsersParseResult Result;
	Result.m_Parsed = m_Revision != 0;
	for(const auto &[Key, Server] : m_Servers)
	{
		Result.m_vServerDistribution.push_back(Server);
		Result.m_OnlineUserCount += Server.m_UserCount;
		Result.m_OnlineDummyCount += Server.m_DummyCount;
	}
	std::sort(Result.m_vServerDistribution.begin(), Result.m_vServerDistribution.end(), [](const auto &Left, const auto &Right) {
		if(Left.m_UserCount != Right.m_UserCount)
			return Left.m_UserCount > Right.m_UserCount;
		if(Left.m_DummyCount != Right.m_DummyCount)
			return Left.m_DummyCount > Right.m_DummyCount;
		return Left.m_ServerAddress < Right.m_ServerAddress;
	});
	for(const auto &[Key, Player] : m_Players)
		Result.m_vLocalServerMarks.push_back(Player);
	return Result;
}

void CQmRealtimeUsersState::Reset()
{
	m_Servers.clear();
	m_Players.clear();
	m_ServerAddress.clear();
	m_Revision = 0;
	m_LeaseSeconds = 0;
	m_NeedsFull = false;
}
