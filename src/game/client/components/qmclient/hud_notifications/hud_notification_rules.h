// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_RULES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_RULES_H

#include <base/system.h>

#include <cstddef>

namespace QmHudNotifications
{
	enum class EMessageKey
	{
		None,
		WhispersOn,
		WhispersOff,
		ShowAllOn,
		ShowAllOff,
		RescueDisabled,
		UnknownEmote,
		TimeoutCodeSet,
		TeamSaveInProgress,
		Count,
	};

	enum class EDynamicMessageKey
	{
		None,
		TeamJoined,
		SwapRequestSent,
		Count,
	};

	struct SDynamicMessageSemantic
	{
		EDynamicMessageKey m_Key = EDynamicMessageKey::None;
		char m_aParamA[128] = {};
		char m_aParamB[128] = {};
		char m_aParamC[128] = {};
	};

	enum class ESoloPrompt
	{
		None,
		Enter,
		Leave,
	};

	enum class EServerMessageRoute
	{
		None,
		Solo,
		System,
	};

	enum class EServerMessageClass
	{
		None,
		BasicInfo,
		HelpInfo,
		Prompt,
	};

	enum class EServerMessageDomain
	{
		None,
		Solo,
		Team,
		SwapRescue,
		VoteModeration,
		Status,
		Unknown,
	};

	struct SServerMessageAnalysis
	{
		EServerMessageRoute m_Route = EServerMessageRoute::None;
		EServerMessageClass m_Class = EServerMessageClass::None;
		EServerMessageDomain m_Domain = EServerMessageDomain::None;
		ESoloPrompt m_SoloPrompt = ESoloPrompt::None;
		EMessageKey m_MessageKey = EMessageKey::None;
		SDynamicMessageSemantic m_DynamicSemantic;
		bool m_UseFallbackLocalization = false;
		char m_aLocalizedText[256] = {};
	};

	struct SServerMessageEntryDecision
	{
		bool m_QueueNotification = false;
		bool m_ClearPendingCompatPrompt = false;
		bool m_UseFallbackNotification = false;
	};

	struct SServerMessageRouteConfig
	{
		bool m_RouteSystemMessages = false;
		bool m_UseCategoryFilters = true;
		bool m_ShowBasicInfo = false;
		bool m_ShowHelpInfo = false;
		bool m_ShowPrompts = true;
		bool m_ShowUnknown = true;
	};

	ESoloPrompt MatchKnownSoloPrompt(const char *pMessage);
	bool ShouldSuppressSoloChatMessage(const char *pMessage, ESoloPrompt PendingCompatPrompt);
	bool ShouldExcludeSystemNotification(const char *pMessage);
	SServerMessageAnalysis AnalyzeServerMessage(const char *pMessage, ESoloPrompt PendingCompatPrompt);
	SServerMessageEntryDecision DecideServerMessageEntry(const SServerMessageAnalysis &Analysis, const SServerMessageRouteConfig &Config);
	SServerMessageEntryDecision DecideServerMessageEntry(const SServerMessageAnalysis &Analysis, bool RouteSystemMessages);
	bool ShouldSuppressServerMessageChat(const SServerMessageAnalysis &Analysis);
	EServerMessageRoute ServerMessageRoute(const char *pMessage, ESoloPrompt PendingCompatPrompt, bool RouteSystemMessages);
	EServerMessageClass ServerMessageClass(const char *pMessage, ESoloPrompt PendingCompatPrompt);
	bool TryFormatLocalizedNotificationMessage(const char *pMessage, char *pBuf, size_t BufSize);
	bool TryFormatLocalizedServerChatMessage(const char *pMessage, char *pBuf, size_t BufSize);
} // namespace QmHudNotifications

#endif
