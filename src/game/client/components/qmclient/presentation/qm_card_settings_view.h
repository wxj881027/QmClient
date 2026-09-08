/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRESENTATION_QM_CARD_SETTINGS_VIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRESENTATION_QM_CARD_SETTINGS_VIEW_H

#include "qm_card_settings_adapter.h"
#include "../core/qm_dispatch_logic.h"

#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui/card_drag_logic.h>
#include <game/client/ui/card_search_index.h>
#include <game/client/ui/card_ui_model.h>
#include <game/client/ui_scrollregion.h>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class CMenus;
class IInput;
class IStorage;
class IEngine;
class CTooltips;
class CSettingsIconResources;
struct SResourcePageMetrics;

using TQmResourceReporter = std::function<void(const char *, const SResourcePageMetrics &, int)>;

// 全局卡片设置页：页面 tabs + 每页投影 + 真实拖拽（让位预览、跨页投放）+ 全局搜索 + 自动保存。
class CQmCardSettingsView final
{
	// 一个渲染条目对应一个 (page, card) 放置；业务状态仍按 card ID 共享。
	struct SCard
	{
		const SCardDescriptor *m_pDescriptor = nullptr;
		std::string m_PageId;
		CButtonContainer m_Toggle;
		std::array<CButtonContainer, 5> m_aActions;
		CButtonContainer m_Header;
		float m_Hover = 0.0f;
		SCardReflowTrack m_YTrack;
	};

	// 一帧的渲染槽位：Rect 为滚动区坐标（随滚动偏移），LocalY 为内容本地 Y（滚动无关，供让位动画轨道使用）。
	struct SSlot
	{
		SCard *m_pCard = nullptr;
		CUIRect m_Rect;
		float m_LocalY = 0.0f;
		int m_Column = 0;
		bool m_Hidden = false;
	};

	CCardUiModel &m_Model;
	CQmCardSettingsAdapter m_Adapter;
	CCardSearchIndex m_SearchIndex;
	CQmDispatchRegistry<EQmInputSlot, 2> m_InputDispatch;
	CLineInputBuffered<128> m_Search;
	CScrollRegion m_Scroll;
	std::array<CButtonContainer, 3> m_aModes;
	std::vector<CButtonContainer> m_vPageTabs;
	CButtonContainer m_ResetPage;
	CButtonContainer m_ResetAll;
	CButtonContainer m_Theme;
	CButtonContainer m_Animations;
	bool m_ShowHidden = false;
	std::string m_ActivePageId;
	std::string m_Query;
	std::deque<SCard> m_vCards;
	std::array<std::vector<SCard *>, 3> m_aDeck;
	bool m_TwoColumns = false;
	bool m_WideDeck = false;
	std::vector<SCard *> m_vpFocus;
	std::vector<SCardDragItem> m_vDragItems;
	std::vector<SSlot> m_vSlots;
	std::array<SCardDragRect, 3> m_aColumnRects{};
	std::vector<SCardDragRect> m_vPageTabRects;
	std::vector<std::string> m_vPageTabIds;
	std::vector<std::pair<const SCardDescriptor *, std::string>> m_vSearchRows;
	std::string m_CachedSearchQuery;
	unsigned m_SearchIndexRevision = 0;
	SCard *m_pFocused = nullptr;
	bool m_ScrollToFocus = false;
	CCardDragState m_Drag;
	bool m_Dragging = false;
	bool m_MouseWasDown = false;
	float m_DragGhostWidth = 0.0f;
	unsigned m_ModelRevision = ~0u;
	bool m_NeedsRefresh = true;
	bool m_SearchSession = false;
	std::string m_SaveError;
	bool m_SavePending = false;
	int64_t m_NextSaveRetry = 0;
	int64_t m_LastRenderTime = 0;
	IStorage *m_pStorage = nullptr;
	std::unique_ptr<CSettingsIconResources> m_pIcons;
	bool m_RenderedThisFrame = false;
	bool m_WasOpen = false;

	SCard &CardEntry(const std::string &PageId, const SCardDescriptor *pDescriptor);
	void RefreshSearchResults();
	void RefreshDeck(const std::string &PageId);
	void RebuildFocusList();
	SCard *PrimaryEntry(const SCardDescriptor *pDescriptor);
	float CardHeight(const SCard &Card, float SlotWidth) const;
	void LayoutDeck(const std::array<std::vector<SCard *>, 3> &Deck, const CUIRect &Content);
	void HandleDrag(CUi &Ui, IInput &Input, const CUIRect &Viewport, const std::vector<SCardDragRect> &vPageTabRects, const std::vector<std::string> &vPageTabIds, float Dt);
	void CommitDrag(const SCardDragUpdate &Update);
	int CommittedModelInsertIndex(const std::string &PageId, ECardColumn Column, const std::string &DraggedId, int VisibleIndex) const;
	void RenderCard(SSlot &Slot, bool Interactive, bool SearchMode, CMenus &Menus, CUi &Ui, IInput &Input, CTooltips &Tooltips, const SCardViewPreferences &ViewPrefs, float Delta, float ContentY, bool Snap);
	void Move(SCard &Card, int Direction);
	void ToggleVisibility(SCard &Card);
	void ToggleCollapsed(SCard &Card);
	void ToggleSetting(SCard &Card);
	void ScheduleSave();
	void HandleKeyboard(CUi &Ui, IInput &Input);
	void HandleListKeyboard(CUi &Ui, IInput &Input);
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
