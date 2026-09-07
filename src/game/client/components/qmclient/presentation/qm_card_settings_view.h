/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRESENTATION_QM_CARD_SETTINGS_VIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRESENTATION_QM_CARD_SETTINGS_VIEW_H

#include "qm_card_settings_adapter.h"
#include "../core/qm_dispatch_logic.h"

#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui/card_ui_model.h>
#include <game/client/ui_scrollregion.h>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CMenus;
class IInput;
class IStorage;
class IEngine;
class CTooltips;
class CSettingsIconResources;
struct SResourcePageMetrics;

using TQmResourceReporter = std::function<void(const char *, const SResourcePageMetrics &, int)>;

class CQmCardSettingsView final
{
	struct SCard
	{
		const SCardDescriptor *m_pDescriptor;
		CButtonContainer m_Toggle;
		std::array<CButtonContainer, 5> m_aActions;
		float m_Hover = 0.0f;
	};

	CCardUiModel &m_Model;
	CQmCardSettingsAdapter m_Adapter;
	CQmDispatchRegistry<EQmInputSlot, 2> m_InputDispatch;
	CLineInputBuffered<128> m_Search;
	CScrollRegion m_Scroll;
	std::array<CButtonContainer, 3> m_aModes;
	CButtonContainer m_Save;
	CButtonContainer m_Reset;
	CButtonContainer m_Theme;
	CButtonContainer m_Animations;
	bool m_ShowHidden = false;
	bool m_CachedShowHidden = false;
	std::deque<SCard> m_vCards;
	std::vector<SCard *> m_vpVisible;
	std::string m_Query;
	unsigned m_Revision = ~0u;
	SCard *m_pFocused = nullptr;
	bool m_ScrollToFocus = false;
	std::string m_SaveError;
	int64_t m_LastRenderTime = 0;
	std::unique_ptr<CSettingsIconResources> m_pIcons;
	bool m_RenderedThisFrame = false;
	bool m_WasOpen = false;

	void Refresh();
	void Move(SCard &Card, int Direction);
	void ToggleVisibility(SCard &Card);
	void ToggleCollapsed(SCard &Card);
	void ToggleSetting(SCard &Card);
	void HandleKeyboard(CUi &Ui, IInput &Input);
	void HandleCardKeyboard(CUi &Ui, IInput &Input);
	static const char *Title(const SCardDescriptor &Card);

public:
	CQmCardSettingsView(CCardUiModel &Model, CConfig &Config, IEngine *pEngine, IStorage *pStorage, IGraphics *pGraphics);
	~CQmCardSettingsView();
	// false 表示绘制原有玩家指示器控件，调用方绘制完后需 EndLegacy。
	bool Render(CMenus &Menus, CUi &Ui, IInput &Input, IStorage &Storage, IGraphics &Graphics, CTooltips &Tooltips, CUIRect &View);
	void EndLegacy(const CUIRect &End);
	void EndFrame(CUi &Ui);
	void Shutdown(CUi &Ui);
	void OnResize();
	void InvalidateResources();
	void SetResourceReporter(TQmResourceReporter Reporter);
};

#endif
