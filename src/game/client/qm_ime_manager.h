// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_IME_MANAGER_H
#define GAME_CLIENT_QM_IME_MANAGER_H

#include "qm_ime_candidate_popup.h"
#include "lineinput.h"

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

class CQmImeManager
{
public:
	void Init(CGameClient *pGameClient);
	void SetClientOwnership(bool OwnsInput);
	void OnFrame();
	void RenderCandidatePopup();
	void Reset();

private:
	SQmImePopupState BuildPopupState() const;

	CGameClient *m_pGameClient = nullptr;
	CQmImeBlocker m_Blocker;
	CQmImeCandidatePopup m_CandidatePopup;
	CQmImeTextInputSession m_TextInputSession;
};

#endif
