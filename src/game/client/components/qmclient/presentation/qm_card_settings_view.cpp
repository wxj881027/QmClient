/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_card_settings_view.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/input.h>
#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>
#include <game/client/ui/card_deck_projection.h>
#include <game/client/ui/card_preferences_storage.h>
#include <game/client/ui/resource_page_graphics.h>
#include <generated/qm_icon_manifest.h>
#include <game/localization.h>

#include <algorithm>
#include <utility>

namespace
{
constexpr float ROW = 24.0f;
constexpr float GAP = 4.0f;
constexpr float DRAG_THRESHOLD = 10.0f;
constexpr float REFLOW_DURATION = 0.12f;

CUIRect GridCell(const CUIRect &Area, int Index, int Columns)
{
	const float Width = (Area.w - GAP * (Columns - 1)) / Columns;
	return {Area.x + (Index % Columns) * (Width + GAP), Area.y + (Index / Columns) * (ROW + GAP), Width, ROW};
}

const char *CardTitleText(const SCardDescriptor &Card)
{
	// qm.diagnostics 的标题沿用旧入口文案，避免依赖尚未稳定的 title key。
	if(Card.m_Id == "qm.diagnostics")
		return Localize("Diagnostics");
	return Localize(Card.m_TitleKey.c_str());
}

// 搜索 provider 与卡片渲染共用同一份本地化文本，不另写一套关键词。
class CCardViewSearchProvider final : public ICardSearchContentProvider
{
public:
	void CollectCardContent(const SCardDescriptor &Card, std::vector<SCardSearchItem> &vOut) const override
	{
		vOut.push_back({CardTitleText(Card), ECardSearchField::TITLE});
		if(!Card.m_DescriptionKey.empty())
			vOut.push_back({Localize(Card.m_DescriptionKey.c_str()), ECardSearchField::DESCRIPTION});
		if(Card.m_PresentationId == "toggle")
		{
			vOut.push_back({Localize("Enabled"), ECardSearchField::CONTROL});
			vOut.push_back({Localize("Toggle"), ECardSearchField::ACTION});
		}
	}
};

CCardViewSearchProvider s_CardViewSearchProvider;
}

class CSettingsIconResources final
{
	CResourcePageLoader m_Loader;
	CResourcePageGraphics m_Textures;
	IGraphics *m_pGraphics;
	uint32_t m_Generation = 0;
	int m_ScaleIndex = -1;
	EResourcePageState m_LastState = EResourcePageState::UNLOADED;
	bool m_Open = false;
	TQmResourceReporter m_Reporter;
	static constexpr const char *s_apPages[] = {"qm.ui.icons.1x", "qm.ui.icons.2x", "qm.ui.icons.4x"};

public:
	CSettingsIconResources(IEngine *pEngine, IStorage *pStorage, IGraphics *pGraphics) :
		m_Loader(pEngine, pStorage), m_Textures(pGraphics), m_pGraphics(pGraphics)
	{
		for(size_t Index = 0; Index < g_apQmUiIconAtlasPaths.size(); ++Index)
			if(!m_Loader.RegisterManifest({s_apPages[Index], {{g_apQmUiIconAtlasPaths[Index]}}}))
				log_error("qm/ui", "could not register icon atlas");
	}

	void Prepare(float PixelScale)
	{
		const int Index = PixelScale > 2.0f ? 2 : PixelScale > 1.0f ? 1 : 0;
		if(m_ScaleIndex != Index)
		{
			Invalidate();
			m_ScaleIndex = Index;
		}
		m_Loader.Poll(m_Textures);
		const auto *pState = m_Loader.Cache().Find(s_apPages[Index]);
		if(!pState)
			return;
		if(!m_Open && pState->m_State == EResourcePageState::READY)
			m_Loader.Request(s_apPages[Index], m_Generation);
		m_Open = true;
		if(pState->m_State == EResourcePageState::UNLOADED)
			m_Loader.Request(s_apPages[Index], m_Generation);
		if(pState->m_State == EResourcePageState::FAILED && m_LastState != EResourcePageState::FAILED)
			log_error("qm/ui", "icon atlas unavailable: %s", pState->m_Error.c_str());
		m_LastState = pState->m_State;
	}

	bool Ready() const
	{
		return m_ScaleIndex >= 0 && m_Textures.Find(s_apPages[m_ScaleIndex], g_apQmUiIconAtlasPaths[m_ScaleIndex]).IsValid();
	}

	void Draw(EQmUiIcon Icon, const CUIRect &Rect, bool Flip)
	{
		if(!Ready() || !m_pGraphics || static_cast<size_t>(Icon) >= g_aQmUiIcons.size())
			return;
		const auto &Entry = g_aQmUiIcons[static_cast<size_t>(Icon)];
		const float Size = std::min({20.0f, Rect.w, Rect.h});
		m_pGraphics->TextureSet(m_Textures.Find(s_apPages[m_ScaleIndex], g_apQmUiIconAtlasPaths[m_ScaleIndex]));
		m_pGraphics->QuadsBegin();
		m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		m_pGraphics->QuadsSetSubset(Entry.m_U0, Flip ? Entry.m_V1 : Entry.m_V0, Entry.m_U1, Flip ? Entry.m_V0 : Entry.m_V1);
		const IGraphics::CQuadItem Quad(Rect.x + (Rect.w - Size) / 2.0f, Rect.y + (Rect.h - Size) / 2.0f, Size, Size);
		m_pGraphics->QuadsDrawTL(&Quad, 1);
		m_pGraphics->QuadsEnd();
	}

	void Invalidate()
	{
		m_Loader.Invalidate(++m_Generation, m_Textures);
		m_LastState = EResourcePageState::UNLOADED;
	}

	void Close()
	{
		if(!m_Open)
			return;
		m_Open = false;
		if(m_ScaleIndex >= 0)
		{
			const auto *pMetrics = m_Loader.Metrics(s_apPages[m_ScaleIndex]);
			if(pMetrics)
			{
				log_info("qm/ui-resource", "page=qm.settings scale=%d attempts=%llu cache_hits=%llu load_ns=%llu decoded_bytes=%llu peak_bytes=%llu failures=%llu",
					1 << m_ScaleIndex,
					static_cast<unsigned long long>(pMetrics->m_LoadAttempts),
					static_cast<unsigned long long>(pMetrics->m_CacheHits),
					static_cast<unsigned long long>(pMetrics->m_LastLoadNanoseconds),
					static_cast<unsigned long long>(pMetrics->m_LastDecodedBytes),
					static_cast<unsigned long long>(pMetrics->m_PeakDecodedBytes),
					static_cast<unsigned long long>(pMetrics->m_FailedLoads));
				if(m_Reporter)
					m_Reporter(s_apPages[m_ScaleIndex], *pMetrics, 1 << m_ScaleIndex);
			}
		}
		// 已提交的小型 atlas 是常驻 cache；关闭页时仅保留它，取消未完成加载。
		if(!Ready() && m_LastState != EResourcePageState::UNLOADED)
			Invalidate();
	}

	void Shutdown()
	{
		Close();
		m_Loader.Shutdown(m_Textures);
	}
	void RetireCancelled() { m_Loader.Poll(m_Textures, 0); }
	void SetReporter(TQmResourceReporter Reporter) { m_Reporter = std::move(Reporter); }
};

const char *CQmCardSettingsView::Title(const SCardDescriptor &Card)
{
	return CardTitleText(Card);
}

CQmCardSettingsView::CQmCardSettingsView(CCardUiModel &Model, CConfig &Config, IEngine *pEngine, IStorage *pStorage, IGraphics *pGraphics) :
	m_Model(Model), m_Adapter(Config), m_pStorage(pStorage),
	m_pIcons(std::make_unique<CSettingsIconResources>(pEngine, pStorage, pGraphics))
{
	const bool InputRegistered =
		m_InputDispatch.Register(EQmInputSlot::UI, 100, "qm.ui.search", 0) == EQmDispatchRegistration::REGISTERED &&
		m_InputDispatch.Register(EQmInputSlot::UI, 0, "qm.ui.cards", 1) == EQmDispatchRegistration::REGISTERED;
	if(!InputRegistered)
		log_error("qm/ui", "failed to register card input priorities");
	m_InputDispatch.Freeze();
	m_SearchIndex.Configure(&m_Model.Registry(), &s_CardViewSearchProvider);
	const auto vpPages = m_Model.Registry().Pages();
	if(!vpPages.empty())
		m_ActivePageId = vpPages.front()->m_Id;
	m_vPageTabs.resize(vpPages.size());
}

CQmCardSettingsView::~CQmCardSettingsView() = default;

void CQmCardSettingsView::RefreshSearchResults()
{
	if(m_Query.empty())
	{
		m_vSearchRows.clear();
		m_SearchSession = false;
		m_CachedSearchQuery.clear();
		return;
	}
	if(!m_SearchSession)
	{
		// 进入搜索会话时重建索引：标题、说明、控件和动作文本以当前语言重新入索引。
		m_SearchIndex.Rebuild();
		m_SearchSession = true;
		m_CachedSearchQuery.clear();
	}
	if(m_CachedSearchQuery == m_Query && m_SearchIndexRevision == m_SearchIndex.Revision())
		return;
	m_CachedSearchQuery = m_Query;
	m_SearchIndexRevision = m_SearchIndex.Revision();
	m_vSearchRows.clear();
	for(const SCardSearchResult &Result : m_SearchIndex.Search(m_Query))
		if(const SCardDescriptor *pCard = m_Model.Registry().FindCard(Result.m_Id))
			m_vSearchRows.emplace_back(pCard, Result.m_BestMatch);
}

CQmCardSettingsView::SCard &CQmCardSettingsView::CardEntry(const std::string &PageId, const SCardDescriptor *pDescriptor)
{
	for(SCard &Card : m_vCards)
		if(Card.m_PageId == PageId && Card.m_pDescriptor == pDescriptor)
			return Card;
	m_vCards.push_back({});
	SCard &Card = m_vCards.back();
	Card.m_pDescriptor = pDescriptor;
	Card.m_PageId = PageId;
	return Card;
}

CQmCardSettingsView::SCard *CQmCardSettingsView::PrimaryEntry(const SCardDescriptor *pDescriptor)
{
	// 主页面 = 页面声明顺序中该卡片的第一处声明；搜索结果的行为都落在主放置上。
	for(const SCardPage *pPage : m_Model.Registry().Pages())
		for(const std::string &CardId : pPage->m_vCardIds)
			if(CardId == pDescriptor->m_Id)
				return &CardEntry(pPage->m_Id, pDescriptor);
	return nullptr;
}

void CQmCardSettingsView::RefreshDeck(const std::string &PageId)
{
	for(auto &Column : m_aDeck)
		Column.clear();
	const SCardDeckProjection Projection = BuildCardDeckProjection(m_Model.Registry(), m_Model.OrderModel(), m_Model, PageId);
	for(int Column = 0; Column < 3; ++Column)
		for(const SCardDescriptor *pCard : Projection.m_aColumns[Column])
			m_aDeck[Column].push_back(&CardEntry(PageId, pCard));
	// Show hidden：隐藏卡片仍以弱化态显示在本页并可恢复；不可用功能不显示。
	if(m_ShowHidden)
	{
		for(const SCardDescriptor *pCard : m_Model.Registry().CardsForPage(PageId))
		{
			const auto Snapshot = m_Model.Snapshot(PageId, pCard->m_Id);
			if(Snapshot.m_Preferences.m_Visible || !Snapshot.m_Available)
				continue;
			const SCardOrderEntry *pEntry = m_Model.OrderModel().Find(PageId, pCard->m_Id);
			m_aDeck[pEntry ? static_cast<int>(pEntry->m_Column) : static_cast<int>(pCard->m_DefaultColumn)].push_back(&CardEntry(PageId, pCard));
		}
	}
	m_TwoColumns = Projection.m_TwoColumns || !m_aDeck[1].empty() || !m_aDeck[2].empty();
}

void CQmCardSettingsView::RebuildFocusList()
{
	m_vpFocus.clear();
	for(SCard *pCard : m_aDeck[0])
		m_vpFocus.push_back(pCard);
	if(m_TwoColumns && m_WideDeck)
	{
		for(SCard *pCard : m_aDeck[1])
			m_vpFocus.push_back(pCard);
		for(SCard *pCard : m_aDeck[2])
			m_vpFocus.push_back(pCard);
	}
	else
	{
		// 单视觉列：左右卡按层交错，与显示顺序一致。
		const size_t Layers = std::max(m_aDeck[1].size(), m_aDeck[2].size());
		for(size_t Layer = 0; Layer < Layers; ++Layer)
		{
			if(Layer < m_aDeck[1].size())
				m_vpFocus.push_back(m_aDeck[1][Layer]);
			if(Layer < m_aDeck[2].size())
				m_vpFocus.push_back(m_aDeck[2][Layer]);
		}
	}
	if(m_pFocused && std::find(m_vpFocus.begin(), m_vpFocus.end(), m_pFocused) == m_vpFocus.end())
		m_pFocused = nullptr;
}

float CQmCardSettingsView::CardHeight(const SCard &Card, const float SlotWidth) const
{
	const auto Snapshot = m_Model.Snapshot(Card.m_PageId, Card.m_pDescriptor->m_Id);
	const auto Value = m_Adapter.Read(*Card.m_pDescriptor);
	const bool RestartRequired = Value && Value->m_RestartRequired;
	const int Columns = std::clamp(static_cast<int>(SlotWidth / 70.0f), 1, 5);
	const int ActionRows = (5 + Columns - 1) / Columns;
	return ROW + (Snapshot.m_Preferences.m_Collapsed ? 0.0f : ROW + GAP + (RestartRequired ? ROW : 0.0f)) + ActionRows * (ROW + GAP) + 12.0f;
}

void CQmCardSettingsView::LayoutDeck(const std::array<std::vector<SCard *>, 3> &Deck, const CUIRect &Content)
{
	m_vSlots.clear();
	m_vDragItems.clear();
	m_WideDeck = m_TwoColumns && Content.w >= 480.0f;
	const float ColW = m_WideDeck ? (Content.w - GAP) / 2.0f : Content.w;
	float Y = Content.y;
	const float FullTop = Y;
	for(SCard *pCard : Deck[0])
	{
		const float H = CardHeight(*pCard, Content.w);
		const auto Snapshot = m_Model.Snapshot(pCard->m_PageId, pCard->m_pDescriptor->m_Id);
		m_vSlots.push_back({pCard, {Content.x, Y, Content.w, H}, Y - Content.y, 0, !Snapshot.m_Preferences.m_Visible});
		m_vDragItems.push_back({pCard->m_pDescriptor->m_Id, 0, {Content.x, Y, Content.w, H}});
		Y += H + GAP;
	}
	const float SideTop = Y;
	float aSideY[2] = {SideTop, SideTop};
	if(m_TwoColumns)
	{
		const size_t Layers = std::max(Deck[1].size(), Deck[2].size());
		for(size_t Layer = 0; Layer < Layers; ++Layer)
		{
			for(int Side = 0; Side < 2; ++Side)
			{
				const int Column = 1 + Side;
				if(Layer >= Deck[Column].size())
					continue;
				SCard *pCard = Deck[Column][Layer];
				const float H = CardHeight(*pCard, m_WideDeck ? ColW : Content.w);
				const float X = m_WideDeck && Side == 1 ? Content.x + ColW + GAP : Content.x;
				const float W = m_WideDeck ? ColW : Content.w;
				const auto Snapshot = m_Model.Snapshot(pCard->m_PageId, pCard->m_pDescriptor->m_Id);
				m_vSlots.push_back({pCard, {X, aSideY[Side], W, H}, aSideY[Side] - Content.y, Column, !Snapshot.m_Preferences.m_Visible});
				m_vDragItems.push_back({pCard->m_pDescriptor->m_Id, Column, {X, aSideY[Side], W, H}});
				aSideY[Side] += H + GAP;
			}
		}
	}
	m_aColumnRects[0] = {Content.x, FullTop, Content.w, SideTop - FullTop};
	m_aColumnRects[1] = {Content.x, SideTop, m_WideDeck ? ColW : Content.w, aSideY[0] - SideTop};
	m_aColumnRects[2] = {Content.x + (m_WideDeck ? ColW + GAP : 0.0f), SideTop, m_WideDeck ? ColW : Content.w, aSideY[1] - SideTop};
	RebuildFocusList();
}

int CQmCardSettingsView::CommittedModelInsertIndex(const std::string &PageId, const ECardColumn Column, const std::string &DraggedId, const int VisibleIndex) const
{
	// 预览插入位是“可见卡片序”；提交时映射为 present 卡片序列中的 dense 插入位，
	// 隐藏卡片与被拖卡片不占用可见序。
	const std::vector<const SCardOrderEntry *> vPresent = m_Model.OrderModel().EntriesForPage(PageId, Column);
	int VisibleSeen = 0;
	for(const SCardOrderEntry *pEntry : vPresent)
	{
		if(pEntry->m_Id == DraggedId)
			continue;
		if(!m_Model.Preferences(PageId, pEntry->m_Id).m_Visible)
			continue;
		if(VisibleSeen == VisibleIndex)
			return pEntry->m_Order;
		++VisibleSeen;
	}
	return static_cast<int>(vPresent.size());
}

void CQmCardSettingsView::CommitDrag(const SCardDragUpdate &Update)
{
	const ECardColumn Column = static_cast<ECardColumn>(std::clamp(Update.m_TargetColumn, 0, 2));
	const int Order = CommittedModelInsertIndex(Update.m_TargetPageId, Column, Update.m_CardId, Update.m_TargetOrder);
	const bool Moved = Update.m_TargetPageId != Update.m_SourcePageId ? m_Model.MoveCard(Update.m_CardId, Update.m_SourcePageId, Update.m_TargetPageId, Column, Order) : m_Model.MoveCardWithinPage(Update.m_TargetPageId, Update.m_CardId, Column, Order);
	if(Moved)
	{
		m_ActivePageId = Update.m_TargetPageId;
		m_ScrollToFocus = true;
		ScheduleSave();
	}
}

void CQmCardSettingsView::HandleDrag(CUi &Ui, IInput &Input, const CUIRect &Viewport, const std::vector<SCardDragRect> &vPageTabRects, const std::vector<std::string> &vPageTabIds, const float Dt)
{
	const bool MouseDown = Input.KeyIsPressed(KEY_MOUSE_1);
	const bool MousePressed = Input.KeyPress(KEY_MOUSE_1);
	// IInput 没有释放边沿：用上一帧按下状态推导释放，保持拖拽事务判定。
	const bool MouseReleased = m_MouseWasDown && !MouseDown;
	m_MouseWasDown = MouseDown;

	if(m_Drag.Phase() == ECardDragPhase::IDLE)
	{
		// 武装：在卡片标题行按下；标题行内没有其他控件，不抢占按钮输入。
		if(!Ui.IsPopupOpen() && MousePressed)
		{
			for(const SSlot &Slot : m_vSlots)
			{
				const CUIRect Header{Slot.m_Rect.x, Slot.m_Rect.y, Slot.m_Rect.w, ROW};
				if(Ui.MouseHovered(&Header))
				{
					m_DragGhostWidth = Slot.m_Rect.w;
					m_Drag.Arm(m_ActivePageId, Slot.m_pCard->m_pDescriptor->m_Id, Slot.m_Column, {Header.x, Header.y, Header.w, Header.h}, Ui.MouseX(), Ui.MouseY());
					break;
				}
			}
		}
		if(m_Drag.Phase() == ECardDragPhase::IDLE)
			return;
	}

	SCardDragInput DragInput;
	DragInput.m_X = Ui.MouseX();
	DragInput.m_Y = Ui.MouseY();
	DragInput.m_Pressed = MousePressed;
	DragInput.m_Down = MouseDown;
	DragInput.m_Released = MouseReleased;
	DragInput.m_Cancelled = Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE);
	DragInput.m_Dt = Dt;

	SCardDragFrame Frame;
	Frame.m_vItems = m_vDragItems;
	Frame.m_Viewport = {Viewport.x, Viewport.y, Viewport.w, Viewport.h};
	Frame.m_aColumnRects = m_aColumnRects;
	Frame.m_vPageTabRects = vPageTabRects;
	Frame.m_vPageTabIds = vPageTabIds;
	Frame.m_TwoColumns = m_TwoColumns;
	Frame.m_SingleVisualColumn = m_TwoColumns && !m_WideDeck;
	const SCardDragUpdate Result = m_Drag.Update(DragInput, Frame, DRAG_THRESHOLD);
	if(Result.m_Started)
		m_Dragging = true;
	if(Result.m_PreviewPageChanged)
		m_NeedsRefresh = true;
	if(Result.m_Committed)
	{
		CommitDrag(Result);
		m_Dragging = false;
		m_NeedsRefresh = true;
	}
	if(Result.m_Cancelled)
	{
		m_Dragging = false;
		m_NeedsRefresh = true;
	}
	if(Result.m_AutoScrollDelta != 0.0f)
		m_Scroll.ScrollRelativeDirect(vec2(0.0f, Result.m_AutoScrollDelta * Dt));
}

void CQmCardSettingsView::Move(SCard &Card, const int Direction)
{
	const auto It = std::find(m_vpFocus.begin(), m_vpFocus.end(), &Card);
	if(It == m_vpFocus.end())
		return;
	const int Target = static_cast<int>(It - m_vpFocus.begin()) + Direction;
	if(Target < 0 || Target >= static_cast<int>(m_vpFocus.size()))
		return;
	SCard *pTarget = m_vpFocus[Target];
	// 焦点列表跨列时相邻项必属同一页面；防御跨页相邻导致的无效移动。
	if(pTarget->m_PageId != Card.m_PageId)
		return;
	if(m_Model.MoveCardRelative(Card.m_PageId, Card.m_pDescriptor->m_Id, pTarget->m_pDescriptor->m_Id, Direction > 0))
		ScheduleSave();
	m_pFocused = &Card;
	m_ScrollToFocus = true;
}

void CQmCardSettingsView::ToggleVisibility(SCard &Card)
{
	auto Preferences = m_Model.Preferences(Card.m_PageId, Card.m_pDescriptor->m_Id);
	Preferences.m_Visible = !Preferences.m_Visible;
	if(m_Model.SetPreferences(Card.m_PageId, Card.m_pDescriptor->m_Id, Preferences))
		ScheduleSave();
}

void CQmCardSettingsView::ToggleCollapsed(SCard &Card)
{
	auto Preferences = m_Model.Preferences(Card.m_PageId, Card.m_pDescriptor->m_Id);
	Preferences.m_Collapsed = !Preferences.m_Collapsed;
	if(m_Model.SetPreferences(Card.m_PageId, Card.m_pDescriptor->m_Id, Preferences))
		ScheduleSave();
}

void CQmCardSettingsView::ToggleSetting(SCard &Card)
{
	const auto Model = m_Model.Snapshot(Card.m_PageId, Card.m_pDescriptor->m_Id);
	const auto Value = m_Adapter.Read(*Card.m_pDescriptor);
	if(Model.m_Available && Value)
		m_Adapter.Apply(*Card.m_pDescriptor, Value->m_Value == 0 ? 1 : 0);
}

void CQmCardSettingsView::ScheduleSave()
{
	m_SavePending = true;
}

void CQmCardSettingsView::HandleKeyboard(CUi &Ui, IInput &Input)
{
	if(Ui.IsPopupOpen() || m_Dragging || m_Drag.Phase() != ECardDragPhase::IDLE)
		return;
	// 官方编辑框先处理文本和 IME；搜索激活时只把 Tab 交给卡片导航。
	m_InputDispatch.DispatchUntilConsumed(
		[](const auto &Entry) { return Entry.m_UserIndex != 0; },
		[this, &Ui, &Input](const auto &Entry) {
			if(Entry.m_UserIndex == 0)
				return !Input.KeyPress(KEY_TAB);
			HandleListKeyboard(Ui, Input);
			return true;
		});
}

void CQmCardSettingsView::HandleListKeyboard(CUi &Ui, IInput &Input)
{
	if(Ui.ConsumeHotkey(CUi::HOTKEY_TAB))
	{
		if(m_Search.IsActive())
			m_Search.Deactivate();
		const auto It = std::find(m_vpFocus.begin(), m_vpFocus.end(), m_pFocused);
		const int Direction = Input.KeyIsPressed(KEY_LSHIFT) || Input.KeyIsPressed(KEY_RSHIFT) ? -1 : 1;
		const int Count = static_cast<int>(m_vpFocus.size());
		if(Count > 0)
		{
			const int Old = It == m_vpFocus.end() ? (Direction > 0 ? -1 : 0) : static_cast<int>(It - m_vpFocus.begin());
			m_pFocused = m_vpFocus[(Old + Direction + Count) % Count];
			m_ScrollToFocus = true;
		}
	}
	if(m_Search.IsActive() || !m_pFocused)
		return;
	const bool Control = Input.KeyIsPressed(KEY_LCTRL) || Input.KeyIsPressed(KEY_RCTRL);
	if(Ui.ConsumeHotkey(CUi::HOTKEY_UP))
	{
		if(Control && m_vSearchRows.empty())
			Move(*m_pFocused, -1);
		else
		{
			const auto It = std::find(m_vpFocus.begin(), m_vpFocus.end(), m_pFocused);
			if(It != m_vpFocus.begin() && It != m_vpFocus.end())
				m_pFocused = *(It - 1);
		}
		m_ScrollToFocus = true;
	}
	if(Ui.ConsumeHotkey(CUi::HOTKEY_DOWN))
	{
		if(Control && m_vSearchRows.empty())
			Move(*m_pFocused, 1);
		else
		{
			const auto It = std::find(m_vpFocus.begin(), m_vpFocus.end(), m_pFocused);
			if(It != m_vpFocus.end() && It + 1 != m_vpFocus.end())
				m_pFocused = *(It + 1);
		}
		m_ScrollToFocus = true;
	}
	if(Ui.ConsumeHotkey(CUi::HOTKEY_ENTER))
		ToggleSetting(*m_pFocused);
	if(Ui.ConsumeHotkey(CUi::HOTKEY_LEFT) || Ui.ConsumeHotkey(CUi::HOTKEY_RIGHT))
		ToggleCollapsed(*m_pFocused);
	if(Ui.ConsumeHotkey(CUi::HOTKEY_DELETE))
		ToggleVisibility(*m_pFocused);
}

void CQmCardSettingsView::RenderCard(SSlot &Slot, const bool Interactive, const bool SearchMode, CMenus &Menus, CUi &Ui, IInput &Input, CTooltips &Tooltips, const SCardViewPreferences &ViewPrefs, const float Delta, const float ContentY, const bool Snap)
{
	SCard &Card = *Slot.m_pCard;
	const SCardDescriptor *pCard = Card.m_pDescriptor;
	const auto Snapshot = m_Model.Snapshot(Card.m_PageId, pCard->m_Id);
	const auto Value = m_Adapter.Read(*pCard);
	const bool RestartRequired = Value && Value->m_RestartRequired;
	const bool Dragged = !SearchMode && m_Dragging && m_Drag.CardId() == pCard->m_Id;

	// 让位动画轨道用内容本地 Y，滚动不改变目标；最终位置与关闭动画时一致。
	UpdateCardReflowTrack(Card.m_YTrack, Slot.m_LocalY, Delta, REFLOW_DURATION, Snap);
	CUIRect CardRect = Slot.m_Rect;
	CardRect.y = ContentY + Card.m_YTrack.m_Value;

	if(Dragged)
	{
		// 被拖卡片本体留作让位占位，视觉由 ghost 承担。
		CardRect.Draw(ColorRGBA(0.2f, 0.6f, 0.45f, 0.10f), IGraphics::CORNER_ALL, 4.0f);
		return;
	}

	const bool Hovered = Interactive && Ui.MouseInside(&CardRect);
	if(Hovered && Input.KeyPress(KEY_MOUSE_1))
		m_pFocused = &Card;
	const float HoverTarget = Hovered || m_pFocused == &Card ? 1.0f : 0.0f;
	Card.m_Hover = ViewPrefs.m_Animations ? mix(Card.m_Hover, HoverTarget, std::min(1.0f, Delta * 16.0f)) : HoverTarget;
	if(ViewPrefs.m_Mode == 1)
	{
		const float Base = ViewPrefs.m_LightTheme ? 0.72f : 0.1f;
		CardRect.Draw(ColorRGBA(Base, Base, Base, 0.4f + 0.2f * Card.m_Hover), IGraphics::CORNER_ALL, 4.0f);
	}
	else if(m_pFocused == &Card)
		CardRect.Draw(ColorRGBA(0.2f, 0.6f, 0.45f, 0.18f), IGraphics::CORNER_ALL, 0.0f);
	if(Slot.m_Hidden)
		CardRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 4.0f);

	CUIRect Body = CardRect;
	Body.Margin(6.0f, &Body);
	CUIRect Label;
	Body.HSplitTop(ROW, &Label, &Body);

	// 搜索模式下点击标题导航到主放置页面。
	if(Interactive && SearchMode && Menus.DoButton_Menu(&Card.m_Header, "", 0, &Label))
	{
		m_ActivePageId = Card.m_PageId;
		m_Search.Clear();
		m_NeedsRefresh = true;
		m_ScrollToFocus = true;
	}
	if(Interactive && SearchMode)
		Tooltips.DoToolTip(&Card.m_Header, &Label, Localize("Open page"));
	Ui.DoLabel(&Label, Title(*pCard), 13.0f, TEXTALIGN_ML);
	if(SearchMode)
	{
		// 右侧显示主放置页面名：搜索结果来自所有页面，导航目标需要可见。
		CUIRect PageLabel = Label;
		const SCardPage *pPage = m_Model.Registry().FindPage(Card.m_PageId);
		if(pPage)
			Ui.DoLabel(&PageLabel, Localize(pPage->m_TitleKey.c_str()), 11.0f, TEXTALIGN_MR);
	}

	if(Snapshot.m_Preferences.m_Collapsed)
		return;
	if(!m_vSearchRows.empty() && SearchMode)
	{
		// 命中字段提示：消费与索引同一份文本源。
		for(const auto &Row : m_vSearchRows)
		{
			if(Row.first != pCard || Row.second.empty())
				continue;
			CUIRect Match;
			Body.HSplitTop(ROW, &Match, &Body);
			Body.HSplitTop(GAP, nullptr, &Body);
			Ui.DoLabel(&Match, Row.second.c_str(), 11.0f, TEXTALIGN_ML);
			break;
		}
	}
	CUIRect Toggle;
	Body.HSplitTop(ROW, &Toggle, &Body);
	Body.HSplitTop(GAP, nullptr, &Body);
	if(Snapshot.m_Available && Value)
	{
		if(Interactive && Menus.DoButton_CheckBox(&Card.m_Toggle, Localize("Enabled"), Value->m_Value != 0, &Toggle))
			ToggleSetting(Card);
	}
	else
		Ui.DoLabel(&Toggle, Localize("Unavailable"), 13.0f, TEXTALIGN_ML);
	if(RestartRequired)
	{
		CUIRect Restart;
		Body.HSplitTop(ROW, &Restart, &Body);
		Ui.DoLabel(&Restart, Localize("Restart required"), 12.0f, TEXTALIGN_ML);
	}

	const char *apActions[] = {Localize("Up"), Localize("Down"), Snapshot.m_Preferences.m_Visible ? Localize("Hide") : Localize("Restore"), Snapshot.m_Preferences.m_Collapsed ? Localize("Expand") : Localize("Collapse"), Localize("Reset")};
	const int Columns = std::clamp(static_cast<int>(Label.w / 70.0f), 1, 5);
	for(int Action = 0; Action < 5; ++Action)
	{
		const CUIRect Button = GridCell(Body, Action, Columns);
		if(!Interactive)
			continue;
		const bool IconReady = Action < 4 && m_pIcons->Ready();
		const bool Pressed = Menus.DoButton_Menu(&Card.m_aActions[Action], IconReady ? "" : apActions[Action], 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f);
		if(IconReady)
		{
			const EQmUiIcon Icon = Action == 2 ?
				(Snapshot.m_Preferences.m_Visible ? EQmUiIcon::EYE_OFF : EQmUiIcon::EYE) : EQmUiIcon::CHEVRON_DOWN;
			m_pIcons->Draw(Icon, Button, Action == 0 || (Action == 3 && !Snapshot.m_Preferences.m_Collapsed));
			Tooltips.DoToolTip(&Card.m_aActions[Action], &Button, apActions[Action]);
		}
		if(!Pressed)
			continue;
		if(Action == 0 || Action == 1)
			Move(Card, Action == 0 ? -1 : 1);
		else if(Action == 2)
			ToggleVisibility(Card);
		else if(Action == 3)
			ToggleCollapsed(Card);
		else if(m_Model.ResetPreferences(Card.m_PageId, pCard->m_Id))
			ScheduleSave();
	}
}

bool CQmCardSettingsView::Render(CMenus &Menus, CUi &Ui, IInput &Input, IStorage &Storage, IGraphics &Graphics, CTooltips &Tooltips, CUIRect &View)
{
	m_RenderedThisFrame = true;
	CUIRect Header, Modes, Tabs, Toolbar, Footer;
	View.HSplitTop(28.0f, &Header, &View);
	Ui.DoLabel(&Header, Localize("QmClient"), 20.0f, TEXTALIGN_ML);

	const auto vpPages = m_Model.Registry().Pages();
	auto ViewPrefs = m_Model.ViewPreferences();

	const int ModeColumns = View.w >= 270.0f ? 3 : 1;
	View.HSplitTop((3 / ModeColumns) * (ROW + GAP), &Modes, &View);
	const char *apModes[] = {Localize("List"), Localize("Cards"), Localize("Player indicators")};
	for(int Index = 0; Index < 3; ++Index)
	{
		const CUIRect Button = GridCell(Modes, Index, ModeColumns);
		if(Menus.DoButton_MenuTab(&m_aModes[Index], apModes[Index], ViewPrefs.m_Mode == Index, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			ViewPrefs.m_Mode = Index;
			m_Model.SetViewPreferences(ViewPrefs);
			ScheduleSave();
			m_Scroll.Reset();
		}
	}
	if(ViewPrefs.m_Mode == 2)
	{
		// Player indicators 模式沿用官方控件；卡片输入与资源全部让出。
		m_pIcons->Close();
		m_Search.Deactivate();
		m_Drag.Reset();
		m_Dragging = false;
		m_Scroll.Begin(&View);
		return false;
	}
	m_pIcons->Prepare(static_cast<float>(Graphics.ScreenHeight()) / std::max(1.0f, Ui.Screen()->h));

	// 页面 tabs：声明式页面的导航；拖拽中高亮预览页并阻止切换点击。
	if(!vpPages.empty())
	{
		View.HSplitTop(ROW + GAP, &Tabs, &View);
		m_vPageTabRects.clear();
		m_vPageTabIds.clear();
		const float TabW = (Tabs.w - GAP * (static_cast<float>(vpPages.size()) - 1.0f)) / vpPages.size();
		for(size_t Index = 0; Index < vpPages.size(); ++Index)
		{
			const CUIRect Tab{Tabs.x + Index * (TabW + GAP), Tabs.y, TabW, Tabs.h};
			m_vPageTabRects.push_back({Tab.x, Tab.y, Tab.w, Tab.h});
			m_vPageTabIds.push_back(vpPages[Index]->m_Id);
			const std::string &PreviewPage = m_Drag.PreviewPageId();
			const bool Active = (m_Dragging ? PreviewPage : m_ActivePageId) == vpPages[Index]->m_Id;
			const bool CanClick = m_Drag.Phase() == ECardDragPhase::IDLE;
			if(CanClick && Menus.DoButton_MenuTab(&m_vPageTabs[Index], Localize(vpPages[Index]->m_TitleKey.c_str()), Active, &Tab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
			{
				m_ActivePageId = vpPages[Index]->m_Id;
				m_NeedsRefresh = true;
				m_Scroll.Reset();
			}
			if(m_Dragging && PreviewPage == vpPages[Index]->m_Id)
				Tab.Draw(ColorRGBA(0.2f, 0.6f, 0.45f, 0.25f), IGraphics::CORNER_ALL, 4.0f);
		}
	}

	View.HSplitTop(ROW, &Toolbar, &View);
	Ui.DoEditBox_Search(&m_Search, &Toolbar, 14.0f, !Ui.IsPopupOpen());
	View.HSplitTop(GAP, nullptr, &View);
	const int OptionColumns = View.w >= 360.0f ? 3 : 1;
	View.HSplitTop((3 / OptionColumns) * (ROW + GAP), &Toolbar, &View);
	CUIRect Option = GridCell(Toolbar, 0, OptionColumns);
	if(Menus.DoButton_CheckBox(&m_ShowHidden, Localize("Show hidden cards"), m_ShowHidden, &Option))
	{
		m_ShowHidden = !m_ShowHidden;
		m_NeedsRefresh = true;
	}
	Option = GridCell(Toolbar, 1, OptionColumns);
	if(Menus.DoButton_CheckBox(&m_Theme, Localize("Light theme"), ViewPrefs.m_LightTheme, &Option))
	{
		ViewPrefs.m_LightTheme = !ViewPrefs.m_LightTheme;
		m_Model.SetViewPreferences(ViewPrefs);
		ScheduleSave();
	}
	Option = GridCell(Toolbar, 2, OptionColumns);
	if(Menus.DoButton_CheckBox(&m_Animations, Localize("Animations"), ViewPrefs.m_Animations, &Option))
	{
		ViewPrefs.m_Animations = !ViewPrefs.m_Animations;
		m_Model.SetViewPreferences(ViewPrefs);
		ScheduleSave();
	}

	View.HSplitBottom(ROW, &View, &Footer);
	CUIRect ResetPage, ResetAll;
	Footer.VSplitMid(&ResetPage, &ResetAll, GAP);
	if(Menus.DoButton_Menu(&m_ResetPage, Localize("Reset page"), 0, &ResetPage) && m_Drag.Phase() == ECardDragPhase::IDLE)
	{
		if(m_Model.ResetPagePreferences(m_ActivePageId))
			ScheduleSave();
		m_NeedsRefresh = true;
	}
	if(Menus.DoButton_Menu(&m_ResetAll, Localize("Restore layout"), 0, &ResetAll) && m_Drag.Phase() == ECardDragPhase::IDLE)
	{
		m_Model.ResetAllPreferences();
		ScheduleSave();
		m_NeedsRefresh = true;
	}
	if(!m_SaveError.empty())
	{
		CUIRect Error;
		View.HSplitBottom(ROW, &View, &Error);
		Ui.DoLabel(&Error, Localize("Could not save preferences"), 12.0f, TEXTALIGN_ML);
	}

	const char *pQuery = m_Search.GetString();
	if(m_Query != pQuery)
	{
		m_Query = pQuery;
		m_NeedsRefresh = true;
	}
	const bool SearchMode = !m_Query.empty();
	if(SearchMode)
		RefreshSearchResults();

	const int64_t Now = time_get();
	const float Delta = m_LastRenderTime == 0 ? 0.0f : std::clamp(static_cast<float>(Now - m_LastRenderTime) / time_freq(), 0.0f, 0.05f);
	m_LastRenderTime = Now;

	HandleKeyboard(Ui, Input);

	const CUIRect Viewport = View;
	m_Scroll.Begin(&View);
	const bool Snap = !ViewPrefs.m_Animations;

	if(SearchMode)
	{
		// 搜索页是动态投影：真实卡片、直接操作，不拥有持久化 placement。
		m_vpFocus.clear();
		for(const auto &Row : m_vSearchRows)
			if(SCard *pEntry = PrimaryEntry(Row.first))
				m_vpFocus.push_back(pEntry);
		if(m_pFocused && std::find(m_vpFocus.begin(), m_vpFocus.end(), m_pFocused) == m_vpFocus.end())
			m_pFocused = nullptr;
		float Y = View.y;
		for(SCard *pCard : m_vpFocus)
		{
			const float H = CardHeight(*pCard, View.w);
			const CUIRect CardRect{View.x, Y, View.w, H};
			SSlot Slot{pCard, CardRect, Y - View.y, 0, false};
			Y += H + GAP;
			if(!m_Scroll.AddRect(CardRect, m_ScrollToFocus && pCard == m_pFocused))
				continue;
			RenderCard(Slot, true, true, Menus, Ui, Input, Tooltips, ViewPrefs, Delta, View.y, true);
		}
		if(m_vpFocus.empty())
		{
			const CUIRect Empty{View.x, Y, View.w, ROW};
			if(m_Scroll.AddRect(Empty))
				Ui.DoLabel(&Empty, Localize("No matching cards"), 14.0f, TEXTALIGN_ML);
		}
		m_Scroll.End();
		m_ScrollToFocus = false;
		return true;
	}

	// 分类页：先按已提交布局解析几何，再由拖拽状态机消费同一份快照。
	const std::string DisplayPage = m_Dragging ? m_Drag.PreviewPageId() : m_ActivePageId;
	if(m_NeedsRefresh || m_ModelRevision != m_Model.Revision())
	{
		RefreshDeck(DisplayPage);
		m_ModelRevision = m_Model.Revision();
		m_NeedsRefresh = false;
	}
	LayoutDeck(m_aDeck, View);
	HandleDrag(Ui, Input, Viewport, m_vPageTabRects, m_vPageTabIds, Delta);
	if(m_NeedsRefresh)
	{
		// 提交/取消/预览页切换后立即用新布局渲染本帧。
		RefreshDeck(m_Dragging ? m_Drag.PreviewPageId() : m_ActivePageId);
		m_ModelRevision = m_Model.Revision();
		m_NeedsRefresh = false;
	}

	std::array<std::vector<SCard *>, 3> RenderDeck = m_aDeck;
	SCard *pAddedEntry = nullptr;
	if(m_Dragging && m_Drag.Phase() == ECardDragPhase::DRAGGING)
	{
		// 跨页预览：目标页没有该卡片放置时补一个临时条目参与让位。
		if(const SCardDescriptor *pDraggedCard = m_Model.Registry().FindCard(m_Drag.CardId()))
		{
			bool InDeck = false;
			for(const auto &Column : m_aDeck)
				for(const SCard *pCard : Column)
					InDeck = InDeck || pCard->m_pDescriptor->m_Id == m_Drag.CardId();
			if(!InDeck)
			{
				pAddedEntry = &CardEntry(DisplayPage, pDraggedCard);
				RenderDeck[std::clamp(m_Drag.TargetColumn(), 0, 2)].push_back(pAddedEntry);
			}
		}
		std::array<std::vector<std::string>, 3> aColumns;
		for(int Column = 0; Column < 3; ++Column)
			for(const SCard *pCard : RenderDeck[Column])
				aColumns[Column].push_back(pCard->m_pDescriptor->m_Id);
		if(m_TwoColumns && !m_WideDeck)
			ApplyCardDragSingleColumnPlacement(aColumns, m_Drag.CardId(), m_Drag.TargetOrder());
		else
			ApplyCardDragPlacement(aColumns, m_Drag.CardId(), m_Drag.TargetColumn(), m_Drag.TargetOrder());
		const auto Resolve = [this, pAddedEntry](const std::string &Id) -> SCard * {
			for(int Column = 0; Column < 3; ++Column)
				for(SCard *pCard : m_aDeck[Column])
					if(pCard->m_pDescriptor->m_Id == Id)
						return pCard;
			return pAddedEntry && pAddedEntry->m_pDescriptor->m_Id == Id ? pAddedEntry : nullptr;
		};
		for(int Column = 0; Column < 3; ++Column)
		{
			std::vector<SCard *> vColumn;
			vColumn.reserve(aColumns[Column].size());
			for(const std::string &Id : aColumns[Column])
				if(SCard *pCard = Resolve(Id))
					vColumn.push_back(pCard);
			RenderDeck[Column] = std::move(vColumn);
		}
		LayoutDeck(RenderDeck, View);
	}

	for(const SSlot &Slot : m_vSlots)
	{
		if(!m_Scroll.AddRect(Slot.m_Rect, m_ScrollToFocus && Slot.m_pCard == m_pFocused))
			continue;
		RenderCard(const_cast<SSlot &>(Slot), true, false, Menus, Ui, Input, Tooltips, ViewPrefs, Delta, View.y, Snap);
	}
	m_Scroll.End();

	// ghost 在裁剪区外绘制，可跟随指针越过工具栏与页面 tabs。
	if(m_Dragging && m_Drag.Phase() == ECardDragPhase::DRAGGING)
	{
		if(const SCardDescriptor *pDraggedCard = m_Model.Registry().FindCard(m_Drag.CardId()))
		{
			const CUIRect Ghost{Ui.MouseX() - m_Drag.GrabOffsetX(), Ui.MouseY() - m_Drag.GrabOffsetY(), m_DragGhostWidth, ROW + 12.0f};
			Ghost.Draw(ColorRGBA(0.2f, 0.6f, 0.45f, 0.85f), IGraphics::CORNER_ALL, 4.0f);
			CUIRect GhostLabel;
			Ghost.Margin(6.0f, &GhostLabel);
			Ui.DoLabel(&GhostLabel, Title(*pDraggedCard), 13.0f, TEXTALIGN_ML);
		}
	}
	m_ScrollToFocus = false;
	return true;
}

void CQmCardSettingsView::EndLegacy(const CUIRect &End)
{
	m_Scroll.AddRect({End.x, End.y, End.w, 1.0f});
	m_Scroll.End();
}

void CQmCardSettingsView::Shutdown(CUi &Ui)
{
	m_pIcons->Shutdown();
	m_Search.Deactivate();
	m_Drag.Reset();
	m_Dragging = false;
	Ui.SetActiveItem(nullptr);
}

void CQmCardSettingsView::OnResize()
{
	// resize 重建几何；已提交布局保留，未提交的拖拽预览取消。
	m_Scroll.Reset();
	m_Drag.Reset();
	m_Dragging = false;
	m_NeedsRefresh = true;
	InvalidateResources();
}

void CQmCardSettingsView::InvalidateResources()
{
	m_pIcons->Invalidate();
}

void CQmCardSettingsView::SetResourceReporter(TQmResourceReporter Reporter)
{
	m_pIcons->SetReporter(std::move(Reporter));
}

void CQmCardSettingsView::EndFrame(CUi &Ui)
{
	if(!m_RenderedThisFrame && m_WasOpen)
	{
		m_pIcons->Close();
		m_Search.Deactivate();
		m_Drag.Reset();
		m_Dragging = false;
		if(Ui.ActiveItem() == &m_Search)
			Ui.SetActiveItem(nullptr);
		m_LastRenderTime = 0;
	}
	// 布局事务完成后自动保存：写盘不在逐帧拖拽路径中执行；失败保留 dirty 并退避重试。
	if(m_SavePending && m_pStorage && m_RenderedThisFrame && !m_Dragging)
	{
		const int64_t Now = time_get();
		if(Now >= m_NextSaveRetry)
		{
			std::string Error;
			if(SaveCardPreferences(*m_pStorage, m_Model, Error))
			{
				m_SavePending = false;
				m_SaveError.clear();
			}
			else
			{
				m_SaveError = Error;
				m_NextSaveRetry = Now + time_freq();
				log_error("qm/ui", "%s", Error.c_str());
			}
		}
	}
	// 最多两个取消中的 job；关闭页面后仍收回迟到结果，不启动或上传新资源。
	m_pIcons->RetireCancelled();
	m_WasOpen = m_RenderedThisFrame;
	m_RenderedThisFrame = false;
}
