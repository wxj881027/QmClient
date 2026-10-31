#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_SERVICE_DATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_REALTIME_SERVICE_DATA_H

#include "qm_json_fields.h"
#include "qm_realtime.h"
#include "qmclient_utils.h"

enum class EQmRealtimePlaytimeAction
{
	NONE,
	START,
	STOP,
};

// 文本借用 Message 的 payload，调用者须在 Message 释放前完成读取或复制。
struct SQmRealtimeServiceDataView
{
	bool m_HasServerTime = false;
	int64_t m_ServerTime = 0;
	bool m_HasPlaytimeSeconds = false;
	int64_t m_PlaytimeSeconds = 0;
	bool m_HasSessionStart = false;
	int64_t m_SessionStart = 0;
	EQmRealtimePlaytimeAction m_PlaytimeAction = EQmRealtimePlaytimeAction::NONE;
	const char *m_pTitle = "";
	const char *m_pBoundName = "";
	const char *m_pStyle = "";
	bool m_TitleAuthenticated = false;
	EQmSponsorChatStyle m_ChatStyle = EQmSponsorChatStyle::NONE;
	bool m_ChatStyleSupported = false;
	SQmTitleAppearance m_Appearance;
	bool m_AppearanceSupported = false;
	bool m_TitleAddressLimitReached = false;
};

inline SQmRealtimeServiceDataView ReadQmRealtimeServiceData(const SQmRealtimeMessage &Message)
{
	SQmRealtimeServiceDataView Data;
	const json_value *pPayload = Message.m_pPayload.get();
	if(!pPayload)
		return Data;
	const auto Text = [pPayload](const char *pName) {
		const json_value *pValue = qm_json::Field(pPayload, pName);
		return qm_json::Text(pValue) ? pValue->u.string.ptr : "";
	};
	if(Message.m_Event == EQmRealtimeEvent::TIME || Message.m_Event == EQmRealtimeEvent::PLAYTIME)
	{
		Data.m_HasServerTime = qm_json::NonNegativeInt64(qm_json::Field(pPayload, "ts"), Data.m_ServerTime) && Data.m_ServerTime > 0;
	}
	if(Message.m_Event == EQmRealtimeEvent::PLAYTIME)
	{
		Data.m_HasPlaytimeSeconds = qm_json::NonNegativeInt64(qm_json::Field(pPayload, "total_seconds"), Data.m_PlaytimeSeconds);
		Data.m_HasSessionStart = qm_json::NonNegativeInt64(qm_json::Field(pPayload, "last_start_at"), Data.m_SessionStart) && Data.m_SessionStart > 0;
		const char *pAction = Text("action");
		if(str_comp(pAction, "start") == 0)
			Data.m_PlaytimeAction = EQmRealtimePlaytimeAction::START;
		else if(str_comp(pAction, "stop") == 0)
			Data.m_PlaytimeAction = EQmRealtimePlaytimeAction::STOP;
	}
	if(Message.m_Event == EQmRealtimeEvent::TITLE_PROFILE)
	{
		Data.m_pTitle = Text("title");
		Data.m_pBoundName = Text("bound_name");
		Data.m_pStyle = Text("style");
		const json_value *pStatus = qm_json::Field(pPayload, "status");
		Data.m_TitleAuthenticated = pStatus->type == json_integer && pStatus->u.integer == 200 && IsValidQmTitle(Data.m_pTitle);
		const json_value *pChatStyle = qm_json::Field(pPayload, "chat_style");
		Data.m_ChatStyleSupported = qm_json::Text(pChatStyle);
		Data.m_ChatStyle = QmSponsorChatStyleFromId(Data.m_ChatStyleSupported ? pChatStyle->u.string.ptr : "");
		Data.m_AppearanceSupported = ParseQmTitleAppearance(qm_json::Field(pPayload, "appearance"), Data.m_Appearance);
	}
	if(Message.m_Event == EQmRealtimeEvent::TITLE_STATUS)
	{
		const json_value *pStatus = qm_json::Field(pPayload, "status");
		Data.m_TitleAddressLimitReached = pStatus->type == json_integer && pStatus->u.integer == 409;
	}
	return Data;
}

#endif
