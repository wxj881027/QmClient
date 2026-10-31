#include "qm_realtime.h"

#include "qm_json_fields.h"

#include <engine/shared/json.h>
#include <engine/shared/protocol.h>

#include <generated/client_data.h>

#include <game/client/components/emoticon.h>

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

namespace
{
	bool IntegerField(const json_value *pObject, const char *pName, int &Out)
	{
		const json_value *pValue = qm_json::Field(pObject, pName);
		if(!pValue || pValue->type != json_integer)
			return false;
		Out = static_cast<int>(std::clamp<int64_t>(pValue->u.integer, 0, 1000000));
		return true;
	}
	bool Int64Field(const json_value *pObject, const char *pName, int64_t &Out)
	{
		const json_value *pValue = qm_json::Field(pObject, pName);
		if(!pValue || pValue->type != json_integer)
			return false;
		Out = std::clamp<int64_t>(pValue->u.integer, 0, 4102444800LL);
		return true;
	}
	// 缺省或类型不符沿旧逻辑忽略；异常字符串拒收整条消息。
	bool StringField(const json_value *pObject, const char *pName, const char *&pOut)
	{
		pOut = nullptr;
		const json_value *pValue = qm_json::Field(pObject, pName);
		if(pValue->type != json_string)
			return true;
		if(!qm_json::Text(pValue))
			return false;
		pOut = pValue->u.string.ptr;
		return true;
	}
}

static bool PrepareQmRealtimeMessage(const char *pData, size_t Size, SQmRealtimeMessage &OutMessage)
{
	OutMessage = {};
	if(!pData || !Size)
		return false;
	std::unique_ptr<json_value, decltype(&json_value_free)> pOwner(json_parse(pData, Size), json_value_free);
	json_value *pRoot = pOwner.get();
	if(!pRoot || pRoot->type != json_object)
		return false;
	const auto RetainPayload = [&pOwner](const json_value *pPayload) {
		// 控制块分配完成才移交整棵树；别名只指向其内部稳定节点。
		std::shared_ptr<const json_value> Root(std::move(pOwner));
		return std::shared_ptr<const json_value>(Root, pPayload);
	};
	const json_value *pType = qm_json::Field(pRoot, "type");
	if(!qm_json::Text(pType, false))
		return false;
	OutMessage.m_Type.assign(pType->u.string.ptr, pType->u.string.length);
	if(str_comp(pType->u.string.ptr, "ping") == 0)
		OutMessage.m_Event = EQmRealtimeEvent::PING;
	else if(str_comp(pType->u.string.ptr, "pong") == 0)
		OutMessage.m_Event = EQmRealtimeEvent::PONG;
	else if(str_comp(pType->u.string.ptr, "state") == 0)
	{
		OutMessage.m_Event = EQmRealtimeEvent::STATE;
		const json_value *pDataObject = qm_json::Field(pRoot, "data");
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
		const json_value *pDataObject = qm_json::Field(pRoot, "data");
		const json_value *pMarkdown = qm_json::Field(pDataObject, "markdown");
		if(pMarkdown && pMarkdown->type == json_string && pMarkdown->u.string.length <= MAX_QM_MARKDOWN_BROADCAST_BYTES)
		{
			if(!qm_json::Text(pMarkdown))
				return false;
			OutMessage.m_HasBroadcast = true;
			OutMessage.m_BroadcastMarkdown.assign(pMarkdown->u.string.ptr, pMarkdown->u.string.length);
			IntegerField(pDataObject, "version", OutMessage.m_BroadcastVersion);
		}
	}
	else if(str_comp(pType->u.string.ptr, "titles") == 0)
	{
		OutMessage.m_Event = EQmRealtimeEvent::TITLES;
		const json_value *pDataObject = qm_json::Field(pRoot, "data");
		const json_value *pPayload = pDataObject && pDataObject->type == json_object ? pDataObject : pRoot;
		const json_value *pPresences = qm_json::Field(pPayload, "presences");
		int64_t ServerTime = 0;
		OutMessage.m_HasTitles = pPresences && pPresences->type == json_array &&
					 Int64Field(pPayload, "server_time", ServerTime) && ServerTime > 0;
		OutMessage.m_HasRealtimeData = pPayload != pRoot;
		OutMessage.m_pTitlePayload = RetainPayload(pPayload);
		OutMessage.m_pPayload = OutMessage.m_pTitlePayload;
		return true;
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
				const json_value *pDataObject = qm_json::Field(pRoot, "data");
				OutMessage.m_HasRealtimeData = pDataObject && pDataObject->type == json_object;
				if(OutMessage.m_HasRealtimeData)
				{
					OutMessage.m_HasServerTime = Int64Field(pDataObject, "server_time", OutMessage.m_ServerTime) || Int64Field(pDataObject, "time", OutMessage.m_ServerTime);
					OutMessage.m_HasPlaytimeSeconds = Int64Field(pDataObject, "playtime_seconds", OutMessage.m_PlaytimeSeconds) || Int64Field(pDataObject, "playtime", OutMessage.m_PlaytimeSeconds);
					if(Event.m_Event == EQmRealtimeEvent::TITLE_PROFILE)
					{
						const char *pTitle, *pBoundName, *pStyle;
						if(!StringField(pDataObject, "title", pTitle) ||
							!StringField(pDataObject, "bound_name", pBoundName) ||
							!StringField(pDataObject, "style", pStyle))
							return false;
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
						const json_value *pStatus = qm_json::Field(pDataObject, "status");
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
						const json_value *pAuthenticated = qm_json::Field(pDataObject, "authenticated");
						if(!OutMessage.m_HasTitleStatusCode && pAuthenticated && pAuthenticated->type == json_boolean)
						{
							OutMessage.m_HasTitleAuthenticated = true;
							OutMessage.m_TitleAuthenticated = pAuthenticated->u.boolean;
						}
					}
				}
				if(OutMessage.m_HasRealtimeData)
				{
					OutMessage.m_pPayload = RetainPayload(pDataObject);
					return true;
				}
				break;
			}
		}
	}
	else if(str_comp(pType->u.string.ptr, "emoticon") == 0)
	{
		OutMessage.m_Event = EQmRealtimeEvent::EMOTICON;
		const json_value *pDataObject = qm_json::Field(pRoot, "data");
		const char *pClientId, *pPlayerName, *pServerAddress;
		if(!StringField(pDataObject, "client_id", pClientId) ||
			!StringField(pDataObject, "player_name", pPlayerName) ||
			!StringField(pDataObject, "server_address", pServerAddress))
			return false;
		const json_value *pLaunchMode = qm_json::Field(pDataObject, "launch_mode");
		const json_value *pSuperLaunch = qm_json::Field(pDataObject, "super_launch");
		const json_value *pEmoticon = qm_json::Field(pDataObject, "emoticon");
		const json_value *pPlayerId = qm_json::Field(pDataObject, "player_id");
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
				const json_value *pSequence = qm_json::Field(pDataObject, "sequence");
				if(pSequence && pSequence->type == json_integer && pSequence->u.integer >= 0)
					OutMessage.m_EmoticonSequence = static_cast<uint64_t>(pSequence->u.integer);
				OutMessage.m_HasEmoticon = true;
				OutMessage.m_EmoticonPayloadValid = true;
				OutMessage.m_HasRealtimeData = true;
				OutMessage.m_pPayload = RetainPayload(pDataObject);
				return true;
			}
		}
	}
	if(OutMessage.m_Event == EQmRealtimeEvent::INVALID)
		OutMessage.m_Event = EQmRealtimeEvent::UNKNOWN;
	return true;
}

bool ParseQmRealtimeMessage(const char *pData, size_t Size, SQmRealtimeMessage &OutMessage)
{
	OutMessage = {};
	try
	{
		SQmRealtimeMessage Candidate;
		if(!PrepareQmRealtimeMessage(pData, Size, Candidate))
			return false;
		OutMessage = std::move(Candidate);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		// 与普通解析失败共用空输出，不把部分事件交给主线程消费者。
		OutMessage = {};
		return false;
	}
}

const char *QmRealtimeEffectiveUrl(const char *pConfiguredUrl)
{
	return pConfiguredUrl && pConfiguredUrl[0] ? pConfiguredUrl : "wss://qmclient.icu/ws";
}

bool QmRealtimeAllowsCredentials(const char *pUrl)
{
	return pUrl && str_comp(pUrl, "wss://qmclient.icu/ws") == 0;
}
