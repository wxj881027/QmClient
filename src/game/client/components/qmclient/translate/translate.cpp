#include "translate.h"

#include "translate_detect.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <array>

namespace
{
	using qm_translate::IsChineseLanguage;
	using qm_translate::IsChineseVariantLanguage;
	// 后端检测出的源语言是否即为目标语言；中文变体目标（zh-TW 等）与本地启发式粒度一致不做判定
	bool AutoDetectedLanguageMatchesTarget(const char *pDetected, const char *pTarget)
	{
		if(!pDetected || pDetected[0] == '\0' || !pTarget || pTarget[0] == '\0')
			return false;
		if(IsChineseVariantLanguage(pTarget))
			return false;
		if(IsChineseLanguage(pDetected) && IsChineseLanguage(pTarget))
			return true;
		return str_comp_nocase(pDetected, pTarget) == 0;
	}
	using SLocalLanguageStats = qm_translate::SLanguageStats;

	SLocalLanguageStats AnalyzeLocalLanguageStats(const char *pText)
	{
		return qm_translate::AnalyzeLanguage(pText);
	}

	SLocalLanguageStats AnalyzeChatLanguage(const char *pText, const CGameClient *pClient)
	{
		std::array<qm_translate::SPlayerReference, MAX_CLIENTS> aPlayers;
		int NumPlayers = 0;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			const auto &Player = pClient->m_aClients[Id];
			if(Player.m_Active)
				aPlayers[NumPlayers++] = {Player.m_aName, Id};
		}
		return qm_translate::AnalyzeLanguage(pText, aPlayers.data(), NumPlayers);
	}

	const char *GetEffectiveTranslateTarget(const char *pTarget)
	{
		return (pTarget && pTarget[0] != '\0') ? pTarget : DefaultConfig::QmTranslateTarget;
	}

	const char *TranslateNoticeText(ETranslateNotice Notice)
	{
		const char *pSource = TranslateNoticeSource(Notice);
		return pSource ? Localize(pSource) : nullptr;
	}

	// 验证语言代码格式
	// 有效格式：2-3 个字母（如 zh, en, ja）或 xx-XX 格式（如 zh-CN, zh-TW）
	static bool IsValidLanguageCode(const char *pCode)
	{
		if(!pCode || pCode[0] == '\0')
			return false;

		size_t Len = str_length(pCode);
		if(Len < 2 || Len > 5) // 最短 "en"，最长 "zh-CN"
			return false;

		// 检查格式：纯字母或 xx-XX 格式
		for(size_t i = 0; i < Len; i++)
		{
			char c = pCode[i];
			if(i == 2 && c == '-')
				continue; // 允许 xx-XX 格式中的连字符
			if(c < 'a' || c > 'z')
			{
				// 允许大写字母（在 - 后面）
				if(i > 2 && c >= 'A' && c <= 'Z')
					continue;
				return false;
			}
		}
		return true;
	}

}

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if(pResult->NumArguments() == 0)
		pName = nullptr;
	else
		pName = pResult->GetString(0);

	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pName);
}

void CTranslate::ConTranslateId(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pResult->GetInteger(0));
}

void CTranslate::OnConsoleInit()
{
	Console()->Register("translate", "?r[name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate last message (of a given name)");
	Console()->Register("translate_id", "v[id]", CFGFLAG_CLIENT, ConTranslateId, this, "Translate last message of the person with this id");
}

void CTranslate::OnReset()
{
	m_Jobs.Clear();
	m_LastDiagnostic = {};
	m_LastMymemoryQuotaNoticeTime = -1;
}

void CTranslate::OnShutdown()
{
	m_Jobs.Clear();
	m_LastDiagnostic = {};
	m_LastMymemoryQuotaNoticeTime = -1;
}

void CTranslate::Translate(int Id, bool ShowProgress)
{
	if(Id < 0 || Id >= (int)std::size(GameClient()->m_aClients))
	{
		GameClient()->m_Chat.Echo(Localize("Invalid ID"));
		return;
	}
	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
	{
		GameClient()->m_Chat.Echo(Localize("This ID is not connected"));
		return;
	}
	Translate(Player.m_aName, ShowProgress);
}

void CTranslate::Translate(const char *pName, bool ShowProgress)
{
	const int BestIndex = SelectTranslateHistoryLine(GameClient()->m_Chat.m_CurrentLine, CChat::MAX_LINES, [&](int Index) {
		const CChat::CLine &Line = GameClient()->m_Chat.m_aLines[Index];
		if(!Line.m_Initialized)
			return -1;
		if(!IsTranslatePlayerCandidate(Line.m_ClientId, Line.m_Initialized, Line.m_aText[0] != '\0', Line.m_pTranslateResponse != nullptr, GameClient()->m_aLocalIds, std::size(GameClient()->m_aLocalIds)))
			return -1;
		if(!pName)
			return 0;
		int Score = -1;
		for(const CChat::SMergedAuthor &Author : Line.m_vMergedAuthors)
			Score = maximum(Score, TranslateNameMatchScore(pName, Author.m_aName, Author.m_aPlayerName));
		if(Line.m_vMergedAuthors.empty())
			Score = TranslateNameMatchScore(pName, Line.m_aName, nullptr);
		return Score;
	});
	if(BestIndex < 0)
	{
		GameClient()->m_Chat.Echo(Localize("No chat message to translate"));
		return;
	}
	Translate(GameClient()->m_Chat.m_aLines[BestIndex], ShowProgress);
}

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress, bool AutoTriggered)
{
	if(Line.m_pTranslateResponse || !Line.m_Initialized || Line.m_aText[0] == '\0')
		return;
	if(!m_Jobs.CanSubmit(GetMaxConcurrency()))
		return;
	if(Line.m_ClientId == CChat::SERVER_MSG)
	{
		if(ShowProgress)
			GameClient()->m_Chat.Echo(Localize("Do not translate server messages"));
		return;
	}
	const int LineIndex = GameClient()->m_Chat.GetLineIndex(&Line);
	if(LineIndex < 0)
		return;

	std::shared_ptr<CTranslateResponse> pResponse;
	bool InvalidBackend = false;
	const bool Accepted = m_Jobs.CreateAndSubmit([&] {
		STranslateJob Job;
		Job.m_LineIndex = LineIndex;
		Job.m_TranslationId = Line.m_TranslationId;
		Job.m_AutoTriggered = AutoTriggered;
		const char *pTarget = GetEffectiveTranslateTarget(g_Config.m_QmTranslateTarget);
		if(!IsValidLanguageCode(pTarget))
			pTarget = "en";
		str_copy(Job.m_aTarget, pTarget, sizeof(Job.m_aTarget));
		const char *pSource = NormalizeTranslateSource(g_Config.m_QmTranslateSource);
		if(!IsValidLanguageCode(pSource) && str_comp_nocase(pSource, "auto") != 0)
			pSource = "auto";
		Job.m_pBackend = CreateTranslateBackend(*Http(), Line.m_aText, Job.m_aTarget, pSource);
		if(!Job.m_pBackend)
		{
			InvalidBackend = true;
			return Job;
		}
		if(ShowProgress)
			str_format(Job.m_pTranslateResponse->m_Text, sizeof(Job.m_pTranslateResponse->m_Text), Localize("%s translating to %s"), Job.m_pBackend->Name(), Job.m_aTarget);
		else
			Job.m_pTranslateResponse->m_Text[0] = '\0';
		pResponse = Job.m_pTranslateResponse;
		return Job;
	},
		GetMaxConcurrency());
	if(!Accepted)
	{
		if(InvalidBackend)
			GameClient()->m_Chat.Echo(Localize("Invalid translation backend"));
		else
			dbg_msg("translate", "could not prepare incoming translation task");
		return;
	}
	Line.m_pTranslateResponse = pResponse;
	if(ShowProgress)
	{
		Line.m_Time = time();
		GameClient()->m_Chat.RebuildChat();
	}
}

bool CTranslate::TryTranslateOutgoingChat(int Team, const char *pText)
{
	const auto Parsed = ParseOutgoingTranslateTarget(pText);
	if(!Parsed)
		return false;
	if(!m_Jobs.CanSubmit(GetMaxConcurrency()))
	{
		GameClient()->m_Chat.Echo(Localize("Too many translation tasks"));
		return true;
	}
	char aProgress[128];
	bool InvalidBackend = false;
	const bool Accepted = m_Jobs.CreateAndSubmit([&] {
		STranslateJob Job;
		Job.m_Outgoing = true;
		Job.m_DisplayOriginal = g_Config.m_QmTranslateOutgoingShowOriginal != 0;
		Job.m_Connection = g_Config.m_ClDummy;
		Job.m_LocalClientId = GameClient()->m_aLocalIds[Job.m_Connection];
		Job.m_Team = Team;
		Job.m_OriginalText.assign(Parsed->m_Text);
		mem_copy(Job.m_aTarget, Parsed->m_Target.data(), Parsed->m_Target.size());
		Job.m_aTarget[Parsed->m_Target.size()] = '\0';
		const char *pSource = NormalizeTranslateSource(g_Config.m_QmTranslateSource);
		if(!IsValidLanguageCode(pSource) && str_comp_nocase(pSource, "auto") != 0)
			pSource = "auto";
		Job.m_pBackend = CreateTranslateBackend(*Http(), Job.m_OriginalText.c_str(), Job.m_aTarget, pSource);
		if(!Job.m_pBackend)
		{
			InvalidBackend = true;
			return Job;
		}
		str_format(aProgress, sizeof(aProgress), Localize("%s translating to %s before send"), Job.m_pBackend->Name(), Job.m_aTarget);
		return Job;
	},
		GetMaxConcurrency());
	if(Accepted)
		GameClient()->m_Chat.Echo(aProgress);
	else if(InvalidBackend)
		GameClient()->m_Chat.Echo(Localize("Invalid translation backend"));
	else
		dbg_msg("translate", "could not prepare explicit outgoing translation task");
	// 显式请求已处理，创建或入队失败也不能回退发送原文。
	return true;
}

void CTranslate::OnRender()
{
	const auto Time = time();
	bool RebuildChat = false;
	const auto vCompleted = m_Jobs.Update([&](const STranslateJob &Job) {
		const CChat::CLine *pLine = GameClient()->m_Chat.GetLineByIndex(Job.m_LineIndex);
		return pLine && IsTranslateResponseCurrent(pLine->m_Initialized, pLine->m_TranslationId, pLine->m_pTranslateResponse, Job);
	});
	for(const auto &Completed : vCompleted)
	{
		const auto &Job = Completed.m_Job;
		CTranslateResponse &Response = *Job.m_pTranslateResponse;
		if(!Completed.m_Success)
		{
			str_copy(m_LastDiagnostic.m_aService, Job.m_pBackend->Name());
			m_LastDiagnostic.m_HttpStatus = Response.m_HttpStatus;
			m_LastDiagnostic.m_Notice = Response.m_Notice == ETranslateNotice::NONE ? ETranslateNotice::INVALID_RESPONSE : Response.m_Notice;
		}
		if(Job.m_Outgoing)
		{
			if(!Completed.m_Success)
			{
				char aBuf[sizeof(Response.m_Text)];
				if(const char *pNotice = TranslateNoticeText(Response.m_Notice))
					str_copy(aBuf, pNotice);
				else
					str_copy(aBuf, Localize(TranslateNoticeSource(ETranslateNotice::INVALID_RESPONSE)));
				GameClient()->m_Chat.Echo(aBuf);
			}
			if(Completed.m_Success)
			{
				// 翻译期间退出、切换身份或断开分身，不能借用另一连接发送。
				if(Job.m_Connection < IClient::CONN_MAIN || Job.m_Connection > IClient::CONN_DUMMY || Job.m_LocalClientId < 0 || Job.m_LocalClientId != GameClient()->m_aLocalIds[Job.m_Connection] || (Job.m_Connection == IClient::CONN_DUMMY && !Client()->DummyConnected()))
				{
					GameClient()->m_Chat.Echo(Localize("Translation canceled because the sender changed"));
					continue;
				}
				if(!DispatchOutgoingTranslation(Completed, [&](int Team, const char *pTranslated, const char *pLocalOriginal, int Connection) {
					   const auto Result = GameClient()->m_Chat.SendChatQueued(Team, pTranslated, false, pLocalOriginal, Connection);
					   return Result == CChat::EChatSendResult::SENT || Result == CChat::EChatSendResult::QUEUED;
				   }))
					GameClient()->m_Chat.Echo(Localize("Translated message could not be queued; original was not sent"));
			}
			continue;
		}
		CChat::CLine *pLine = GameClient()->m_Chat.GetLineByIndex(Job.m_LineIndex);
		// 出站错误提示可能写入聊天环形缓冲，发布前再次核对 owner。
		if(!pLine || !IsTranslateResponseCurrent(pLine->m_Initialized, pLine->m_TranslationId, pLine->m_pTranslateResponse, Job))
			continue;
		if(Completed.m_Success)
		{
			if((Job.m_AutoTriggered && g_Config.m_QmTranslateAutoMode == 0 && AutoDetectedLanguageMatchesTarget(Response.m_Language, Job.m_aTarget)) || str_comp_nocase(pLine->m_aText, Response.m_Text) == 0)
				Response.m_Text[0] = '\0';
		}
		else
		{
			char aBuf[sizeof(Response.m_Text)];
			// 失败标记供聊天错误样式与本地化提示分支使用
			Response.m_Error = true;
			// 服务提示（翻译记忆样板/屏蔽说明等）按固定本地化文案展示，不透出英文原文
			if(const char *pNotice = TranslateNoticeText(Response.m_Notice))
				str_copy(aBuf, pNotice);
			else
				str_copy(aBuf, Localize(TranslateNoticeSource(ETranslateNotice::INVALID_RESPONSE)));
			// 配额提示按连接节流；消除进度文本后也刷新聊天布局。
			bool SuppressNotice = false;
			if(str_comp(Job.m_pBackend->Name(), "MyMemory") == 0 && Response.m_Notice == ETranslateNotice::QUOTA_EXCEEDED)
			{
				const int64_t Now = time_get();
				SuppressNotice = m_LastMymemoryQuotaNoticeTime >= 0 && Now - m_LastMymemoryQuotaNoticeTime < time_freq() * 60;
				if(!SuppressNotice)
					m_LastMymemoryQuotaNoticeTime = Now;
			}
			if(SuppressNotice)
			{
				Response.m_Error = false;
				Response.m_Text[0] = '\0';
			}
			else
				str_copy(Response.m_Text, aBuf);
		}
		pLine->m_Time = Time;
		RebuildChat = true;
	}
	if(RebuildChat)
		GameClient()->m_Chat.RebuildChat();
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_QmTranslateAuto)
		return;
	if(Line.m_ClientId == CChat::CLIENT_MSG)
		return;
	if(Line.m_ClientId == CChat::SERVER_MSG)
		return;
	for(const int Id : GameClient()->m_aLocalIds)
	{
		if(Id >= 0 && Id == Line.m_ClientId)
			return;
	}
	if(str_comp(g_Config.m_QmTranslateBackend, "ftapi") == 0)
	{
		// FTAPI 过载保护：默认禁用自动翻译，防止服务过载
		if(!g_Config.m_QmTranslateFtapiAutoEnable)
		{
			static bool s_Warned = false;
			if(!s_Warned)
			{
				GameClient()->m_Chat.Echo(Localize("FTAPI auto-translate is disabled to prevent overload. Enable in settings if needed."));
				s_Warned = true;
			}
			return;
		}
	}
	const char *pTarget = GetEffectiveTranslateTarget(g_Config.m_QmTranslateTarget);
	const SLocalLanguageStats LocalStats = AnalyzeChatLanguage(Line.m_aText, GameClient());
	if(!qm_translate::ShouldTranslateIncoming(LocalStats, pTarget, g_Config.m_QmTranslateLocalDetectMinChars, g_Config.m_QmTranslateLocalDetectRatio, g_Config.m_QmTranslateAutoMode == 1))
		return;
	Translate(Line, false, true);
}

bool CTranslate::ShouldAutoTranslateOutgoing(const char *pText) const
{
	if(!g_Config.m_QmTranslateAutoOutgoing)
		return false;

	if(!pText || pText[0] == '\0' || pText[0] == '/')
		return false;

	const char *pTarget = GetEffectiveTranslateTarget(g_Config.m_QmTranslateOutgoingTarget);
	const SLocalLanguageStats LocalStats = AnalyzeChatLanguage(pText, GameClient());
	const char *pSource = NormalizeTranslateSource(g_Config.m_QmTranslateSource);
	if(!IsValidLanguageCode(pSource) && str_comp_nocase(pSource, "auto") != 0)
		pSource = "auto";
	return qm_translate::ShouldTranslateOutgoing(LocalStats, pTarget, pSource,
		g_Config.m_QmTranslateLocalDetectMinChars, g_Config.m_QmTranslateLocalDetectRatio, g_Config.m_QmTranslateAutoOutgoingMode == 1);
}

void CTranslate::StartAutoOutgoingTranslate(int Team, const char *pText)
{
	if(!m_Jobs.CanSubmit(GetMaxConcurrency()))
	{
		GameClient()->m_Chat.Echo(Localize("Too many translation tasks"));
		return;
	}
	char aProgress[128];
	bool InvalidBackend = false;
	const bool Accepted = m_Jobs.CreateAndSubmit([&] {
		STranslateJob Job;
		Job.m_Outgoing = true;
		Job.m_AutoTriggered = true;
		Job.m_DisplayOriginal = g_Config.m_QmTranslateOutgoingShowOriginal != 0;
		Job.m_Connection = g_Config.m_ClDummy;
		Job.m_LocalClientId = GameClient()->m_aLocalIds[Job.m_Connection];
		Job.m_OriginalText = pText;
		Job.m_Team = Team;
		const char *pTarget = GetEffectiveTranslateTarget(g_Config.m_QmTranslateOutgoingTarget);
		if(!IsValidLanguageCode(pTarget))
			pTarget = "en";
		str_copy(Job.m_aTarget, pTarget, sizeof(Job.m_aTarget));
		const char *pSource = NormalizeTranslateSource(g_Config.m_QmTranslateSource);
		if(!IsValidLanguageCode(pSource) && str_comp_nocase(pSource, "auto") != 0)
			pSource = "auto";
		Job.m_pBackend = CreateTranslateBackend(*Http(), Job.m_OriginalText.c_str(), Job.m_aTarget, pSource);
		if(!Job.m_pBackend)
		{
			InvalidBackend = true;
			return Job;
		}
		str_format(aProgress, sizeof(aProgress), Localize("Translating to %s..."), Job.m_aTarget);
		return Job;
	},
		GetMaxConcurrency());
	if(Accepted)
		GameClient()->m_Chat.Echo(aProgress);
	else if(InvalidBackend)
		GameClient()->m_Chat.Echo(Localize("Invalid translation backend"));
	else
		dbg_msg("translate", "could not prepare automatic outgoing translation task");
}

int CTranslate::GetMaxConcurrency() const
{
	return GetTranslateConcurrency();
}
