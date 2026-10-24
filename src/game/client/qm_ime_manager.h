// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_IME_MANAGER_H
#define GAME_CLIENT_QM_IME_MANAGER_H

#include "lineinput.h"
#include "qm_ime_candidate_popup.h"

#include <engine/shared/qm_ime_policy.h>

#include <algorithm>
#include <optional>

class CGameClient;

// 输入框残留的活动标记不能替代所属界面的存活状态。
inline bool QmImeHasLiveInputOwner(EInputPriority Priority, bool MenuActive, bool ChatActive, bool ConsoleActive, bool HudEditorActive, bool ChatPopupOpen)
{
	switch(Priority)
	{
	case EInputPriority::UI: return MenuActive || HudEditorActive || (ChatActive && ChatPopupOpen);
	case EInputPriority::CHAT: return ChatActive;
	case EInputPriority::CONSOLE: return ConsoleActive;
	case EInputPriority::NONE: return false;
	}
	return false;
}

enum class EQmImeCandidateRenderAction
{
	VALIDATE_ONLY = 0,
	LEGACY,
	POPUP,
};

inline EQmImeCandidateRenderAction QmImeComputeCandidateRenderAction(bool SupportsCustomCandidateUi, int NewImeMode)
{
	if(!SupportsCustomCandidateUi)
		return EQmImeCandidateRenderAction::VALIDATE_ONLY;

	return NewImeMode == 0 ? EQmImeCandidateRenderAction::LEGACY : EQmImeCandidateRenderAction::POPUP;
}

class CQmImeBlocker
{
public:
	bool WantsTextInput(CGameClient *pGameClient) const;

private:
	bool HasTextFocus(CGameClient *pGameClient) const;
	bool IsGameplayOverlayActive(const CGameClient *pGameClient) const;
};

// 编辑器自行管理输入框；客户端仅在持有输入管理权时同步焦点。
class CQmImeTextInputSession
{
public:
	enum class EAction
	{
		NONE,
		START,
		STOP,
	};

	bool SetClientOwnership(bool OwnsInput)
	{
		if(m_ClientOwnsInput == OwnsInput)
			return false;
		m_ClientOwnsInput = OwnsInput;
		ResetFocus();
		return true;
	}

	EAction UpdateFocus(bool Wanted)
	{
		if(!m_ClientOwnsInput || Wanted == m_TextInputWanted)
			return EAction::NONE;
		m_TextInputWanted = Wanted;
		return Wanted ? EAction::START : EAction::STOP;
	}

	void ResetFocus() { m_TextInputWanted = false; }
	bool ClientOwnsInput() const { return m_ClientOwnsInput; }

private:
	bool m_ClientOwnsInput = true;
	bool m_TextInputWanted = false;
};

// 只发布完整的展示快照；复制失败不改变输入设备或上次的淡出缓存。
template<typename TCopy>
std::optional<SQmImePopupState> QmImeBuildPopupState(const IInput *pInput, bool Enabled, vec2 AnchorScreen, float LineHeightScreen, TCopy &&Copy) noexcept
{
	try
	{
		SQmImePopupState State;
		if(pInput == nullptr || !Enabled)
		{
			State.m_Disabled = true;
			return State;
		}
		const int CandidateCount = pInput->GetCandidateCount();
		// 组合串可能在翻页时短暂清空，可见性仍以候选为准。
		State.m_Visible = QmImePopupShouldBeVisible(CandidateCount);
		if(!State.m_Visible)
			return State;
		State.m_Composition = Copy(pInput->GetComposition());
		State.m_vCandidates.reserve(CandidateCount);
		for(int i = 0; i < CandidateCount; ++i)
			State.m_vCandidates.push_back(Copy(pInput->GetCandidate(i)));
		State.m_SelectedIndex = qm_ime_overlay::NormalizeSelectedCandidateIndex(pInput->GetCandidateSelectedIndex(), CandidateCount);

		const int PageSize = pInput->GetCandidatePageSize();
		const int PageStart = pInput->GetCandidatePageStart();
		const int TotalCount = pInput->GetCandidateTotalCount();
		if(PageSize > 0 && TotalCount > PageSize)
		{
			State.m_PageCount = (TotalCount - 1) / PageSize + 1;
			State.m_PageIndex = std::clamp(PageStart / PageSize, 0, State.m_PageCount - 1);
		}
		State.m_AnchorScreen = AnchorScreen;
		State.m_LineHeightScreen = LineHeightScreen;
		return State;
	}
	catch(...)
	{
		return std::nullopt;
	}
}

inline std::optional<SQmImePopupState> QmImeBuildPopupState(const IInput *pInput, bool Enabled, vec2 AnchorScreen, float LineHeightScreen) noexcept
{
	return QmImeBuildPopupState(pInput, Enabled, AnchorScreen, LineHeightScreen, [](const char *pText) { return std::string(pText); });
}

class CQmImeManager
{
public:
	void Init(CGameClient *pGameClient);
	void SetClientOwnership(bool OwnsInput);
	void OnFrame();
	void RenderCandidatePopup();
	void Reset();

private:
	std::optional<SQmImePopupState> BuildPopupState() const noexcept;

	CGameClient *m_pGameClient = nullptr;
	CQmImeBlocker m_Blocker;
	CQmImeCandidatePopup m_CandidatePopup;
	CQmImeTextInputSession m_TextInputSession;
};

#endif
