#include "qm_realtime.h"

#include "decorative_throw_policy.h"

#include <engine/shared/json.h>
#include <engine/shared/protocol.h>

#include <generated/client_data.h>

#include <game/client/components/emoticon.h>

#include <algorithm>

namespace
{
	const json_value *ObjectField(const json_value *pObject, const char *pName)
	{
		return pObject && pObject->type == json_object ? json_object_get(pObject, pName) : nullptr;
	}
	bool VectorField(const json_value *pObject, const char *pName, vec2 &Out)
	{
		const json_value *pVector = ObjectField(pObject, pName);
		const json_value *pX = ObjectField(pVector, "x");
		const json_value *pY = ObjectField(pVector, "y");
		if(!pX || !pY || (pX->type != json_integer && pX->type != json_double) || (pY->type != json_integer && pY->type != json_double))
			return false;
		Out = vec2(pX->type == json_integer ? pX->u.integer : pX->u.dbl, pY->type == json_integer ? pY->u.integer : pY->u.dbl);
		return std::isfinite(Out.x) && std::isfinite(Out.y);
	}
	bool IntegerField(const json_value *pObject, const char *pName, int &Out)
	{
		const json_value *pValue = ObjectField(pObject, pName);
		if(!pValue || pValue->type != json_integer)
			return false;
		Out = static_cast<int>(std::clamp<int64_t>(pValue->u.integer, 0, 1000000));
		return true;
	}
	bool Int64Field(const json_value *pObject, const char *pName, int64_t &Out)
	{
		const json_value *pValue = ObjectField(pObject, pName);
		if(!pValue || pValue->type != json_integer)
			return false;
		Out = std::clamp<int64_t>(pValue->u.integer, 0, 4102444800LL);
		return true;
	}
	const char *StringField(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = ObjectField(pObject, pName);
		return pValue && pValue->type == json_string ? pValue->u.string.ptr : nullptr;
	}
}

bool ParseQmRealtimeMessage(const char *pData, size_t Size, SQmRealtimeMessage &OutMessage)
{
	OutMessage = {};
	if(!pData || !Size)
		return false;
	json_value *pRoot = json_parse(pData, Size);
	if(!pRoot || pRoot->type != json_object)
	{
		if(pRoot)
			json_value_free(pRoot);
		return false;
	}
	const json_value *pType = ObjectField(pRoot, "type");
	if(!pType || pType->type != json_string || !pType->u.string.ptr[0])
	{
		json_value_free(pRoot);
		return false;
	}
	OutMessage.m_Type = pType->u.string.ptr;
	if(str_comp(pType->u.string.ptr, "ping") == 0)
		OutMessage.m_Event = EQmRealtimeEvent::PING;
	else if(str_comp(pType->u.string.ptr, "pong") == 0)
		OutMessage.m_Event = EQmRealtimeEvent::PONG;
	else if(str_comp(pType->u.string.ptr, "state") == 0)
	{
		OutMessage.m_Event = EQmRealtimeEvent::STATE;
		const json_value *pDataObject = ObjectField(pRoot, "data");
		if(pDataObject && pDataObject->type == json_object)
		{
			OutMessage.m_StatePayloadValid = true;
			OutMessage.m_HasOnlineUsers = IntegerField(pDataObject, "online_users", OutMessage.m_OnlineUsers);
			OutMessage.m_HasOnlineDummies = IntegerField(pDataObject, "online_dummies", OutMessage.m_OnlineDummies);
		}
	}
	else if(str_comp(pType->u.string.ptr, "broadcast") == 0)
	{
		constexpr size_t MAX_QM_MARKDOWN_BROADCAST_BYTES = 64 * 1024;
		OutMessage.m_Event = EQmRealtimeEvent::BROADCAST;
		const json_value *pDataObject = ObjectField(pRoot, "data");
		const json_value *pMarkdown = ObjectField(pDataObject, "markdown");
		if(pMarkdown && pMarkdown->type == json_string && pMarkdown->u.string.length <= MAX_QM_MARKDOWN_BROADCAST_BYTES)
		{
			OutMessage.m_HasBroadcast = true;
			OutMessage.m_BroadcastMarkdown.assign(pMarkdown->u.string.ptr, pMarkdown->u.string.length);
			IntegerField(pDataObject, "version", OutMessage.m_BroadcastVersion);
		}
	}
	else if(str_comp(pType->u.string.ptr, "titles") == 0)
	{
		OutMessage.m_Event = EQmRealtimeEvent::TITLES;
		const json_value *pDataObject = ObjectField(pRoot, "data");
		const json_value *pPayload = pDataObject && pDataObject->type == json_object ? pDataObject : pRoot;
		const json_value *pPresences = ObjectField(pPayload, "presences");
		int64_t ServerTime = 0;
		OutMessage.m_HasTitles = pPresences && pPresences->type == json_array &&
					 Int64Field(pPayload, "server_time", ServerTime) && ServerTime > 0;
		OutMessage.m_HasRealtimeData = pPayload != pRoot;
		OutMessage.m_pTitlePayload = std::shared_ptr<const json_value>(pPayload, [pRoot](const json_value *) { json_value_free(pRoot); });
		OutMessage.m_pPayload = OutMessage.m_pTitlePayload;
		return true;
	}
	else if(str_comp(pType->u.string.ptr, "decorative_throw") == 0)
	{
		OutMessage.m_Event = EQmRealtimeEvent::DECORATIVE_THROW;
		const json_value *pObject = ObjectField(pRoot, "data");
		const char *pClient = StringField(pObject, "client_id");
		const char *pName = StringField(pObject, "player_name");
		const char *pServer = StringField(pObject, "server_address");
		const int Type = QmDecorativeThrow::TypeFromName(StringField(pObject, "projectile"));
		const json_value *pPlayer = ObjectField(pObject, "player_id");
		if(pClient && pClient[0] && str_length(pClient) <= 128 && pName && str_length(pName) <= 63 &&
			pServer && pServer[0] && str_length(pServer) <= 128 && Type >= 0 &&
			pPlayer && pPlayer->type == json_integer && pPlayer->u.integer >= 0 && pPlayer->u.integer < MAX_CLIENTS &&
			VectorField(pObject, "origin", OutMessage.m_ThrowOrigin) && VectorField(pObject, "direction", OutMessage.m_ThrowDirection) &&
			QmDecorativeThrow::ValidGeometry(OutMessage.m_ThrowOrigin, OutMessage.m_ThrowDirection))
		{
			OutMessage.m_PlayerId = static_cast<int>(pPlayer->u.integer);
			OutMessage.m_ThrowType = Type;
			OutMessage.m_ThrowClientId = pClient;
			OutMessage.m_ThrowPlayerName = pName;
			OutMessage.m_ThrowServerAddress = pServer;
			OutMessage.m_HasDecorativeThrow = true;
		}
	}
	else if(str_comp(pType->u.string.ptr, "emoticon") != 0)
	{
		static const struct
		{
			const char *m_pName;
			EQmRealtimeEvent m_Event;
		} aEvents[] = {
			{"sponsors", EQmRealtimeEvent::SPONSORS},
			{"titles", EQmRealtimeEvent::TITLES},
			{"users", EQmRealtimeEvent::USERS},
			{"users_sync", EQmRealtimeEvent::USERS_SYNC},
			{"developers", EQmRealtimeEvent::DEVELOPERS},
			{"playtime", EQmRealtimeEvent::PLAYTIME},
			{"time", EQmRealtimeEvent::TIME},
			{"title_profile", EQmRealtimeEvent::TITLE_PROFILE},
			{"title_status", EQmRealtimeEvent::TITLE_STATUS},
			{"error", EQmRealtimeEvent::ERROR},
		};
		for(const auto &Event : aEvents)
		{
			if(str_comp(pType->u.string.ptr, Event.m_pName) == 0)
			{
				OutMessage.m_Event = Event.m_Event;
				const json_value *pDataObject = ObjectField(pRoot, "data");
				OutMessage.m_HasRealtimeData = pDataObject && pDataObject->type == json_object;
				if(OutMessage.m_HasRealtimeData)
				{
					OutMessage.m_HasServerTime = Int64Field(pDataObject, "server_time", OutMessage.m_ServerTime) || Int64Field(pDataObject, "time", OutMessage.m_ServerTime);
					OutMessage.m_HasPlaytimeSeconds = Int64Field(pDataObject, "playtime_seconds", OutMessage.m_PlaytimeSeconds) || Int64Field(pDataObject, "playtime", OutMessage.m_PlaytimeSeconds);
					if(Event.m_Event == EQmRealtimeEvent::TITLE_PROFILE)
					{
						const char *pTitle = StringField(pDataObject, "title");
						const char *pBoundName = StringField(pDataObject, "bound_name");
						const char *pStyle = StringField(pDataObject, "style");
						OutMessage.m_HasTitleProfile = pTitle || pBoundName || pStyle;
						if(pTitle)
						{
							OutMessage.m_HasTitleText = true;
							OutMessage.m_TitleText = pTitle;
						}
						if(pBoundName)
						{
							OutMessage.m_HasTitleBoundName = true;
							OutMessage.m_TitleBoundName = pBoundName;
						}
						if(pStyle)
						{
							OutMessage.m_HasTitleStyle = true;
							OutMessage.m_TitleStyle = pStyle;
						}
					}
					if(Event.m_Event == EQmRealtimeEvent::TITLE_PROFILE || Event.m_Event == EQmRealtimeEvent::TITLE_STATUS)
					{
						const json_value *pStatus = ObjectField(pDataObject, "status");
						if(pStatus && pStatus->type == json_integer && pStatus->u.integer >= 0 && pStatus->u.integer <= 999)
						{
							OutMessage.m_HasTitleStatusCode = true;
							OutMessage.m_TitleStatusCode = (int)pStatus->u.integer;
							if(Event.m_Event == EQmRealtimeEvent::TITLE_PROFILE)
							{
								OutMessage.m_HasTitleAuthenticated = true;
								OutMessage.m_TitleAuthenticated = OutMessage.m_TitleStatusCode == 200;
							}
						}
						const json_value *pAuthenticated = ObjectField(pDataObject, "authenticated");
						if(!OutMessage.m_HasTitleStatusCode && pAuthenticated && pAuthenticated->type == json_boolean)
						{
							OutMessage.m_HasTitleAuthenticated = true;
							OutMessage.m_TitleAuthenticated = pAuthenticated->u.boolean;
						}
					}
				}
				if(OutMessage.m_HasRealtimeData)
				{
					OutMessage.m_pPayload = std::shared_ptr<const json_value>(pDataObject, [pRoot](const json_value *) { json_value_free(pRoot); });
					return true;
				}
				break;
			}
		}
	}
	else if(str_comp(pType->u.string.ptr, "emoticon") == 0)
	{
		OutMessage.m_Event = EQmRealtimeEvent::EMOTICON;
		const json_value *pDataObject = ObjectField(pRoot, "data");
		const char *pClientId = StringField(pDataObject, "client_id");
		const char *pPlayerName = StringField(pDataObject, "player_name");
		const char *pServerAddress = StringField(pDataObject, "server_address");
		const json_value *pLaunchMode = ObjectField(pDataObject, "launch_mode");
		const json_value *pSuperLaunch = ObjectField(pDataObject, "super_launch");
		const json_value *pEmoticon = ObjectField(pDataObject, "emoticon");
		const json_value *pPlayerId = ObjectField(pDataObject, "player_id");
		if(pClientId && pPlayerName && pServerAddress &&
			pEmoticon && pEmoticon->type == json_integer && pEmoticon->u.integer >= 0 && pEmoticon->u.integer < NUM_EMOTICONS &&
			pPlayerId && pPlayerId->type == json_integer && pPlayerId->u.integer >= 0 && pPlayerId->u.integer < MAX_CLIENTS)
		{
			OutMessage.m_LaunchMode = pLaunchMode && pLaunchMode->type == json_boolean && pLaunchMode->u.boolean;
			OutMessage.m_SuperLaunch = pSuperLaunch && pSuperLaunch->type == json_boolean && pSuperLaunch->u.boolean;
			if(OutMessage.m_LaunchMode || OutMessage.m_SuperLaunch)
			{
				OutMessage.m_Emoticon = static_cast<int>(pEmoticon->u.integer);
				OutMessage.m_PlayerId = static_cast<int>(pPlayerId->u.integer);
				OutMessage.m_EmoticonId = OutMessage.m_Emoticon;
				OutMessage.m_EmoticonPlayerId = OutMessage.m_PlayerId;
				OutMessage.m_EmoticonLaunch = OutMessage.m_LaunchMode;
				OutMessage.m_EmoticonSuperLaunch = OutMessage.m_SuperLaunch;
				OutMessage.m_EmoticonClientId = pClientId;
				OutMessage.m_EmoticonPlayerName = pPlayerName;
				OutMessage.m_EmoticonServerAddress = pServerAddress;
				const json_value *pSequence = ObjectField(pDataObject, "sequence");
				if(pSequence && pSequence->type == json_integer && pSequence->u.integer >= 0)
					OutMessage.m_EmoticonSequence = static_cast<uint64_t>(pSequence->u.integer);
				OutMessage.m_HasEmoticon = true;
				OutMessage.m_EmoticonPayloadValid = true;
				OutMessage.m_HasRealtimeData = true;
				OutMessage.m_pPayload = std::shared_ptr<const json_value>(pDataObject, [pRoot](const json_value *) { json_value_free(pRoot); });
				return true;
			}
		}
	}
	if(OutMessage.m_Event == EQmRealtimeEvent::INVALID)
		OutMessage.m_Event = EQmRealtimeEvent::UNKNOWN;
	json_value_free(pRoot);
	return true;
}

const char *QmRealtimeEffectiveUrl(const char *pConfiguredUrl)
{
	return pConfiguredUrl && pConfiguredUrl[0] ? pConfiguredUrl : "wss://qmclient.icu/ws";
}

bool QmRealtimeAllowsCredentials(const char *pUrl)
{
	return pUrl && str_comp(pUrl, "wss://qmclient.icu/ws") == 0;
}
