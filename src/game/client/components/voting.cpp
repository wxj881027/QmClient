/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "voting.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/components/scoreboard.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

namespace
{
bool ExtractMapName(const char *pDescription, char *pMapName, int MaxLen)
{
	if(!pDescription)
		return false;
	const char *pMapPrefix = str_find_nocase(pDescription, "Map:");
	if(pMapPrefix)
	{
		pMapPrefix += 4;
		while(*pMapPrefix == ' ')
			pMapPrefix++;
		str_copy(pMapName, pMapPrefix, MaxLen);
		return true;
	}
	const char *pBy = str_find_nocase(pDescription, " by ");
	if(pBy && (str_find(pDescription, "★") || str_find(pDescription, "✰")))
	{
		int Len = minimum((int)(pBy - pDescription), MaxLen - 1);
		str_copy(pMapName, pDescription, Len + 1);
		return true;
	}
	return false;
}

bool HasConfusableSubstring(const char *pText, const char *pNeedle)
{
	if(!pText || !pNeedle || pNeedle[0] == '\0')
		return false;

	int aText[128];
	int aNeedle[64];
	const int TextLen = str_utf8_to_skeleton(pText, aText, (int)(sizeof(aText) / sizeof(aText[0])));
	const int NeedleLen = str_utf8_to_skeleton(pNeedle, aNeedle, (int)(sizeof(aNeedle) / sizeof(aNeedle[0])));
	if(NeedleLen <= 0 || NeedleLen > TextLen)
		return false;

	for(int i = 0; i + NeedleLen <= TextLen; ++i)
	{
		bool Match = true;
		for(int j = 0; j < NeedleLen; ++j)
		{
			if(aText[i + j] != aNeedle[j])
			{
				Match = false;
				break;
			}
		}
		if(Match)
			return true;
	}
	return false;
}

bool HasTypeVoteMatch(const char *pDescription, const char *pNeedle)
{
	if(!pDescription || !pNeedle || pNeedle[0] == '\0')
		return false;
	if(str_utf8_find_nocase(pDescription, pNeedle))
		return true;

	char aDescLower[256];
	char aNeedleLower[96];
	str_utf8_tolower(pDescription, aDescLower, sizeof(aDescLower));
	str_utf8_tolower(pNeedle, aNeedleLower, sizeof(aNeedleLower));
	return HasConfusableSubstring(aDescLower, aNeedleLower);
}
}

void CVoting::ConCallvote(IConsole::IResult *pResult, void *pUserData)
{
	CVoting *pSelf = (CVoting *)pUserData;
	pSelf->Callvote(pResult->GetString(0), pResult->GetString(1), pResult->NumArguments() > 2 ? pResult->GetString(2) : "");
}

void CVoting::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CVoting *pSelf = (CVoting *)pUserData;
	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->Vote(1);
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->Vote(-1);
}

void CVoting::Callvote(const char *pType, const char *pValue, const char *pReason)
{
	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_CallVote Msg;
		Msg.m_pType = pType;
		Msg.m_pValue = pValue;
		Msg.m_pReason = pReason;
		Msg.m_Force = false;
		Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL, true);
		return;
	}
	CNetMsg_Cl_CallVote Msg = {nullptr};
	Msg.m_pType = pType;
	Msg.m_pValue = pValue;
	Msg.m_pReason = pReason;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

void CVoting::CallvoteSpectate(int ClientId, const char *pReason, bool ForceVote)
{
	if(ForceVote)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "set_team %d -1", ClientId);
		Client()->Rcon(aBuf);
	}
	else
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%d", ClientId);
		Callvote("spectate", aBuf, pReason);
	}
}

void CVoting::CallvoteKick(int ClientId, const char *pReason, bool ForceVote)
{
	if(ForceVote)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "force_vote kick %d %s", ClientId, pReason);
		Client()->Rcon(aBuf);
	}
	else
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%d", ClientId);
		Callvote("kick", aBuf, pReason);
	}
}

void CVoting::CallvoteOption(int OptionId, const char *pReason, bool ForceVote)
{
	CVoteOptionClient *pOption = m_pFirst;
	while(pOption && OptionId >= 0)
	{
		if(OptionId == 0)
		{
			if(ForceVote)
			{
				char aBuf[128] = "force_vote option \"";
				char *pDst = aBuf + str_length(aBuf);
				str_escape(&pDst, pOption->m_aDescription, aBuf + sizeof(aBuf));
				str_append(aBuf, "\" \"");
				pDst = aBuf + str_length(aBuf);
				str_escape(&pDst, pReason, aBuf + sizeof(aBuf));
				str_append(aBuf, "\"");
				Client()->Rcon(aBuf);
			}
			else
				Callvote("option", pOption->m_aDescription, pReason);
			break;
		}

		OptionId--;
		pOption = pOption->m_pNext;
	}
}

void CVoting::RemovevoteOption(int OptionId)
{
	CVoteOptionClient *pOption = m_pFirst;
	while(pOption && OptionId >= 0)
	{
		if(OptionId == 0)
		{
			char aBuf[128] = "remove_vote \"";
			char *pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, pOption->m_aDescription, aBuf + sizeof(aBuf));
			str_append(aBuf, "\"");
			Client()->Rcon(aBuf);
			break;
		}

		OptionId--;
		pOption = pOption->m_pNext;
	}
}

void CVoting::AddvoteOption(const char *pDescription, const char *pCommand)
{
	char aBuf[128] = "add_vote \"";
	char *pDst = aBuf + str_length(aBuf);
	str_escape(&pDst, pDescription, aBuf + sizeof(aBuf));
	str_append(aBuf, "\" \"");
	pDst = aBuf + str_length(aBuf);
	str_escape(&pDst, pCommand, aBuf + sizeof(aBuf));
	str_append(aBuf, "\"");
	Client()->Rcon(aBuf);
}

void CVoting::Vote(int v)
{
	CNetMsg_Cl_Vote Msg = {v};
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

int CVoting::SecondsLeft() const
{
	return (m_Closetime - time()) / time_freq();
}

CVoting::CVoting()
{
	ClearOptions();
	OnReset();
	ClearUnfinishedMapVoteChain();
}

int CVoting::FindMapVoteOptionIndex(const char *pMapName) const
{
	if(!pMapName || pMapName[0] == '\0')
		return -1;

	int i = 0;
	for(const CVoteOptionClient *pOption = m_pFirst; pOption; pOption = pOption->m_pNext, ++i)
	{
		char aMapName[128];
		if(ExtractMapName(pOption->m_aDescription, aMapName, sizeof(aMapName)) && str_comp_nocase(aMapName, pMapName) == 0)
			return i;
	}
	return -1;
}

int CVoting::FindTypeVoteOptionIndex(const char *pTypeKey, const char *pTypeLabel) const
{
	if(!pTypeKey || pTypeKey[0] == '\0')
		return -1;

	char aTypeLabelPlural[64];
	char aTypeLabelSingular[64];
	char aTypeLabelLocalized[64];
	str_format(aTypeLabelPlural, sizeof(aTypeLabelPlural), "%s Maps", pTypeKey);
	str_format(aTypeLabelSingular, sizeof(aTypeLabelSingular), "%s Map", pTypeKey);
	if(pTypeLabel && pTypeLabel[0] != '\0')
		str_format(aTypeLabelLocalized, sizeof(aTypeLabelLocalized), "%s图", pTypeLabel);
	else
		aTypeLabelLocalized[0] = '\0';

	int i = 0;
	for(const CVoteOptionClient *pOption = m_pFirst; pOption; pOption = pOption->m_pNext, ++i)
	{
		if(HasTypeVoteMatch(pOption->m_aDescription, aTypeLabelPlural) || HasTypeVoteMatch(pOption->m_aDescription, aTypeLabelSingular))
			return i;
		if(aTypeLabelLocalized[0] != '\0' && HasTypeVoteMatch(pOption->m_aDescription, aTypeLabelLocalized))
			return i;
		if(pTypeLabel && pTypeLabel[0] != '\0' && HasTypeVoteMatch(pOption->m_aDescription, pTypeLabel))
			return i;
	}
	return -1;
}

bool CVoting::MatchTypeVoteDescription(const char *pDescription) const
{
	if(!pDescription || m_aPendingTypeKey[0] == '\0')
		return false;

	char aTypeLabelPlural[64];
	char aTypeLabelSingular[64];
	str_format(aTypeLabelPlural, sizeof(aTypeLabelPlural), "%s Maps", m_aPendingTypeKey);
	str_format(aTypeLabelSingular, sizeof(aTypeLabelSingular), "%s Map", m_aPendingTypeKey);
	if(HasTypeVoteMatch(pDescription, aTypeLabelPlural) || HasTypeVoteMatch(pDescription, aTypeLabelSingular))
		return true;

	if(m_aPendingTypeLabel[0] != '\0')
	{
		char aTypeLabelLocalized[64];
		str_format(aTypeLabelLocalized, sizeof(aTypeLabelLocalized), "%s图", m_aPendingTypeLabel);
		if(HasTypeVoteMatch(pDescription, aTypeLabelLocalized))
			return true;
		if(HasTypeVoteMatch(pDescription, m_aPendingTypeLabel))
			return true;
	}

	return false;
}

bool CVoting::TryCallPendingMapVote()
{
	if(!m_PendingMapVoteReady || m_aPendingMap[0] == '\0')
		return false;
	if(IsVoting())
		return false;

	const int MapOptionIndex = FindMapVoteOptionIndex(m_aPendingMap);
	if(MapOptionIndex < 0)
		return false;

	CallvoteOption(MapOptionIndex, "");
	ClearUnfinishedMapVoteChain();
	return true;
}

CVoting::EUnfinishedMapVoteAction CVoting::StartUnfinishedMapVoteChain(const char *pMapName, const char *pTypeKey, const char *pTypeLabel)
{
	ClearUnfinishedMapVoteChain();
	if(!pMapName || pMapName[0] == '\0')
		return EUnfinishedMapVoteAction::NO_OPTION;

	const int MapOptionIndex = FindMapVoteOptionIndex(pMapName);
	if(MapOptionIndex >= 0)
	{
		CallvoteOption(MapOptionIndex, "");
		return EUnfinishedMapVoteAction::MAP_VOTE_SENT;
	}

	const int TypeOptionIndex = FindTypeVoteOptionIndex(pTypeKey, pTypeLabel);
	if(TypeOptionIndex >= 0)
	{
		str_copy(m_aPendingMap, pMapName, sizeof(m_aPendingMap));
		str_copy(m_aPendingTypeKey, pTypeKey ? pTypeKey : "", sizeof(m_aPendingTypeKey));
		str_copy(m_aPendingTypeLabel, pTypeLabel ? pTypeLabel : "", sizeof(m_aPendingTypeLabel));
		m_PendingTypeVoteActive = false;
		m_PendingMapVoteReady = false;
		CallvoteOption(TypeOptionIndex, "");
		return EUnfinishedMapVoteAction::TYPE_VOTE_SENT;
	}

	return EUnfinishedMapVoteAction::NO_OPTION;
}

void CVoting::ClearUnfinishedMapVoteChain()
{
	m_aPendingMap[0] = '\0';
	m_aPendingTypeKey[0] = '\0';
	m_aPendingTypeLabel[0] = '\0';
	m_PendingTypeVoteActive = false;
	m_PendingMapVoteReady = false;
}

void CVoting::OnVoteResult(EVoteResult Result)
{
	if(m_aPendingMap[0] == '\0')
		return;

	if(!m_PendingTypeVoteActive)
	{
		ClearUnfinishedMapVoteChain();
		return;
	}

	m_PendingTypeVoteActive = false;
	if(Result == EVoteResult::PASS)
	{
		m_PendingMapVoteReady = true;
		TryCallPendingMapVote();
	}
	else
	{
		ClearUnfinishedMapVoteChain();
	}
}

void CVoting::AddOption(const char *pDescription)
{
	if(m_NumVoteOptions == MAX_VOTE_OPTIONS)
		return;

	CVoteOptionClient *pOption;
	if(m_pRecycleFirst)
	{
		pOption = m_pRecycleFirst;
		m_pRecycleFirst = m_pRecycleFirst->m_pNext;
		if(m_pRecycleFirst)
			m_pRecycleFirst->m_pPrev = nullptr;
		else
			m_pRecycleLast = nullptr;
	}
	else
		pOption = m_Heap.Allocate<CVoteOptionClient>();

	pOption->m_pNext = nullptr;
	pOption->m_pPrev = m_pLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	m_pLast = pOption;
	if(!m_pFirst)
		m_pFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription);
	++m_NumVoteOptions;

	if(m_PendingMapVoteReady)
		TryCallPendingMapVote();
}

void CVoting::RemoveOption(const char *pDescription)
{
	for(CVoteOptionClient *pOption = m_pFirst; pOption; pOption = pOption->m_pNext)
	{
		if(str_comp(pOption->m_aDescription, pDescription) == 0)
		{
			// remove it from the list
			if(m_pFirst == pOption)
				m_pFirst = m_pFirst->m_pNext;
			if(m_pLast == pOption)
				m_pLast = m_pLast->m_pPrev;
			if(pOption->m_pPrev)
				pOption->m_pPrev->m_pNext = pOption->m_pNext;
			if(pOption->m_pNext)
				pOption->m_pNext->m_pPrev = pOption->m_pPrev;
			--m_NumVoteOptions;

			// add it to recycle list
			pOption->m_pNext = nullptr;
			pOption->m_pPrev = m_pRecycleLast;
			if(pOption->m_pPrev)
				pOption->m_pPrev->m_pNext = pOption;
			m_pRecycleLast = pOption;
			if(!m_pRecycleFirst)
				m_pRecycleLast = pOption;

			break;
		}
	}
}

void CVoting::ClearOptions()
{
	m_Heap.Reset();

	m_NumVoteOptions = 0;
	m_pFirst = nullptr;
	m_pLast = nullptr;

	m_pRecycleFirst = nullptr;
	m_pRecycleLast = nullptr;
}

void CVoting::OnReset()
{
	m_Opentime = m_Closetime = 0;
	m_aDescription[0] = '\0';
	m_aReason[0] = '\0';
	m_Yes = m_No = m_Pass = m_Total = 0;
	m_Voted = 0;
	m_ReceivingOptions = false;

	if(GameClient() && Client()->State() != IClient::STATE_ONLINE)
		ClearUnfinishedMapVoteChain();
}

void CVoting::OnConsoleInit()
{
	Console()->Register("callvote", "s['kick'|'spectate'|'option'] s[id|option text] ?r[reason]", CFGFLAG_CLIENT, ConCallvote, this, "Call vote");
	Console()->Register("vote", "r['yes'|'no']", CFGFLAG_CLIENT, ConVote, this, "Vote yes/no");
}

void CVoting::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_VOTESET)
	{
		CNetMsg_Sv_VoteSet *pMsg = (CNetMsg_Sv_VoteSet *)pRawMsg;
		OnReset();
		if(pMsg->m_Timeout)
		{
			str_copy(m_aDescription, pMsg->m_pDescription);
			str_copy(m_aReason, pMsg->m_pReason);
			m_Opentime = time();
			m_Closetime = time() + time_freq() * pMsg->m_Timeout;
			if(MatchTypeVoteDescription(pMsg->m_pDescription))
				m_PendingTypeVoteActive = true;

			if(Client()->RconAuthed())
			{
				char aDescription[VOTE_DESC_LENGTH];
				char aReason[VOTE_REASON_LENGTH];
				GameClient()->FormatStreamerVoteText(m_aDescription, aDescription, sizeof(aDescription));
				GameClient()->FormatStreamerVoteText(m_aReason, aReason, sizeof(aReason));
				char aBuf[512];
				str_format(aBuf, sizeof(aBuf), "%s (%s)", aDescription, aReason);
				Client()->Notify("DDNet Vote", aBuf);
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
			}
		}
	}
	else if(MsgType == NETMSGTYPE_SV_VOTESTATUS)
	{
		CNetMsg_Sv_VoteStatus *pMsg = (CNetMsg_Sv_VoteStatus *)pRawMsg;
		m_Yes = pMsg->m_Yes;
		m_No = pMsg->m_No;
		m_Pass = pMsg->m_Pass;
		m_Total = pMsg->m_Total;
	}
	else if(MsgType == NETMSGTYPE_SV_VOTECLEAROPTIONS)
	{
		ClearOptions();
	}
	else if(MsgType == NETMSGTYPE_SV_VOTEOPTIONLISTADD)
	{
		CNetMsg_Sv_VoteOptionListAdd *pMsg = (CNetMsg_Sv_VoteOptionListAdd *)pRawMsg;
		int NumOptions = pMsg->m_NumOptions;
		for(int i = 0; i < NumOptions; ++i)
		{
			switch(i)
			{
			case 0: AddOption(pMsg->m_pDescription0); break;
			case 1: AddOption(pMsg->m_pDescription1); break;
			case 2: AddOption(pMsg->m_pDescription2); break;
			case 3: AddOption(pMsg->m_pDescription3); break;
			case 4: AddOption(pMsg->m_pDescription4); break;
			case 5: AddOption(pMsg->m_pDescription5); break;
			case 6: AddOption(pMsg->m_pDescription6); break;
			case 7: AddOption(pMsg->m_pDescription7); break;
			case 8: AddOption(pMsg->m_pDescription8); break;
			case 9: AddOption(pMsg->m_pDescription9); break;
			case 10: AddOption(pMsg->m_pDescription10); break;
			case 11: AddOption(pMsg->m_pDescription11); break;
			case 12: AddOption(pMsg->m_pDescription12); break;
			case 13: AddOption(pMsg->m_pDescription13); break;
			case 14: AddOption(pMsg->m_pDescription14);
			}
		}
	}
	else if(MsgType == NETMSGTYPE_SV_VOTEOPTIONADD)
	{
		CNetMsg_Sv_VoteOptionAdd *pMsg = (CNetMsg_Sv_VoteOptionAdd *)pRawMsg;
		AddOption(pMsg->m_pDescription);
	}
	else if(MsgType == NETMSGTYPE_SV_VOTEOPTIONREMOVE)
	{
		CNetMsg_Sv_VoteOptionRemove *pMsg = (CNetMsg_Sv_VoteOptionRemove *)pRawMsg;
		RemoveOption(pMsg->m_pDescription);
	}
	else if(MsgType == NETMSGTYPE_SV_YOURVOTE)
	{
		CNetMsg_Sv_YourVote *pMsg = (CNetMsg_Sv_YourVote *)pRawMsg;
		m_Voted = pMsg->m_Voted;
	}
	else if(MsgType == NETMSGTYPE_SV_VOTEOPTIONGROUPSTART)
	{
		m_ReceivingOptions = true;
	}
	else if(MsgType == NETMSGTYPE_SV_VOTEOPTIONGROUPEND)
	{
		m_ReceivingOptions = false;
	}
}

void CVoting::Render()
{
	const bool HudEditorPreview = GameClient()->m_HudEditor.IsActive();
	if((!g_Config.m_ClShowVotesAfterVoting && !GameClient()->m_Scoreboard.IsActive() && TakenChoice()) || (!IsVoting() && !HudEditorPreview))
		return;
	int Seconds = SecondsLeft();
	if(Seconds < 0)
	{
		if(HudEditorPreview)
			Seconds = 24;
		else
		{
			OnReset();
			return;
		}
	}

	// TClient
	if(g_Config.m_TcMiniVoteHud > 0 && !HudEditorPreview)
	{
		GameClient()->m_TClient.RenderMiniVoteHud();
		return;
	}

	const bool PreviewVote = HudEditorPreview && !IsVoting();
	if(PreviewVote)
		Seconds = 24;

	CUIRect View = {0.0f, 60.0f, 120.0f, 38.0f};
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::Voting, View);
	View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_R, 3.0f);
	View.Margin(3.0f, &View);

	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), Seconds);

	CUIRect Row, LeftColumn, RightColumn, ProgressSpinner;
	View.HSplitTop(6.0f, &Row, &View);
	Row.VSplitRight(TextRender()->TextWidth(6.0f, aBuf), &LeftColumn, &RightColumn);
	LeftColumn.VSplitRight(2.0f, &LeftColumn, nullptr);
	LeftColumn.VSplitRight(6.0f, &LeftColumn, &ProgressSpinner);
	LeftColumn.VSplitRight(2.0f, &LeftColumn, nullptr);

	SProgressSpinnerProperties ProgressProps;
	ProgressProps.m_Progress = std::clamp((time() - m_Opentime) / (float)(m_Closetime - m_Opentime), 0.0f, 1.0f);
	Ui()->RenderProgressSpinner(ProgressSpinner.Center(), ProgressSpinner.h / 2.0f, ProgressProps);

	Ui()->DoLabel(&RightColumn, aBuf, 6.0f, TEXTALIGN_MR);

	char aDescription[VOTE_DESC_LENGTH];
	char aReason[VOTE_REASON_LENGTH];
	if(PreviewVote)
	{
		str_copy(aDescription, "funvote", sizeof(aDescription));
		str_copy(aReason, "No reason given", sizeof(aReason));
	}
	else
	{
		GameClient()->FormatStreamerVoteText(VoteDescription(), aDescription, sizeof(aDescription));
		GameClient()->FormatStreamerVoteText(VoteReason(), aReason, sizeof(aReason));
	}

	Props.m_MaxWidth = LeftColumn.w;
	Ui()->DoLabel(&LeftColumn, aDescription, 6.0f, TEXTALIGN_ML, Props);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(6.0f, &Row, &View);
	str_format(aBuf, sizeof(aBuf), "%s %s", Localize("Reason:"), aReason);
	Props.m_MaxWidth = Row.w;
	Ui()->DoLabel(&Row, aBuf, 6.0f, TEXTALIGN_ML, Props);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(4.0f, &Row, &View);
	RenderBars(Row);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(6.0f, &Row, &View);
	Row.VSplitMid(&LeftColumn, &RightColumn, 4.0f);

	char aKey[64];
	GameClient()->m_Binds.GetKey("vote yes", aKey, sizeof(aKey));
	str_format(aBuf, sizeof(aBuf), "%s - %s", aKey, Localize("Vote yes"));
	TextRender()->TextColor(!PreviewVote && TakenChoice() == 1 ? ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&LeftColumn, aBuf, 6.0f, TEXTALIGN_ML);

	GameClient()->m_Binds.GetKey("vote no", aKey, sizeof(aKey));
	str_format(aBuf, sizeof(aBuf), "%s - %s", Localize("Vote no"), aKey);
	TextRender()->TextColor(!PreviewVote && TakenChoice() == -1 ? ColorRGBA(0.95f, 0.25f, 0.25f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&RightColumn, aBuf, 6.0f, TEXTALIGN_MR);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->m_HudEditor.UpdateVisibleRect(EHudEditorElement::Voting, {0.0f, 60.0f, 120.0f, 38.0f});
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CVoting::RenderBars(CUIRect Bars) const
{
	Bars.Draw(ColorRGBA(0.8f, 0.8f, 0.8f, 0.5f), IGraphics::CORNER_ALL, Bars.h / 2.0f);

	CUIRect Splitter;
	Bars.VMargin((Bars.w - 2.0f) / 2.0f, &Splitter);
	Splitter.Draw(ColorRGBA(0.4f, 0.4f, 0.4f, 0.5f), IGraphics::CORNER_NONE, 0.0f);

	if(m_Total)
	{
		if(m_Yes)
		{
			CUIRect YesArea;
			Bars.VSplitLeft(Bars.w * m_Yes / m_Total, &YesArea, nullptr);
			YesArea.Draw(ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f), IGraphics::CORNER_ALL, YesArea.h / 2.0f);
		}

		if(m_No)
		{
			CUIRect NoArea;
			Bars.VSplitRight(Bars.w * m_No / m_Total, nullptr, &NoArea);
			NoArea.Draw(ColorRGBA(0.9f, 0.2f, 0.2f, 0.85f), IGraphics::CORNER_ALL, NoArea.h / 2.0f);
		}
	}
}
