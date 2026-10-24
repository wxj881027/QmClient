// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_ime_manager.h"

#include "gameclient.h"
#include "lineinput.h"
#include "ui.h"

#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/shared/qm_ime_policy.h>

#include <utility>

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
	CLineInput::SetTextInputAutoManaged(true);
	Reset();
}

void CQmImeManager::Reset()
{
	if(m_TextInputSession.ClientOwnsInput() && m_pGameClient != nullptr && m_pGameClient->Input() != nullptr)
		m_pGameClient->Input()->StopTextInput();
	m_TextInputSession.ResetFocus();
	m_CandidatePopup.Reset();
}

void CQmImeManager::SetClientOwnership(bool OwnsInput)
{
	if(!m_TextInputSession.SetClientOwnership(OwnsInput))
		return;

	// 旧界面的输入框不能带着焦点和组合串进入新界面。
	if(CLineInput *pActiveInput = CLineInput::GetActiveInput())
		pActiveInput->Deactivate();
	if(m_pGameClient != nullptr && m_pGameClient->Input() != nullptr)
		m_pGameClient->Input()->StopTextInput();
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

	const bool Wanted = g_Config.m_QmImeAutoManage != 0 ? m_Blocker.WantsTextInput(m_pGameClient) : CLineInput::GetActiveInput() != nullptr;
	const CQmImeTextInputSession::EAction Action = m_TextInputSession.UpdateFocus(Wanted);
	if(Action == CQmImeTextInputSession::EAction::START)
		pInput->StartTextInput();
	else if(Action == CQmImeTextInputSession::EAction::STOP)
		pInput->StopTextInput();
	else if(!Wanted && (pInput->HasComposition() || pInput->GetCandidateCount() > 0))
	{
		pInput->StopTextInput();
	}
}

std::optional<SQmImePopupState> CQmImeManager::BuildPopupState() const noexcept
{
	const IInput *pInput = m_pGameClient != nullptr ? m_pGameClient->Input() : nullptr;
	const bool Wanted = pInput != nullptr && (g_Config.m_QmImeAutoManage != 0 ? m_Blocker.WantsTextInput(m_pGameClient) : CLineInput::GetActiveInput() != nullptr);
	return QmImeBuildPopupState(pInput, Wanted, CLineInput::GetCompositionWindowPosition(), CLineInput::GetCompositionLineHeight());
}

void CQmImeManager::RenderCandidatePopup()
{
	if(!m_TextInputSession.ClientOwnsInput())
		return;
	CUiScopedGaussianBlur GaussianBlurScope(m_pGameClient->Ui());
	const EQmImeCandidateRenderAction RenderAction = QmImeComputeCandidateRenderAction(QmImeShouldRenderCustomCandidateUi(), g_Config.m_QmNewIme);

	if(RenderAction == EQmImeCandidateRenderAction::VALIDATE_ONLY)
	{
		CLineInput::ValidateActiveInputRenderedThisFrame();
		m_CandidatePopup.Reset();
		return;
	}

	if(RenderAction == EQmImeCandidateRenderAction::LEGACY)
	{
		m_CandidatePopup.Reset();
		CLineInput::RenderLegacyCandidates();
		return;
	}

	const bool ActiveInputRendered = CLineInput::ValidateActiveInputRenderedThisFrame();
	std::optional<SQmImePopupState> State;
	if(ActiveInputRendered)
		State = BuildPopupState();
	else
	{
		State.emplace();
		State->m_Disabled = true;
	}
	m_CandidatePopup.Render(m_pGameClient, std::move(State));
}
