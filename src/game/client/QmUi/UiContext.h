// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UICONTEXT_H
#define GAME_CLIENT_QMUI_UICONTEXT_H

#include "QmAnimationBackend.h"

#include <base/system.h>

#include <cstdint>

class CUi;
class CMenus;
class CUiV2Tree;
class CTooltips;
class ITextRender;
struct SUiTheme;

// Lightweight dependency-injection bundle for ui_widget primitives. Constructed
// once at the start of a page render (CMenus has access to the protected
// component getters), passed by const reference to primitives. Members may be
// null when the primitive does not need them (e.g. Tooltip-only callers can
// leave m_pMenus null).
struct IUiContext
{
	CUi *m_pUi = nullptr;
	CUiV2AnimationRuntime *m_pAnim = nullptr;
	CUiV2Tree *m_pTree = nullptr;
	CMenus *m_pMenus = nullptr;
	CTooltips *m_pTooltips = nullptr;
	ITextRender *m_pTextRender = nullptr;
	const SUiTheme *m_pTheme = nullptr;
	uint64_t m_ScopeHash = 0;
	float m_FrameDt = 1.0f / 60.0f;
	float m_UiScale = 1.0f;
};

// Stable hash of a page identifier, used to seed BuildUiAnimNodeKey so
// different pages do not share animation IDs.
inline uint64_t MakeUiScopeHash(const char *pName)
{
	return pName != nullptr ? static_cast<uint64_t>(str_quickhash(pName)) : 0;
}

#endif
