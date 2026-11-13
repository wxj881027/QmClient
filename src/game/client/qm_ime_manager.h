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
		RESTART,
	};

	bool SetClientOwnership(bool OwnsInput)
	{
		if(m_ClientOwnsInput == OwnsInput)
			return false;
		m_ClientOwnsInput = OwnsInput;
		ResetFocus();
		return true;
	}

	EAction UpdateFocus(IInput &Input, const void *pOwner)
	{
		if(!m_ClientOwnsInput || pOwner == m_pOwner)
			return EAction::NONE;
		const bool HadOwner = m_pOwner != nullptr;
		m_pOwner = pOwner;
		// 同帧从一个输入框切到另一个时，先取消旧组合，再为新 owner 建立会话。
		if(HadOwner)
			Input.StopTextInput();
		if(pOwner != nullptr)
			Input.StartTextInput();
		if(pOwner == nullptr)
			return EAction::STOP;
		return HadOwner ? EAction::RESTART : EAction::START;
	}

	void ResetFocus() { m_pOwner = nullptr; }
	bool ClientOwnsInput() const { return m_ClientOwnsInput; }

private:
	bool m_ClientOwnsInput = true;
	// 只保存输入框身份，不拥有或解引用输入框。
	const void *m_pOwner = nullptr;
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
	void Reset();

private:
	void RenderCandidatePopup();
	std::optional<SQmImePopupState> BuildPopupState() const noexcept;

	CGameClient *m_pGameClient = nullptr;
	CQmImeBlocker m_Blocker;
	CQmImeCandidatePopup m_CandidatePopup;
	CQmImeTextInputSession m_TextInputSession;
};

#endif
