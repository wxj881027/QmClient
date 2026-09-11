// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qmclient_utils.h"

#include <base/str.h>

#include <engine/shared/json.h>
#include <engine/shared/protocol.h>

#include <algorithm>
#include <unordered_map>

namespace
{

	const json_value *JsonObjectField(const json_value *pObject, const char *pName)
	{
		if(!pObject || pObject->type != json_object)
			return &json_value_none;
		return json_object_get(pObject, pName);
	}
	bool JsonReadBoolean(const json_value *pValue, bool &OutValue)
	{
		if(!pValue)
			return false;

		if(pValue->type == json_boolean)
		{
			OutValue = json_boolean_get(pValue) != 0;
			return true;
		}
		if(pValue->type == json_integer)
		{
			OutValue = pValue->u.integer != 0;
			return true;
		}
		if(pValue->type == json_double)
		{
			OutValue = pValue->u.dbl != 0.0;
			return true;
		}
		return false;
	}
	bool JsonReadInteger(const json_value *pValue, int64_t &OutValue)
	{
		if(!pValue || pValue->type != json_integer)
			return false;
		OutValue = pValue->u.integer;
		return true;
	}
	bool IsDeveloperPresenceValidAt(const SQmDeveloperPresence &Presence, int64_t Now)
	{
		return !Presence.m_DeveloperId.empty() &&
		       !Presence.m_ServerAddress.empty() &&
		       Presence.m_PlayerId >= 0 && Presence.m_PlayerId < MAX_CLIENTS &&
		       !Presence.m_PlayerName.empty() &&
		       Presence.m_IssuedAt <= Now && Now < Presence.m_ExpiresAt &&
		       Presence.m_StyleBucket >= 0 && Presence.m_StyleBucket < 100;
	}
	EClientBrand JsonReadClientBrand(const json_value *pEntry)
	{
		const json_value *pClientType = JsonObjectField(pEntry, "client_type");
		if(pClientType == &json_value_none)
			pClientType = JsonObjectField(pEntry, "type");
		if(pClientType == &json_value_none || pClientType->type != json_string)
			return EClientBrand::QM;

		const char *pType = pClientType->u.string.ptr;
		if(str_comp_nocase(pType, "arg") == 0 || str_comp_nocase(pType, "arghena") == 0)
			return EClientBrand::ARG;
		if(str_comp_nocase(pType, "qm") == 0 || str_comp_nocase(pType, "qmclient") == 0 || str_comp_nocase(pType, "q1meng") == 0)
			return EClientBrand::QM;
		return EClientBrand::QM;
	}
} // namespace

bool ParseQmClientUsersJson(const json_value *pRoot, const char *pServerAddress, SQmClientUsersParseResult &OutResult)
{
	OutResult = SQmClientUsersParseResult();
	if(!pRoot)
		return false;

	const json_value *pUsers = pRoot;
	if(pUsers->type == json_object)
	{
		const json_value *pUsersField = JsonObjectField(pUsers, "users");
		if(pUsersField != &json_value_none)
			pUsers = pUsersField;
	}

	if(pUsers->type != json_array)
		return false;

	OutResult.m_Parsed = true;
	std::unordered_map<std::string, size_t> ServerIndexByAddress;
	ServerIndexByAddress.reserve(pUsers->u.array.length);
	OutResult.m_vServerDistribution.reserve(pUsers->u.array.length);

	const char *pLocalServerAddress = pServerAddress ? pServerAddress : "";
	for(unsigned Index = 0; Index < pUsers->u.array.length; ++Index)
	{
		const json_value *pEntry = pUsers->u.array.values[Index];
		if(!pEntry || pEntry->type != json_object)
			continue;

		const json_value *pServerAddressField = JsonObjectField(pEntry, "server_address");
		if(pServerAddressField == &json_value_none)
			pServerAddressField = JsonObjectField(pEntry, "server");
		if(pServerAddressField == &json_value_none || pServerAddressField->type != json_string)
			continue;

		const json_value *pPlayerName = JsonObjectField(pEntry, "player_name");
		if(pPlayerName == &json_value_none)
			pPlayerName = JsonObjectField(pEntry, "name");
		if(pPlayerName == &json_value_none || pPlayerName->type != json_string || pPlayerName->u.string.ptr[0] == '\0')
			continue;

		bool Dummy = false;
		const json_value *pDummy = JsonObjectField(pEntry, "dummy");
		if(pDummy != &json_value_none)
			JsonReadBoolean(pDummy, Dummy);

		const std::string ServerAddress = pServerAddressField->u.string.ptr;
		const auto ItServer = ServerIndexByAddress.find(ServerAddress);
		if(ItServer == ServerIndexByAddress.end())
		{
			ServerIndexByAddress[ServerAddress] = OutResult.m_vServerDistribution.size();
			OutResult.m_vServerDistribution.push_back({ServerAddress, 0, 0});
		}
		SQmClientServerDistribution &ServerDistribution = OutResult.m_vServerDistribution[ServerIndexByAddress[ServerAddress]];
		if(Dummy)
		{
			++ServerDistribution.m_DummyCount;
			++OutResult.m_OnlineDummyCount;
		}
		else
		{
			++ServerDistribution.m_UserCount;
			++OutResult.m_OnlineUserCount;
		}

		if(str_comp(ServerAddress.c_str(), pLocalServerAddress) != 0)
			continue;

		SQmClientRecognitionMark &Mark = OutResult.m_vLocalServerMarks.emplace_back();
		Mark.m_Name = pPlayerName->u.string.ptr;
		Mark.m_ClientBrand = JsonReadClientBrand(pEntry);

		const json_value *pQidField = JsonObjectField(pEntry, "qid");
		if(pQidField != &json_value_none && pQidField->type == json_string)
			Mark.m_Qid = pQidField->u.string.ptr;

		const json_value *pFootParticlesEnabled = JsonObjectField(pEntry, "foot_particles_enabled");
		JsonReadBoolean(pFootParticlesEnabled, Mark.m_FootParticlesEnabled);

		const json_value *pRemoteParticlesEnabled = JsonObjectField(pEntry, "remote_particles_enabled");
		JsonReadBoolean(pRemoteParticlesEnabled, Mark.m_RemoteParticlesEnabled);

		Mark.m_VoiceSupported = true;
		const json_value *pVoiceSupported = JsonObjectField(pEntry, "voice_supported");
		if(pVoiceSupported != &json_value_none)
			JsonReadBoolean(pVoiceSupported, Mark.m_VoiceSupported);
	}

	std::sort(OutResult.m_vServerDistribution.begin(), OutResult.m_vServerDistribution.end(), [](const SQmClientServerDistribution &Left, const SQmClientServerDistribution &Right) {
		if(Left.m_UserCount != Right.m_UserCount)
			return Left.m_UserCount > Right.m_UserCount;
		if(Left.m_DummyCount != Right.m_DummyCount)
			return Left.m_DummyCount > Right.m_DummyCount;
		return str_comp(Left.m_ServerAddress.c_str(), Right.m_ServerAddress.c_str()) < 0;
	});
	return true;
}

bool ParseQmDeveloperPresencesJson(const json_value *pRoot, const char *pServerAddress, SQmDeveloperPresenceParseResult &OutResult)
{
	OutResult = SQmDeveloperPresenceParseResult();
	if(!pRoot || pRoot->type != json_object)
		return false;

	int64_t ServerTime;
	if(!JsonReadInteger(JsonObjectField(pRoot, "server_time"), ServerTime) || ServerTime <= 0)
		return false;
	const json_value *pPresences = JsonObjectField(pRoot, "presences");

	if(pPresences->type != json_array)
		return false;

	OutResult.m_Parsed = true;
	OutResult.m_ServerTime = ServerTime;
	OutResult.m_vPresences.reserve(pPresences->u.array.length);
	const char *pLocalServerAddress = pServerAddress ? pServerAddress : "";
	if(pLocalServerAddress[0] == '\0')
		return true;

	for(unsigned Index = 0; Index < pPresences->u.array.length; ++Index)
	{
		const json_value *pEntry = pPresences->u.array.values[Index];
		if(!pEntry || pEntry->type != json_object)
			continue;

		const json_value *pDeveloperId = JsonObjectField(pEntry, "developer_id");
		const json_value *pEntryServerAddress = JsonObjectField(pEntry, "server_address");
		const json_value *pPlayerName = JsonObjectField(pEntry, "player_name");
		const json_value *pDummy = JsonObjectField(pEntry, "dummy");
		if(pDeveloperId == &json_value_none || pDeveloperId->type != json_string || pDeveloperId->u.string.ptr[0] == '\0' ||
			pEntryServerAddress == &json_value_none || pEntryServerAddress->type != json_string || pEntryServerAddress->u.string.ptr[0] == '\0' ||
			pPlayerName == &json_value_none || pPlayerName->type != json_string || pPlayerName->u.string.ptr[0] == '\0' ||
			pDummy == &json_value_none || pDummy->type != json_boolean)
		{
			continue;
		}

		if(str_comp(pEntryServerAddress->u.string.ptr, pLocalServerAddress) != 0)
			continue;

		int64_t PlayerId;
		int64_t IssuedAt;
		int64_t ExpiresAt;
		int64_t StyleBucket;
		if(!JsonReadInteger(JsonObjectField(pEntry, "player_id"), PlayerId) ||
			!JsonReadInteger(JsonObjectField(pEntry, "issued_at"), IssuedAt) ||
			!JsonReadInteger(JsonObjectField(pEntry, "expires_at"), ExpiresAt) ||
			!JsonReadInteger(JsonObjectField(pEntry, "style_bucket"), StyleBucket) ||
			PlayerId < 0 || PlayerId >= MAX_CLIENTS ||
			StyleBucket < 0 || StyleBucket >= 100)
		{
			continue;
		}

		SQmDeveloperPresence Presence;
		Presence.m_DeveloperId = pDeveloperId->u.string.ptr;
		Presence.m_ServerAddress = pEntryServerAddress->u.string.ptr;
		Presence.m_PlayerId = (int)PlayerId;
		Presence.m_PlayerName = pPlayerName->u.string.ptr;
		Presence.m_Dummy = json_boolean_get(pDummy) != 0;
		Presence.m_IssuedAt = IssuedAt;
		Presence.m_ExpiresAt = ExpiresAt;
		Presence.m_StyleBucket = (int)StyleBucket;
		if(IsDeveloperPresenceValidAt(Presence, ServerTime))
			OutResult.m_vPresences.push_back(Presence);
	}

	return true;
}

const SQmDeveloperPresence *FindQmDeveloperPresence(const std::vector<SQmDeveloperPresence> &vPresences, const char *pServerAddress, int PlayerId, const char *pPlayerName, int64_t Now)
{
	const char *pExpectedServerAddress = pServerAddress ? pServerAddress : "";
	const char *pExpectedPlayerName = pPlayerName ? pPlayerName : "";
	for(const SQmDeveloperPresence &Presence : vPresences)
	{
		if(IsDeveloperPresenceValidAt(Presence, Now) &&
			Presence.m_PlayerId == PlayerId &&
			str_comp(Presence.m_ServerAddress.c_str(), pExpectedServerAddress) == 0 &&
			str_comp(Presence.m_PlayerName.c_str(), pExpectedPlayerName) == 0)
		{
			return &Presence;
		}
	}
	return nullptr;
}

EQmDeveloperBadgeStyle QmDeveloperBadgeStyleFromBucket(int StyleBucket)
{
	if(StyleBucket >= 0 && StyleBucket < 20)
		return EQmDeveloperBadgeStyle::RAINBOW;
	return EQmDeveloperBadgeStyle::BLACK;
}

bool ShouldShowQmDeveloperBadge(bool Authenticated, bool ShowName, bool HideIdentity)
{
	return Authenticated && ShowName && !HideIdentity;
}

bool IsQmDeveloperMarkCurrent(bool Active, const char *pMarkedName, const char *pCurrentName, int64_t ExpireTick, int64_t NowTick)
{
	return Active &&
	       pMarkedName && pMarkedName[0] != '\0' &&
	       pCurrentName && str_comp(pMarkedName, pCurrentName) == 0 &&
	       ExpireTick > NowTick;
}

bool IsValidQmTitle(const char *pTitle)
{
	if(!pTitle || !pTitle[0] || !str_utf8_check(pTitle))
		return false;
	int Width = 0;
	while(*pTitle)
	{
		const int Code = str_utf8_decode(&pTitle);
		if(Code < 32 || (Code >= 127 && Code <= 159) || Code == '[' || Code == ']' ||
			(Code >= 0x200b && Code <= 0x200f) || (Code >= 0x2028 && Code <= 0x202e) ||
			(Code >= 0x2060 && Code <= 0x206f) || Code == 0xfeff)
			return false;
		Width += Code < 128 ? 1 : 2;
		if(Width > 12)
			return false;
	}
	return true;
}

std::vector<SQmTitlePresence> ParseQmTitlePresences(const json_value *pRoot, const char *pServerAddress)
{
	std::vector<SQmTitlePresence> Result;
	int64_t Now;
	if(!pServerAddress || !JsonReadInteger(JsonObjectField(pRoot, "server_time"), Now) || Now <= 0)
		return Result;
	const json_value *pEntries = JsonObjectField(pRoot, "presences");
	if(pEntries->type != json_array)
		return Result;
	for(unsigned i = 0; i < pEntries->u.array.length; ++i)
	{
		const json_value *pEntry = pEntries->u.array.values[i];
		const json_value *pServer = JsonObjectField(pEntry, "server_address");
		const json_value *pName = JsonObjectField(pEntry, "player_name");
		const json_value *pTitle = JsonObjectField(pEntry, "title");
		int64_t Id, Issued, Expires;
		if(pServer->type != json_string || str_comp(pServer->u.string.ptr, pServerAddress) != 0 ||
			pName->type != json_string || !pName->u.string.ptr[0] || pName->u.string.length >= MAX_NAME_LENGTH ||
			pTitle->type != json_string || !IsValidQmTitle(pTitle->u.string.ptr) ||
			!JsonReadInteger(JsonObjectField(pEntry, "player_id"), Id) || Id < 0 || Id >= MAX_CLIENTS ||
			!JsonReadInteger(JsonObjectField(pEntry, "issued_at"), Issued) || Issued > Now ||
			!JsonReadInteger(JsonObjectField(pEntry, "expires_at"), Expires) || Expires <= Now)
			continue;
		Result.push_back({(int)Id, pName->u.string.ptr, pTitle->u.string.ptr, std::min<int64_t>(Expires - Now, 15)});
	}
	return Result;
}
