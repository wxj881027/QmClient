// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "hud_notification_rules.h"

#include "hud_notification_catalog.h"
#include "hud_notification_static_alias_rules.h"
#include "hud_notification_static_rules.h"
#include "hud_notification_static_upstream_rules.h"

#include <game/localization.h>

namespace
{
	void SetLocalizedAnalysis(QmHudNotifications::SServerMessageAnalysis &Analysis, QmHudNotifications::EServerMessageRoute Route, QmHudNotifications::EServerMessageClass Class, QmHudNotifications::EServerMessageDomain Domain, const char *pLocalizedText, QmHudNotifications::ESoloPrompt SoloPrompt = QmHudNotifications::ESoloPrompt::None)
	{
		Analysis.m_Route = Route;
		Analysis.m_Class = Class;
		Analysis.m_Domain = Domain;
		Analysis.m_SoloPrompt = SoloPrompt;
		Analysis.m_UseFallbackLocalization = false;
		if(pLocalizedText != Analysis.m_aLocalizedText)
			str_copy(Analysis.m_aLocalizedText, pLocalizedText, sizeof(Analysis.m_aLocalizedText));
	}

	void SetFallbackAnalysis(QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		Analysis.m_Route = QmHudNotifications::EServerMessageRoute::System;
		Analysis.m_Class = QmHudNotifications::EServerMessageClass::Prompt;
		Analysis.m_Domain = QmHudNotifications::EServerMessageDomain::Unknown;
		Analysis.m_SoloPrompt = QmHudNotifications::ESoloPrompt::None;
		Analysis.m_UseFallbackLocalization = true;
		Analysis.m_aLocalizedText[0] = '\0';
	}

	void SetDynamicSemantic(QmHudNotifications::SServerMessageAnalysis &Analysis, QmHudNotifications::EDynamicMessageKey Key, const char *pParamA = "", const char *pParamB = "", const char *pParamC = "")
	{
		Analysis.m_DynamicSemantic.m_Key = Key;
		str_copy(Analysis.m_DynamicSemantic.m_aParamA, pParamA, sizeof(Analysis.m_DynamicSemantic.m_aParamA));
		str_copy(Analysis.m_DynamicSemantic.m_aParamB, pParamB, sizeof(Analysis.m_DynamicSemantic.m_aParamB));
		str_copy(Analysis.m_DynamicSemantic.m_aParamC, pParamC, sizeof(Analysis.m_DynamicSemantic.m_aParamC));
	}

	void SetSemanticLocalizedAnalysis(QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		if(Analysis.m_MessageKey != QmHudNotifications::EMessageKey::None)
		{
			if(const auto *pMeta = QmHudNotifications::FindMessageMetadata(Analysis.m_MessageKey))
				SetLocalizedAnalysis(Analysis, pMeta->m_Route, pMeta->m_Class, pMeta->m_Domain, Analysis.m_aLocalizedText, Analysis.m_SoloPrompt);
			return;
		}

		if(Analysis.m_DynamicSemantic.m_Key != QmHudNotifications::EDynamicMessageKey::None)
		{
			if(const auto *pMeta = QmHudNotifications::FindMessageMetadata(Analysis.m_DynamicSemantic.m_Key))
				SetLocalizedAnalysis(Analysis, pMeta->m_Route, pMeta->m_Class, pMeta->m_Domain, Analysis.m_aLocalizedText, Analysis.m_SoloPrompt);
		}
	}

	void FormatDynamicLocalizedText(const QmHudNotifications::SDynamicMessageSemantic &Semantic, char *pBuf, size_t BufSize)
	{
		if(BufSize == 0)
			return;
		pBuf[0] = '\0';

		switch(Semantic.m_Key)
		{
		case QmHudNotifications::EDynamicMessageKey::TeamJoined:
			str_format(pBuf, BufSize, Localize("'%s' joined team %s"), Semantic.m_aParamA, Semantic.m_aParamB);
			break;
		case QmHudNotifications::EDynamicMessageKey::SwapRequestSent:
			str_format(pBuf, BufSize, Localize("You have requested to swap with %s. Use /cancelswap to cancel the request."), Semantic.m_aParamA);
			break;
		case QmHudNotifications::EDynamicMessageKey::None:
			break;
		case QmHudNotifications::EDynamicMessageKey::Count:
			break;
		}
	}

	bool SemanticAnalysisExcludedByMetadata(const QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		if(Analysis.m_MessageKey != QmHudNotifications::EMessageKey::None)
		{
			const auto *pMeta = QmHudNotifications::FindMessageMetadata(Analysis.m_MessageKey);
			return pMeta != nullptr && pMeta->m_ExcludeFromNotifications;
		}
		if(Analysis.m_DynamicSemantic.m_Key != QmHudNotifications::EDynamicMessageKey::None)
		{
			const auto *pMeta = QmHudNotifications::FindMessageMetadata(Analysis.m_DynamicSemantic.m_Key);
			return pMeta != nullptr && pMeta->m_ExcludeFromNotifications;
		}
		return false;
	}

	bool TryMatchStaticMessageKey(const char *pMessage, QmHudNotifications::EMessageKey &Key)
	{
		Key = QmHudNotifications::EMessageKey::None;

#define QM_TRY_STATIC_KEY(pLiteral, KeyName) \
	if(str_comp(pMessage, pLiteral) == 0) \
	{ \
		Key = QmHudNotifications::EMessageKey::KeyName; \
		return true; \
	}
		QM_HUD_NOTIFICATION_STATIC_UPSTREAM_RULES(QM_TRY_STATIC_KEY)
		QM_HUD_NOTIFICATION_STATIC_ALIAS_RULES(QM_TRY_STATIC_KEY)
#undef QM_TRY_STATIC_KEY

		return false;
	}

	bool ExtractWrappedValue(const char *pMessage, const char *pPrefix, const char *pSuffix, char *pValue, size_t ValueSize)
	{
		const int PrefixLen = str_length(pPrefix);
		const int SuffixLen = str_length(pSuffix);
		const int MessageLen = str_length(pMessage);
		if(MessageLen < PrefixLen + SuffixLen)
			return false;
		if(str_comp_num(pMessage, pPrefix, PrefixLen) != 0)
			return false;
		if(str_comp(pMessage + MessageLen - SuffixLen, pSuffix) != 0)
			return false;
		str_truncate(pValue, (int)ValueSize, pMessage + PrefixLen, MessageLen - PrefixLen - SuffixLen);
		return true;
	}

	bool ExtractTwoWrappedValues(const char *pMessage, const char *pPrefix, const char *pMiddle, const char *pSuffix, char *pValueA, size_t ValueASize, char *pValueB, size_t ValueBSize)
	{
		const int PrefixLen = str_length(pPrefix);
		const int SuffixLen = str_length(pSuffix);
		if(str_comp_num(pMessage, pPrefix, PrefixLen) != 0)
			return false;
		const char *pMiddlePos = str_find(pMessage + PrefixLen, pMiddle);
		if(pMiddlePos == nullptr)
			return false;
		if(str_comp(pMiddlePos + str_length(pMiddle), pSuffix) == 0)
			return false;
		if(!str_endswith(pMessage, pSuffix))
			return false;
		str_truncate(pValueA, (int)ValueASize, pMessage + PrefixLen, pMiddlePos - (pMessage + PrefixLen));
		const char *pValueBStart = pMiddlePos + str_length(pMiddle);
		str_truncate(pValueB, (int)ValueBSize, pValueBStart, pMessage + str_length(pMessage) - SuffixLen - pValueBStart);
		return true;
	}

	bool TryCopyStaticLocalizedNotification(const char *pMessage, char *pBuf, size_t BufSize, QmHudNotifications::EMessageKey &MessageKey, QmHudNotifications::EServerMessageDomain &Domain, QmHudNotifications::ESoloPrompt &SoloPrompt)
	{
		if(BufSize > 0)
			pBuf[0] = '\0';
		MessageKey = QmHudNotifications::EMessageKey::None;

		SoloPrompt = QmHudNotifications::MatchKnownSoloPrompt(pMessage);
		if(SoloPrompt == QmHudNotifications::ESoloPrompt::Enter)
		{
			Domain = QmHudNotifications::EServerMessageDomain::Solo;
			str_copy(pBuf, Localize("You are now in a solo part"), BufSize);
			return true;
		}
		if(SoloPrompt == QmHudNotifications::ESoloPrompt::Leave)
		{
			Domain = QmHudNotifications::EServerMessageDomain::Solo;
			str_copy(pBuf, Localize("You are now out of the solo part"), BufSize);
			return true;
		}
		if(TryMatchStaticMessageKey(pMessage, MessageKey))
		{
			const auto *pMeta = QmHudNotifications::FindMessageMetadata(MessageKey);
			if(pMeta != nullptr)
			{
				Domain = pMeta->m_Domain;
				str_copy(pBuf, Localize(QmHudNotifications::CanonicalMessageText(MessageKey)), BufSize);
				return true;
			}
		}

#define QM_TRY_FORMAT_STATIC_NOTIFICATION(pDomain, pOriginal, pLocalized) \
	if(str_comp(pMessage, pOriginal) == 0) \
	{ \
		Domain = pDomain; \
		str_copy(pBuf, Localize(pLocalized), BufSize); \
		return true; \
	} \
	if(str_comp(pMessage, pLocalized) == 0) \
	{ \
		Domain = pDomain; \
		str_copy(pBuf, Localize(pLocalized), BufSize); \
		return true; \
	}
#define QM_TRY_FORMAT_STATIC_TEAM_NOTIFICATION(pOriginal, pLocalized) \
	QM_TRY_FORMAT_STATIC_NOTIFICATION(QmHudNotifications::EServerMessageDomain::Team, pOriginal, pLocalized)
#define QM_TRY_FORMAT_STATIC_SWAP_RESCUE_NOTIFICATION(pOriginal, pLocalized) \
	QM_TRY_FORMAT_STATIC_NOTIFICATION(QmHudNotifications::EServerMessageDomain::SwapRescue, pOriginal, pLocalized)
#define QM_TRY_FORMAT_STATIC_VOTE_MODERATION_NOTIFICATION(pOriginal, pLocalized) \
	QM_TRY_FORMAT_STATIC_NOTIFICATION(QmHudNotifications::EServerMessageDomain::VoteModeration, pOriginal, pLocalized)
#define QM_TRY_FORMAT_STATIC_STATUS_NOTIFICATION(pOriginal, pLocalized) \
	QM_TRY_FORMAT_STATIC_NOTIFICATION(QmHudNotifications::EServerMessageDomain::Status, pOriginal, pLocalized)
		QM_HUD_NOTIFICATION_STATIC_TEAM_RULES(QM_TRY_FORMAT_STATIC_TEAM_NOTIFICATION)
		QM_HUD_NOTIFICATION_STATIC_SWAP_RESCUE_RULES(QM_TRY_FORMAT_STATIC_SWAP_RESCUE_NOTIFICATION)
		QM_HUD_NOTIFICATION_STATIC_VOTE_MODERATION_RULES(QM_TRY_FORMAT_STATIC_VOTE_MODERATION_NOTIFICATION)
		QM_HUD_NOTIFICATION_STATIC_STATUS_RULES(QM_TRY_FORMAT_STATIC_STATUS_NOTIFICATION)
#undef QM_TRY_FORMAT_STATIC_STATUS_NOTIFICATION
#undef QM_TRY_FORMAT_STATIC_VOTE_MODERATION_NOTIFICATION
#undef QM_TRY_FORMAT_STATIC_SWAP_RESCUE_NOTIFICATION
#undef QM_TRY_FORMAT_STATIC_TEAM_NOTIFICATION
#undef QM_TRY_FORMAT_STATIC_NOTIFICATION

		return false;
	}

	bool AnalyzeTeamMessage(const char *pMessage, QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		char aValueA[128];
		char aValueB[128];
		char aValueC[128];

		if(ExtractWrappedValue(pMessage, "'", "' joined team 0", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' joined team 0"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "Team save in progress. You'll be able to load with '/load ", "'", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Team save in progress. You'll be able to load with '/load %s'"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractTwoWrappedValues(pMessage, "Team save in progress. You'll be able to load with '/load ", "' if save is successful or with '/load ", "' if it fails", aValueA, sizeof(aValueA), aValueB, sizeof(aValueB)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Team save in progress. You'll be able to load with '/load %s' if save is successful or with '/load %s' if it fails"), aValueA, aValueB);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractTwoWrappedValues(pMessage, "Team successfully saved by ", ". The database connection failed, using generated save code instead to avoid collisions. Use '/load ", "' to continue", aValueA, sizeof(aValueA), aValueB, sizeof(aValueB)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' to continue"), aValueA, aValueB);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "Team successfully saved by "))
		{
			const char *pPrefix = "Team successfully saved by ";
			const char *pMiddle = ". The database connection failed, using generated save code instead to avoid collisions. Use '/load ";
			const char *pMiddle2 = "' on ";
			const char *pSuffix = " to continue";
			const char *pNameStart = pMessage + str_length(pPrefix);
			const char *pMiddlePos = str_find(pNameStart, pMiddle);
			if(pMiddlePos != nullptr)
			{
				const char *pCodeStart = pMiddlePos + str_length(pMiddle);
				const char *pMiddle2Pos = str_find(pCodeStart, pMiddle2);
				if(pMiddle2Pos != nullptr && str_endswith(pMessage, pSuffix))
				{
					str_truncate(aValueA, sizeof(aValueA), pNameStart, pMiddlePos - pNameStart);
					str_truncate(aValueB, sizeof(aValueB), pCodeStart, pMiddle2Pos - pCodeStart);
					const char *pServerStart = pMiddle2Pos + str_length(pMiddle2);
					str_truncate(aValueC, sizeof(aValueC), pServerStart, pMessage + str_length(pMessage) - str_length(pSuffix) - pServerStart);
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' on %s to continue"), aValueA, aValueC, aValueB);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(ExtractWrappedValue(pMessage, "'", "' disabled practice mode for your team", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' disabled practice mode for your team"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' locked your team.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' locked your team."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 锁定了你们的队伍", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' locked your team."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' locked your team. After the race starts, killing will kill everyone in your team.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' locked your team. After the race starts, killing will kill everyone in your team."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 锁定了你们的队伍。比赛开始后，任何人 kill 都会导致整队死亡", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' locked your team. After the race starts, killing will kill everyone in your team."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' unlocked your team.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' unlocked your team."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 解锁了你们的队伍", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' unlocked your team."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "This team already has the maximum allowed size of ", " players", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("This team already has the maximum allowed size of %s players"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "Can't disable team 0 mode. This team exceeds the maximum allowed size of ", " players for regular team", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Can't disable team 0 mode. This team exceeds the maximum allowed size of %s players for regular team"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "无法关闭 team 0 模式。该队伍人数已超过普通队伍允许上限 "))
		{
			str_copy(aValueA, pMessage + str_length("无法关闭 team 0 模式。该队伍人数已超过普通队伍允许上限 "), sizeof(aValueA));
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Can't disable team 0 mode. This team exceeds the maximum allowed size of %s players for regular team"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' disabled team 0 mode.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' disabled team 0 mode."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 关闭了 team 0 模式。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' disabled team 0 mode."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' enabled team 0 mode. This will make your team behave like team 0.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' enabled team 0 mode. This will make your team behave like team 0."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 开启了 team 0 模式。你们的队伍现在会按 team 0 规则运作。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' enabled team 0 mode. This will make your team behave like team 0."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "You are in team ") && str_find(pMessage, " having ") != nullptr)
		{
			const char *pTeamStart = pMessage + str_length("You are in team ");
			const char *pHaving = str_find(pTeamStart, " having ");
			const char *pPlayers = pHaving == nullptr ? nullptr : str_find(pHaving + str_length(" having "), " ");
			if(pHaving != nullptr && pPlayers != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pTeamStart, pHaving - pTeamStart);
				str_truncate(aValueB, sizeof(aValueB), pHaving + str_length(" having "), pPlayers - (pHaving + str_length(" having ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You are in team %s having %s players"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(pMessage[0] == '\'' && str_find(pMessage + 1, "' joined team ") != nullptr)
		{
			const char *pTeamPos = str_find(pMessage, "' joined team ");
			if(pTeamPos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pTeamPos - (pMessage + 1));
				const char *pTeamStart = pTeamPos + str_length("' joined team ");
				str_truncate(aValueB, sizeof(aValueB), pTeamStart, str_length(pTeamStart));
				SetDynamicSemantic(Analysis, QmHudNotifications::EDynamicMessageKey::TeamJoined, aValueA, aValueB);
				FormatDynamicLocalizedText(Analysis.m_DynamicSemantic, Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText));
				SetSemanticLocalizedAnalysis(Analysis);
				return true;
			}
		}
		if(pMessage[0] == '\'' && str_find(pMessage + 1, "' 加入了 ") != nullptr && str_endswith(pMessage, " 队"))
		{
			const char *pTeamPos = str_find(pMessage, "' 加入了 ");
			if(pTeamPos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pTeamPos - (pMessage + 1));
				const char *pTeamStart = pTeamPos + str_length("' 加入了 ");
				str_truncate(aValueB, sizeof(aValueB), pTeamStart, pMessage + str_length(pMessage) - str_length(" 队") - pTeamStart);
				SetDynamicSemantic(Analysis, QmHudNotifications::EDynamicMessageKey::TeamJoined, aValueA, aValueB);
				FormatDynamicLocalizedText(Analysis.m_DynamicSemantic, Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText));
				SetSemanticLocalizedAnalysis(Analysis);
				return true;
			}
		}
		if(pMessage[0] == '\'' && str_find(pMessage + 1, "' invited you to team ") != nullptr)
		{
			const char *pTeamPos = str_find(pMessage, "' invited you to team ");
			const char *pUsePos = pTeamPos == nullptr ? nullptr : str_find(pTeamPos + str_length("' invited you to team "), ". Use /team ");
			if(pTeamPos != nullptr && pUsePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pTeamPos - (pMessage + 1));
				str_truncate(aValueB, sizeof(aValueB), pTeamPos + str_length("' invited you to team "), pUsePos - (pTeamPos + str_length("' invited you to team ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' invited you to team %s. Use /team %s to join"), aValueA, aValueB, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(pMessage[0] == '\'' && str_find(pMessage + 1, "' 邀请你加入 ") != nullptr && str_find(pMessage, "。输入 /team ") != nullptr)
		{
			const char *pTeamPos = str_find(pMessage, "' 邀请你加入 ");
			const char *pUsePos = pTeamPos == nullptr ? nullptr : str_find(pTeamPos + str_length("' 邀请你加入 "), "。输入 /team ");
			if(pTeamPos != nullptr && pUsePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pTeamPos - (pMessage + 1));
				str_truncate(aValueB, sizeof(aValueB), pTeamPos + str_length("' 邀请你加入 "), pUsePos - (pTeamPos + str_length("' 邀请你加入 ")));
				if(str_endswith(aValueB, " 队"))
					aValueB[str_length(aValueB) - str_length(" 队")] = '\0';
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' invited you to team %s. Use /team %s to join"), aValueA, aValueB, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' invited '") != nullptr && str_endswith(pMessage, "' to your team."))
		{
			const char *pMiddle = "' invited '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				str_truncate(aValueB, sizeof(aValueB), pTargetStart, pMessage + str_length(pMessage) - str_length("' to your team.") - pTargetStart);
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' invited '%s' to your team."), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' 邀请了 '") != nullptr && str_endswith(pMessage, "' 加入你们的队伍。"))
		{
			const char *pMiddle = "' 邀请了 '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				str_truncate(aValueB, sizeof(aValueB), pTargetStart, pMessage + str_length(pMessage) - str_length("' 加入你们的队伍。") - pTargetStart);
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' invited '%s' to your team."), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(ExtractWrappedValue(pMessage, "This team cannot finish anymore because '", "' left the team before hitting the start", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("This team cannot finish anymore because '%s' left the team before hitting the start"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Team, Analysis.m_aLocalizedText);
			return true;
		}
		return false;
	}

	bool AnalyzeSwapRescueMessage(const char *pMessage, QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		char aValueA[128];
		char aValueB[128];
		char aValueC[128];

		if(ExtractWrappedValue(pMessage, "You have already requested to swap with ", ".", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You have already requested to swap with %s."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "你已经向 ", " 发过交换请求了", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You have already requested to swap with %s."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You have requested to swap with ", ". Use /cancelswap to cancel the request.", aValueA, sizeof(aValueA)))
		{
			SetDynamicSemantic(Analysis, QmHudNotifications::EDynamicMessageKey::SwapRequestSent, aValueA);
			FormatDynamicLocalizedText(Analysis.m_DynamicSemantic, Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText));
			SetSemanticLocalizedAnalysis(Analysis);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "你已向 ", " 发出交换请求。输入 /cancelswap 可取消", aValueA, sizeof(aValueA)))
		{
			SetDynamicSemantic(Analysis, QmHudNotifications::EDynamicMessageKey::SwapRequestSent, aValueA);
			FormatDynamicLocalizedText(Analysis.m_DynamicSemantic, Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText));
			SetSemanticLocalizedAnalysis(Analysis);
			return true;
		}
		if(str_endswith(pMessage, ".") && str_find(pMessage, " has requested to swap with you. To complete the swap process please wait ") != nullptr)
		{
			const char *pMiddle = " has requested to swap with you. To complete the swap process please wait ";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			const char *pSecondsStart = pMiddlePos == nullptr ? nullptr : pMiddlePos + str_length(pMiddle);
			const char *pMiddle2 = " seconds and then type /swap ";
			const char *pMiddle2Pos = pSecondsStart == nullptr ? nullptr : str_find(pSecondsStart, pMiddle2);
			if(pMiddlePos != nullptr && pMiddle2Pos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage, pMiddlePos - pMessage);
				str_truncate(aValueB, sizeof(aValueB), pSecondsStart, pMiddle2Pos - pSecondsStart);
				const char *pNameStart = pMiddle2Pos + str_length(pMiddle2);
				str_truncate(aValueC, sizeof(aValueC), pNameStart, pMessage + str_length(pMessage) - 1 - pNameStart);
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("%s has requested to swap with you. To complete the swap process please wait %s seconds and then type /swap %s."), aValueA, aValueB, aValueC);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(ExtractTwoWrappedValues(pMessage, "", " has requested to swap with ", ".", aValueA, sizeof(aValueA), aValueB, sizeof(aValueB)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("%s has requested to swap with %s"), aValueA, aValueB);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You have to wait ", " seconds until you can swap.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You have to wait %s seconds until you can swap"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You can jump ", " time", aValueA, sizeof(aValueA)) ||
			ExtractWrappedValue(pMessage, "You can jump ", " times", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You can now jump %s times"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "你现在可以跳 ", " 次", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You can now jump %s times"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "Rescue mode changed to ", ".", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Rescue mode switched to %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "救援模式已切换为 ", "。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Rescue mode switched to %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "Current rescue mode: ", ".", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Current rescue mode: %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "当前救援模式：", "。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Current rescue mode: %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "Your swap request timed out ", " seconds ago. Use /swap again to re-initiate it.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Your swap request timed out %s seconds ago, use /swap again to request"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractTwoWrappedValues(pMessage, "", " has swapped with ", ".", aValueA, sizeof(aValueA), aValueB, sizeof(aValueB)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("%s and %s have swapped"), aValueA, aValueB);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You have canceled swap with ", ".", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You canceled the swap with %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "", " has canceled swap with you.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("%s canceled the swap with you"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractTwoWrappedValues(pMessage, "", " has canceled swap with ", ".", aValueA, sizeof(aValueA), aValueB, sizeof(aValueB)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("%s canceled the swap with %s"), aValueA, aValueB);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "Everyone in your locked team was killed because '") && str_endswith(pMessage, "."))
		{
			const char *pPrefix = "Everyone in your locked team was killed because '";
			const char *pMiddle = "' ";
			const char *pNameStart = pMessage + str_length(pPrefix);
			const char *pMiddlePos = str_find(pNameStart, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pNameStart, pMiddlePos - pNameStart);
				const char *pActionStart = pMiddlePos + str_length(pMiddle);
				str_truncate(aValueB, sizeof(aValueB), pActionStart, pMessage + str_length(pMessage) - 1 - pActionStart);
				const char *pAction = str_comp(aValueB, "killed") == 0 ? Localize("killed themselves") : Localize("died");
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Your locked team was killed because '%s' %s"), aValueA, pAction);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::SwapRescue, Analysis.m_aLocalizedText);
				return true;
			}
		}
		return false;
	}

	bool AnalyzeVoteModerationMessage(const char *pMessage, QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		char aValueA[128];
		char aValueB[128];
		char aValueC[128];
		char aValueD[64];

		if(str_find(pMessage, "' voted to ") != nullptr && str_endswith(pMessage, " required votes)"))
		{
			const char *pMiddle = "' voted to ";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr && pMessage[0] == '\'')
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pModeStart = pMiddlePos + str_length(pMiddle);
				const char *pModeEnd = str_find(pModeStart, " /practice mode for your team, which means you can use practice commands, but you can't earn a rank. Type /practice to vote (");
				if(pModeEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pModeStart, pModeEnd - pModeStart);
					const char *pVoteStart = pModeEnd + str_length(" /practice mode for your team, which means you can use practice commands, but you can't earn a rank. Type /practice to vote (");
					const char *pSlashPos = str_find(pVoteStart, "/");
					const char *pSuffixPos = str_find(pVoteStart, " required votes)");
					if(pSlashPos != nullptr && pSuffixPos != nullptr)
					{
						str_truncate(aValueC, sizeof(aValueC), pVoteStart, pSlashPos - pVoteStart);
						str_truncate(aValueD, sizeof(aValueD), pSlashPos + 1, pSuffixPos - (pSlashPos + 1));
						str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called vote to %s practice mode for your team. Current votes %s/%s"), aValueA, str_comp(aValueB, "enable") == 0 ? Localize("enable") : Localize("disable"), aValueC, aValueD);
						SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
						return true;
					}
				}
			}
		}
		if(ExtractWrappedValue(pMessage, "'", "' isn't an option on this server", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' is not a valid option on this server"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 不是本服务器支持的选项", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' is not a valid option on this server"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' called vote to change server option '") != nullptr)
		{
			const char *pMiddle = "' called vote to change server option '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pOptionStart = pMiddlePos + str_length(pMiddle);
				const char *pOptionEnd = str_find(pOptionStart, "'");
				if(pOptionEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pOptionStart, pOptionEnd - pOptionStart);
					if(str_comp(pOptionEnd, "'") == 0)
					{
						str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called vote to change server option: %s"), aValueA, aValueB);
						SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
						return true;
					}
					if(str_startswith(pOptionEnd, "' (") && str_endswith(pMessage, ")"))
					{
						str_truncate(aValueC, sizeof(aValueC), pOptionEnd + 3, pMessage + str_length(pMessage) - 1 - (pOptionEnd + 3));
						str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called vote to change server option: %s (reason: %s)"), aValueA, aValueB, aValueC);
						SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
						return true;
					}
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' 发起了修改服务器选项 '") != nullptr)
		{
			const char *pMiddle = "' 发起了修改服务器选项 '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pOptionStart = pMiddlePos + str_length(pMiddle);
				const char *pOptionEnd = str_find(pOptionStart, "'");
				if(pOptionEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pOptionStart, pOptionEnd - pOptionStart);
					if(str_comp(pOptionEnd, "' 的投票") == 0)
					{
						str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called vote to change server option: %s"), aValueA, aValueB);
						SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
						return true;
					}
					if(str_startswith(pOptionEnd, "' 的投票（") && str_endswith(pMessage, "）"))
					{
						const char *pReasonStart = pOptionEnd + str_length("' 的投票（");
						str_truncate(aValueC, sizeof(aValueC), pReasonStart, pMessage + str_length(pMessage) - str_length("）") - pReasonStart);
						str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called vote to change server option: %s (reason: %s)"), aValueA, aValueB, aValueC);
						SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
						return true;
					}
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' called vote to kick '") != nullptr && str_endswith(pMessage, ")"))
		{
			const char *pMiddle = "' called vote to kick '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pTargetEnd = str_find(pTargetStart, "' (");
				if(pTargetEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pTargetEnd - pTargetStart);
					str_truncate(aValueC, sizeof(aValueC), pTargetEnd + 3, pMessage + str_length(pMessage) - 1 - (pTargetEnd + 3));
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to kick '%s' (reason: %s)"), aValueA, aValueB, aValueC);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' 发起了踢出 '") != nullptr && str_endswith(pMessage, "）"))
		{
			const char *pMiddle = "' 发起了踢出 '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pTargetEnd = str_find(pTargetStart, "' 的投票（");
				if(pTargetEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pTargetEnd - pTargetStart);
					const char *pReasonStart = pTargetEnd + str_length("' 的投票（");
					str_truncate(aValueC, sizeof(aValueC), pReasonStart, pMessage + str_length(pMessage) - str_length("）") - pReasonStart);
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to kick '%s' (reason: %s)"), aValueA, aValueB, aValueC);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' called for vote to mute '") != nullptr && str_endswith(pMessage, ")"))
		{
			const char *pMiddle = "' called for vote to mute '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pTargetEnd = str_find(pTargetStart, "' (");
				if(pTargetEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pTargetEnd - pTargetStart);
					str_truncate(aValueC, sizeof(aValueC), pTargetEnd + 3, pMessage + str_length(pMessage) - 1 - (pTargetEnd + 3));
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to mute '%s' (reason: %s)"), aValueA, aValueB, aValueC);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' 发起了禁言 '") != nullptr && str_endswith(pMessage, "）"))
		{
			const char *pMiddle = "' 发起了禁言 '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pTargetEnd = str_find(pTargetStart, "' 的投票（");
				if(pTargetEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pTargetEnd - pTargetStart);
					const char *pReasonStart = pTargetEnd + str_length("' 的投票（");
					str_truncate(aValueC, sizeof(aValueC), pReasonStart, pMessage + str_length(pMessage) - str_length("）") - pReasonStart);
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to mute '%s' (reason: %s)"), aValueA, aValueB, aValueC);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' called for vote to pause '") != nullptr && str_endswith(pMessage, ")"))
		{
			const char *pMiddle = "' called for vote to pause '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pForPos = str_find(pTargetStart, "' for ");
				const char *pReasonPos = pForPos == nullptr ? nullptr : str_find(pForPos + str_length("' for "), " seconds (");
				if(pForPos != nullptr && pReasonPos != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pForPos - pTargetStart);
					str_truncate(aValueC, sizeof(aValueC), pForPos + str_length("' for "), pReasonPos - (pForPos + str_length("' for ")));
					str_truncate(aValueD, sizeof(aValueD), pReasonPos + str_length(" seconds ("), pMessage + str_length(pMessage) - 1 - (pReasonPos + str_length(" seconds (")));
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to force-pause '%s' for %s seconds (reason: %s)"), aValueA, aValueB, aValueC, aValueD);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' 发起了暂停 '") != nullptr && str_endswith(pMessage, "）"))
		{
			const char *pMiddle = "' 发起了暂停 '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pForPos = str_find(pTargetStart, "' ");
				const char *pReasonPos = pForPos == nullptr ? nullptr : str_find(pForPos + str_length("' "), " 秒的投票（");
				if(pForPos != nullptr && pReasonPos != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pForPos - pTargetStart);
					str_truncate(aValueC, sizeof(aValueC), pForPos + str_length("' "), pReasonPos - (pForPos + str_length("' ")));
					const char *pReasonStart = pReasonPos + str_length(" 秒的投票（");
					str_truncate(aValueD, sizeof(aValueD), pReasonStart, pMessage + str_length(pMessage) - str_length("）") - pReasonStart);
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to force-pause '%s' for %s seconds (reason: %s)"), aValueA, aValueB, aValueC, aValueD);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' called for vote to move '") != nullptr && str_find(pMessage, "' to spectators (") != nullptr && str_endswith(pMessage, ")"))
		{
			const char *pMiddle = "' called for vote to move '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pTargetEnd = str_find(pTargetStart, "' to spectators (");
				if(pTargetEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pTargetEnd - pTargetStart);
					str_truncate(aValueC, sizeof(aValueC), pTargetEnd + str_length("' to spectators ("), pMessage + str_length(pMessage) - 1 - (pTargetEnd + str_length("' to spectators (")));
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to move '%s' to spectators (reason: %s)"), aValueA, aValueB, aValueC);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "'") && str_find(pMessage, "' 发起了把 '") != nullptr && str_find(pMessage, "' 移到旁观的投票（") != nullptr && str_endswith(pMessage, "）"))
		{
			const char *pMiddle = "' 发起了把 '";
			const char *pMiddlePos = str_find(pMessage, pMiddle);
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pTargetStart = pMiddlePos + str_length(pMiddle);
				const char *pTargetEnd = str_find(pTargetStart, "' 移到旁观的投票（");
				if(pTargetEnd != nullptr)
				{
					str_truncate(aValueB, sizeof(aValueB), pTargetStart, pTargetEnd - pTargetStart);
					const char *pReasonStart = pTargetEnd + str_length("' 移到旁观的投票（");
					str_truncate(aValueC, sizeof(aValueC), pReasonStart, pMessage + str_length(pMessage) - str_length("）") - pReasonStart);
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to move '%s' to spectators (reason: %s)"), aValueA, aValueB, aValueC);
					SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
					return true;
				}
			}
		}
		if(str_startswith(pMessage, "There's a ") && str_find(pMessage, " second wait time between kick votes for each player please wait ") != nullptr && str_endswith(pMessage, " second(s)"))
		{
			const char *pPrefixEnd = pMessage + str_length("There's a ");
			const char *pMiddlePos = str_find(pPrefixEnd, " second wait time between kick votes for each player please wait ");
			const char *pWaitStart = str_find(pMessage, "please wait ");
			const char *pWaitPos = str_find(pMessage, " second(s)");
			if(pMiddlePos != nullptr && pWaitStart != nullptr && pWaitPos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pPrefixEnd, pMiddlePos - pPrefixEnd);
				str_truncate(aValueB, sizeof(aValueB), pWaitStart + str_length("please wait "), pWaitPos - (pWaitStart + str_length("please wait ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("There's a %s second wait time between kick votes for each player please wait %s second(s)"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(str_startswith(pMessage, "每位玩家的踢人投票之间有 ") && str_find(pMessage, " 秒冷却，请等待 ") != nullptr && str_endswith(pMessage, " 秒"))
		{
			const char *pPrefixEnd = pMessage + str_length("每位玩家的踢人投票之间有 ");
			const char *pMiddlePos = str_find(pPrefixEnd, " 秒冷却，请等待 ");
			const char *pWaitStart = str_find(pMessage, "请等待 ");
			const char *pWaitPos = str_find(pMessage, " 秒");
			if(pMiddlePos != nullptr && pWaitStart != nullptr && pWaitPos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pPrefixEnd, pMiddlePos - pPrefixEnd);
				str_truncate(aValueB, sizeof(aValueB), pWaitStart + str_length("请等待 "), pWaitPos - (pWaitStart + str_length("请等待 ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("There's a %s second wait time between kick votes for each player please wait %s second(s)"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(ExtractWrappedValue(pMessage, "Kick voting requires ", " players", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Kick voting requires %s players"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "发起踢人投票需要 ", " 名玩家", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Kick voting requires %s players"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "Authorized player forced vote '", "'", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Authorized player forced vote '%s'"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "授权玩家强制将当前投票设为 '", "'", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Authorized player forced vote '%s'"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "There's a ") && str_find(pMessage, " second delay between map-votes, please wait ") != nullptr && str_endswith(pMessage, " seconds."))
		{
			const char *pPrefixEnd = pMessage + str_length("There's a ");
			const char *pMiddlePos = str_find(pPrefixEnd, " second delay between map-votes, please wait ");
			const char *pWaitStart = str_find(pMessage, "please wait ");
			const char *pWaitPos = str_find(pMessage, " seconds.");
			if(pMiddlePos != nullptr && pWaitStart != nullptr && pWaitPos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pPrefixEnd, pMiddlePos - pPrefixEnd);
				str_truncate(aValueB, sizeof(aValueB), pWaitStart + str_length("please wait "), pWaitPos - (pWaitStart + str_length("please wait ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("There's a %s second delay between map-votes, please wait %s seconds."), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(str_startswith(pMessage, "换图投票之间有 ") && str_find(pMessage, " 秒冷却，请等待 ") != nullptr && str_endswith(pMessage, " 秒。"))
		{
			const char *pPrefixEnd = pMessage + str_length("换图投票之间有 ");
			const char *pMiddlePos = str_find(pPrefixEnd, " 秒冷却，请等待 ");
			const char *pWaitStart = str_find(pMessage, "请等待 ");
			const char *pWaitPos = str_find(pMessage, " 秒。");
			if(pMiddlePos != nullptr && pWaitStart != nullptr && pWaitPos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pPrefixEnd, pMiddlePos - pPrefixEnd);
				str_truncate(aValueB, sizeof(aValueB), pWaitStart + str_length("请等待 "), pWaitPos - (pWaitStart + str_length("请等待 ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("There's a %s second delay between map-votes, please wait %s seconds."), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(ExtractWrappedValue(pMessage, "'", "' called for vote to kick you", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to kick you"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 发起了针对你的踢人投票", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to kick you"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' called for vote to move you to spectators", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to move you to spectators"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 发起了针对你的旁观投票", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' called for vote to move you to spectators"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You must wait ", " seconds before making your first vote.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You must wait %s seconds before making your first vote."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "你还需要等待 ", " 秒才能发起第一次投票。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You must wait %s seconds before making your first vote."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You must wait ", " seconds before making another vote.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You must wait %s seconds before making another vote."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "你还需要等待 ", " 秒才能再次发起投票。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You must wait %s seconds before making another vote."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You are not permitted to vote for the next ", " seconds.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You are not permitted to vote for the next %s seconds."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "你在接下来的 ", " 秒内不能发起投票。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You are not permitted to vote for the next %s seconds."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::VoteModeration, Analysis.m_aLocalizedText);
			return true;
		}
		return false;
	}

	bool AnalyzeStatusMessage(const char *pMessage, QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		char aValueA[128];
		char aValueB[128];

		if(ExtractWrappedValue(pMessage, "This server has an initial chat delay, you will be able to talk in ", " seconds.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("This server has an initial chat delay, you will be able to talk in %s seconds."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "本服务器有初始聊天延迟，你将在 ", " 秒后可以发言。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("This server has an initial chat delay, you will be able to talk in %s seconds."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You are not permitted to talk for the next ", " seconds.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You are not permitted to talk for the next %s seconds."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "你在接下来的 ", " 秒内不能发言。", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You are not permitted to talk for the next %s seconds."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "Timer is displayed in "))
		{
			str_copy(aValueA, pMessage + str_length("Timer is displayed in "), sizeof(aValueA));
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Timer is displayed in %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "计时器显示在 "))
		{
			str_copy(aValueA, pMessage + str_length("计时器显示在 "), sizeof(aValueA));
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Timer is displayed in %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_comp(pMessage, "计时器不会显示。") == 0)
		{
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Localize("Timer isn't displayed."));
			return true;
		}
		if(str_startswith(pMessage, "Time to wait before changing team: "))
		{
			str_copy(aValueA, pMessage + str_length("Time to wait before changing team: "), sizeof(aValueA));
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Time to wait before changing team: %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_startswith(pMessage, "距离下次切换队伍还需等待："))
		{
			str_copy(aValueA, pMessage + str_length("距离下次切换队伍还需等待："), sizeof(aValueA));
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Time to wait before changing team: %s"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "You are force-paused for ", " seconds.", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("You are force-paused for %s seconds."), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(str_find(pMessage, " current race time is ") != nullptr)
		{
			const char *pMiddle = str_find(pMessage, " current race time is ");
			if(pMiddle != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage, pMiddle - pMessage);
				str_copy(aValueB, pMiddle + str_length(" current race time is "), sizeof(aValueB));
				if(str_comp(aValueA, "Your") == 0)
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Your current race time is %s"), aValueB);
				else
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("%s current race time is %s"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(str_find(pMessage, "当前用时是 ") != nullptr)
		{
			const char *pMiddle = str_find(pMessage, "当前用时是 ");
			if(pMiddle != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage, pMiddle - pMessage);
				str_copy(aValueB, pMiddle + str_length("当前用时是 "), sizeof(aValueB));
				if(str_comp(aValueA, "你的") == 0)
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Your current race time is %s"), aValueB);
				else
					str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("%s current race time is %s"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(str_startswith(pMessage, "Showing the checkpoint times for '") && str_find(pMessage, "' with a race time of ") != nullptr)
		{
			const char *pNameStart = pMessage + str_length("Showing the checkpoint times for '");
			const char *pTimePos = str_find(pNameStart, "' with a race time of ");
			if(pTimePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pNameStart, pTimePos - pNameStart);
				str_truncate(aValueB, sizeof(aValueB), pTimePos + str_length("' with a race time of "), str_length(pTimePos + str_length("' with a race time of ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Showing the checkpoint times for '%s' with a race time of %s"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(str_startswith(pMessage, "正在显示 '") && str_find(pMessage, "' 的检查点用时，当前成绩为 ") != nullptr)
		{
			const char *pNameStartZh = pMessage + str_length("正在显示 '");
			const char *pTimePosZh = str_find(pNameStartZh, "' 的检查点用时，当前成绩为 ");
			if(pTimePosZh != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pNameStartZh, pTimePosZh - pNameStartZh);
				str_truncate(aValueB, sizeof(aValueB), pTimePosZh + str_length("' 的检查点用时，当前成绩为 "), str_length(pTimePosZh + str_length("' 的检查点用时，当前成绩为 ")));
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("Showing the checkpoint times for '%s' with a race time of %s"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(ExtractWrappedValue(pMessage, "'", "' would have timed out, but can use timeout protection now", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' would have timed out, but can use timeout protection now"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(ExtractWrappedValue(pMessage, "'", "' 原本会超时掉线，但现在可以使用超时保护", aValueA, sizeof(aValueA)))
		{
			str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' would have timed out, but can use timeout protection now"), aValueA);
			SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
			return true;
		}
		if(pMessage[0] == '\'' && str_find(pMessage + 1, "' was force-paused for ") != nullptr && str_endswith(pMessage, "s"))
		{
			const char *pMiddlePos = str_find(pMessage + 1, "' was force-paused for ");
			if(pMiddlePos != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePos - (pMessage + 1));
				const char *pSecondsStart = pMiddlePos + str_length("' was force-paused for ");
				const char *pSuffixPos = pMessage + str_length(pMessage) - 1;
				str_truncate(aValueB, sizeof(aValueB), pSecondsStart, pSuffixPos - pSecondsStart);
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' was force-paused for %s seconds"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
				return true;
			}
		}
		if(pMessage[0] == '\'' && str_find(pMessage + 1, "' 被强制暂停 ") != nullptr && str_endswith(pMessage, " 秒"))
		{
			const char *pMiddlePosZh = str_find(pMessage + 1, "' 被强制暂停 ");
			if(pMiddlePosZh != nullptr)
			{
				str_truncate(aValueA, sizeof(aValueA), pMessage + 1, pMiddlePosZh - (pMessage + 1));
				const char *pSecondsStartZh = pMiddlePosZh + str_length("' 被强制暂停 ");
				str_truncate(aValueB, sizeof(aValueB), pSecondsStartZh, pMessage + str_length(pMessage) - str_length(" 秒") - pSecondsStartZh);
				str_format(Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), Localize("'%s' was force-paused for %s seconds"), aValueA, aValueB);
				SetLocalizedAnalysis(Analysis, QmHudNotifications::EServerMessageRoute::System, QmHudNotifications::EServerMessageClass::Prompt, QmHudNotifications::EServerMessageDomain::Status, Analysis.m_aLocalizedText);
				return true;
			}
		}
		return false;
	}

	bool TryAnalyzeRecognizedSemanticMessage(const char *pMessage, QmHudNotifications::SServerMessageAnalysis &Analysis)
	{
		if(pMessage == nullptr || pMessage[0] == '\0')
			return false;

		QmHudNotifications::EMessageKey StaticMessageKey = QmHudNotifications::EMessageKey::None;
		QmHudNotifications::EServerMessageDomain StaticDomain = QmHudNotifications::EServerMessageDomain::None;
		QmHudNotifications::ESoloPrompt StaticSoloPrompt = QmHudNotifications::ESoloPrompt::None;
		if(TryCopyStaticLocalizedNotification(pMessage, Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), StaticMessageKey, StaticDomain, StaticSoloPrompt) &&
			StaticMessageKey != QmHudNotifications::EMessageKey::None)
		{
			Analysis.m_MessageKey = StaticMessageKey;
			Analysis.m_SoloPrompt = StaticSoloPrompt;
			SetSemanticLocalizedAnalysis(Analysis);
			return Analysis.m_MessageKey != QmHudNotifications::EMessageKey::None;
		}

		QmHudNotifications::SServerMessageAnalysis DynamicAnalysis;
		if((AnalyzeTeamMessage(pMessage, DynamicAnalysis) || AnalyzeSwapRescueMessage(pMessage, DynamicAnalysis)) &&
			DynamicAnalysis.m_DynamicSemantic.m_Key != QmHudNotifications::EDynamicMessageKey::None)
		{
			Analysis = DynamicAnalysis;
			return true;
		}

		return false;
	}

	bool RecognizedSemanticMessageExcludedByMetadata(const char *pMessage)
	{
		QmHudNotifications::SServerMessageAnalysis Analysis;
		return TryAnalyzeRecognizedSemanticMessage(pMessage, Analysis) && SemanticAnalysisExcludedByMetadata(Analysis);
	}

	bool MessageStartsWithAny(const char *pMessage, const char *const *ppPrefixes, size_t NumPrefixes)
	{
		for(size_t i = 0; i < NumPrefixes; ++i)
		{
			if(str_startswith(pMessage, ppPrefixes[i]))
				return true;
		}
		return false;
	}

	bool MessageEqualsAny(const char *pMessage, const char *const *ppMessages, size_t NumMessages)
	{
		for(size_t i = 0; i < NumMessages; ++i)
		{
			if(str_comp(pMessage, ppMessages[i]) == 0)
				return true;
		}
		return false;
	}

	bool IsHelpOrExampleServerMessage(const char *pMessage)
	{
		static const char *const s_apPrefixes[] = {
			"Usage:",
			"用法：",
			"Example:",
			"示例：",
			"Bad:",
			"错误示例：",
			"Available practice commands:",
			"可用练习命令：",
			"Available rescue modes:",
			"可用救援模式：",
			"Emote commands are:",
			"可用表情命令：",
		};
		static const char *const s_apExactMessages[] = {
			"See /practicecmdlist for a list of all available practice commands. Most commonly used ones are /telecursor, /lasttp and /rescue",
			"输入 /practicecmdlist 可以查看所有可用的练习命令。最常用的是 /telecursor、/lasttp 和 /rescue",
			"示例：/map adr3 可以发起 Adrenaline 3 的换图投票。这表示地图名必须以 'a' 开头，并按顺序包含 'd'、'r'、'3'",
			"Example: /map adr3 to call vote for Adrenaline 3. This means that the map name must start with 'a' and contain the characters 'd', 'r' and '3' in that order",
		};
		return MessageStartsWithAny(pMessage, s_apPrefixes, std::size(s_apPrefixes)) ||
		       MessageEqualsAny(pMessage, s_apExactMessages, std::size(s_apExactMessages));
	}

	bool IsJoinOrLeaveBroadcast(const char *pMessage)
	{
		const char *pLeaveGameMarker = pMessage[0] == '\'' ? str_find(pMessage + 1, "' has left the game") : nullptr;
		const bool IsLeaveGameBroadcast = pLeaveGameMarker != nullptr &&
						  (str_comp(pLeaveGameMarker, "' has left the game") == 0 ||
							  (str_comp_num(pLeaveGameMarker, "' has left the game (", str_length("' has left the game (")) == 0 && str_endswith(pMessage, ")") != nullptr));
		const char *pLeaveGameMarkerZh = pMessage[0] == '\'' ? str_find(pMessage + 1, "' 离开了游戏") : nullptr;
		const bool IsLeaveGameBroadcastZh = pLeaveGameMarkerZh != nullptr &&
						    (str_comp(pLeaveGameMarkerZh, "' 离开了游戏") == 0 ||
							    (str_comp_num(pLeaveGameMarkerZh, "' 离开了游戏（", str_length("' 离开了游戏（")) == 0 && str_endswith(pMessage, "）") != nullptr));
		return str_endswith(pMessage, " entered and joined the game") ||
		       str_endswith(pMessage, " joined the game") ||
		       IsLeaveGameBroadcast ||
		       IsLeaveGameBroadcastZh;
	}

	bool IsBasicInfoServerMessage(const char *pMessage)
	{
		static const char *const s_apPrefixes[] = {
			"DDraceNetwork 版本:",
			"DDraceNetwork Version:",
			"Git 提交哈希:",
			"Git revision hash:",
			"官方网站:",
			"Official site:",
			"更多命令请查看:",
			"For more info:",
			"或访问 DDNet.org",
			"请访问 DDNet.org",
			"Please visit DDNet.org",
		};
		static const char *const s_apExactMessages[] = {
			"请友善交流。",
			"未设置服务器规则，请联系管理员。",
			"Be nice.",
			"No Rules Defined, Kill em all!!",
		};
		return MessageStartsWithAny(pMessage, s_apPrefixes, std::size(s_apPrefixes)) ||
		       MessageEqualsAny(pMessage, s_apExactMessages, std::size(s_apExactMessages)) ||
		       IsJoinOrLeaveBroadcast(pMessage);
	}
} // namespace

namespace QmHudNotifications
{
	ESoloPrompt MatchKnownSoloPrompt(const char *pMessage)
	{
		if(pMessage == nullptr)
			return ESoloPrompt::None;
		if(str_comp(pMessage, "You are now in a solo part") == 0 || str_comp(pMessage, "你现在处于单人区域") == 0)
			return ESoloPrompt::Enter;
		if(str_comp(pMessage, "You are now out of the solo part") == 0 || str_comp(pMessage, "你现在已离开单人区域") == 0)
			return ESoloPrompt::Leave;
		return ESoloPrompt::None;
	}

	bool ShouldSuppressSoloChatMessage(const char *pMessage, ESoloPrompt PendingCompatPrompt)
	{
		const ESoloPrompt Known = MatchKnownSoloPrompt(pMessage);
		if(Known == ESoloPrompt::None)
			return false;
		return PendingCompatPrompt == ESoloPrompt::None || Known == PendingCompatPrompt;
	}

	bool ShouldExcludeSystemNotification(const char *pMessage)
	{
		if(pMessage == nullptr || pMessage[0] == '\0')
			return true;
		if(IsHelpOrExampleServerMessage(pMessage))
			return true;
		// Stable semantic notifications use catalog metadata; help/example shaped text stays literal-based until it has a real semantic family.
		if(RecognizedSemanticMessageExcludedByMetadata(pMessage))
			return true;
		if(IsBasicInfoServerMessage(pMessage))
			return true;
		return false;
	}

	SServerMessageAnalysis AnalyzeServerMessage(const char *pMessage, ESoloPrompt PendingCompatPrompt)
	{
		SServerMessageAnalysis Analysis;
		if(pMessage == nullptr || pMessage[0] == '\0')
			return Analysis;

		if(ShouldSuppressSoloChatMessage(pMessage, PendingCompatPrompt))
		{
			const ESoloPrompt SoloPrompt = MatchKnownSoloPrompt(pMessage);
			SetLocalizedAnalysis(Analysis, EServerMessageRoute::Solo, EServerMessageClass::Prompt, EServerMessageDomain::Solo, SoloPrompt == ESoloPrompt::Enter ? Localize("You are now in a solo part") : Localize("You are now out of the solo part"), SoloPrompt);
			return Analysis;
		}

		if(IsHelpOrExampleServerMessage(pMessage))
		{
			Analysis.m_Class = EServerMessageClass::HelpInfo;
			Analysis.m_Domain = EServerMessageDomain::Status;
			Analysis.m_UseFallbackLocalization = true;
			return Analysis;
		}

		if(TryAnalyzeRecognizedSemanticMessage(pMessage, Analysis))
		{
			if(SemanticAnalysisExcludedByMetadata(Analysis))
			{
				Analysis.m_Route = EServerMessageRoute::None;
				Analysis.m_Class = EServerMessageClass::BasicInfo;
			}
			return Analysis;
		}

		if(ShouldExcludeSystemNotification(pMessage))
		{
			Analysis.m_Class = EServerMessageClass::BasicInfo;
			Analysis.m_Domain = EServerMessageDomain::Status;
			Analysis.m_UseFallbackLocalization = true;
			return Analysis;
		}

		// 单次分析既决定是否进通知栏，也决定如何本地化，避免排除规则和格式化规则继续分叉漂移。
		EServerMessageDomain StaticDomain = EServerMessageDomain::None;
		ESoloPrompt StaticSoloPrompt = ESoloPrompt::None;
		EMessageKey StaticMessageKey = EMessageKey::None;
		if(TryCopyStaticLocalizedNotification(pMessage, Analysis.m_aLocalizedText, sizeof(Analysis.m_aLocalizedText), StaticMessageKey, StaticDomain, StaticSoloPrompt))
		{
			if(StaticMessageKey != EMessageKey::None)
			{
				Analysis.m_MessageKey = StaticMessageKey;
				Analysis.m_SoloPrompt = StaticSoloPrompt;
				SetSemanticLocalizedAnalysis(Analysis);
				if(Analysis.m_Route != EServerMessageRoute::None || Analysis.m_Class != EServerMessageClass::None)
					return Analysis;
				else
					SetLocalizedAnalysis(Analysis, EServerMessageRoute::System, EServerMessageClass::Prompt, StaticDomain, Analysis.m_aLocalizedText, StaticSoloPrompt);
			}
			else
				SetLocalizedAnalysis(Analysis, EServerMessageRoute::System, EServerMessageClass::Prompt, StaticDomain, Analysis.m_aLocalizedText, StaticSoloPrompt);
			return Analysis;
		}
		if(AnalyzeTeamMessage(pMessage, Analysis) || AnalyzeSwapRescueMessage(pMessage, Analysis) || AnalyzeVoteModerationMessage(pMessage, Analysis) || AnalyzeStatusMessage(pMessage, Analysis))
			return Analysis;

		SetFallbackAnalysis(Analysis);
		return Analysis;
	}

	SServerMessageEntryDecision DecideServerMessageEntry(const SServerMessageAnalysis &Analysis, const SServerMessageRouteConfig &Config)
	{
		SServerMessageEntryDecision Decision;
		if(!Config.m_RouteSystemMessages)
			return Decision;
		if(!Config.m_UseCategoryFilters)
		{
			if(Analysis.m_Class == EServerMessageClass::BasicInfo || Analysis.m_Class == EServerMessageClass::HelpInfo || Analysis.m_Domain == EServerMessageDomain::Unknown)
			{
				Decision.m_QueueNotification = true;
				Decision.m_UseFallbackNotification = true;
				return Decision;
			}
		}
		else
		{
			if(Analysis.m_Class == EServerMessageClass::BasicInfo && !Config.m_ShowBasicInfo)
				return Decision;
			if(Analysis.m_Class == EServerMessageClass::HelpInfo && !Config.m_ShowHelpInfo)
				return Decision;
			if(Analysis.m_Class == EServerMessageClass::Prompt && Analysis.m_Domain != EServerMessageDomain::Unknown && !Config.m_ShowPrompts)
				return Decision;
			if(Analysis.m_Domain == EServerMessageDomain::Unknown && !Config.m_ShowUnknown)
				return Decision;
		}
		if(Analysis.m_Route == EServerMessageRoute::Solo)
		{
			Decision.m_QueueNotification = true;
			Decision.m_ClearPendingCompatPrompt = true;
			return Decision;
		}
		if(Analysis.m_Class == EServerMessageClass::BasicInfo || Analysis.m_Class == EServerMessageClass::HelpInfo)
		{
			Decision.m_QueueNotification = true;
			Decision.m_UseFallbackNotification = true;
			return Decision;
		}
		if(Analysis.m_Route == EServerMessageRoute::System)
		{
			Decision.m_QueueNotification = true;
			Decision.m_UseFallbackNotification = Analysis.m_UseFallbackLocalization;
		}
		return Decision;
	}

	SServerMessageEntryDecision DecideServerMessageEntry(const SServerMessageAnalysis &Analysis, bool RouteSystemMessages)
	{
		SServerMessageRouteConfig Config;
		Config.m_RouteSystemMessages = RouteSystemMessages;
		return DecideServerMessageEntry(Analysis, Config);
	}

	bool ShouldSuppressServerMessageChat(const SServerMessageAnalysis &Analysis)
	{
		return Analysis.m_Route == EServerMessageRoute::Solo;
	}

	EServerMessageRoute ServerMessageRoute(const char *pMessage, ESoloPrompt PendingCompatPrompt, bool RouteSystemMessages)
	{
		if(pMessage == nullptr || pMessage[0] == '\0')
			return EServerMessageRoute::None;
		if(!RouteSystemMessages)
			return EServerMessageRoute::None;
		return AnalyzeServerMessage(pMessage, PendingCompatPrompt).m_Route;
	}

	EServerMessageClass ServerMessageClass(const char *pMessage, ESoloPrompt PendingCompatPrompt)
	{
		return AnalyzeServerMessage(pMessage, PendingCompatPrompt).m_Class;
	}

	bool TryFormatLocalizedNotificationMessage(const char *pMessage, char *pBuf, size_t BufSize)
	{
		if(BufSize > 0)
			pBuf[0] = '\0';
		const SServerMessageAnalysis Analysis = AnalyzeServerMessage(pMessage, ESoloPrompt::None);
		if(Analysis.m_aLocalizedText[0] == '\0')
			return false;
		str_copy(pBuf, Analysis.m_aLocalizedText, BufSize);
		return true;
	}

	const char *LocalizationOnlyCanonical(const char *pMessage)
	{
#define QM_TRY_LOCALIZATION_ONLY_CANONICAL(pSource, pCanonical) \
	if(str_comp(pMessage, pSource) == 0) \
		return pCanonical;
		QM_HUD_NOTIFICATION_LOCALIZATION_ONLY_RULES(QM_TRY_LOCALIZATION_ONLY_CANONICAL)
#undef QM_TRY_LOCALIZATION_ONLY_CANONICAL
		return nullptr;
	}

	bool TryFormatLocalizedServerChatMessage(const char *pMessage, char *pBuf, size_t BufSize)
	{
		if(BufSize > 0)
			pBuf[0] = '\0';
		if(pMessage == nullptr || pMessage[0] == '\0')
			return false;

		const SServerMessageAnalysis Analysis = AnalyzeServerMessage(pMessage, ESoloPrompt::None);
		if(Analysis.m_aLocalizedText[0] != '\0')
		{
			str_copy(pBuf, Analysis.m_aLocalizedText, BufSize);
			return true;
		}

		// 服务端消息中文化后不再是英文 key，这里先用 canonical 归一表把消息映射回
		// 英文 source 再交给 Localize；该表不参与 HUD 通知分类，只服务本地化显示。
		// 只有语言文件确实登记了该 key 才替换：生成链尚未重跑时保持原文，避免把
		// 中文服务端消息显示成英文 canonical。译文与英文 source 相同也算已登记
		// （例如德语的 Team Top 5），这类情况同样要替换。
		if(const char *pCanonical = LocalizationOnlyCanonical(pMessage))
		{
			if(g_Localization.FindString(str_quickhash(pCanonical), str_quickhash("")) != nullptr)
			{
				str_copy(pBuf, Localize(pCanonical), BufSize);
				return true;
			}
		}

		// 兜底只把完整服务端消息当作本地化 key，不拆解或猜测动态参数。
		// Localize 未命中时会原样返回，因此旧语言文件和普通提示保持兼容。
		if(!Analysis.m_UseFallbackLocalization)
			return false;
		const char *pLocalized = Localize(pMessage);
		if(str_comp(pLocalized, pMessage) == 0)
			return false;
		str_copy(pBuf, pLocalized, BufSize);
		return true;
	}
} // namespace QmHudNotifications
