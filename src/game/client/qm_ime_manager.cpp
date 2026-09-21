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

#include <algorithm>

bool CQmImeBlocker::HasTextFocus(const CGameClient *pGameClient) const
{
	if(CLineInput::GetActiveInput() == nullptr)
		return false;

	switch(CLineInput::GetActiveInputPriority())
	{
	case EInputPriority::UI:
	case EInputPriority::CHAT:
	case EInputPriority::CONSOLE:
		break;
	case EInputPriority::NONE:
		return false;
	}

	if(pGameClient == nullptr)
		return false;
	return true;
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

bool CQmImeBlocker::WantsTextInput(const CGameClient *pGameClient) const
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
	if(m_pGameClient != nullptr && m_pGameClient->Input() != nullptr)
		m_pGameClient->Input()->StopTextInput();
	m_TextInputWanted = false;
	m_CandidatePopup.Reset();
}

void CQmImeManager::OnFrame()
{
	if(m_pGameClient == nullptr)
	{
		m_TextInputWanted = false;
		return;
	}

	IInput *pInput = m_pGameClient->Input();
	if(pInput == nullptr)
		return;

	const bool Wanted = g_Config.m_QmImeAutoManage != 0 ? m_Blocker.WantsTextInput(m_pGameClient) : CLineInput::GetActiveInput() != nullptr;
	if(Wanted != m_TextInputWanted)
	{
		if(Wanted)
			pInput->StartTextInput();
		else
			pInput->StopTextInput();
		m_TextInputWanted = Wanted;
	}
	else if(!Wanted && (pInput->HasComposition() || pInput->GetCandidateCount() > 0))
	{
		pInput->StopTextInput();
	}
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
	SQmImePopupState State;
	if(ActiveInputRendered)
		State = BuildPopupState();
	else
		State.m_Disabled = true;
	m_CandidatePopup.Render(m_pGameClient, State);
}
