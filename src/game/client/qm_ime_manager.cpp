// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_ime_manager.h"

#include "components/qmclient/ime/qm_ime_engine.h"
#include "components/qmclient/ime/qm_ime_lexicon.h"
#include "components/qmclient/ime/qm_ime_sources.h"
#include "gameclient.h"
#include "lineinput.h"
#include "ui.h"

#include <engine/console.h>
#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/shared/qm_ime_policy.h>

#include <algorithm>

CQmImeManager::CQmImeManager() = default;

CQmImeManager::~CQmImeManager()
{
	// GameClient 在 Input 和 Storage 之前销毁，先解除回调借用再释放 provider。
	if(m_pGameClient != nullptr && m_pGameClient->Input() != nullptr)
		m_pGameClient->Input()->SetImeProvider(nullptr);
	if(m_pLexicon != nullptr)
		m_pLexicon->Save();
}

bool CQmImeBlocker::HasTextFocus(CGameClient *pGameClient) const
{
	if(pGameClient == nullptr || CLineInput::GetActiveInput() == nullptr)
		return false;
	return QmImeHasLiveInputOwner(CLineInput::GetActiveInputPriority(),
		pGameClient->m_Menus.IsActive(), pGameClient->m_Chat.IsActive(), pGameClient->m_GameConsole.IsActive(),
		pGameClient->m_HudEditor.IsActive(), pGameClient->Ui()->IsPopupOpen());
}

bool CQmImeBlocker::IsGameplayOverlayActive(const CGameClient *pGameClient) const
{
	if(pGameClient == nullptr)
		return true;

	return pGameClient->m_KeyBinder.IsActive() ||
	       pGameClient->m_Spectator.IsActive() ||
	       pGameClient->m_BindWheel.IsActive() ||
	       pGameClient->m_Emoticon.IsActive() ||
	       pGameClient->m_PieMenu.IsActive();
}

bool CQmImeBlocker::WantsTextInput(CGameClient *pGameClient) const
{
	if(!HasTextFocus(pGameClient))
		return false;
	if(IsGameplayOverlayActive(pGameClient))
		return false;
	return true;
}

void CQmImeManager::Init(CGameClient *pGameClient)
{
	m_pGameClient = pGameClient;
	m_pLexicon = std::make_unique<CQmImeDomainLexicon>(pGameClient->Storage());
	m_pSources = std::make_unique<CQmImeSources>(pGameClient, m_pLexicon.get());
	m_pEngine = std::make_unique<CQmImeEngine>(pGameClient->Storage());
	m_pEngine->SetDomainLexicon(m_pLexicon.get());
	pGameClient->Input()->SetImeProvider(m_pEngine.get());
	pGameClient->Console()->Register("qm_ime_clear_learned", "", CFGFLAG_CLIENT,
		[](IConsole::IResult *, void *pUserData) { static_cast<CQmImeManager *>(pUserData)->ClearLearned(); },
		this, "Clear learned built-in pinyin words");
	CLineInput::SetTextInputAutoManaged(true);
	Reset();
}

void CQmImeManager::Reset()
{
	if(m_TextInputSession.ClientOwnsInput() && m_pGameClient != nullptr && m_pGameClient->Input() != nullptr)
		m_pGameClient->Input()->StopTextInput();
	m_TextInputSession.ResetFocus();
	m_CandidatePopup.Reset();
	if(m_pSources != nullptr)
		m_pSources->Reset();
}

void CQmImeManager::ClearLearned()
{
	if(m_pLexicon == nullptr)
		return;
	// 不能在首次 Load 之前清空内存，随后又把磁盘旧记录加载回来。
	if(!m_LexiconLoaded)
	{
		m_pLexicon->Load();
		m_LexiconLoaded = true;
	}
	m_pLexicon->ClearLearned();
	if(m_pEngine != nullptr)
		m_pEngine->RefreshCandidates();
}

void CQmImeManager::SetClientOwnership(bool OwnsInput)
{
	if(!m_TextInputSession.SetClientOwnership(OwnsInput))
		return;

	// 旧界面的输入框不能带着焦点和组合串进入新界面。
	if(CLineInput *pActiveInput = CLineInput::GetActiveInput())
		pActiveInput->Deactivate();
	if(m_pGameClient != nullptr && m_pGameClient->Input() != nullptr)
	{
		m_pGameClient->Input()->StopTextInput();
		m_pGameClient->Input()->SetImeEngineEnabled(false);
	}
	CLineInput::SetTextInputAutoManaged(OwnsInput);
	m_CandidatePopup.Reset();
}

void CQmImeManager::OnFrame()
{
	if(!m_TextInputSession.ClientOwnsInput())
		return;
	if(m_pGameClient == nullptr)
	{
		m_TextInputSession.ResetFocus();
		return;
	}

	IInput *pInput = m_pGameClient->Input();
	if(pInput == nullptr)
		return;

	// 先验证本帧输入框，再交接会话，最后绘制候选，防止旧候选跟到新输入框。
	const bool OwnerRendered = CLineInput::ValidateActiveInputRenderedThisFrame();
	const bool Wanted = g_Config.m_QmImeAutoManage != 0 ? m_Blocker.WantsTextInput(m_pGameClient) : CLineInput::GetActiveInput() != nullptr;
	const bool EngineWanted = Wanted && QmImeEmbeddedEngineAllowed() && g_Config.m_QmImeEngine != 0;
	if(m_pLexicon != nullptr)
	{
		m_pLexicon->SetEnabled(g_Config.m_QmImeDomainWords != 0);
		m_pLexicon->SetLearningEnabled(g_Config.m_QmImeLearning != 0);
		if(EngineWanted && !m_LexiconLoaded)
		{
			m_pLexicon->Load();
			m_LexiconLoaded = true;
		}
		if(EngineWanted)
			m_pSources->OnFrame();
		m_pLexicon->Tick();
		if(m_LexiconRevision != m_pLexicon->Revision())
		{
			m_LexiconRevision = m_pLexicon->Revision();
			m_pEngine->RefreshCandidates();
		}
	}
	if(m_pEngine != nullptr && CLineInput::GetActiveInput() != nullptr)
	{
		const char *pLine = CLineInput::GetActiveInput()->GetString();
		m_pEngine->SetCommandContext(pLine[0] == '\0' || pLine[0] == '/');
	}
	pInput->SetImeEngineEnabled(EngineWanted);
	const CQmImeTextInputSession::EAction Action = m_TextInputSession.UpdateFocus(*pInput, Wanted ? CLineInput::GetActiveInput() : nullptr);
	if(g_Config.m_QmImeTrace)
	{
		if(m_TraceWanted != static_cast<int>(Wanted) || m_TraceEngineWanted != static_cast<int>(EngineWanted) || Action != CQmImeTextInputSession::EAction::NONE)
			dbg_msg("ime/trace", "layer=manager time_ms=%lld wanted=%d engine_wanted=%d engine_config=%d auto_manage=%d owner_rendered=%d owner_present=%d priority=%d action=%d engine_active=%d composing=%d candidates=%d chat=%d menu=%d console=%d keybinder=%d spectator=%d bindwheel=%d emoticon=%d piemenu=%d",
				static_cast<long long>(time_get() * 1000 / time_freq()), Wanted, EngineWanted, g_Config.m_QmImeEngine, g_Config.m_QmImeAutoManage, OwnerRendered, CLineInput::GetActiveInput() != nullptr, static_cast<int>(CLineInput::GetActiveInputPriority()), static_cast<int>(Action),
				m_pEngine != nullptr && m_pEngine->IsActive(), pInput->HasComposition(), pInput->GetCandidateCount(), m_pGameClient->m_Chat.IsActive(), m_pGameClient->m_Menus.IsActive(), m_pGameClient->m_GameConsole.IsActive(),
				m_pGameClient->m_KeyBinder.IsActive(), m_pGameClient->m_Spectator.IsActive(), m_pGameClient->m_BindWheel.IsActive(), m_pGameClient->m_Emoticon.IsActive(), m_pGameClient->m_PieMenu.IsActive());
		m_TraceWanted = Wanted;
		m_TraceEngineWanted = EngineWanted;
	}
	else
	{
		m_TraceWanted = -1;
		m_TraceEngineWanted = -1;
	}
	if(Action != CQmImeTextInputSession::EAction::NONE)
		m_CandidatePopup.Reset();
	else if(!Wanted && (pInput->HasComposition() || pInput->GetCandidateCount() > 0))
	{
		pInput->StopTextInput();
	}
	RenderCandidatePopup();
}

SQmImePopupState CQmImeManager::BuildPopupState() const
{
	SQmImePopupState State;
	if(m_pGameClient == nullptr || m_pGameClient->Input() == nullptr)
	{
		State.m_Disabled = true;
		return State;
	}

	IInput *pInput = m_pGameClient->Input();
	const bool WantsTextInput = g_Config.m_QmImeAutoManage != 0 ? m_Blocker.WantsTextInput(m_pGameClient) : CLineInput::GetActiveInput() != nullptr;
	State.m_Disabled = !WantsTextInput;
	if(State.m_Disabled)
		return State;

	const int CandidateCount = pInput->GetCandidateCount();
	// 候选列表驱动可见性：组合串可能被 IME 短暂清空，单独要求会导致弹窗闪烁
	State.m_Visible = QmImePopupShouldBeVisible(CandidateCount);
	if(!State.m_Visible)
		return State;

	State.m_Composition = pInput->GetComposition();

	const int CopyCount = std::max(CandidateCount, 0);
	State.m_vCandidates.reserve(CopyCount);
	for(int i = 0; i < CopyCount; ++i)
		State.m_vCandidates.emplace_back(pInput->GetCandidate(i));
	State.m_SelectedIndex = qm_ime_overlay::NormalizeSelectedCandidateIndex(pInput->GetCandidateSelectedIndex(), CopyCount);

	const int PageSize = pInput->GetCandidatePageSize();
	const int PageStart = pInput->GetCandidatePageStart();
	const int TotalCount = pInput->GetCandidateTotalCount();
	if(PageSize > 0 && TotalCount > PageSize)
	{
		State.m_PageCount = (TotalCount + PageSize - 1) / PageSize;
		State.m_PageIndex = std::clamp(PageStart / PageSize, 0, State.m_PageCount - 1);
	}

	State.m_AnchorScreen = CLineInput::GetCompositionWindowPosition();
	State.m_LineHeightScreen = CLineInput::GetCompositionLineHeight();
	return State;
}

void CQmImeManager::RenderCandidatePopup()
{
	if(!m_TextInputSession.ClientOwnsInput())
		return;
	CUiScopedGaussianBlur GaussianBlurScope(m_pGameClient->Ui());
	const EQmImeCandidateRenderAction RenderAction = m_pEngine != nullptr && m_pEngine->IsActive() ?
		EQmImeCandidateRenderAction::POPUP : QmImeComputeCandidateRenderAction(QmImeShouldRenderCustomCandidateUi(), g_Config.m_QmNewIme);
	if(g_Config.m_QmImeTrace && m_TraceRenderAction != static_cast<int>(RenderAction))
		dbg_msg("ime/trace", "layer=manager event=render_action action=%d engine_active=%d custom_supported=%d new_ime=%d", static_cast<int>(RenderAction), m_pEngine != nullptr && m_pEngine->IsActive(), QmImeShouldRenderCustomCandidateUi(), g_Config.m_QmNewIme);
	m_TraceRenderAction = g_Config.m_QmImeTrace ? static_cast<int>(RenderAction) : -1;

	if(RenderAction == EQmImeCandidateRenderAction::VALIDATE_ONLY)
	{
		m_CandidatePopup.Reset();
		return;
	}

	if(RenderAction == EQmImeCandidateRenderAction::LEGACY)
	{
		m_CandidatePopup.Reset();
		CLineInput::RenderLegacyCandidates();
		return;
	}

	m_CandidatePopup.Render(m_pGameClient, BuildPopupState());
}
