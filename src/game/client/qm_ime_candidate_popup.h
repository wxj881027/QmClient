// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_IME_CANDIDATE_POPUP_H
#define GAME_CLIENT_QM_IME_CANDIDATE_POPUP_H

#include <base/vmath.h>

#include <cstdint>
#include <string>
#include <vector>

class CGameClient;

struct SQmImePopupState
{
	bool m_Visible = false;
	bool m_Disabled = false;
	std::string m_Composition;
	std::vector<std::string> m_vCandidates;
	int m_SelectedIndex = -1;
	int m_PageIndex = -1;
	int m_PageCount = 0;
	vec2 m_AnchorScreen = vec2(0.0f, 0.0f);
	float m_LineHeightScreen = 0.0f;
};

inline bool QmImeHasPopupContent(const SQmImePopupState &State)
{
	return State.m_Visible && !State.m_Disabled && !State.m_vCandidates.empty();
}

class CQmImeCandidatePopup
{
public:
	void Reset();
	void Render(CGameClient *pGameClient, const SQmImePopupState &State);

private:
	struct SPresentationTargets
	{
		bool m_Initialized = false;
		float m_TargetX = 0.0f;
		float m_TargetY = 0.0f;
		float m_TargetWidth = 0.0f;
		float m_TargetHeight = 0.0f;
		float m_TargetRadius = 0.0f;
		float m_TargetAlpha = 0.0f;
		float m_TargetCandidateAlpha = 0.0f;
		float m_TargetCandidateScale = 1.0f;
		float m_TargetSelectedX = 0.0f;
		float m_TargetSelectedY = 0.0f;
		float m_TargetSelectedWidth = 0.0f;
		float m_TargetSelectedHeight = 0.0f;
	};

	SQmImePopupState m_LastState;
	SPresentationTargets m_Presentation;
	int m_CandidateStart = 0;
	bool m_WasVisible = false;
	uint64_t m_PresenceGeneration = 1;
};

#endif
