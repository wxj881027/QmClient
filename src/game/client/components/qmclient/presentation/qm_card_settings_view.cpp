/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_card_settings_view.h"

#include <base/math.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/input.h>
#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>
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

CUIRect GridCell(const CUIRect &Area, int Index, int Columns)
{
	const float Width = (Area.w - GAP * (Columns - 1)) / Columns;
	return {Area.x + (Index % Columns) * (Width + GAP), Area.y + (Index / Columns) * (ROW + GAP), Width, ROW};
}
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

CQmCardSettingsView::CQmCardSettingsView(CCardUiModel &Model, CConfig &Config, IEngine *pEngine, IStorage *pStorage, IGraphics *pGraphics) :
	m_Model(Model), m_Adapter(Config),
	m_pIcons(std::make_unique<CSettingsIconResources>(pEngine, pStorage, pGraphics))
{
	const bool InputRegistered =
		m_InputDispatch.Register(EQmInputSlot::UI, 100, "qm.ui.search", 0) == EQmDispatchRegistration::REGISTERED &&
		m_InputDispatch.Register(EQmInputSlot::UI, 0, "qm.ui.cards", 1) == EQmDispatchRegistration::REGISTERED;
	if(!InputRegistered)
		log_error("qm/ui", "failed to register card input priorities");
	m_InputDispatch.Freeze();
	for(const auto &Entry : Model.OrderModel().Entries())
	{
		const SCardDescriptor *pCard = Model.Registry().FindCard(Entry.m_Id);
		if(pCard && m_Adapter.Read(*pCard))
			m_vCards.push_back({pCard, {}, {}, 0.0f});
	}
	m_vpVisible.reserve(m_vCards.size());
}

CQmCardSettingsView::~CQmCardSettingsView() = default;

const char *CQmCardSettingsView::Title(const SCardDescriptor &Card)
{
	if(Card.m_Id == "qm.diagnostics")
		return Localize("Diagnostics");
	return Localize(Card.m_TitleKey.c_str());
}

void CQmCardSettingsView::Refresh()
{
	const char *pQuery = m_Search.GetString();
	if(m_Revision == m_Model.Revision() && m_Query == pQuery && m_CachedShowHidden == m_ShowHidden)
		return;
	m_Query = pQuery;
	m_Revision = m_Model.Revision();
	m_CachedShowHidden = m_ShowHidden;
	m_vpVisible.clear();
	for(SCard &Card : m_vCards)
	{
		const auto Snapshot = m_Model.Snapshot(Card.m_pDescriptor->m_Id);
		if(!m_ShowHidden && !Snapshot.m_Preferences.m_Visible)
			continue;
		bool Matches = m_Query.empty() || str_utf8_find_nocase(Title(*Card.m_pDescriptor), pQuery) != nullptr ||
			str_utf8_find_nocase(Card.m_pDescriptor->m_Id.c_str(), pQuery) != nullptr;
		for(const auto &Keyword : Card.m_pDescriptor->m_SearchKeywords)
			Matches = Matches || str_utf8_find_nocase(Keyword.c_str(), pQuery) != nullptr;
		if(Matches)
			m_vpVisible.push_back(&Card);
	}
	std::sort(m_vpVisible.begin(), m_vpVisible.end(), [this](const SCard *pLeft, const SCard *pRight) {
		const auto *pA = m_Model.OrderModel().Find(pLeft->m_pDescriptor->m_Id);
		const auto *pB = m_Model.OrderModel().Find(pRight->m_pDescriptor->m_Id);
		const auto *pPageA = m_Model.Registry().FindPage(pA->m_PageId);
		const auto *pPageB = m_Model.Registry().FindPage(pB->m_PageId);
		if(pPageA->m_Order != pPageB->m_Order)
			return pPageA->m_Order < pPageB->m_Order;
		if(pA->m_PageId != pB->m_PageId)
			return pA->m_PageId < pB->m_PageId;
		if(pA->m_Column != pB->m_Column)
			return pA->m_Column < pB->m_Column;
		return pA->m_Order < pB->m_Order;
	});
	if(std::find(m_vpVisible.begin(), m_vpVisible.end(), m_pFocused) == m_vpVisible.end())
		m_pFocused = nullptr;
}

void CQmCardSettingsView::Move(SCard &Card, int Direction)
{
	const auto It = std::find(m_vpVisible.begin(), m_vpVisible.end(), &Card);
	if(It == m_vpVisible.end())
		return;
	const int Index = static_cast<int>(It - m_vpVisible.begin());
	const int Target = Index + Direction;
	if(Target < 0 || Target >= static_cast<int>(m_vpVisible.size()))
		return;
	m_Model.MoveCardRelative(Card.m_pDescriptor->m_Id, m_vpVisible[Target]->m_pDescriptor->m_Id, Direction > 0);
	m_pFocused = &Card;
	m_ScrollToFocus = true;
}

void CQmCardSettingsView::ToggleVisibility(SCard &Card)
{
	auto Preferences = m_Model.Preferences(Card.m_pDescriptor->m_Id);
	Preferences.m_Visible = !Preferences.m_Visible;
	m_Model.SetPreferences(Card.m_pDescriptor->m_Id, Preferences);
}

void CQmCardSettingsView::ToggleCollapsed(SCard &Card)
{
	auto Preferences = m_Model.Preferences(Card.m_pDescriptor->m_Id);
	Preferences.m_Collapsed = !Preferences.m_Collapsed;
	m_Model.SetPreferences(Card.m_pDescriptor->m_Id, Preferences);
}

void CQmCardSettingsView::ToggleSetting(SCard &Card)
{
	const auto Model = m_Model.Snapshot(Card.m_pDescriptor->m_Id);
	const auto Value = m_Adapter.Read(*Card.m_pDescriptor);
	if(Model.m_Available && Value)
		m_Adapter.Apply(*Card.m_pDescriptor, Value->m_Value == 0 ? 1 : 0);
}

void CQmCardSettingsView::HandleKeyboard(CUi &Ui, IInput &Input)
{
	if(Ui.IsPopupOpen())
		return;
	// 官方编辑框先处理文本和 IME；搜索激活时只把 Tab 交给卡片导航。
	m_InputDispatch.DispatchUntilConsumed(
		[this](const auto &Entry) { return Entry.m_UserIndex != 0 || m_Search.IsActive(); },
		[this, &Ui, &Input](const auto &Entry) {
			if(Entry.m_UserIndex == 0)
				return !Input.KeyPress(KEY_TAB);
			HandleCardKeyboard(Ui, Input);
			return true;
		});
}

void CQmCardSettingsView::HandleCardKeyboard(CUi &Ui, IInput &Input)
{
	if(Ui.ConsumeHotkey(CUi::HOTKEY_TAB))
	{
		if(m_Search.IsActive())
			m_Search.Deactivate();
		const auto It = std::find(m_vpVisible.begin(), m_vpVisible.end(), m_pFocused);
		const int Direction = Input.KeyIsPressed(KEY_LSHIFT) || Input.KeyIsPressed(KEY_RSHIFT) ? -1 : 1;
		const int Count = static_cast<int>(m_vpVisible.size());
		if(Count > 0)
		{
			const int Old = It == m_vpVisible.end() ? (Direction > 0 ? -1 : 0) : static_cast<int>(It - m_vpVisible.begin());
			m_pFocused = m_vpVisible[(Old + Direction + Count) % Count];
			m_ScrollToFocus = true;
		}
	}
	if(m_Search.IsActive() || !m_pFocused)
		return;
	const bool Control = Input.KeyIsPressed(KEY_LCTRL) || Input.KeyIsPressed(KEY_RCTRL);
	if(Ui.ConsumeHotkey(CUi::HOTKEY_UP))
	{
		if(Control)
			Move(*m_pFocused, -1);
		else
		{
			const auto It = std::find(m_vpVisible.begin(), m_vpVisible.end(), m_pFocused);
			if(It != m_vpVisible.begin() && It != m_vpVisible.end())
				m_pFocused = *(It - 1);
		}
		m_ScrollToFocus = true;
	}
	if(Ui.ConsumeHotkey(CUi::HOTKEY_DOWN))
	{
		if(Control)
			Move(*m_pFocused, 1);
		else
		{
			const auto It = std::find(m_vpVisible.begin(), m_vpVisible.end(), m_pFocused);
			if(It != m_vpVisible.end() && It + 1 != m_vpVisible.end())
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

bool CQmCardSettingsView::Render(CMenus &Menus, CUi &Ui, IInput &Input, IStorage &Storage, IGraphics &Graphics, CTooltips &Tooltips, CUIRect &View)
{
	m_RenderedThisFrame = true;
	CUIRect Header, Modes, Toolbar, Footer;
	View.HSplitTop(28.0f, &Header, &View);
	Ui.DoLabel(&Header, Localize("QmClient"), 20.0f, TEXTALIGN_ML);
	const int ModeColumns = View.w >= 270.0f ? 3 : 1;
	View.HSplitTop((3 / ModeColumns) * (ROW + GAP), &Modes, &View);
	const char *apModes[] = {Localize("List"), Localize("Cards"), Localize("Player indicators")};
	auto Preferences = m_Model.ViewPreferences();
	for(int Index = 0; Index < 3; ++Index)
	{
		const CUIRect Button = GridCell(Modes, Index, ModeColumns);
		if(Menus.DoButton_MenuTab(&m_aModes[Index], apModes[Index], Preferences.m_Mode == Index, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			Preferences.m_Mode = Index;
			m_Model.SetViewPreferences(Preferences);
			m_Scroll.Reset();
		}
	}
	if(Preferences.m_Mode == 2)
	{
		m_pIcons->Close();
		m_Search.Deactivate();
		m_Scroll.Begin(&View);
		return false;
	}
	m_pIcons->Prepare(static_cast<float>(Graphics.ScreenHeight()) / std::max(1.0f, Ui.Screen()->h));

	View.HSplitTop(ROW, &Toolbar, &View);
	Ui.DoEditBox_Search(&m_Search, &Toolbar, 14.0f, !Ui.IsPopupOpen());
	View.HSplitTop(GAP, nullptr, &View);
	const int OptionColumns = View.w >= 360.0f ? 3 : 1;
	View.HSplitTop((3 / OptionColumns) * (ROW + GAP), &Toolbar, &View);
	CUIRect Option = GridCell(Toolbar, 0, OptionColumns);
	if(Menus.DoButton_CheckBox(&m_ShowHidden, Localize("Show hidden cards"), m_ShowHidden, &Option))
		m_ShowHidden = !m_ShowHidden;
	Option = GridCell(Toolbar, 1, OptionColumns);
	if(Menus.DoButton_CheckBox(&m_Theme, Localize("Light theme"), Preferences.m_LightTheme, &Option))
	{
		Preferences.m_LightTheme = !Preferences.m_LightTheme;
		m_Model.SetViewPreferences(Preferences);
	}
	Option = GridCell(Toolbar, 2, OptionColumns);
	if(Menus.DoButton_CheckBox(&m_Animations, Localize("Animations"), Preferences.m_Animations, &Option))
	{
		Preferences.m_Animations = !Preferences.m_Animations;
		m_Model.SetViewPreferences(Preferences);
	}
	View.HSplitBottom(ROW, &View, &Footer);
	CUIRect Save, Reset;
	Footer.VSplitMid(&Reset, &Save, GAP);
	if(Menus.DoButton_Menu(&m_Reset, Localize("Restore layout"), 0, &Reset))
		m_Model.ResetAllPreferences();
	if(Menus.DoButton_Menu(&m_Save, Localize("Save"), 0, &Save))
		SaveCardPreferences(Storage, m_Model, m_SaveError);
	if(!m_SaveError.empty())
	{
		CUIRect Error;
		View.HSplitBottom(ROW, &View, &Error);
		Ui.DoLabel(&Error, Localize("Could not save preferences"), 12.0f, TEXTALIGN_ML);
	}

	Refresh();
	HandleKeyboard(Ui, Input);
	const int64_t Now = time_get();
	const float Delta = m_LastRenderTime == 0 ? 0.0f : std::clamp(static_cast<float>(Now - m_LastRenderTime) / time_freq(), 0.0f, 0.05f);
	m_LastRenderTime = Now;
	m_Scroll.Begin(&View);
	for(SCard *pCard : m_vpVisible)
	{
		const auto Snapshot = m_Model.Snapshot(pCard->m_pDescriptor->m_Id);
		const auto Value = m_Adapter.Read(*pCard->m_pDescriptor);
		const bool RestartRequired = Value && Value->m_RestartRequired;
		const int Columns = std::clamp(static_cast<int>(View.w / 70.0f), 1, 5);
		const int ActionRows = (5 + Columns - 1) / Columns;
		const float Height = ROW + (Snapshot.m_Preferences.m_Collapsed ? 0.0f : ROW + GAP + (RestartRequired ? ROW : 0.0f)) + ActionRows * (ROW + GAP) + 12.0f;
		CUIRect Card;
		View.HSplitTop(Height, &Card, &View);
		View.HSplitTop(GAP * 2.0f, nullptr, &View);
		if(!m_Scroll.AddRect(Card, m_ScrollToFocus && pCard == m_pFocused))
			continue;
		const bool Hovered = Ui.MouseInside(&Card);
		if(Hovered && Input.KeyPress(KEY_MOUSE_1))
			m_pFocused = pCard;
		const float Target = Hovered || pCard == m_pFocused ? 1.0f : 0.0f;
		pCard->m_Hover = Preferences.m_Animations ? mix(pCard->m_Hover, Target, std::min(1.0f, Delta * 16.0f)) : Target;
		if(Preferences.m_Mode == 1)
		{
			const float Base = Preferences.m_LightTheme ? 0.72f : 0.1f;
			Card.Draw(ColorRGBA(Base, Base, Base, 0.4f + 0.2f * pCard->m_Hover), IGraphics::CORNER_ALL, 4.0f);
		}
		else if(pCard == m_pFocused)
			Card.Draw(ColorRGBA(0.2f, 0.6f, 0.45f, 0.18f), IGraphics::CORNER_ALL, 0.0f);
		Card.Margin(6.0f, &Card);
		CUIRect Label;
		Card.HSplitTop(ROW, &Label, &Card);
		Ui.DoLabel(&Label, Title(*pCard->m_pDescriptor), 13.0f, TEXTALIGN_ML);
		if(!Snapshot.m_Preferences.m_Collapsed)
		{
			CUIRect Toggle;
			Card.HSplitTop(ROW, &Toggle, &Card);
			Card.HSplitTop(GAP, nullptr, &Card);
			if(Snapshot.m_Available && Value)
			{
				if(Menus.DoButton_CheckBox(&pCard->m_Toggle, Localize("Enabled"), Value->m_Value != 0, &Toggle))
					ToggleSetting(*pCard);
			}
			else
				Ui.DoLabel(&Toggle, Localize("Unavailable"), 13.0f, TEXTALIGN_ML);
			if(RestartRequired)
			{
				CUIRect Restart;
				Card.HSplitTop(ROW, &Restart, &Card);
				Ui.DoLabel(&Restart, Localize("Restart required"), 12.0f, TEXTALIGN_ML);
			}
		}
		const char *apActions[] = {Localize("Up"), Localize("Down"), Snapshot.m_Preferences.m_Visible ? Localize("Hide") : Localize("Restore"), Snapshot.m_Preferences.m_Collapsed ? Localize("Expand") : Localize("Collapse"), Localize("Reset")};
		for(int Action = 0; Action < 5; ++Action)
		{
			const CUIRect Button = GridCell(Card, Action, Columns);
			const bool IconReady = Action < 4 && m_pIcons->Ready();
			const bool Pressed = Menus.DoButton_Menu(&pCard->m_aActions[Action], IconReady ? "" : apActions[Action], 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f);
			if(IconReady)
			{
				const EQmUiIcon Icon = Action == 2 ?
					(Snapshot.m_Preferences.m_Visible ? EQmUiIcon::EYE_OFF : EQmUiIcon::EYE) : EQmUiIcon::CHEVRON_DOWN;
				m_pIcons->Draw(Icon, Button, Action == 0 || (Action == 3 && !Snapshot.m_Preferences.m_Collapsed));
				Tooltips.DoToolTip(&pCard->m_aActions[Action], &Button, apActions[Action]);
			}
			if(!Pressed)
				continue;
			if(Action == 0 || Action == 1)
				Move(*pCard, Action == 0 ? -1 : 1);
			else if(Action == 2)
				ToggleVisibility(*pCard);
			else if(Action == 3)
				ToggleCollapsed(*pCard);
			else
				m_Model.ResetPreferences(pCard->m_pDescriptor->m_Id);
		}
	}
	if(m_vpVisible.empty())
	{
		CUIRect Empty;
		View.HSplitTop(ROW, &Empty, &View);
		if(m_Scroll.AddRect(Empty))
			Ui.DoLabel(&Empty, Localize("No matching cards"), 14.0f, TEXTALIGN_ML);
	}
	m_Scroll.End();
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
	Ui.SetActiveItem(nullptr);
}

void CQmCardSettingsView::OnResize()
{
	m_Scroll.Reset();
	m_Revision = ~0u;
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
		if(Ui.ActiveItem() == &m_Search)
			Ui.SetActiveItem(nullptr);
		m_LastRenderTime = 0;
	}
	// 最多两个取消中的 job；关闭页面后仍收回迟到结果，不启动或上传新资源。
	m_pIcons->RetireCancelled();
	m_WasOpen = m_RenderedThisFrame;
	m_RenderedThisFrame = false;
}
