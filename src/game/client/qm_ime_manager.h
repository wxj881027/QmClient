// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_IME_MANAGER_H
#define GAME_CLIENT_QM_IME_MANAGER_H

#include "lineinput.h"
#include "qm_ime_candidate_popup.h"

#include <cstdint>
#include <memory>

class CGameClient;
class CQmImeDomainLexicon;
class CQmImeSources;
class CQmImeEngine;

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

class CQmImeManager
{
public:
	CQmImeManager();
	~CQmImeManager();
	void Init(CGameClient *pGameClient);
	void SetClientOwnership(bool OwnsInput);
	void OnFrame();
	void Reset();
	void ClearLearned();

private:
	void RenderCandidatePopup();
	SQmImePopupState BuildPopupState() const;

	CGameClient *m_pGameClient = nullptr;
	CQmImeBlocker m_Blocker;
	CQmImeCandidatePopup m_CandidatePopup;
	CQmImeTextInputSession m_TextInputSession;
	std::unique_ptr<CQmImeDomainLexicon> m_pLexicon;
	std::unique_ptr<CQmImeSources> m_pSources;
	std::unique_ptr<CQmImeEngine> m_pEngine;
	bool m_LexiconLoaded = false;
	uint64_t m_LexiconRevision = 0;
	int m_TraceWanted = -1;
	int m_TraceEngineWanted = -1;
	int m_TraceRenderAction = -1;
};

#endif
