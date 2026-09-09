/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "menus.h"

#include "background.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/client/updater.h>
#include <engine/config.h>
#include <engine/editor.h>
#include <engine/friends.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimCurves.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/UiContainers.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/console.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

extern bool gs_SettingsAssetsEntityGamePreview;

namespace
{
	constexpr float MENU_MENUBAR_HEIGHT_NEW = 24.0f;
	constexpr float MENU_MENUBAR_HEIGHT_LEGACY = 30.0f;
	constexpr float MENU_MENUBAR_CONTENT_SCALE_NEW = 1.10f;

	constexpr float MenuMenubarHeight(bool UseNewUi)
	{
		return UseNewUi ? MENU_MENUBAR_HEIGHT_NEW : MENU_MENUBAR_HEIGHT_LEGACY;
	}

	class CUiRenderOnlyScope
	{
	public:
		explicit CUiRenderOnlyScope(CUi *pUi) :
			m_pUi(pUi)
		{
			m_pUi->BeginRenderOnly();
		}

		~CUiRenderOnlyScope()
		{
			m_pUi->EndRenderOnly();
		}

	private:
		CUi *m_pUi;
	};

	bool SettingsMenuTextPlanItemBuildable(const CMenus::SMenuTextPlanItem &Item)
	{
		return !Item.m_TextId.empty() && !Item.m_Text.empty() && Item.m_FontSize > 0.0f && Item.m_Rect.w > 0.0f && Item.m_Rect.h > 0.0f;
	}

	int CanonicalizeTClientCacheTab(int Tab)
	{
		static constexpr int TCLIENT_CACHE_SLOTS = 6;
		auto IsTabHidden = [](int Candidate) {
			return (g_Config.m_TcTClientSettingsTabs & (1 << Candidate)) != 0;
		};
		if(Tab < 0 || Tab >= TCLIENT_CACHE_SLOTS || IsTabHidden(Tab))
		{
			for(int Candidate = 0; Candidate < TCLIENT_CACHE_SLOTS; ++Candidate)
			{
				if(!IsTabHidden(Candidate))
					return Candidate;
			}
			return 0;
		}
		return Tab;
	}

}

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
	constexpr float MENU_SWITCH_DURATION = 0.18f;
	constexpr float MENU_SWITCH_ALPHA_MAX = 0.12f;
	constexpr float MENU_TAB_HOVER_DURATION = 0.10f;
	constexpr float MENU_TAB_DEFAULT_X_OFFSET = 0.0f;
	constexpr float MENU_TAB_DEFAULT_Y_OFFSET = -1.5f;
	constexpr float MENU_TAB_DEFAULT_W_OFFSET = 0.0f;
	constexpr float MENU_TAB_DEFAULT_H_OFFSET = 3.0f;
	constexpr float MENU_TAB_ANIM_EPSILON = 0.0001f;
	constexpr int MENU_IDLE_REFRESH_RATE = 60;
	constexpr auto MENU_IDLE_INTERACTION_GRACE_TIME = 450ms;
	int s_LastPlanCollectionScreenBucket = -1;
	CUIRect MenuButtonTextRect(const CUIRect *pRect, float FontFactor, float HoverLift)
	{
		CUIRect Text = *pRect;
		Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
		Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
		Text.y += HoverLift;
		return Text;
	}

	bool PerfDebugEnabled()
	{
		return QmPerfEnabled();
	}

	ColorRGBA MenuUiColorSurface(float AlphaScale, float ColorScale)
	{
		ColorHSLA UiHsla(g_Config.m_QmUiColor);
		UiHsla = UiHsla.UnclampLighting(0.42f);
		const ColorRGBA UiColor = color_cast<ColorRGBA>(UiHsla);
		const float BaseAlpha = maximum(UiColor.a, 0.70f);
		const float UiAlpha = g_Config.m_QmUiOpacity / 100.0f;
		return ColorRGBA(
			std::clamp(UiColor.r * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.g * ColorScale, 0.0f, 1.0f),
			std::clamp(UiColor.b * ColorScale, 0.0f, 1.0f),
			std::clamp(BaseAlpha * UiAlpha * AlphaScale, 0.0f, 1.0f));
	}

	ColorRGBA MenuUiColorAccent(float AlphaScale)
	{
		ColorHSLA UiHsla(g_Config.m_QmUiColor);
		UiHsla = UiHsla.UnclampLighting(0.48f);
		const ColorRGBA UiColor = color_cast<ColorRGBA>(UiHsla);
		const float UiAlpha = g_Config.m_QmUiOpacity / 100.0f;
		return UiColor.WithAlpha(std::clamp(maximum(UiColor.a, 0.85f) * UiAlpha * AlphaScale, 0.0f, 1.0f));
	}

	ColorRGBA MenuTabDefaultColor()
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
		return Base.WithAlpha(std::clamp(g_Config.m_QmUiOpacity / 100.0f, 0.0f, 1.0f));
	}

	ColorRGBA MenuIconButtonDefaultColor()
	{
		const bool UseNewUi = g_Config.m_QmNewUi != 0;
		return UseNewUi ? MenuTabDefaultColor() : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	}

	ColorRGBA MenuTabActiveColor()
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
		return Base.WithAlpha(std::clamp(g_Config.m_QmUiOpacity / 100.0f, 0.0f, 1.0f));
	}

	ColorRGBA MenuTabHoverColor()
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
		return Base.WithAlpha(std::clamp(g_Config.m_QmUiOpacity / 100.0f, 0.0f, 1.0f));
	}

	ColorRGBA MenuMenubarHoverColor()
	{
		return MenuUiColorSurface(0.78f, 0.42f);
	}

	ColorRGBA MenuDangerTabDefaultColor()
	{
		const ColorRGBA Base = MenuTabDefaultColor();
		return ColorRGBA(maximum(Base.r, 0.20f), Base.g * 0.35f, Base.b * 0.35f, maximum(Base.a, 0.32f));
	}

	ColorRGBA MenuDangerTabHoverColor()
	{
		return ColorRGBA(1.0f, 0.15f, 0.15f, 0.52f);
	}

	void LogPerfStage(IClient *pClient, const char *pStage, const double DurationMs, const bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

	void LogSettingsInvalidatePerf(ESettingsInvalidationReason Reason, bool ClearsText, bool ClearsSection, bool ClearsPage, bool ClearsResource)
	{
		if(!PerfDebugEnabled())
			return;
		char aPayload[128];
		str_format(aPayload, sizeof(aPayload), "reason=%s text=%d section=%d page=%d resource=%d",
			SettingsInvalidationReasonName(Reason), ClearsText ? 1 : 0, ClearsSection ? 1 : 0, ClearsPage ? 1 : 0, ClearsResource ? 1 : 0);
		QmPerfLogPayload("perf/settings-invalidate", aPayload);
	}

	const char *MenuTextScopeName(CMenus::EMenuTextScope Scope)
	{
		switch(Scope)
		{
		case CMenus::MENU_TEXT_SCOPE_SETTINGS: return "settings";
		case CMenus::MENU_TEXT_SCOPE_INGAME: return "ingame";
		default: return "unknown";
		}
	}

	int MenuTextBucket(float Value)
	{
		return round_to_int(Value * 10.0f);
	}

	std::string MenuTextCacheKey(CMenus::EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const CMenus::SMenuTextStyleKey &StyleKey)
	{
		char aKey[512];
		str_format(aKey, sizeof(aKey), "%s:%d:%d:%d:%s:fs%d:al%d:mw%d:us%d:hd%d:cm%d",
			MenuTextScopeName(Scope), Page, Tab, Subtab, pTextId != nullptr ? pTextId : "",
			MenuTextBucket(StyleKey.m_FontSize), StyleKey.m_Align, StyleKey.m_MaxWidthBucket,
			StyleKey.m_UiScaleBucket, StyleKey.m_HiDpiScaleBucket, StyleKey.m_CompactMode);
		return aKey;
	}

	std::string MenuTextDescriptorKey(CMenus::EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId)
	{
		char aKey[256];
		str_format(aKey, sizeof(aKey), "%s:%d:%d:%d:%s",
			MenuTextScopeName(Scope), Page, Tab, Subtab, pTextId != nullptr ? pTextId : "");
		return aKey;
	}

	float QmPerfEwma(float Previous, float Sample, float Alpha)
	{
		if(Sample <= 0.0f)
			return Previous * (1.0f - Alpha);
		if(Previous <= 0.0f)
			return Sample;
		return Previous * (1.0f - Alpha) + Sample * Alpha;
	}
}

CMenus::SMenuTextStyleKey CMenus::BuildMenuTextStyleKey(const CUIRect *pRect, float FontSize, int Align, const SLabelProperties &LabelProps) const
{
	SMenuTextStyleKey StyleKey;
	StyleKey.m_FontSize = FontSize;
	StyleKey.m_Align = Align;
	const float MaxWidth = LabelProps.m_MaxWidth >= 0.0f ? LabelProps.m_MaxWidth : (pRect != nullptr ? pRect->w : -1.0f);
	StyleKey.m_MaxWidthBucket = MaxWidth >= 0.0f && std::isfinite(MaxWidth) ? (round_to_int(MaxWidth / 4.0f) * 4) : -1;
	StyleKey.m_UiScaleBucket = std::clamp(g_Config.m_QmUiScale, 50, 200);
	const float HiDpiScale = Graphics() != nullptr ? Graphics()->ScreenHiDPIScale() : 1.0f;
	StyleKey.m_HiDpiScaleBucket = HiDpiScale >= 0.0f && std::isfinite(HiDpiScale) ? round_to_int(HiDpiScale * 100.0f) : 100;
	StyleKey.m_CompactMode = g_Config.m_QmNewUi ? 1 : 0;
	return StyleKey;
}

CMenus::SMenuTextStyleKey CMenus::SettingsMenuTextPlanStyleKey(const SMenuTextPlanItem &Item) const
{
	switch(Item.m_StyleMode)
	{
	case MENU_TEXT_STYLE_DEFAULT:
		return Item.m_StyleKey;
	case MENU_TEXT_STYLE_EXACT:
		return Item.m_StyleKey;
	case MENU_TEXT_STYLE_ALLOWLIST_DYNAMIC:
		return Item.m_StyleKey;
	case MENU_TEXT_STYLE_RECT:
	default:
		break;
	}
	return BuildMenuTextStyleKey(&Item.m_Rect, Item.m_FontSize, Item.m_Align, Item.m_LabelProps);
}

namespace
{
	CUIRect MenuTextSettingsContentView(CUIRect Screen)
	{
		CUIRect TabBar, MainView;
		const bool UseNewUi = g_Config.m_QmNewUi != 0;
		Screen.HSplitTop(MenuMenubarHeight(UseNewUi), &TabBar, &MainView);
		if(UseNewUi)
			MainView.HSplitTop(6.0f, nullptr, &MainView);
		return MainView;
	}

	const char *MenuTextInvalidationReasonName(ESettingsInvalidationReason Reason)
	{
		[[maybe_unused]] static constexpr const char *s_apMenuTextInvalidationReasonTaxonomy[] = {"language", "font", "window", "dpi", "layout_width", "compact_mode", "ui_scale", "theme", "config", "backend", "style"};
		switch(Reason)
		{
		case ESettingsInvalidationReason::LANGUAGE_CHANGED: return "language";
		case ESettingsInvalidationReason::FONT_CHANGED: return "font";
		case ESettingsInvalidationReason::BACKEND_CHANGED: return "backend";
		case ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED: return "window";
		case ESettingsInvalidationReason::DPI_CHANGED: return "dpi";
		case ESettingsInvalidationReason::UI_SCALE_CHANGED: return "ui_scale";
		case ESettingsInvalidationReason::CONFIG_HASH_CHANGED: return "config";
		case ESettingsInvalidationReason::SECTION_SIZE_CHANGED: return "layout_width";
		case ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED: return "config";
		default: return "style";
		}
	}

	void LogSettingsTextPoolCoverageGap(IClient *pClient, const char *pEvent, CMenus::EMenuTextScope Scope, const char *pScopeName, int Page, int Tab, int Subtab, const char *pKey, const char *pReason, const char *pPlanStatus, const char *pOperation, uint64_t Frame)
	{
		if(!PerfDebugEnabled())
			return;
		static uint64_t s_LastFrame = 0;
		static int s_LastPage = -1000;
		static int s_LastTab = -1000;
		static int s_LastSubtab = -1000;
		static char s_aLastReason[32] = "";
		static char s_aLastPlanStatus[32] = "";
		static int s_SamplesThisBucket = 0;
		const char *pSafeReason = pReason != nullptr ? pReason : "unknown";
		const char *pSafePlanStatus = pPlanStatus != nullptr ? pPlanStatus : "unknown";
		if(s_LastFrame != Frame || s_LastPage != Page || s_LastTab != Tab || s_LastSubtab != Subtab || str_comp(s_aLastReason, pSafeReason) != 0 || str_comp(s_aLastPlanStatus, pSafePlanStatus) != 0)
		{
			s_LastFrame = Frame;
			s_LastPage = Page;
			s_LastTab = Tab;
			s_LastSubtab = Subtab;
			str_copy(s_aLastReason, pSafeReason, sizeof(s_aLastReason));
			str_copy(s_aLastPlanStatus, pSafePlanStatus, sizeof(s_aLastPlanStatus));
			s_SamplesThisBucket = 0;
		}
		constexpr int MaxGapSamplesPerFrameBucket = 4;
		if(s_SamplesThisBucket >= MaxGapSamplesPerFrameBucket)
			return;
		++s_SamplesThisBucket;
		char aPayload[768];
		char aPage[32];
		if(Scope == CMenus::MENU_TEXT_SCOPE_SETTINGS)
			str_copy(aPage, SettingsPageCacheKey(Page, -1).c_str(), sizeof(aPage));
		else
			str_format(aPage, sizeof(aPage), "%d", Page);
		str_format(aPayload, sizeof(aPayload), "event=%s scope=%s page=%s tab=%d subtab=%d key=%s reason=%s plan_status=%s operation=%s frame=%" PRIu64 " log_sample=%d log_sample_limit=%d",
			pEvent != nullptr ? pEvent : "settings_text_miss", pScopeName != nullptr ? pScopeName : MenuTextScopeName(Scope), aPage, Tab, Subtab,
			pKey != nullptr ? pKey : "", pSafeReason, pSafePlanStatus, pOperation != nullptr ? pOperation : "unknown", Frame, s_SamplesThisBucket, MaxGapSamplesPerFrameBucket);
		QmPerfLogPayload("perf/settings-text", aPayload, pClient, aPage);
	}

	void LogSettingsTextPoolUsage(IClient *pClient, CMenus::EMenuTextScope Scope, const char *pScopeName, int Page, int Tab, int Subtab, const char *pOperation, uint64_t Frame, int Candidates, int Hits, int Reused, int Misses, int Stales, int TextNew, int TextReused, int Planned, int Unplanned, int PoolHits, int RenderReadyHits, int BuildQueued, int FallbackImmediate)
	{
		if(!PerfDebugEnabled() || Candidates <= 0)
			return;
		char aPayload[512];
		char aPage[32];
		if(Scope == CMenus::MENU_TEXT_SCOPE_SETTINGS)
			str_copy(aPage, SettingsPageCacheKey(Page, -1).c_str(), sizeof(aPage));
		else
			str_format(aPage, sizeof(aPage), "%d", Page);
		str_format(aPayload, sizeof(aPayload), "event=settings_text_usage scope=%s page=%s tab=%d subtab=%d operation=%s frame=%" PRIu64 " text_class=static_stable scheduler_coverage=%s candidates=%d hits=%d reused=%d miss=%d stale=%d text_new=%d text_reused=%d planned=%d unplanned=%d pool_hit=%d render_ready_hit=%d build_queued=%d fallback_immediate=%d",
			pScopeName != nullptr ? pScopeName : MenuTextScopeName(Scope), aPage, Tab, Subtab, pOperation != nullptr ? pOperation : "unknown", Frame,
			FallbackImmediate > 0 ? "uncovered" : "budgeted",
			Candidates, Hits, Reused, Misses, Stales, TextNew, TextReused, Planned, Unplanned, PoolHits, RenderReadyHits, BuildQueued, FallbackImmediate);
		QmPerfLogPayload("perf/settings-text", aPayload, pClient, aPage);
	}

	[[maybe_unused]] const char *StableTextMiss()
	{
		return "event=settings_text_miss";
	}

	[[maybe_unused]] const char *StableTextStale()
	{
		return "event=settings_text_stale";
	}

	const char *MenuPageName(const int Page)
	{
		switch(Page)
		{
		case CMenus::PAGE_NEWS: return "news";
		case CMenus::PAGE_INTERNET: return "internet";
		case CMenus::PAGE_LAN: return "lan";
		case CMenus::PAGE_FAVORITES: return "favorites";
		case CMenus::PAGE_FAVORITE_COMMUNITY_1: return "favorite_community_1";
		case CMenus::PAGE_FAVORITE_COMMUNITY_2: return "favorite_community_2";
		case CMenus::PAGE_FAVORITE_COMMUNITY_3: return "favorite_community_3";
		case CMenus::PAGE_FAVORITE_COMMUNITY_4: return "favorite_community_4";
		case CMenus::PAGE_FAVORITE_COMMUNITY_5: return "favorite_community_5";
		case CMenus::PAGE_FAVORITE_MAPS: return "favorite_maps";
		case CMenus::PAGE_DEMOS: return "demos";
		case CMenus::PAGE_SETTINGS: return "settings";
		case CMenus::PAGE_STATS: return "stats";
		default: return "unknown";
		}
	}

	const char *GamePageName(const int Page)
	{
		switch(Page)
		{
		case CMenus::PAGE_GAME: return "game";
		case CMenus::PAGE_PLAYERS: return "players";
		case CMenus::PAGE_SERVER_INFO: return "server_info";
		case CMenus::PAGE_NETWORK: return "browser";
		case CMenus::PAGE_GHOST: return "ghost";
		case CMenus::PAGE_CALLVOTE: return "call_vote";
		case CMenus::PAGE_SETTINGS: return "settings";
		case CMenus::PAGE_DEMOS: return "demos";
		case CMenus::PAGE_UNFINISHED_MAPS: return "unfinished_maps";
		default: return "unknown";
		}
	}

	const char *ClientStateName(const IClient::EClientState State)
	{
		switch(State)
		{
		case IClient::STATE_OFFLINE: return "offline";
		case IClient::STATE_CONNECTING: return "connecting";
		case IClient::STATE_LOADING: return "loading";
		case IClient::STATE_ONLINE: return "online";
		case IClient::STATE_DEMOPLAYBACK: return "demoplayback";
		case IClient::STATE_QUITTING: return "quitting";
		case IClient::STATE_RESTARTING: return "restarting";
		default: return "unknown";
		}
	}
}

ColorRGBA CMenus::ms_GuiColor;
ColorRGBA CMenus::ms_ColorTabbarInactiveOutgame;
ColorRGBA CMenus::ms_ColorTabbarActiveOutgame;
ColorRGBA CMenus::ms_ColorTabbarHoverOutgame;
ColorRGBA CMenus::ms_ColorTabbarInactive;
ColorRGBA CMenus::ms_ColorTabbarActive = ColorRGBA(0, 0, 0, 0.5f);
ColorRGBA CMenus::ms_ColorTabbarHover;
ColorRGBA CMenus::ms_ColorTabbarInactiveIngame;
ColorRGBA CMenus::ms_ColorTabbarActiveIngame;
ColorRGBA CMenus::ms_ColorTabbarHoverIngame;

float CMenus::ms_ButtonHeight = 25.0f;
float CMenus::ms_ListheaderHeight = 17.0f;

CMenus::CMenus()
{
	m_Popup = POPUP_NONE;
	m_MenuPage = 0;
	m_GamePage = PAGE_GAME;

	m_NeedRestartGraphics = false;
	m_NeedRestartSound = false;
	m_NeedSendinfo = false;
	m_NeedSendDummyinfo = false;
	m_MenuActive = true;
	m_ShowStart = true;

	m_DemoBrowserSource = DEMO_BROWSER_SOURCE_DEMOS;
	ResetDemoBrowserFolder();

	m_DemoPlayerState = DEMOPLAYER_NONE;
	m_Dummy = false;

	for(SUIAnimator &Animator : m_aAnimatorsSettingsTab)
	{
		Animator.m_Active = false;
		Animator.m_ScaleLabel = false;
		Animator.m_YOffset = -2.5f;
		Animator.m_HOffset = 5.0f;
		Animator.m_WOffset = 5.0f;
		Animator.m_RepositionLabel = true;
		Animator.m_XOffset = 0.0f;
		Animator.m_Value = 0.0f;
		Animator.m_Time = std::chrono::nanoseconds::zero();
	}

	for(SUIAnimator &Animator : m_aAnimatorsBigPage)
	{
		Animator.m_Active = false;
		Animator.m_ScaleLabel = false;
		Animator.m_RepositionLabel = false;
		Animator.m_XOffset = 0.0f;
		Animator.m_YOffset = 0.0f;
		Animator.m_HOffset = 0.0f;
		Animator.m_WOffset = 0.0f;
		Animator.m_Value = 0.0f;
		Animator.m_Time = std::chrono::nanoseconds::zero();
	}

	for(SUIAnimator &Animator : m_aAnimatorsSmallPage)
	{
		Animator.m_Active = false;
		Animator.m_ScaleLabel = false;
		Animator.m_RepositionLabel = false;
		Animator.m_XOffset = 0.0f;
		Animator.m_YOffset = 0.0f;
		Animator.m_HOffset = 0.0f;
		Animator.m_WOffset = 0.0f;
		Animator.m_Value = 0.0f;
		Animator.m_Time = std::chrono::nanoseconds::zero();
	}

	m_PasswordInput.SetBuffer(g_Config.m_Password, sizeof(g_Config.m_Password));
	m_PasswordInput.SetHidden(true);
}

IUiContext CMenus::SettingsUiContext(const char *pScope, const float UiScale)
{
	m_SettingsUiTheme = ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(g_Config.m_QmUiFocusColor), ColorHSLA(g_Config.m_QmUiAccentColor), ColorHSLA(g_Config.m_QmUiSelectedColor));
	IUiContext Context;
	Context.m_pUi = Ui();
	Context.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	Context.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	Context.m_pIconManager = GameClient()->QmIconManager();
	Context.m_pMenus = this;
	Context.m_pTooltips = &GameClient()->m_Tooltips;
	Context.m_pTextRender = TextRender();
	Context.m_pTheme = &m_SettingsUiTheme;
	Context.m_ScopeHash = MakeUiScopeHash(pScope);
	Context.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	Context.m_UiScale = UiScale;
	if(Ui()->RenderOnly())
	{
		Context.m_pAnim = nullptr;
		Context.m_pTree = nullptr;
	}
	return Context;
}

int CMenus::DoSettingsDropDown(CUIRect *pRect, const int CurSelection, const char *const *ppStrs, const int Num, CUi::SDropDownState &State, CUi::SDropDownProperties Properties)
{
	// 所有设置页下拉框统一使用当前设置主题，调用点不得回退到旧的默认配色。
	Properties.m_VisualStyle = QmSettingsDropdownVisualStyle(m_SettingsUiTheme);
	// 锚点必须留在当前卡片内，弹层可以越过卡片但不能越过设置页 viewport。
	if(Properties.m_pAnchorViewport == nullptr)
		Properties.m_pAnchorViewport = Ui()->IsClipped() ? Ui()->ClipArea() : Ui()->Screen();
	if(Properties.m_pPopupViewport == nullptr)
		Properties.m_pPopupViewport = Ui()->OutermostClipArea();
	return Ui()->DoDropDown(pRect, CurSelection, ppStrs, Num, State, Properties);
}

SCardMotionSpec CMenus::SettingsCardMotionSpec() const
{
	return ResolveCardMotionSpec(
		g_Config.m_QmUiMotionLevel,
		g_Config.m_QmUiListEntryAnimations != 0,
		g_Config.m_QmUiCardHeightAnimations != 0,
		g_Config.m_QmUiCardReflowAnimations != 0,
		g_Config.m_QmExtraAnimations != 0);
}

SSettingsCardDeckVisualOptions CMenus::SettingsCardDeckVisualOptions() const
{
	SSettingsCardDeckVisualOptions Options;
	Options.m_RainbowTitles = g_Config.m_QmUiCardRainbowTitles != 0;
	Options.m_AlwaysShowBorders = g_Config.m_QmUiCardBorders != 0;
	Options.m_BorderColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiCardBorderColor, true));
	const ColorRGBA CardColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiCardColor).UnclampLighting(0.42f));
	Options.m_SurfaceColor = CardColor.WithAlpha(std::clamp(g_Config.m_QmUiCardOpacity / 100.0f, 0.0f, 1.0f));
	Options.m_UseSurfaceColor = true;
	return Options;
}

qm_card_order::CModel &CMenus::SettingsCardOrderModel()
{
	if(!m_SettingsCardOrderLoaded)
		LoadSettingsCardOrderModel();
	return m_SettingsCardOrderModel;
}

qm_card_order::CModel &CMenus::SettingsCardOrderModelForRenderPass()
{
	if(!Ui()->RenderOnly())
		return SettingsCardOrderModel();
	if(!m_SettingsCardRenderOnlyOrderInitialized || m_SettingsCardRenderOnlyOrderSource != g_Config.m_QmGlobalCardOrder)
	{
		m_SettingsCardRenderOnlyOrderModel.LoadMerged(g_Config.m_QmGlobalCardOrder, qm_card_registry::BuildDefaultEntries());
		m_SettingsCardRenderOnlyOrderSource = g_Config.m_QmGlobalCardOrder;
		m_SettingsCardRenderOnlyOrderInitialized = true;
	}
	return m_SettingsCardRenderOnlyOrderModel;
}

CSettingsCardDeck &CMenus::SettingsCardDeckForRenderPass()
{
	return Ui()->RenderOnly() ? m_SettingsCardRenderOnlyDeck : m_SettingsCardDeck;
}

void CMenus::LoadSettingsCardOrderModel()
{
	if(m_SettingsCardOrderLoaded)
		return;

	const std::vector<qm_card_order::SEntry> Defaults = qm_card_registry::BuildDefaultEntries();
	m_SettingsCardOrderModel.LoadMerged(g_Config.m_QmGlobalCardOrder, Defaults);
	const auto CopyModelEntries = [](const qm_card_order::CModel &Source) {
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(Source.Count());
		for(int EntryIndex = 0; EntryIndex < Source.Count(); ++EntryIndex)
			vEntries.push_back(Source.Entry(EntryIndex));
		return vEntries;
	};
	const auto MakeCandidate = [&](qm_card_order::CModel &Candidate) {
		Candidate.SetEntries(CopyModelEntries(m_SettingsCardOrderModel));
		if(!m_SettingsCardOrderModel.IsDirty())
			Candidate.ClearDirty();
	};
	const auto PersistCandidate = [&](const qm_card_order::CModel &Candidate, const bool CandidateChanged, const bool ForcePersist = false) {
		if(!ForcePersist && !CandidateChanged && !m_SettingsCardOrderModel.IsDirty())
			return true;
		char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)];
		if(!Candidate.Serialize(aSerialized, sizeof(aSerialized)))
			return false;
		str_copy(g_Config.m_QmGlobalCardOrder, aSerialized, sizeof(g_Config.m_QmGlobalCardOrder));
		if(CandidateChanged)
			m_SettingsCardOrderModel.SetEntries(CopyModelEntries(Candidate));
		m_SettingsCardOrderModel.ClearDirty();
		return true;
	};
	const auto PersistCurrentLayout = [&]() {
		char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)];
		if(!m_SettingsCardOrderModel.Serialize(aSerialized, sizeof(aSerialized)))
			return false;
		str_copy(g_Config.m_QmGlobalCardOrder, aSerialized, sizeof(g_Config.m_QmGlobalCardOrder));
		m_SettingsCardOrderModel.ClearDirty();
		return true;
	};
	const auto PersistAndAdvanceLayoutVersion = [&](const int Version, const bool ForcePersist = false) {
		if((ForcePersist || m_SettingsCardOrderModel.IsDirty()) && !PersistCurrentLayout())
			return false;
		g_Config.m_QmCardLayoutVersion = Version;
		return true;
	};
	if(g_Config.m_QmGlobalCardOrder[0] == '\0' && g_Config.m_QmCardOrderMigrated == 0)
	{
		qm_card_order::CModel Candidate;
		MakeCandidate(Candidate);
		const bool QmLayoutChanged = qm_module::LoadLegacyQmLayoutIntoModel(Candidate, g_Config.m_QmSidebarCardOrder);
		const bool TClientLayoutChanged = qm_module::LoadLegacyTClientLayoutIntoModel(Candidate, g_Config.m_QmSettingsCardOrder);
		const bool CandidateChanged = QmLayoutChanged || TClientLayoutChanged;
		if(!PersistCandidate(Candidate, CandidateChanged, true))
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		g_Config.m_QmCardOrderMigrated = 1;
	}
	if(g_Config.m_QmCardLayoutVersion < 1)
	{
		qm_card_order::CModel Candidate;
		MakeCandidate(Candidate);
		const auto IsAtOldDefault = [&](const char *pStableId, const char *pTab, int OldColumn, int OldOrder) {
			const int Index = Candidate.FindByStableId(pStableId);
			if(Index < 0)
				return false;
			const qm_card_order::SEntry &Entry = Candidate.Entry(Index);
			return str_comp(Entry.m_pDefaultTab, pTab) == 0 && Entry.m_Column == OldColumn && Entry.m_OrderInColumn == OldOrder;
		};
		bool CandidateChanged = false;
		const std::vector<const char *> vContributorIds = {
			"deck:qmclient-contributors-community",
			"deck:qmclient-contributors-sponsors",
		};
		const bool ContributorsStillOldDefault =
			IsAtOldDefault("deck:qmclient-contributors-community", "qmclient-contributors", 0, 0) &&
			IsAtOldDefault("deck:qmclient-contributors-sponsors", "qmclient-contributors", 0, 1) &&
			qm_card_order::TabContainsOnlyStableIds(Candidate, "qmclient-contributors", vContributorIds);
		if(ContributorsStillOldDefault)
		{
			Candidate.MoveToTab("deck:qmclient-contributors-sponsors", "qmclient-contributors", 2, 0);
			Candidate.MoveToTab("deck:qmclient-contributors-community", "qmclient-contributors", 1, 0);
			CandidateChanged = true;
		}
		const std::vector<const char *> vBindWheelIds = {
			"deck:tclient-bind-wheel-editor",
			"deck:tclient-bind-wheel-preview",
		};
		const bool BindWheelStillOldDefault =
			IsAtOldDefault("deck:tclient-bind-wheel-editor", "tclient-bind-wheel", 1, 0) &&
			IsAtOldDefault("deck:tclient-bind-wheel-preview", "tclient-bind-wheel", 1, 1) &&
			qm_card_order::TabContainsOnlyStableIds(Candidate, "tclient-bind-wheel", vBindWheelIds);
		if(BindWheelStillOldDefault)
		{
			Candidate.MoveToTab("deck:tclient-bind-wheel-preview", "tclient-bind-wheel", 2, 0);
			CandidateChanged = true;
		}
		if(!PersistCandidate(Candidate, CandidateChanged))
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		g_Config.m_QmCardLayoutVersion = 1;
	}
	if(g_Config.m_QmCardLayoutVersion < 2)
	{
		qm_card_order::CModel Candidate;
		MakeCandidate(Candidate);
		const std::vector<qm_card_order::SEntry> vLegacyProfileLayout = {
			{"deck:tclient-profiles-actions", "tclient-profiles", 0, 0},
			{"deck:tclient-profiles-options", "tclient-profiles", 2, 0},
			{"deck:tclient-profiles-list", "tclient-profiles", 1, 0},
		};
		const std::vector<qm_card_order::SEntry> vTargetProfileLayout = {
			{"deck:tclient-profiles-actions", "tclient-profiles", 1, 0},
			{"deck:tclient-profiles-options", "tclient-profiles", 2, 0},
			{"deck:tclient-profiles-list", "tclient-profiles", 1, 1},
		};
		const std::vector<const char *> vProfileIds = {
			"deck:tclient-profiles-actions",
			"deck:tclient-profiles-options",
			"deck:tclient-profiles-list",
		};
		const std::vector<const char *> vRequiredProfileIds = {"deck:tclient-profiles-actions"};
		const qm_card_order::EExplicitLayoutStatus ExplicitStatus = qm_card_order::ClassifyExplicitLayout(g_Config.m_QmGlobalCardOrder, vLegacyProfileLayout, vRequiredProfileIds);
		if(ExplicitStatus == qm_card_order::EExplicitLayoutStatus::INVALID)
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		const bool ShouldMigrate = ExplicitStatus == qm_card_order::EExplicitLayoutStatus::MATCH;
		const bool CandidateChanged = ShouldMigrate && qm_card_order::MigrateExactLayout(Candidate, "tclient-profiles", vLegacyProfileLayout, vTargetProfileLayout, vProfileIds);
		if(ShouldMigrate && !CandidateChanged)
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		if(!PersistCandidate(Candidate, CandidateChanged))
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		g_Config.m_QmCardLayoutVersion = 2;
	}
	if(g_Config.m_QmCardLayoutVersion < 3)
	{
		qm_card_order::CModel Candidate;
		MakeCandidate(Candidate);
		const std::vector<qm_card_order::SEntry> vLegacyStatusBarDefaults = {
			{"deck:tclient-status-bar-settings", "tclient-status-bar", 1, 0},
			{"deck:tclient-status-bar-items", "tclient-status-bar", 1, 1},
			{"deck:tclient-status-bar-preview", "tclient-status-bar", 1, 2},
		};
		const std::vector<const char *> vStatusBarIds = {
			"deck:tclient-status-bar-settings",
			"deck:tclient-status-bar-items",
			"deck:tclient-status-bar-preview",
		};
		const std::vector<qm_card_order::SEntry> vTargetStatusBarLayout = {
			{"deck:tclient-status-bar-settings", "tclient-status-bar", 1, 0},
			{"deck:tclient-status-bar-items", "tclient-status-bar", 2, 0},
			{"deck:tclient-status-bar-preview", "tclient-status-bar", 1, 1},
		};
		const qm_card_order::EExplicitLayoutStatus ExplicitStatus = qm_card_order::ClassifyExplicitLayout(g_Config.m_QmGlobalCardOrder, vLegacyStatusBarDefaults, vStatusBarIds);
		if(ExplicitStatus == qm_card_order::EExplicitLayoutStatus::INVALID)
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		const bool ShouldMigrate = ExplicitStatus == qm_card_order::EExplicitLayoutStatus::MATCH;
		const bool CandidateChanged = ShouldMigrate && qm_card_order::MigrateExactLayout(Candidate, "tclient-status-bar", vLegacyStatusBarDefaults, vTargetStatusBarLayout, vStatusBarIds);
		if(ShouldMigrate && !CandidateChanged)
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		if(!PersistCandidate(Candidate, CandidateChanged))
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		g_Config.m_QmCardLayoutVersion = 3;
	}
	if(g_Config.m_QmCardLayoutVersion < 4)
	{
		qm_card_order::CModel Candidate;
		MakeCandidate(Candidate);
		const std::vector<qm_card_order::SEntry> vLegacyTeeDefaults = {
			{"deck:tee-identity", "tee", 0, 0},
			{"deck:tee-skin-options", "tee", 1, 0},
			{"deck:tee-skin-list", "tee", 2, 0},
		};
		const std::vector<const char *> vTeeIds = {
			"deck:tee-identity",
			"deck:tee-skin-options",
			"deck:tee-skin-list",
		};
		const std::vector<qm_card_order::SEntry> vTargetTeeLayout = {
			{"deck:tee-identity", "tee", 1, 0},
			{"deck:tee-skin-options", "tee", 2, 0},
			{"deck:tee-skin-list", "tee", 0, 0},
		};
		const qm_card_order::EExplicitLayoutStatus ExplicitStatus = qm_card_order::ClassifyExplicitLayout(g_Config.m_QmGlobalCardOrder, vLegacyTeeDefaults, vTeeIds);
		if(ExplicitStatus == qm_card_order::EExplicitLayoutStatus::INVALID)
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		const bool ShouldMigrate = ExplicitStatus == qm_card_order::EExplicitLayoutStatus::MATCH;
		const bool CandidateChanged = ShouldMigrate && qm_card_order::MigrateExactLayout(Candidate, "tee", vLegacyTeeDefaults, vTargetTeeLayout, vTeeIds);
		if(ShouldMigrate && !CandidateChanged)
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		if(!PersistCandidate(Candidate, CandidateChanged))
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
		g_Config.m_QmCardLayoutVersion = 4;
	}
	if(g_Config.m_QmCardLayoutVersion < 5)
	{
		// 敌对列表从五张独立卡收敛为一个完整编辑器；LoadMerged 已按新 registry
		// 清理旧 stable ID，这里把规范化后的结果写回，避免每次启动重复迁移。
		if(!PersistAndAdvanceLayoutVersion(5, true))
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
	}
	if(g_Config.m_QmCardLayoutVersion < 6)
	{
		char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)];
		const qm_card_registry::ETClientMainCardsMigrationResult MigrationResult = qm_card_registry::MigrateTClientMainCardsToAlternatingColumns(m_SettingsCardOrderModel, aSerialized, sizeof(aSerialized));
		const qm_card_registry::STClientMainCardsMigrationCommitPlan CommitPlan = qm_card_registry::TClientMainCardsMigrationCommitPlan(MigrationResult);
		if(CommitPlan.m_PersistSerialized)
		{
			str_copy(g_Config.m_QmGlobalCardOrder, aSerialized, sizeof(g_Config.m_QmGlobalCardOrder));
			m_SettingsCardOrderModel.ClearDirty();
		}
		if(CommitPlan.m_AdvanceVersion)
			g_Config.m_QmCardLayoutVersion = 6;
	}
	if(g_Config.m_QmCardLayoutVersion < 7)
	{
		// 状态栏代码已并入预览卡。LoadMerged 会按当前 registry 移除旧 stable ID，
		// 强制写回使旧布局不会在后续启动时反复参与合并。
		if(!PersistAndAdvanceLayoutVersion(7, true))
		{
			m_SettingsCardOrderLoaded = true;
			return;
		}
	}
	m_SettingsCardOrderLoaded = true;
}

bool CMenus::SaveSettingsCardOrderModel()
{
	if(!m_SettingsCardOrderLoaded || !m_SettingsCardOrderModel.IsDirty())
		return false;
	char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)];
	if(!m_SettingsCardOrderModel.Serialize(aSerialized, sizeof(aSerialized)))
		return false;
	str_copy(g_Config.m_QmGlobalCardOrder, aSerialized, sizeof(g_Config.m_QmGlobalCardOrder));
	m_SettingsCardOrderModel.ClearDirty();
	return true;
}

uint64_t CMenus::UiAnimNodeKey(const char *pScope, const uint64_t Id) const
{
	const uint64_t ScopeHash = static_cast<uint64_t>(str_quickhash(pScope));
	return BuildUiAnimNodeKey(ScopeHash, Id);
}

void CMenus::TriggerUiSwitchAnimation(const uint64_t NodeKey, const float DurationSec)
{
	if(Ui()->RenderOnly())
		return;

	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_X, 1.0f);

	SUiAnimRequest Request;
	Request.m_NodeKey = NodeKey;
	Request.m_Property = EUiAnimProperty::POS_X;
	Request.m_Target = 0.0f;
	Request.m_Transition.m_DurationSec = DurationSec;
	Request.m_Transition.m_DelaySec = 0.0f;
	Request.m_Transition.m_Priority = 1;
	Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	Request.m_Transition.m_Easing = EEasing::EASE_OUT;
	AnimRuntime.RequestAnimation(Request);
}

float CMenus::ReadUiSwitchAnimation(const uint64_t NodeKey) const
{
	if(Ui()->RenderOnly())
		return 0.0f;

	const CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	return std::clamp(AnimRuntime.GetValue(NodeKey, EUiAnimProperty::POS_X, 0.0f), 0.0f, 1.0f);
}

float CMenus::UiSwitchAnimationAlpha(const float Strength) const
{
	return Strength * MENU_SWITCH_ALPHA_MAX;
}

void CMenus::DrawUiSwitchTransitionOverlay(const CUIRect &Rect, const ColorRGBA Color)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	Rect.Draw(Color, IGraphics::CORNER_NONE, 0.0f);
}

float CMenus::ApplyUiSwitchOffset(CUIRect &View, const float Strength, const float Direction, const bool Vertical, const float RelativeOffset, const float MinOffset, const float MaxOffset) const
{
	if(Strength <= 0.0f || Direction == 0.0f)
		return 0.0f;

	const float AxisSize = Vertical ? View.h : View.w;
	const float Offset = Strength * std::clamp(AxisSize * RelativeOffset, MinOffset, MaxOffset) * Direction;
	if(Vertical)
		View.y += Offset;
	else
		View.x += Offset;
	return Offset;
}

float CMenus::ResolveMenuTabAnimationValue(const void *pButtonId, const bool Active, const float DurationSec) const
{
	const float Target = Active ? 1.0f : 0.0f;
	if(Ui()->RenderOnly())
		return Target;

	static const uint64_t s_ScopeHash = static_cast<uint64_t>(str_quickhash("menu_tab_hover"));
	const uint64_t NodeKey = BuildUiAnimNodeKey(s_ScopeHash, reinterpret_cast<uint64_t>(pButtonId));
	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	return std::clamp(ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::SCALE, Target, DurationSec, EEasing::EASE_OUT), 0.0f, 1.0f);
}

ColorRGBA CMenus::MenuPanelColor(float AlphaScale) const
{
	const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
	return Base.WithAlpha(std::clamp((g_Config.m_QmUiOpacity / 100.0f) * AlphaScale, 0.0f, 1.0f));
}

ColorRGBA CMenus::MenuPanelElevatedColor(float AlphaScale) const
{
	const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
	return Base.WithAlpha(std::clamp((g_Config.m_QmUiOpacity / 100.0f) * AlphaScale, 0.0f, 1.0f));
}

ColorRGBA CMenus::BrowserPanelColor(float AlphaScale) const
{
	const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmMapBrowserColor));
	return Base.WithAlpha(std::clamp((g_Config.m_QmMapBrowserOpacity / 100.0f) * AlphaScale, 0.0f, 1.0f));
}

ColorRGBA CMenus::BrowserPanelElevatedColor(float AlphaScale) const
{
	const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmMapBrowserColor));
	return Base.WithAlpha(std::clamp((g_Config.m_QmMapBrowserOpacity / 100.0f) * AlphaScale, 0.0f, 1.0f));
}

ColorRGBA CMenus::SettingsTabbarColor(float AlphaScale) const
{
	const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiColor));
	return Base.WithAlpha(std::clamp((g_Config.m_QmUiOpacity / 100.0f) * AlphaScale, 0.0f, 1.0f));
}

int CMenus::DoButton_Toggle(const void *pId, int Checked, const CUIRect *pRect, bool Active, const unsigned Flags)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	const float HoverTarget = Active && Ui()->HotItem() == pId ? 1.0f : 0.0f;
	float HoverAlpha = HoverTarget;
	if(!Ui()->RenderOnly())
	{
		static const uint64_t s_ScopeHash = static_cast<uint64_t>(str_quickhash("menu_toggle_hover"));
		const uint64_t NodeKey = BuildUiAnimNodeKey(s_ScopeHash, reinterpret_cast<uint64_t>(pId));
		CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
		HoverAlpha = std::clamp(ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::ALPHA, HoverTarget, 0.10f, EEasing::EASE_OUT), 0.0f, 1.0f);
	}

	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_GUIBUTTONS].m_Id);
	Graphics()->QuadsBegin();
	if(!Active)
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.5f);
	Graphics()->SelectSprite(Checked ? SPRITE_GUIBUTTON_ON : SPRITE_GUIBUTTON_OFF);
	IGraphics::CQuadItem QuadItem(pRect->x, pRect->y, pRect->w, pRect->h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	if(Active && HoverAlpha > MENU_TAB_ANIM_EPSILON)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, HoverAlpha);
		Graphics()->SelectSprite(SPRITE_GUIBUTTON_HOVER);
		QuadItem = IGraphics::CQuadItem(pRect->x, pRect->y, pRect->w, pRect->h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
	}
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->QuadsEnd();

	return Active ? Ui()->DoButtonLogic(pId, Checked, pRect, Flags) : 0;
}

int CMenus::DoButton_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, const unsigned Flags, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color, CUIElement *pTextUiElement, float TextFontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	CUIRect Text = *pRect;
	const bool MouseInside = Ui()->HotItem() == pButtonContainer;
	const bool Pressed = Ui()->CheckActiveItem(pButtonContainer);
	const float HoverTarget = Checked || MouseInside || Pressed ? 1.0f : 0.0f;
	float HoverStrength = HoverTarget;
	if(!Ui()->RenderOnly())
	{
		static const uint64_t s_ScopeHash = static_cast<uint64_t>(str_quickhash("menu_button_hover"));
		const uint64_t NodeKey = BuildUiAnimNodeKey(s_ScopeHash, reinterpret_cast<uint64_t>(pButtonContainer));
		CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
		HoverStrength = std::clamp(ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::ALPHA, HoverTarget, 0.11f, EEasing::EASE_OUT), 0.0f, 1.0f);
	}
	const float HoverLift = -1.25f * HoverStrength;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	else // TClient, why was this not here? ig they never use "checked" anywhere important
		Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	DrawRoundedSurface(Ui(), *pRect, Color, ColorRGBA(), Rounding, 0.0f, Corners);
	if(HoverStrength > MENU_TAB_ANIM_EPSILON)
	{
		const float OverlayAlpha = (Checked ? 0.05f : 0.08f) * HoverStrength;
		DrawRoundedSurface(Ui(), *pRect, ColorRGBA(1.0f, 1.0f, 1.0f, OverlayAlpha), ColorRGBA(), Rounding, 0.0f, Corners);
	}

	if(pImageName)
	{
		CUIRect Image;
		pRect->VSplitRight(pRect->h * 4.0f, &Text, &Image); // always correct ratio for image
		Image.y += HoverLift;

		// render image
		const CMenuImage *pImage = FindMenuImage(pImageName);
		if(pImage)
		{
			Graphics()->TextureSet(Ui()->HotItem() == pButtonContainer ? pImage->m_OrgTexture : pImage->m_GreyTexture);
			Graphics()->WrapClamp();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Image.x, Image.y, Image.w, Image.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
	}

	Text = MenuButtonTextRect(&Text, FontFactor, HoverLift);
	if(pText != nullptr && pText[0] != '\0')
	{
		const float ResolvedTextFontSize = TextFontSize > 0.0f ? std::min(TextFontSize, Text.h * CUi::ms_FontmodHeight) : Text.h * CUi::ms_FontmodHeight;
		if(pTextUiElement != nullptr)
			DoSettingsLabelStreamed(*pTextUiElement, &Text, pText, ResolvedTextFontSize, TEXTALIGN_MC);
		else
			Ui()->DoLabel(&Text, pText, ResolvedTextFontSize, TEXTALIGN_MC);
	}

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, Flags);
}

int CMenus::DoButton_MenuTab(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator, const ColorRGBA *pDefaultColor, const ColorRGBA *pActiveColor, const ColorRGBA *pHoverColor, float EdgeRounding, const CCommunityIcon *pCommunityIcon, CUIElement *pTextUiElement, float FontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	const bool MouseInside = Ui()->HotItem() == pButtonContainer;
	CUIRect AnimatedLabelRect = *pRect;
	const bool TabActive = Checked || MouseInside;
	const float AnimValue = ResolveMenuTabAnimationValue(pButtonContainer, TabActive, MENU_TAB_HOVER_DURATION);

	float XOffset = MENU_TAB_DEFAULT_X_OFFSET;
	float YOffset = MENU_TAB_DEFAULT_Y_OFFSET;
	float WOffset = MENU_TAB_DEFAULT_W_OFFSET;
	float HOffset = MENU_TAB_DEFAULT_H_OFFSET;
	bool RepositionLabel = false;
	bool ScaleLabel = false;
	if(pAnimator != nullptr)
	{
		XOffset = pAnimator->m_XOffset;
		YOffset = pAnimator->m_YOffset;
		WOffset = pAnimator->m_WOffset;
		HOffset = pAnimator->m_HOffset;
		RepositionLabel = pAnimator->m_RepositionLabel;
		ScaleLabel = pAnimator->m_ScaleLabel;
	}
	AnimatedLabelRect.w += AnimValue * WOffset;
	AnimatedLabelRect.h += AnimValue * HOffset;
	AnimatedLabelRect.x += AnimValue * XOffset;
	AnimatedLabelRect.y += AnimValue * YOffset;

	if(Checked)
	{
		ColorRGBA ColorMenuTab = ms_ColorTabbarActive;
		if(pActiveColor)
			ColorMenuTab = *pActiveColor;
		else if(g_Config.m_QmNewUi)
			ColorMenuTab = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.42f);

		DrawRoundedSurface(Ui(), *pRect, ColorMenuTab, ColorRGBA(), EdgeRounding, 0.0f, Corners);
	}
	else
	{
		if(MouseInside)
		{
			ColorRGBA HoverColorMenuTab = ms_ColorTabbarHover;
			if(pHoverColor)
				HoverColorMenuTab = *pHoverColor;
			else if(g_Config.m_QmNewUi)
				HoverColorMenuTab = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.20f);

			DrawRoundedSurface(Ui(), *pRect, HoverColorMenuTab, ColorRGBA(), EdgeRounding, 0.0f, Corners);
		}
		else
		{
			ColorRGBA ColorMenuTab = ms_ColorTabbarInactive;
			if(pDefaultColor)
				ColorMenuTab = *pDefaultColor;

			DrawRoundedSurface(Ui(), *pRect, ColorMenuTab, ColorRGBA(), EdgeRounding, 0.0f, Corners);
		}
	}

	if(pAnimator != nullptr)
	{
		if(RepositionLabel)
		{
			AnimatedLabelRect.x += AnimatedLabelRect.w - pRect->w + AnimatedLabelRect.x - pRect->x;
			AnimatedLabelRect.y += AnimatedLabelRect.h - pRect->h + AnimatedLabelRect.y - pRect->y;
		}

		if(!ScaleLabel)
		{
			AnimatedLabelRect.w = pRect->w;
			AnimatedLabelRect.h = pRect->h;
		}
	}

	// Keep tab contents clipped horizontally to the original button rect.
	// Extend clipping vertically to include the animated rect so "float up"
	// animations don't cut off icon/text.
	CUIRect ContentClip = *pRect;
	const float ClipTop = minimum(pRect->y, AnimatedLabelRect.y);
	const float ClipBottom = maximum(pRect->y + pRect->h, AnimatedLabelRect.y + AnimatedLabelRect.h);
	ContentClip.y = ClipTop;
	ContentClip.h = ClipBottom - ClipTop;
	Ui()->ClipEnable(&ContentClip);

	if(pCommunityIcon)
	{
		CUIRect CommunityIcon;
		CUIRect StaticIconRect = *pRect;
		StaticIconRect.Margin(2.0f, &CommunityIcon);
		m_CommunityIcons.Render(pCommunityIcon, CommunityIcon, true);
	}
	else
	{
		CUIRect Label;
		AnimatedLabelRect.HMargin(2.0f, &Label);
		const bool FixedFontSize = FontSize > 0.0f;
		FontSize = FixedFontSize ? FontSize : Label.h * CUi::ms_FontmodHeight;
		SLabelProperties Props;
		if(FixedFontSize)
		{
			Props.m_MaxWidth = Label.w;
			Props.m_MinimumFontSize = FontSize;
			Props.m_EllipsisAtEnd = true;
		}
		if(pTextUiElement != nullptr && pTextUiElement->AreRectsInit())
			Ui()->DoLabelStreamed(*pTextUiElement->Rect(0), &Label, pText, FontSize, TEXTALIGN_MC, Props);
		else
			Ui()->DoLabel(&Label, pText, FontSize, TEXTALIGN_MC, Props);
	}
	Ui()->ClipDisable();

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::InitSettingsTabLabelCache()
{
	if(m_SettingsTabLabelElementsInit)
		return;

	for(CUIElement &LabelElement : m_aSettingsTabLabelElements)
		LabelElement.Init(Ui(), 1);
	m_SettingsTabLabelElementsInit = true;
}

void CMenus::UpdateSettingsTabLabels()
{
	const bool Sixup = Client()->IsSixup();
	if(m_SettingsTabLabelsInit && str_comp(m_aSettingsTabLanguageFile, g_Config.m_ClLanguagefile) == 0 && m_SettingsTabSixup == Sixup)
		return;

	str_copy(m_aSettingsTabLanguageFile, g_Config.m_ClLanguagefile, sizeof(m_aSettingsTabLanguageFile));
	m_SettingsTabSixup = Sixup;
	m_SettingsTabLabelsInit = true;

	m_apSettingsTabs[SETTINGS_LANGUAGE] = Localize("Language");
	m_apSettingsTabs[SETTINGS_GENERAL] = Localize("General");
	m_apSettingsTabs[SETTINGS_PLAYER] = Localize("Player");
	m_apSettingsTabs[SETTINGS_TEE] = Sixup ? Localize("Tee 0.7") : Localize("Tee");
	m_apSettingsTabs[SETTINGS_APPEARANCE] = Localize("Appearance");
	m_apSettingsTabs[SETTINGS_CONTROLS] = Localize("Controls");
	m_apSettingsTabs[SETTINGS_GRAPHICS] = Localize("Graphics");
	m_apSettingsTabs[SETTINGS_SOUND] = Localize("Sound");
	m_apSettingsTabs[SETTINGS_DDNET] = Localize("DDNet");
	m_apSettingsTabs[SETTINGS_ASSETS] = Localize("Assets");
	m_apSettingsTabs[SETTINGS_TCLIENT] = Localize("TClient");
	m_apSettingsTabs[SETTINGS_QMCLIENT] = Localize("QmClient");
	m_apSettingsTabs[SETTINGS_SEARCH] = Localize("Search");
	m_apSettingsTabs[SETTINGS_PROFILES] = Localize("Profiles");
	m_apSettingsTabs[SETTINGS_CONFIGS] = Localize("Configs");
	m_apSettingsTabs[SETTINGS_CONTRIBUTORS] = Localize("Contributors");

	if(m_SettingsTabLabelElementsInit)
	{
		for(CUIElement &LabelElement : m_aSettingsTabLabelElements)
			Ui()->ResetUIElement(LabelElement);
	}
}

void CMenus::PrepareSettingsTabLabelCache(float MainViewWidth, float TabBarWidthOverride)
{
	InitSettingsTabLabelCache();
	UpdateSettingsTabLabels();

	const float TabBarWidth = TabBarWidthOverride > 0.0f ? TabBarWidthOverride : std::clamp(MainViewWidth * 0.14f, 108.0f, 120.0f);
	CUIRect Button;
	Button.x = 0.0f;
	Button.y = 0.0f;
	Button.w = TabBarWidth;
	Button.h = ui_token::settings::TAB_HEIGHT;

	CUIRect Label;
	Button.HMargin(2.0f, &Label);
	const float FontSize = ui_token::settings::TAB_FONT_SIZE;

	for(int i = 0; i < SETTINGS_LENGTH; i++)
	{
		if(!SettingsPageVisibleInRightTabBar(i))
			continue;

		CUIElement::SUIElementRect &RectEl = *m_aSettingsTabLabelElements[i].Rect(0);
		const char *pText = m_apSettingsTabs[i];
		const bool ColorChanged = RectEl.m_TextColor != TextRender()->GetTextColor() || RectEl.m_TextOutlineColor != TextRender()->GetTextOutlineColor();
		const bool TextChanged = str_comp(RectEl.m_Text.c_str(), pText) != 0;
		const bool SizeChanged = RectEl.m_Width != Label.w || RectEl.m_Height != Label.h;
		if(RectEl.m_UITextContainer.Valid() && !ColorChanged && !TextChanged && !SizeChanged)
			continue;

		if(!SettingsWarmupConsumeBudget(m_SettingsFrameBudget, ESettingsWarmupCost::TEXT_CONTAINER))
			return;
		TextRender()->DeleteTextContainer(RectEl.m_UITextContainer);
		RectEl.m_X = Label.x;
		RectEl.m_Y = Label.y;
		RectEl.m_Width = Label.w;
		RectEl.m_Height = Label.h;
		RectEl.m_Text = pText;
		RectEl.m_ReadCursorGlyphCount = -1;

		CUIRect TmpRect;
		TmpRect.x = 0.0f;
		TmpRect.y = 0.0f;
		TmpRect.w = Label.w;
		TmpRect.h = Label.h;
		Ui()->DoLabel(RectEl, &TmpRect, pText, FontSize, TEXTALIGN_TL);
	}
}

int CMenus::DoButton_GridHeader(const void *pId, const char *pText, int Checked, const CUIRect *pRect, int Align)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	if(Checked == 2)
		DrawRoundedSurface(Ui(), *pRect, ColorRGBA(1, 0.98f, 0.5f, 0.55f), ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_T);
	else if(Checked)
		DrawRoundedSurface(Ui(), *pRect, ColorRGBA(1, 1, 1, 0.5f), ColorRGBA(), 5.0f, 0.0f, IGraphics::CORNER_T);

	CUIRect Temp;
	pRect->VMargin(5.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, pRect->h * CUi::ms_FontmodHeight, Align);
	return Ui()->DoButtonLogic(pId, Checked, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_Favorite(const void *pButtonId, const void *pParentId, bool Checked, const CUIRect *pRect)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	const bool ShouldShow = Checked || (pParentId != nullptr && Ui()->HotItem() == pParentId) || Ui()->HotItem() == pButtonId;
	static const uint64_t s_VisibilityScopeHash = static_cast<uint64_t>(str_quickhash("menu_favorite_visibility"));
	static const uint64_t s_HoverScopeHash = static_cast<uint64_t>(str_quickhash("menu_favorite_hover"));
	const uint64_t VisibilityNodeKey = BuildUiAnimNodeKey(s_VisibilityScopeHash, reinterpret_cast<uint64_t>(pButtonId));
	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	CUiV2Tree &Tree = GameClient()->UiRuntimeV2()->Tree();
	SUiAnimTransition VisibilityTransition;
	VisibilityTransition.m_DurationSec = 0.12f;
	VisibilityTransition.m_Easing = EEasing::EASE_OUT;
	const SUiPresenceResult Visibility = Tree.ResolvePresence(AnimRuntime, VisibilityNodeKey, ShouldShow, VisibilityTransition);
	const float ShowAlpha = std::clamp(Visibility.m_Alpha, 0.0f, 1.0f);
	if(Visibility.m_Render && ShowAlpha > MENU_TAB_ANIM_EPSILON)
	{
		const uint64_t HoverNodeKey = BuildUiAnimNodeKey(s_HoverScopeHash, reinterpret_cast<uint64_t>(pButtonId));
		const float HoverStrength = std::clamp(ResolveUiAnimValue(AnimRuntime, HoverNodeKey, EUiAnimProperty::SCALE, Ui()->HotItem() == pButtonId ? 1.0f : 0.0f, 0.10f, EEasing::EASE_OUT), 0.0f, 1.0f);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		const float BaseAlpha = std::clamp((Checked ? 0.8f : 0.65f) + 0.2f * HoverStrength, 0.0f, 1.0f);
		TextRender()->TextColor(Checked ? ColorRGBA(1.0f, 0.85f, 0.3f, BaseAlpha * ShowAlpha) : ColorRGBA(0.5f, 0.5f, 0.5f, BaseAlpha * ShowAlpha));
		SLabelProperties Props;
		Props.m_MaxWidth = pRect->w;
		Ui()->DoLabel(pRect, FONT_ICON_STAR, 12.0f + HoverStrength, TEXTALIGN_MC, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	return Ui()->DoButtonLogic(pButtonId, 0, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_CheckBox_Common(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, const unsigned Flags, const bool ProcessInput)
{
	return DoButton_CheckBox_Common_WithLabelElement(pId, pText, pBoxText, pRect, Flags, nullptr, ProcessInput);
}

void CMenus::SplitSettingsScrollbarRects(const CUIRect &Rect, unsigned Flags, CUIRect *pLabelRect, CUIRect *pValueRect, CUIRect *pScrollBarRect) const
{
	const bool MultiLine = Flags & CUi::SCROLLBAR_OPTION_MULTILINE;
	CUIRect Label, ValueText, ScrollBar;
	if(MultiLine)
	{
		Rect.HSplitMid(&Label, &ScrollBar);
		ValueText = Label;
		if(pValueRect != nullptr || pLabelRect != nullptr)
			Label.VSplitLeft(Label.w * 0.68f, &Label, &ValueText);
	}
	else
	{
		const float ValueWidth = std::clamp(Rect.w * 0.12f, 42.0f, 68.0f);
		const float LabelWidth = std::clamp(Rect.w * 0.25f, 108.0f, 180.0f);
		CUIRect Controls;
		Rect.VSplitLeft(LabelWidth, &Label, &Controls);
		Controls.VSplitRight(ValueWidth, &ScrollBar, &ValueText);
		ScrollBar.VMargin(minimum(10.0f, Rect.w * 0.025f), &ScrollBar);
	}

	if(pLabelRect != nullptr)
		*pLabelRect = Label;
	if(pValueRect != nullptr)
		*pValueRect = ValueText;
	if(pScrollBarRect != nullptr)
		*pScrollBarRect = ScrollBar;
}

int CMenus::DoButton_CheckBox_Common_WithLabelElement(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, const unsigned Flags, CUIElement *pLabelElement, const bool ProcessInput, const float LabelFontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
	const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();
	const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();
	const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();
	const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();

	CUIRect Box, Label;
	pRect->VSplitLeft(pRect->h, &Box, &Label);
	Label.VSplitLeft(5.0f, nullptr, &Label);

	const bool Hovered = Ui()->HotItem() == pId || Ui()->CheckActiveItem(pId);
	static const uint64_t s_HoverScopeHash = static_cast<uint64_t>(str_quickhash("menu_checkbox_hover"));
	static const uint64_t s_CheckScopeHash = static_cast<uint64_t>(str_quickhash("menu_checkbox_mark"));
	const uint64_t HoverNodeKey = BuildUiAnimNodeKey(s_HoverScopeHash, reinterpret_cast<uint64_t>(pId));
	const uint64_t CheckNodeKey = BuildUiAnimNodeKey(s_CheckScopeHash, reinterpret_cast<uint64_t>(pId));
	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	const float HoverStrength = std::clamp(ResolveUiAnimValue(AnimRuntime, HoverNodeKey, EUiAnimProperty::SCALE, Hovered ? 1.0f : 0.0f, 0.10f, EEasing::EASE_OUT), 0.0f, 1.0f);
	const float CheckStrength = std::clamp(ResolveUiAnimValue(AnimRuntime, CheckNodeKey, EUiAnimProperty::ALPHA, *pBoxText == 'X' ? 1.0f : 0.0f, 0.10f, EEasing::EASE_OUT), 0.0f, 1.0f);

	Box.Margin(2.0f, &Box);
	const float BoxAlpha = std::clamp(0.25f * Ui()->ButtonColorMul(pId) + 0.10f * HoverStrength + 0.08f * CheckStrength, 0.0f, 1.0f);
	const ColorRGBA BoxColor(1.0f, 1.0f, 1.0f, BoxAlpha);
	DrawRoundedSurface(Ui(), Box, BoxColor, BoxColor, 3.0f);

	const bool HasCustomGlyph = pBoxText[0] != '\0' && pBoxText[0] != 'X';
	if(HasCustomGlyph)
	{
		Ui()->DoLabel(&Box, pBoxText, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	}
	else if(CheckStrength > MENU_TAB_ANIM_EPSILON)
	{
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		const ColorRGBA DefaultColor = TextRender()->DefaultTextColor();
		TextRender()->TextColor(ColorRGBA(DefaultColor.r, DefaultColor.g, DefaultColor.b, DefaultColor.a * CheckStrength));
		Ui()->DoLabel(&Box, FONT_ICON_XMARK, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->SetRenderFlags(PreviousRenderFlags);
		TextRender()->SetFontPreset(PreviousFontPreset);
		TextRender()->TextOutlineColor(PreviousTextOutlineColor);
		TextRender()->TextSelectionColor(PreviousTextSelectionColor);
		TextRender()->TextColor(PreviousTextColor);
	}

	TextRender()->SetRenderFlags(PreviousRenderFlags);
	const bool FixedFontSize = LabelFontSize > 0.0f;
	const float FontSize = LabelFontSize > 0.0f ? LabelFontSize : Box.h * CUi::ms_FontmodHeight;
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_MinimumFontSize = FixedFontSize ? FontSize : FontSize * 0.7f;
	Props.m_EllipsisAtEnd = FixedFontSize;
	if(pText != nullptr && pText[0] != '\0')
	{
		if(pLabelElement != nullptr)
			DoSettingsLabelStreamed(*pLabelElement, &Label, pText, FontSize, TEXTALIGN_ML, Props);
		else
			Ui()->DoLabel(&Label, pText, FontSize, TEXTALIGN_ML, Props);
	}

	TextRender()->SetRenderFlags(PreviousRenderFlags);
	TextRender()->SetFontPreset(PreviousFontPreset);
	TextRender()->TextOutlineColor(PreviousTextOutlineColor);
	TextRender()->TextSelectionColor(PreviousTextSelectionColor);
	TextRender()->TextColor(PreviousTextColor);

	return ProcessInput ? Ui()->DoButtonLogic(pId, 0, pRect, Flags) : 0;
}

int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect)
{
	return DoSettingsButton_CheckBox(Page, Tab, -1, pId, pTextId, pText, Checked, pRect);
}

int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect)
{
	SLabelProperties Props;
	return DoSettingsButton_CheckBox(Page, Tab, Subtab, pId, pTextId, pText, Checked, pRect, Props);
}

int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SLabelProperties &LabelProps)
{
	return DoSettingsButton_CheckBox(Page, Tab, Subtab, pId, pTextId, pText, Checked, pRect, LabelProps, true);
}

int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SLabelProperties &LabelProps, const bool ProcessInput, const float RequestedFontSize)
{
	const float BodySize = RequestedFontSize > 0.0f ? RequestedFontSize : CurrentSettingsContentMetrics().m_BodySize;
	if(pTextId == nullptr)
	{
		return DoButton_CheckBox_Common_WithLabelElement(pId, pText, Checked ? "X" : "", pRect, BUTTONFLAG_LEFT, nullptr, ProcessInput, BodySize);
	}
	if(g_Config.m_QmNewUi)
	{
		CUIRect Label, ToggleRect;
		pRect->VSplitRight(std::max(pRect->h * 1.65f, 30.0f), &Label, &ToggleRect);
		Label.VSplitRight(8.0f, &Label, nullptr);
		SLabelProperties Props = LabelProps;
		Props.m_MaxWidth = Label.w;
		DoSettingsMenuLabel(Page, Tab, Subtab, pTextId, &Label, pText, BodySize, TEXTALIGN_ML, Props);
		if(m_MenuTextPlanCollecting)
			return 0;

		bool ToggleValue = Checked != 0;
		IUiContext Context = SettingsUiContext("settings-switch", CurrentSettingsContentMetrics().m_UiScale);
		ui_widget::Toggle(Context, pId, &ToggleValue, ToggleRect, false, ProcessInput);
		return ProcessInput ? Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_LEFT) : 0;
	}
	CUIRect Box, Label;
	pRect->VSplitLeft(pRect->h, &Box, &Label);
	Label.VSplitLeft(5.0f, nullptr, &Label);
	Box.Margin(2.0f, &Box);
	SLabelProperties Props = LabelProps;
	Props.m_MaxWidth = Label.w;
	const bool AutoMinimumFontSize = Props.m_MinimumFontSize <= 0.0f;
	const float FontSize = ResolveSettingsCheckboxFontSize(BodySize, RequestedFontSize, pRect->h, Box.h, CUi::ms_FontmodHeight);
	if(AutoMinimumFontSize)
		Props.m_MinimumFontSize = FontSize * 0.7f;
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Label, FontSize, TEXTALIGN_ML, Props);
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, pText, &Label, FontSize, TEXTALIGN_ML, Props, StyleKey);
		return 0;
	}
	CUIElement &LabelElement = MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, StyleKey);
	return DoButton_CheckBox_Common_WithLabelElement(pId, pText, Checked ? "X" : "", pRect, BUTTONFLAG_LEFT, &LabelElement, ProcessInput, FontSize);
}

int CMenus::DoSettingsButton_CheckBoxAutoVMarginAndSet(int Page, int Tab, const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float RowHeight, float RowSpacing, float BodySize)
{
	CUIRect CheckBoxRect;
	pRect->HSplitTop(RowHeight, &CheckBoxRect, pRect);
	if(RowSpacing > 0.0f)
		pRect->HSplitTop(RowSpacing, nullptr, pRect);

	SLabelProperties LabelProps;
	const int Logic = DoSettingsButton_CheckBox(Page, Tab, -1, pId, pTextId, pText, *pValue, &CheckBoxRect, LabelProps, true, BodySize);
	if(Logic)
		*pValue ^= 1;
	return Logic;
}

void CMenus::DoSettingsLabel(int Page, int Tab, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, bool Render)
{
	SLabelProperties EffectiveProps = LabelProps;
	if(Page == SETTINGS_TCLIENT && Size > 0.0f)
	{
		EffectiveProps.m_MaxWidth = EffectiveProps.m_MaxWidth >= 0.0f ? EffectiveProps.m_MaxWidth : pRect->w;
		EffectiveProps.m_MinimumFontSize = Size;
		EffectiveProps.m_EllipsisAtEnd = true;
	}
	if(pTextId == nullptr)
	{
		if(Render)
			Ui()->DoLabel(pRect, pText, Size, Align, EffectiveProps);
		return;
	}
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(pRect, Size, Align, EffectiveProps);
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, -1, pTextId, pText, pRect, Size, Align, EffectiveProps, StyleKey);
		return;
	}
	CUIElement &Element = MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, -1, pTextId, StyleKey);
	DoSettingsLabelStreamed(Element, pRect, pText, Size, Align, EffectiveProps, -1, nullptr, Render);
}

void CMenus::DoSettingsMenuLabel(int Page, int Tab, int Subtab, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &Props, int MaxWidth)
{
	SLabelProperties EffectiveProps = Props;
	if(Page == SETTINGS_TCLIENT && Size > 0.0f)
	{
		EffectiveProps.m_MaxWidth = EffectiveProps.m_MaxWidth >= 0.0f ? EffectiveProps.m_MaxWidth : (MaxWidth >= 0 ? (float)MaxWidth : pRect->w);
		EffectiveProps.m_MinimumFontSize = Size;
		EffectiveProps.m_EllipsisAtEnd = true;
	}
	if(pTextId == nullptr)
	{
		Ui()->DoLabel(pRect, pText, Size, Align, EffectiveProps);
		return;
	}
	SLabelProperties LabelProps = EffectiveProps;
	if(MaxWidth >= 0)
		LabelProps.m_MaxWidth = (float)MaxWidth;
	CUIRect ShellTitleLabel;
	const bool ShellTitle = pTextId != nullptr && str_comp(pTextId, "settings-shell-title") == 0;
	const SMenuTextStyleKey StyleKey = ShellTitle ? BuildSettingsShellTitleTextStyle(*pRect, &ShellTitleLabel) : BuildMenuTextStyleKey(pRect, Size, Align, LabelProps);
	const CUIRect *pLabelRect = ShellTitle ? &ShellTitleLabel : pRect;
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, pText, pLabelRect, Size, Align, LabelProps, StyleKey);
		return;
	}
	CUIElement &Element = MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, StyleKey);
	DoSettingsLabelStreamed(Element, pLabelRect, pText, Size, Align, LabelProps, -1, nullptr, true);
}

int CMenus::DoSettingsButton_Menu(int Page, int Tab, int Subtab, CButtonContainer *pBC, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, int Flags, int Corners, float Rounding, const ColorRGBA &Color, float FontFactor, float BodySize)
{
	dbg_assert(pBC != nullptr, "settings menu button requires a stable button container");
	const float ResolvedBodySize = BodySize > 0.0f ? BodySize : CurrentSettingsContentMetrics().m_BodySize;
	if(pTextId == nullptr)
	{
		return DoButton_Menu(pBC, pText, Checked, pRect, Flags, nullptr, Corners, Rounding, FontFactor, Color, nullptr, ResolvedBodySize);
	}
	CUIRect Text = MenuButtonTextRect(pRect, 0.0f, 0.0f);
	SLabelProperties Props;
	Props.m_MaxWidth = Text.w;
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Text, ResolvedBodySize, TEXTALIGN_MC, Props);
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, pText, &Text, ResolvedBodySize, TEXTALIGN_MC, Props, StyleKey);
		return 0;
	}
	CUIElement &TextElement = MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, StyleKey);
	return DoButton_Menu(pBC, pText, Checked, pRect, Flags, nullptr, Corners, Rounding, FontFactor, Color, &TextElement, ResolvedBodySize);
}

int CMenus::DoSettingsButton_Menu(int Page, int Tab, int Subtab, CButtonContainer *pBC, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SSettingsContentMetrics &Metrics, int Flags, int Corners, float Rounding, const ColorRGBA &Color, float FontFactor)
{
	return DoSettingsButton_Menu(Page, Tab, Subtab, pBC, pTextId, pText, Checked, pRect, Flags, Corners, Rounding, Color, FontFactor, Metrics.m_BodySize);
}

bool CMenus::DoSettingsScrollbarOption(int Page, int Tab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)
{
	return DoSettingsScrollbarOption(Page, Tab, -1, pTextId, pId, pOption, pRect, pStr, Min, Max, pScale, Flags, pSuffix, pMaxText);
}

bool CMenus::PrepareSettingsNumericFieldLabel(int Page, int Tab, int Subtab, const char *pTextId, const CUIRect &Rect, const char *pLabel, unsigned Flags, ui_widget::SNumericFieldOptions &Options)
{
	if(pTextId == nullptr || pTextId[0] == '\0')
		return false;

	const CUIRect Label = ui_widget::SliderInputFieldLabelRect(Rect, pLabel != nullptr && pLabel[0] != '\0', Flags);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	if(m_MenuTextPlanCollecting)
	{
		const float FontSize = Options.m_FontSize;
		const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Label, FontSize, TEXTALIGN_ML, Props);
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, pLabel, &Label, FontSize, TEXTALIGN_ML, Props, StyleKey);
		return true;
	}

	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Label, Options.m_FontSize, Options.m_LabelAlign, Props);
	Options.m_pLabelElement = &MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, StyleKey);
	return false;
}

bool CMenus::DoSettingsScrollbarOption(int Page, int Tab, int Subtab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)
{
	ui_widget::SNumericFieldOptions Options;
	Options.m_pLabel = pStr;
	Options.m_pSuffix = pSuffix;
	Options.m_pScale = pScale;
	Options.m_Flags = Flags;
	Options.m_pMaxText = pMaxText;
	Options.m_FontSize = CurrentSettingsContentMetrics().m_BodySize;
	Options.m_LabelAlign = TEXTALIGN_ML;
	Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
	if(PrepareSettingsNumericFieldLabel(Page, Tab, Subtab, pTextId, *pRect, pStr, Flags, Options))
		return false;

	ui_widget::SNumericFieldState *pState = GetSettingsNumericFieldState(pId);
	IUiContext InputCtx = SettingsUiContext("settings_slider_input", Options.m_FontSize / ui_token::font::BODY);
	return ui_widget::NumericField(InputCtx, pState, pId, pOption, Min, Max, *pRect, Options);
}

ui_widget::SNumericFieldState *CMenus::GetSettingsNumericFieldState(const void *pId)
{
	auto &pState = m_vpSettingsNumericFieldStates[pId];
	if(!pState)
		pState = std::make_unique<ui_widget::SNumericFieldState>();
	return pState.get();
}

void CMenus::DoLaserPreview(const CUIRect *pRect, const ColorHSLA LaserOutlineColor, const ColorHSLA LaserInnerColor, const int LaserType)
{
	CUIRect Section = *pRect;
	vec2 From = vec2(Section.x + 30.0f, Section.y + Section.h / 2.0f);
	vec2 Pos = vec2(Section.x + Section.w - 20.0f, Section.y + Section.h / 2.0f);

	const ColorRGBA OuterColor = color_cast<ColorRGBA>(ColorHSLA(LaserOutlineColor));
	const ColorRGBA InnerColor = color_cast<ColorRGBA>(ColorHSLA(LaserInnerColor));
	const float TicksHead = Client()->GlobalTime() * Client()->GameTickSpeed();

	// TicksBody = 4.0 for less laser width for weapon alignment
	if(LaserType == LASERTYPE_RIFLE || LaserType == LASERTYPE_SHOTGUN)
	{
		switch(LaserType)
		{
		case LASERTYPE_RIFLE:
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponLaser);
			Graphics()->SelectSprite(SPRITE_WEAPON_LASER_BODY);
			Graphics()->QuadsBegin();
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
			Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);
			Graphics()->QuadsEnd();
			break;
		case LASERTYPE_SHOTGUN:
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponShotgun);
			Graphics()->SelectSprite(SPRITE_WEAPON_SHOTGUN_BODY);
			Graphics()->QuadsBegin();
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
			Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);
			Graphics()->QuadsEnd();
			break;
		}
		GameClient()->m_Items.RenderLaser(From, Pos, OuterColor, InnerColor, 4.0f, TicksHead, LaserType, g_Config.m_QmLaserGlowIntensity);
	}
	else
	{
		const vec2 EntityLaserEnd = LaserType == LASERTYPE_DOOR ? Pos - vec2(34.0f, 0.0f) : Pos - vec2(42.0f, 0.0f);
		GameClient()->m_Items.RenderLaser(From, EntityLaserEnd, OuterColor, InnerColor, 4.0f, TicksHead, LaserType, 100.0f);
		switch(LaserType)
		{
		case LASERTYPE_DRAGGER:
		{
			CTeeRenderInfo TeeRenderInfo;
			TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
			TeeRenderInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
			TeeRenderInfo.m_Size = 64.0f;
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos - vec2(20.0f, 0.0f));
			break;
		}
		case LASERTYPE_FREEZE:
		{
			CTeeRenderInfo TeeRenderInfo;
			if(g_Config.m_ClShowNinja)
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find("x_ninja"));
			else
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
			TeeRenderInfo.m_TeeRenderFlags = TEE_EFFECT_FROZEN;
			TeeRenderInfo.m_Size = 64.0f;
			TeeRenderInfo.m_ColorBody = ColorRGBA(1, 1, 1);
			TeeRenderInfo.m_ColorFeet = ColorRGBA(1, 1, 1);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_PAIN, vec2(1, 0), From);
			GameClient()->m_Effects.FreezingFlakes(From, vec2(32, 32), 1.0f);
			break;
		}
		default:
			break;
		}
	}
}

bool CMenus::DoLine_RadioMenu(CUIRect &View, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value)
{
	dbg_assert(vButtonContainers.size() == vValues.size(), "vButtonContainers and vValues must have the same size");
	dbg_assert(vButtonContainers.size() == vLabels.size(), "vButtonContainers and vLabels must have the same size");
	const int N = vButtonContainers.size();
	const float Spacing = 2.0f;
	const float ButtonHeight = 20.0f;
	CUIRect Label, Buttons;
	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(ButtonHeight, &Buttons, &View);
	Buttons.VSplitMid(&Label, &Buttons, 10.0f);
	Buttons.HMargin(2.0f, &Buttons);
	Ui()->DoLabel(&Label, pLabel, 13.0f, TEXTALIGN_ML);
	const float W = Buttons.w / N;
	bool Pressed = false;
	for(int i = 0; i < N; ++i)
	{
		CUIRect Button;
		Buttons.VSplitLeft(W, &Button, &Buttons);
		int Corner = IGraphics::CORNER_NONE;
		if(i == 0)
			Corner = IGraphics::CORNER_L;
		if(i == N - 1)
			Corner = IGraphics::CORNER_R;
		if(DoButton_Menu(&vButtonContainers[i], vLabels[i], vValues[i] == Value, &Button, BUTTONFLAG_LEFT, nullptr, Corner))
		{
			Pressed = true;
			Value = vValues[i];
		}
	}
	return Pressed;
}

bool CMenus::DoSettingsLine_RadioMenu(int Page, int Tab, int Subtab, CUIRect &View, const char *pLabelTextId, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vButtonTextIds, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value, const SSettingsContentMetrics &Metrics)
{
	dbg_assert(vButtonContainers.size() == vValues.size(), "vButtonContainers and vValues must have the same size");
	dbg_assert(vButtonContainers.size() == vLabels.size(), "vButtonContainers and vLabels must have the same size");
	dbg_assert(vButtonContainers.size() == vButtonTextIds.size(), "vButtonContainers and vButtonTextIds must have the same size");
	const int N = vButtonContainers.size();
	const SSettingsRadioRowLayout Layout = ResolveSettingsRadioRowLayout(View, N, Metrics);
	CUIRect Label = Layout.m_LabelRect;
	CUIRect Buttons = Layout.m_ButtonsRect;
	View.HSplitTop(Layout.m_Height, nullptr, &View);
	DoSettingsLabel(Page, Tab, pLabelTextId, &Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML);
	const float W = Buttons.w / N;
	bool Pressed = false;
	for(int i = 0; i < N; ++i)
	{
		CUIRect Button;
		Buttons.VSplitLeft(W, &Button, &Buttons);
		int Corner = IGraphics::CORNER_NONE;
		if(i == 0)
			Corner = IGraphics::CORNER_L;
		if(i == N - 1)
			Corner = IGraphics::CORNER_R;
		if(DoSettingsButton_Menu(Page, Tab, Subtab, &vButtonContainers[i], vButtonTextIds[i], vLabels[i], vValues[i] == Value, &Button, BUTTONFLAG_LEFT, Corner, 5.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, Metrics.m_BodySize))
		{
			Pressed = true;
			Value = vValues[i];
		}
	}
	return Pressed;
}

ColorHSLA CMenus::DoLine_ColorPicker(CButtonContainer *pResetId, const SSettingsContentMetrics &Metrics, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, const ColorRGBA DefaultColor, bool CheckBoxSpacing, int *pCheckBoxValue, bool Alpha, bool TrailingSpacing)
{
	const SSettingsColorRowLayout Layout = ResolveSettingsColorRowLayout(*pMainRect, Metrics, CheckBoxSpacing && pCheckBoxValue == nullptr, TrailingSpacing);
	pMainRect->y += Layout.m_ConsumedHeight;
	pMainRect->h = std::max(0.0f, pMainRect->h - Layout.m_ConsumedHeight);
	CUIRect Label = Layout.m_LabelRect;

	if(pCheckBoxValue != nullptr)
	{
		if(DoButton_CheckBox(pCheckBoxValue, pText, *pCheckBoxValue, &Label, Metrics.m_BodySize))
			*pCheckBoxValue ^= 1;
	}
	if(pCheckBoxValue == nullptr)
	{
		Ui()->DoLabel(&Label, pText, Metrics.m_BodySize, TEXTALIGN_ML);
	}

	const ColorHSLA PickedColor = DoButton_ColorPicker(&Layout.m_ColorButtonRect, pColorValue, Alpha);

	if(DoButton_Menu(pResetId, Localize("Reset"), 0, &Layout.m_ResetButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), nullptr, Metrics.m_BodySize))
	{
		*pColorValue = color_cast<ColorHSLA>(DefaultColor).Pack(Alpha);
	}

	return PickedColor;
}

bool CMenus::DoLine_AlphaColorPicker(CButtonContainer *pResetId, const SSettingsContentMetrics &Metrics, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, int *pOpacity, const unsigned int DefaultColor, const int DefaultOpacity)
{
	SSettingsAlphaColorPickerState &State = m_SettingsAlphaColorPickerStates[pColorValue];
	const unsigned int ConfigPackedColor = PackSettingsAlphaColor(*pColorValue, *pOpacity);
	const bool Editing = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &State.m_PackedColor;
	if(!State.m_Initialized || !Editing)
	{
		State.m_PackedColor = ConfigPackedColor;
		State.m_Initialized = true;
	}

	DoLine_ColorPicker(pResetId, Metrics, pMainRect, pText, &State.m_PackedColor, color_cast<ColorRGBA>(ColorHSLA(DefaultColor).WithAlpha(DefaultOpacity / 100.0f)), false, nullptr, true);
	if(State.m_PackedColor == ConfigPackedColor)
		return false;

	UnpackSettingsAlphaColor(State.m_PackedColor, *pColorValue, *pOpacity);
	return true;
}

ColorHSLA CMenus::DoLine_ColorPicker(CButtonContainer *pResetId, const float LineSize, const float LabelSize, const float BottomMargin, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, const ColorRGBA DefaultColor, bool CheckBoxSpacing, int *pCheckBoxValue, bool Alpha)
{
	SSettingsContentMetrics Metrics;
	Metrics.m_UiScale = std::clamp(LineSize / ui_token::settings::ROW_HEIGHT, 0.5f, 1.5f);
	Metrics.m_LineHeight = std::max(0.0f, LineSize);
	Metrics.m_ButtonHeight = std::max(0.0f, LineSize);
	Metrics.m_BodySize = std::max(0.0f, LabelSize);
	Metrics.m_LineSpacing = std::max(0.0f, BottomMargin);
	return DoLine_ColorPicker(pResetId, Metrics, pMainRect, pText, pColorValue, DefaultColor, CheckBoxSpacing, pCheckBoxValue, Alpha);
}

ColorHSLA CMenus::DoButton_ColorPicker(const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	ColorHSLA HslaColor = ColorHSLA(*pHslaColor, Alpha);

	ColorRGBA Outline = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f);
	Outline.a *= Ui()->ButtonColorMul(pHslaColor);

	DrawRoundedSurface(Ui(), *pRect, color_cast<ColorRGBA>(HslaColor), Outline, 4.0f, 3.0f);

	if(Ui()->DoButtonLogic(pHslaColor, 0, pRect, BUTTONFLAG_LEFT))
	{
		m_ColorPickerPopupContext.m_pHslaColor = pHslaColor;
		m_ColorPickerPopupContext.m_HslaColor = HslaColor;
		m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HsvaColor);
		m_ColorPickerPopupContext.m_Alpha = Alpha;
		Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
	}
	else if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == pHslaColor)
	{
		HslaColor = color_cast<ColorHSLA>(m_ColorPickerPopupContext.m_HsvaColor);
	}

	return HslaColor;
}

int CMenus::DoButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pText, int *pValue, CUIRect *pRect, float VMargin)
{
	CUIRect CheckBoxRect;
	pRect->HSplitTop(VMargin, &CheckBoxRect, pRect);

	int Logic;
	Logic = DoButton_CheckBox_Common(pId, pText, *pValue ? "X" : "", &CheckBoxRect, BUTTONFLAG_LEFT);

	if(Logic)
		*pValue ^= 1;

	return Logic;
}

int CMenus::DoButton_CheckBox(const void *pId, const char *pText, int Checked, const CUIRect *pRect, const float BodySize)
{
	return DoButton_CheckBox_Common_WithLabelElement(pId, pText, Checked ? "X" : "", pRect, BUTTONFLAG_LEFT, nullptr, true, BodySize);
}

int CMenus::DoButton_CheckBox_Number(const void *pId, const char *pText, int Checked, const CUIRect *pRect)
{
	char aBuf[16];
	str_format(aBuf, sizeof(aBuf), "%d", Checked);
	return DoButton_CheckBox_Common(pId, pText, aBuf, pRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
}

int CMenus::DoMenuTabV2(CButtonContainer *pButtonContainer, const char *pText, bool Active, const CUIRect *pRect, int Corners, const ColorRGBA *pCustomDefault, const ColorRGBA *pCustomActive, const ColorRGBA *pCustomHover, const CCommunityIcon *pCommunityIcon, CUIElement *pTextUiElement, float ContentScale)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	// Compose target background color from active / hover / idle states. Custom
	// overrides are honored when supplied (Quit red, Home news green, favorite
	// community appear-fade etc.); otherwise we fall back to feat-003 tokens.
	const bool Hover = Ui()->HotItem() == static_cast<const void *>(pButtonContainer);
	const bool UseNewUi = g_Config.m_QmNewUi != 0;
	const ColorRGBA DefaultColor = UseNewUi ? MenuTabDefaultColor() : ms_ColorTabbarInactive;
	const ColorRGBA ActiveColor = UseNewUi ? MenuTabActiveColor() : ms_ColorTabbarActive;
	const ColorRGBA HoverColor = UseNewUi ? MenuTabHoverColor() : ms_ColorTabbarHover;
	ColorRGBA Target;
	if(Hover)
		Target = pCustomHover != nullptr ? *pCustomHover : HoverColor;
	else if(Active)
		Target = pCustomActive != nullptr ? *pCustomActive : ActiveColor;
	else
		Target = pCustomDefault != nullptr ? *pCustomDefault : DefaultColor;

	ColorRGBA Resolved = Target;
	if(!Ui()->RenderOnly())
	{
		const uint64_t NodeKey = BuildUiAnimNodeKey(MakeUiScopeHash("menubar_v2_tab"), reinterpret_cast<uint64_t>(pButtonContainer));
		CUiV2AnimationRuntime &AnimRt = GameClient()->UiRuntimeV2()->AnimRuntime();
		Resolved = ResolveUiAnimValueColor(AnimRt, NodeKey, Target, ui_token::motion::BTN_HOVER.m_DurationSec, ui_token::motion::BTN_HOVER.m_Easing);
	}
	DrawRoundedSurface(Ui(), *pRect, Resolved, ColorRGBA(), UseNewUi ? 7.0f * ContentScale : 10.0f, 0.0f, Corners);

	if(pCommunityIcon != nullptr)
	{
		CUIRect IconRect;
		pRect->Margin(2.0f * ContentScale, &IconRect);
		m_CommunityIcons.Render(pCommunityIcon, IconRect, true);
	}
	else
	{
		CUIRect Label;
		pRect->HMargin(2.0f * ContentScale, &Label);
		const float LabelFontSize = UseNewUi ? ui_token::settings::TAB_FONT_SIZE * ContentScale : Label.h * CUi::ms_FontmodHeight;
		if(pTextUiElement != nullptr)
		{
			CUIElement::SUIElementRect *pElementRect = pTextUiElement->Rect(0);
			const bool HadReadyContainer = pElementRect->m_UITextContainer.Valid();
			DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, *pTextUiElement, &Label, pText, LabelFontSize, TEXTALIGN_MC);
			if(pTextUiElement != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid())
			{
				CountMenuTextImmediateFallback();
				Ui()->DoLabel(&Label, pText, LabelFontSize, TEXTALIGN_MC);
			}
		}
		else
			Ui()->DoLabel(&Label, pText, LabelFontSize, TEXTALIGN_MC);
	}

	return Ui()->DoButtonLogic(pButtonContainer, Active ? 1 : 0, pRect, BUTTONFLAG_LEFT);
}

void CMenus::RenderMenubar(CUIRect Box, IClient::EClientState ClientState)
{
	CUIRect Button;

	int NewPage = -1;
	int ActivePage = -1;
	if(ClientState == IClient::STATE_OFFLINE)
	{
		ActivePage = m_MenuPage;
	}
	else if(ClientState == IClient::STATE_ONLINE)
	{
		ActivePage = m_GamePage;
	}
	else
	{
		dbg_assert_failed("Client state %d is invalid for RenderMenubar", ClientState);
	}

	// feat-004: track the rect of whichever tab matches ActivePage so we can
	// paint a Steam-blue underline indicator after all tabs are rendered.
	CUIRect MenubarActiveRect = {0.0f, 0.0f, 0.0f, 0.0f};
	bool MenubarHaveActive = false;
	auto MenubarTrackActive = [&](int Page, const CUIRect &R) {
		if(Page == ActivePage)
		{
			MenubarActiveRect = R;
			MenubarHaveActive = true;
		}
	};

	// First render buttons aligned from right side so remaining
	// width is known when rendering buttons from left side.
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	const bool UseNewUi = g_Config.m_QmNewUi != 0;
	auto RenderFavoriteMapsIcon = [&](const CUIRect &Tab) {
		const float IconSide = minimum(Tab.w, Tab.h) * 0.56f;
		const CUIRect IconRect{Tab.x + (Tab.w - IconSide) * 0.5f, Tab.y + (Tab.h - IconSide) * 0.5f, IconSide, IconSide};
		const ColorRGBA IconColor = ConfiguredQmUiIconColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		if(GameClient()->QmIconManager()->RenderIcon(EQmIcon::BOOKMARK, IconRect, IconColor))
			return;

		const unsigned OldFlags = TextRender()->GetRenderFlags();
		const EFontPreset OldPreset = TextRender()->GetFontPreset();
		TextRender()->SetFontPreset(QmIconWeightUsesBoldFontFallback(g_Config.m_QmUiIconWeight) ? EFontPreset::ICON_FONT_BOLD : EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ui()->DoLabel(&Tab, FONT_ICON_BOOKMARK, IconSide, TEXTALIGN_MC);
		TextRender()->SetRenderFlags(OldFlags);
		TextRender()->SetFontPreset(OldPreset);
	};
	if(UseNewUi)
	{
		const float MenubarOuterInsetX = 6.0f;
		const float MenubarBaseOuterInsetY = 2.5f;
		const float MenubarOuterInsetY = (Box.h - (Box.h - 2.0f * MenubarBaseOuterInsetY) * MENU_MENUBAR_CONTENT_SCALE_NEW) * 0.5f;
		Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f), IGraphics::CORNER_ALL, 10.0f);
		Box.VMargin(MenubarOuterInsetX, &Box);
		Box.HMargin(MenubarOuterInsetY, &Box);

		const float MenubarIconButtonSize = Box.h;
		const float MenubarIconGap = 6.0f;
		const float MenubarItemGap = 4.0f;
		const ColorRGBA IconButtonDefault = MenuIconButtonDefaultColor();
		const ColorRGBA IconButtonActive = MenuTabActiveColor();
		const ColorRGBA IconButtonHover = MenuMenubarHoverColor();
		const ColorRGBA HomeButtonDefault = ui_token::color::ACCENT_PRIMARY.WithMultipliedAlpha(0.95f);
		const ColorRGBA HomeButtonHover = ui_token::color::ACCENT_PRIMARY;
		const ColorRGBA QuitButtonDefault = MenuDangerTabDefaultColor();
		const ColorRGBA QuitButtonHover = MenuDangerTabHoverColor();
		Box.VSplitRight(MenubarIconButtonSize, &Box, &Button);
		static CButtonContainer s_QuitButton;
		{
			CUIRect QuitButton = Button;
			const float CircleSize = minimum(QuitButton.w, QuitButton.h);
			QuitButton.x += (QuitButton.w - CircleSize) / 2.0f;
			QuitButton.w = CircleSize;
			if(DoMenuTabV2(&s_QuitButton, FONT_ICON_POWER_OFF, false, &QuitButton, IGraphics::CORNER_ALL, &QuitButtonDefault, nullptr, &QuitButtonHover, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
			{
				if(GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0) || m_MenusIngameTouchControls.UnsavedChanges() || GameClient()->m_TouchControls.HasEditingChanges())
				{
					m_Popup = POPUP_QUIT;
				}
				else
				{
					Client()->Quit();
				}
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&s_QuitButton, &Button, Localize("Quit"));

		Box.VSplitRight(MenubarIconGap, &Box, nullptr);
		Box.VSplitRight(MenubarIconButtonSize, &Box, &Button);
		static CButtonContainer s_SettingsButton;
		{
			CUIRect SettingsButton = Button;
			const float CircleSize = minimum(SettingsButton.w, SettingsButton.h);
			SettingsButton.x += (SettingsButton.w - CircleSize) / 2.0f;
			SettingsButton.w = CircleSize;
			if(DoMenuTabV2(&s_SettingsButton, FONT_ICON_GEAR, ActivePage == PAGE_SETTINGS, &SettingsButton, IGraphics::CORNER_ALL, &IconButtonDefault, &IconButtonActive, &IconButtonHover, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
			{
				NewPage = PAGE_SETTINGS;
			}
			MenubarTrackActive(PAGE_SETTINGS, SettingsButton);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SettingsButton, &Button, Localize("Settings"));

		Box.VSplitRight(MenubarIconGap, &Box, nullptr);
		Box.VSplitRight(MenubarIconButtonSize, &Box, &Button);
		static CButtonContainer s_EditorButton;
		{
			CUIRect EditorButton = Button;
			const float CircleSize = minimum(EditorButton.w, EditorButton.h);
			EditorButton.x += (EditorButton.w - CircleSize) / 2.0f;
			EditorButton.w = CircleSize;
			if(DoMenuTabV2(&s_EditorButton, FONT_ICON_PEN_TO_SQUARE, false, &EditorButton, IGraphics::CORNER_ALL, &IconButtonDefault, nullptr, &IconButtonHover, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
			{
				g_Config.m_ClEditor = 1;
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&s_EditorButton, &Button, Localize("Editor"));

		if(ClientState == IClient::STATE_OFFLINE)
		{
			Box.VSplitRight(MenubarIconGap, &Box, nullptr);
			Box.VSplitRight(MenubarIconButtonSize, &Box, &Button);
			static CButtonContainer s_DemoButton;
			{
				CUIRect DemoButton = Button;
				const float CircleSize = minimum(DemoButton.w, DemoButton.h);
				DemoButton.x += (DemoButton.w - CircleSize) / 2.0f;
				DemoButton.w = CircleSize;
				if(DoMenuTabV2(&s_DemoButton, FONT_ICON_CLAPPERBOARD, ActivePage == PAGE_DEMOS, &DemoButton, IGraphics::CORNER_ALL, &IconButtonDefault, &IconButtonActive, &IconButtonHover, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
				{
					NewPage = PAGE_DEMOS;
				}
				MenubarTrackActive(PAGE_DEMOS, DemoButton);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
			Box.VSplitRight(MenubarIconGap, &Box, nullptr);

			Box.VSplitLeft(MenubarIconButtonSize, &Button, &Box);

			bool GotNewsOrUpdate = false;

#if defined(CONF_AUTOUPDATE)
			int State = Updater()->GetCurrentState();
			bool NeedUpdate = str_comp(Client()->LatestVersion(), "0");
			if(State == IUpdater::CLEAN && NeedUpdate)
			{
				GotNewsOrUpdate = true;
			}
#endif

			GotNewsOrUpdate |= (bool)g_Config.m_UiUnreadNews;

			ColorRGBA HomeButtonColorAlert = HomeButtonDefault;
			ColorRGBA HomeButtonColorAlertHover = HomeButtonHover;
			ColorRGBA *pHomeButtonColor = nullptr;
			ColorRGBA *pHomeButtonColorHover = nullptr;

			const char *pHomeScreenButtonLabel = FONT_ICON_HOUSE;
			if(GotNewsOrUpdate)
			{
				pHomeScreenButtonLabel = FONT_ICON_NEWSPAPER;
				pHomeButtonColor = &HomeButtonColorAlert;
				pHomeButtonColorHover = &HomeButtonColorAlertHover;
			}

			static CButtonContainer s_StartButton;
			{
				CUIRect HomeButton = Button;
				const float CircleSize = minimum(HomeButton.w, HomeButton.h);
				HomeButton.x += (HomeButton.w - CircleSize) / 2.0f;
				HomeButton.w = CircleSize;
				if(DoMenuTabV2(&s_StartButton, pHomeScreenButtonLabel, false, &HomeButton, IGraphics::CORNER_ALL, pHomeButtonColor != nullptr ? pHomeButtonColor : &HomeButtonDefault, nullptr, pHomeButtonColorHover != nullptr ? pHomeButtonColorHover : &HomeButtonHover, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
				{
					m_ShowStart = true;
				}
			}
			GameClient()->m_Tooltips.DoToolTip(&s_StartButton, &Button, Localize("Main menu"));

			const float BrowserButtonWidth = 58.0f * MENU_MENUBAR_CONTENT_SCALE_NEW;
			Box.VSplitLeft(6.0f, nullptr, &Box);
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_InternetButton;
			if(DoMenuTabV2(&s_InternetButton, FONT_ICON_EARTH_AMERICAS, ActivePage == PAGE_INTERNET, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
			{
				NewPage = PAGE_INTERNET;
			}
			MenubarTrackActive(PAGE_INTERNET, Button);
			GameClient()->m_Tooltips.DoToolTip(&s_InternetButton, &Button, Localize("Internet"));

			Box.VSplitLeft(MenubarItemGap, nullptr, &Box);
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_LanButton;
			if(DoMenuTabV2(&s_LanButton, FONT_ICON_NETWORK_WIRED, ActivePage == PAGE_LAN, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
			{
				NewPage = PAGE_LAN;
			}
			MenubarTrackActive(PAGE_LAN, Button);
			GameClient()->m_Tooltips.DoToolTip(&s_LanButton, &Button, Localize("LAN"));

			Box.VSplitLeft(MenubarItemGap, nullptr, &Box);
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_FavoritesButton;
			if(DoMenuTabV2(&s_FavoritesButton, FONT_ICON_STAR, ActivePage == PAGE_FAVORITES, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
			{
				NewPage = PAGE_FAVORITES;
			}
			MenubarTrackActive(PAGE_FAVORITES, Button);
			GameClient()->m_Tooltips.DoToolTip(&s_FavoritesButton, &Button, Localize("Favorites"));

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			Box.VSplitLeft(MenubarItemGap, nullptr, &Box);
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_FavoriteMapsButton;
			if(DoMenuTabV2(&s_FavoriteMapsButton, "", ActivePage == PAGE_FAVORITE_MAPS, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
			{
				NewPage = PAGE_FAVORITE_MAPS;
			}
			RenderFavoriteMapsIcon(Button);
			MenubarTrackActive(PAGE_FAVORITE_MAPS, Button);
			GameClient()->m_Tooltips.DoToolTip(&s_FavoriteMapsButton, &Button, Localize("Favorite map"));

			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

			int MaxPage = PAGE_FAVORITES + ServerBrowser()->FavoriteCommunities().size();
			if(
				!Ui()->IsPopupOpen() &&
				CLineInput::GetActiveInput() == nullptr &&
				((g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= MaxPage) || g_Config.m_UiPage == PAGE_FAVORITE_MAPS) &&
				((m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5) || m_MenuPage == PAGE_FAVORITE_MAPS))
			{
				if(Input()->KeyPress(KEY_RIGHT))
				{
					if(g_Config.m_UiPage == PAGE_FAVORITES)
					{
						NewPage = PAGE_FAVORITE_MAPS;
					}
					else if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
					{
						NewPage = ServerBrowser()->FavoriteCommunities().empty() ? PAGE_INTERNET : PAGE_FAVORITE_COMMUNITY_1;
					}
					else
					{
						NewPage = g_Config.m_UiPage + 1;
					}
					if(NewPage > MaxPage && NewPage != PAGE_FAVORITE_MAPS)
						NewPage = PAGE_INTERNET;
				}
				if(Input()->KeyPress(KEY_LEFT))
				{
					if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
					{
						NewPage = PAGE_FAVORITES;
					}
					else if(!ServerBrowser()->FavoriteCommunities().empty() && g_Config.m_UiPage == PAGE_FAVORITE_COMMUNITY_1)
					{
						NewPage = PAGE_FAVORITE_MAPS;
					}
					else
					{
						NewPage = g_Config.m_UiPage - 1;
					}
					if(NewPage < PAGE_INTERNET)
						NewPage = ServerBrowser()->FavoriteCommunities().empty() ? PAGE_FAVORITE_MAPS : MaxPage;
				}
			}

			size_t FavoriteCommunityIndex = 0;
			static CButtonContainer s_aFavoriteCommunityButtons[5];
			static const uint64_t s_FavoriteCommunityAppearScopeHash = static_cast<uint64_t>(str_quickhash("menu_favorite_community_tab_appear"));
			CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
			CUiV2Tree &Tree = GameClient()->UiRuntimeV2()->Tree();
			SUiAnimTransition AppearTransition;
			AppearTransition.m_DurationSec = 0.18f;
			AppearTransition.m_Easing = EEasing::EASE_OUT;
			static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)PAGE_FAVORITE_COMMUNITY_5 - PAGE_FAVORITE_COMMUNITY_1 + 1);
			static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)BIT_TAB_FAVORITE_COMMUNITY_5 - BIT_TAB_FAVORITE_COMMUNITY_1 + 1);
			static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)IServerBrowser::TYPE_FAVORITE_COMMUNITY_5 - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 + 1);
			for(const CCommunity *pCommunity : ServerBrowser()->FavoriteCommunities())
			{
				if(Box.w < BrowserButtonWidth + MenubarItemGap)
					break;
				Box.VSplitLeft(MenubarItemGap, nullptr, &Box);
				Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);

				const uint64_t NodeKey = BuildUiAnimNodeKey(s_FavoriteCommunityAppearScopeHash, static_cast<uint64_t>(str_quickhash(pCommunity->Id())));
				const SUiPresenceResult Presence = Tree.ResolvePresence(AnimRuntime, NodeKey, true, AppearTransition);
				const float AppearStrength = std::clamp(Presence.m_Alpha, 0.0f, 1.0f);
				const float RevealWidth = maximum(2.0f, Button.w * AppearStrength);
				CUIRect AnimatedButton = Button;
				AnimatedButton.x += (Button.w - RevealWidth) * 0.5f;
				AnimatedButton.w = RevealWidth;

				ColorRGBA InactiveColor = MenuTabDefaultColor();
				ColorRGBA ActiveColor = MenuTabActiveColor();
				ColorRGBA HoverColor = MenuMenubarHoverColor();
				InactiveColor.a *= AppearStrength;
				ActiveColor.a *= AppearStrength;
				HoverColor.a *= AppearStrength;

				const int Page = PAGE_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex;
				if(DoMenuTabV2(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], FONT_ICON_ELLIPSIS, ActivePage == Page, &AnimatedButton, IGraphics::CORNER_ALL, &InactiveColor, &ActiveColor, &HoverColor, m_CommunityIcons.Find(pCommunity->Id()), nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
				{
					NewPage = Page;
				}
				MenubarTrackActive(Page, AnimatedButton);
				GameClient()->m_Tooltips.DoToolTip(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], &AnimatedButton, pCommunity->Name());

				++FavoriteCommunityIndex;
				if(FavoriteCommunityIndex >= std::size(s_aFavoriteCommunityButtons))
					break;
			}

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else
		{
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

			const bool CompactOnlineMenuTabs = Graphics()->ScreenAspect() <= 1.45f || Box.w < 690.0f;
			const float GameButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;
			const float PlayersButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;
			const float ServerInfoButtonWidth = (CompactOnlineMenuTabs ? 94.0f : 104.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;
			const float BrowserButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;
			const float GhostButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;
			const float CallVoteButtonWidth = (CompactOnlineMenuTabs ? 80.0f : 88.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;
			const float OnlineTabGap = 4.0f;

			Box.VSplitLeft(GameButtonWidth, &Button, &Box);
			static CButtonContainer s_GameButton;
			if(DoIngameMenuTab(&s_GameButton, PAGE_GAME, "ingame-tab-game", Localize("Game"), ActivePage == PAGE_GAME, &Button, IGraphics::CORNER_ALL))
				NewPage = PAGE_GAME;
			MenubarTrackActive(PAGE_GAME, Button);

			Box.VSplitLeft(OnlineTabGap, nullptr, &Box);
			Box.VSplitLeft(PlayersButtonWidth, &Button, &Box);
			static CButtonContainer s_PlayersButton;
			if(DoIngameMenuTab(&s_PlayersButton, PAGE_PLAYERS, "ingame-tab-players", Localize("Players"), ActivePage == PAGE_PLAYERS, &Button, IGraphics::CORNER_ALL))
				NewPage = PAGE_PLAYERS;
			MenubarTrackActive(PAGE_PLAYERS, Button);

			Box.VSplitLeft(OnlineTabGap, nullptr, &Box);
			Box.VSplitLeft(ServerInfoButtonWidth, &Button, &Box);
			static CButtonContainer s_ServerInfoButton;
			if(DoIngameMenuTab(&s_ServerInfoButton, PAGE_SERVER_INFO, "ingame-tab-server-info", Localize("Server info"), ActivePage == PAGE_SERVER_INFO, &Button, IGraphics::CORNER_ALL))
				NewPage = PAGE_SERVER_INFO;
			MenubarTrackActive(PAGE_SERVER_INFO, Button);

			Box.VSplitLeft(OnlineTabGap, nullptr, &Box);
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_NetworkButton;
			if(DoIngameMenuTab(&s_NetworkButton, PAGE_NETWORK, "ingame-tab-browser", Localize("Browser"), ActivePage == PAGE_NETWORK, &Button, IGraphics::CORNER_ALL))
				NewPage = PAGE_NETWORK;
			MenubarTrackActive(PAGE_NETWORK, Button);

			if(GameClient()->m_GameInfo.m_Race)
			{
				Box.VSplitLeft(OnlineTabGap, nullptr, &Box);
				Box.VSplitLeft(GhostButtonWidth, &Button, &Box);
				static CButtonContainer s_GhostButton;
				if(DoIngameMenuTab(&s_GhostButton, PAGE_GHOST, "ingame-tab-ghost", Localize("Ghost"), ActivePage == PAGE_GHOST, &Button, IGraphics::CORNER_ALL))
					NewPage = PAGE_GHOST;
				MenubarTrackActive(PAGE_GHOST, Button);
			}

			Box.VSplitLeft(OnlineTabGap, nullptr, &Box);
			Box.VSplitLeft(CallVoteButtonWidth, &Button, &Box);
			static CButtonContainer s_CallVoteButton;
			if(DoIngameMenuTab(&s_CallVoteButton, PAGE_CALLVOTE, "ingame-tab-call-vote", Localize("Call vote"), ActivePage == PAGE_CALLVOTE, &Button, IGraphics::CORNER_ALL))
			{
				NewPage = PAGE_CALLVOTE;
				m_ControlPageOpening = true;
			}
			MenubarTrackActive(PAGE_CALLVOTE, Button);

			if(Box.w >= 10.0f + 33.0f + 10.0f)
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

				Box.VSplitRight(10.0f, &Box, nullptr);
				Box.VSplitRight(33.0f, &Box, &Button);
				static CButtonContainer s_DemoButton;
				CUIRect DemoButton = Button;
				const float CircleSize = minimum(DemoButton.w, DemoButton.h);
				DemoButton.x += (DemoButton.w - CircleSize) / 2.0f;
				DemoButton.w = CircleSize;
				if(DoMenuTabV2(&s_DemoButton, FONT_ICON_CLAPPERBOARD, ActivePage == PAGE_DEMOS, &DemoButton, IGraphics::CORNER_ALL, &IconButtonDefault, &IconButtonActive, &IconButtonHover, nullptr, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW))
				{
					NewPage = PAGE_DEMOS;
				}
				MenubarTrackActive(PAGE_DEMOS, DemoButton);
				GameClient()->m_Tooltips.DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
				Box.VSplitRight(10.0f, &Box, nullptr);

				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
		}
	}
	else
	{
		Box.VSplitRight(33.0f, &Box, &Button);
		static CButtonContainer s_QuitButton;
		ColorRGBA QuitColor(1.0f, 0.0f, 0.0f, 0.5f);
		if(DoButton_MenuTab(&s_QuitButton, FONT_ICON_POWER_OFF, 0, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_QUIT], nullptr, nullptr, &QuitColor, 10.0f))
		{
			if(GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0) || m_MenusIngameTouchControls.UnsavedChanges() || GameClient()->m_TouchControls.HasEditingChanges())
			{
				m_Popup = POPUP_QUIT;
			}
			else
			{
				Client()->Quit();
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&s_QuitButton, &Button, Localize("Quit"));

		Box.VSplitRight(10.0f, &Box, nullptr);
		Box.VSplitRight(33.0f, &Box, &Button);
		static CButtonContainer s_SettingsButton;
		if(DoButton_MenuTab(&s_SettingsButton, FONT_ICON_GEAR, ActivePage == PAGE_SETTINGS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_SETTINGS]))
		{
			NewPage = PAGE_SETTINGS;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_SettingsButton, &Button, Localize("Settings"));

		Box.VSplitRight(10.0f, &Box, nullptr);
		Box.VSplitRight(33.0f, &Box, &Button);
		static CButtonContainer s_EditorButton;
		if(DoButton_MenuTab(&s_EditorButton, FONT_ICON_PEN_TO_SQUARE, 0, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_EDITOR]))
		{
			g_Config.m_ClEditor = 1;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_EditorButton, &Button, Localize("Editor"));

		if(ClientState == IClient::STATE_OFFLINE)
		{
			Box.VSplitRight(10.0f, &Box, nullptr);
			Box.VSplitRight(33.0f, &Box, &Button);
			static CButtonContainer s_DemoButton;
			if(DoMenuTabV2(&s_DemoButton, FONT_ICON_CLAPPERBOARD, ActivePage == PAGE_DEMOS, &Button))
			{
				NewPage = PAGE_DEMOS;
			}
			MenubarTrackActive(PAGE_DEMOS, Button);
			GameClient()->m_Tooltips.DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
			Box.VSplitRight(10.0f, &Box, nullptr);

			Box.VSplitLeft(33.0f, &Button, &Box);

			bool GotNewsOrUpdate = false;

#if defined(CONF_AUTOUPDATE)
			int State = Updater()->GetCurrentState();
			bool NeedUpdate = str_comp(Client()->LatestVersion(), "0");
			if(State == IUpdater::CLEAN && NeedUpdate)
			{
				GotNewsOrUpdate = true;
			}
#endif

			GotNewsOrUpdate |= (bool)g_Config.m_UiUnreadNews;

			ColorRGBA HomeButtonColorAlert(0.0f, 1.0f, 0.0f, 0.25f);
			ColorRGBA HomeButtonColorAlertHover(0.0f, 1.0f, 0.0f, 0.5f);
			ColorRGBA *pHomeButtonColor = nullptr;
			ColorRGBA *pHomeButtonColorHover = nullptr;

			const char *pHomeScreenButtonLabel = FONT_ICON_HOUSE;
			if(GotNewsOrUpdate)
			{
				pHomeScreenButtonLabel = FONT_ICON_NEWSPAPER;
				pHomeButtonColor = &HomeButtonColorAlert;
				pHomeButtonColorHover = &HomeButtonColorAlertHover;
			}

			static CButtonContainer s_StartButton;
			if(DoButton_MenuTab(&s_StartButton, pHomeScreenButtonLabel, false, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_HOME], pHomeButtonColor, pHomeButtonColor, pHomeButtonColorHover, 10.0f))
			{
				m_ShowStart = true;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_StartButton, &Button, Localize("Main menu"));

			const float BrowserButtonWidth = 75.0f;
			Box.VSplitLeft(10.0f, nullptr, &Box);
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_InternetButton;
			if(DoButton_MenuTab(&s_InternetButton, FONT_ICON_EARTH_AMERICAS, ActivePage == PAGE_INTERNET, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_INTERNET]))
			{
				NewPage = PAGE_INTERNET;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_InternetButton, &Button, Localize("Internet"));

			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_LanButton;
			if(DoButton_MenuTab(&s_LanButton, FONT_ICON_NETWORK_WIRED, ActivePage == PAGE_LAN, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_LAN]))
			{
				NewPage = PAGE_LAN;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_LanButton, &Button, Localize("LAN"));

			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_FavoritesButton;
			if(DoButton_MenuTab(&s_FavoritesButton, FONT_ICON_STAR, ActivePage == PAGE_FAVORITES, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_FAVORITES]))
			{
				NewPage = PAGE_FAVORITES;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_FavoritesButton, &Button, Localize("Favorites"));

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_FavoriteMapsButton;
			if(DoButton_MenuTab(&s_FavoriteMapsButton, "", ActivePage == PAGE_FAVORITE_MAPS, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_FAVORITE_MAPS]))
			{
				NewPage = PAGE_FAVORITE_MAPS;
			}
			RenderFavoriteMapsIcon(Button);
			GameClient()->m_Tooltips.DoToolTip(&s_FavoriteMapsButton, &Button, Localize("Favorite map"));

			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

			int MaxPage = PAGE_FAVORITES + ServerBrowser()->FavoriteCommunities().size();
			if(
				!Ui()->IsPopupOpen() &&
				CLineInput::GetActiveInput() == nullptr &&
				((g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= MaxPage) || g_Config.m_UiPage == PAGE_FAVORITE_MAPS) &&
				((m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5) || m_MenuPage == PAGE_FAVORITE_MAPS))
			{
				if(Input()->KeyPress(KEY_RIGHT))
				{
					if(g_Config.m_UiPage == PAGE_FAVORITES)
					{
						NewPage = PAGE_FAVORITE_MAPS;
					}
					else if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
					{
						NewPage = ServerBrowser()->FavoriteCommunities().empty() ? PAGE_INTERNET : PAGE_FAVORITE_COMMUNITY_1;
					}
					else
					{
						NewPage = g_Config.m_UiPage + 1;
					}
					if(NewPage > MaxPage && NewPage != PAGE_FAVORITE_MAPS)
						NewPage = PAGE_INTERNET;
				}
				if(Input()->KeyPress(KEY_LEFT))
				{
					if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
					{
						NewPage = PAGE_FAVORITES;
					}
					else if(!ServerBrowser()->FavoriteCommunities().empty() && g_Config.m_UiPage == PAGE_FAVORITE_COMMUNITY_1)
					{
						NewPage = PAGE_FAVORITE_MAPS;
					}
					else
					{
						NewPage = g_Config.m_UiPage - 1;
					}
					if(NewPage < PAGE_INTERNET)
						NewPage = ServerBrowser()->FavoriteCommunities().empty() ? PAGE_FAVORITE_MAPS : MaxPage;
				}
			}

			size_t FavoriteCommunityIndex = 0;
			static CButtonContainer s_aFavoriteCommunityButtons[5];
			static const uint64_t s_FavoriteCommunityAppearScopeHash = static_cast<uint64_t>(str_quickhash("menu_favorite_community_tab_appear"));
			CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
			CUiV2Tree &Tree = GameClient()->UiRuntimeV2()->Tree();
			SUiAnimTransition AppearTransition;
			AppearTransition.m_DurationSec = 0.18f;
			AppearTransition.m_Easing = EEasing::EASE_OUT;
			static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)PAGE_FAVORITE_COMMUNITY_5 - PAGE_FAVORITE_COMMUNITY_1 + 1);
			static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)BIT_TAB_FAVORITE_COMMUNITY_5 - BIT_TAB_FAVORITE_COMMUNITY_1 + 1);
			static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)IServerBrowser::TYPE_FAVORITE_COMMUNITY_5 - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 + 1);
			for(const CCommunity *pCommunity : ServerBrowser()->FavoriteCommunities())
			{
				if(Box.w < BrowserButtonWidth)
					break;
				Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);

				const uint64_t NodeKey = BuildUiAnimNodeKey(s_FavoriteCommunityAppearScopeHash, static_cast<uint64_t>(str_quickhash(pCommunity->Id())));
				const SUiPresenceResult Presence = Tree.ResolvePresence(AnimRuntime, NodeKey, true, AppearTransition);
				const float AppearStrength = std::clamp(Presence.m_Alpha, 0.0f, 1.0f);
				const float RevealWidth = maximum(2.0f, Button.w * AppearStrength);
				CUIRect AnimatedButton = Button;
				AnimatedButton.x += (Button.w - RevealWidth) * 0.5f;
				AnimatedButton.w = RevealWidth;

				ColorRGBA InactiveColor = ms_ColorTabbarInactive;
				ColorRGBA ActiveColor = ms_ColorTabbarActive;
				ColorRGBA HoverColor = ms_ColorTabbarHover;
				InactiveColor.a *= AppearStrength;
				ActiveColor.a *= AppearStrength;
				HoverColor.a *= AppearStrength;

				const int Page = PAGE_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex;
				if(DoButton_MenuTab(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], FONT_ICON_ELLIPSIS, ActivePage == Page, &AnimatedButton, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIT_TAB_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex], &InactiveColor, &ActiveColor, &HoverColor, 10.0f, m_CommunityIcons.Find(pCommunity->Id())))
				{
					NewPage = Page;
				}
				GameClient()->m_Tooltips.DoToolTip(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], &AnimatedButton, pCommunity->Name());

				++FavoriteCommunityIndex;
				if(FavoriteCommunityIndex >= std::size(s_aFavoriteCommunityButtons))
					break;
			}

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else
		{
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

			const bool CompactOnlineMenuTabs = Graphics()->ScreenAspect() <= 1.45f || Box.w < 690.0f;
			const float GameButtonWidth = CompactOnlineMenuTabs ? 78.0f : 90.0f;
			const float PlayersButtonWidth = CompactOnlineMenuTabs ? 78.0f : 90.0f;
			const float ServerInfoButtonWidth = CompactOnlineMenuTabs ? 112.0f : 130.0f;
			const float BrowserButtonWidth = CompactOnlineMenuTabs ? 78.0f : 90.0f;
			const float GhostButtonWidth = CompactOnlineMenuTabs ? 78.0f : 90.0f;
			const float CallVoteButtonWidth = CompactOnlineMenuTabs ? 88.0f : 100.0f;
			const float CallVoteSpacing = CompactOnlineMenuTabs ? 2.0f : 4.0f;

			Box.VSplitLeft(GameButtonWidth, &Button, &Box);
			static CButtonContainer s_GameButton;
			if(DoIngameMenuTab(&s_GameButton, PAGE_GAME, "ingame-tab-game", Localize("Game"), ActivePage == PAGE_GAME, &Button, IGraphics::CORNER_TL))
				NewPage = PAGE_GAME;

			Box.VSplitLeft(PlayersButtonWidth, &Button, &Box);
			static CButtonContainer s_PlayersButton;
			if(DoIngameMenuTab(&s_PlayersButton, PAGE_PLAYERS, "ingame-tab-players", Localize("Players"), ActivePage == PAGE_PLAYERS, &Button, IGraphics::CORNER_NONE))
				NewPage = PAGE_PLAYERS;

			Box.VSplitLeft(ServerInfoButtonWidth, &Button, &Box);
			static CButtonContainer s_ServerInfoButton;
			if(DoIngameMenuTab(&s_ServerInfoButton, PAGE_SERVER_INFO, "ingame-tab-server-info", Localize("Server info"), ActivePage == PAGE_SERVER_INFO, &Button, IGraphics::CORNER_NONE))
				NewPage = PAGE_SERVER_INFO;

			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			static CButtonContainer s_NetworkButton;
			if(DoIngameMenuTab(&s_NetworkButton, PAGE_NETWORK, "ingame-tab-browser", Localize("Browser"), ActivePage == PAGE_NETWORK, &Button, IGraphics::CORNER_NONE))
				NewPage = PAGE_NETWORK;

			if(GameClient()->m_GameInfo.m_Race)
			{
				Box.VSplitLeft(GhostButtonWidth, &Button, &Box);
				static CButtonContainer s_GhostButton;
				if(DoIngameMenuTab(&s_GhostButton, PAGE_GHOST, "ingame-tab-ghost", Localize("Ghost"), ActivePage == PAGE_GHOST, &Button, IGraphics::CORNER_NONE))
					NewPage = PAGE_GHOST;
			}

			Box.VSplitLeft(CallVoteButtonWidth, &Button, &Box);
			Box.VSplitLeft(CallVoteSpacing, nullptr, &Box);
			static CButtonContainer s_CallVoteButton;
			if(DoIngameMenuTab(&s_CallVoteButton, PAGE_CALLVOTE, "ingame-tab-call-vote", Localize("Call vote"), ActivePage == PAGE_CALLVOTE, &Button, IGraphics::CORNER_TR))
			{
				NewPage = PAGE_CALLVOTE;
				m_ControlPageOpening = true;
			}

			if(Box.w >= 10.0f + 33.0f + 10.0f)
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

				Box.VSplitRight(10.0f, &Box, nullptr);
				Box.VSplitRight(33.0f, &Box, &Button);
				static CButtonContainer s_DemoButton;
				if(DoButton_MenuTab(&s_DemoButton, FONT_ICON_CLAPPERBOARD, ActivePage == PAGE_DEMOS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_DEMOBUTTON]))
				{
					NewPage = PAGE_DEMOS;
				}
				GameClient()->m_Tooltips.DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
				Box.VSplitRight(10.0f, &Box, nullptr);

				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
		}
	}

	// feat-004: draw a 2px ACCENT_PRIMARY underline below the active page tab.
	// This includes page-shaped icon buttons such as Settings/Demos, but still
	// excludes pure action buttons like Quit/Editor.
	if(UseNewUi && MenubarHaveActive && !Ui()->RenderOnly())
	{
		CUIRect IndicatorTarget;
		IndicatorTarget.x = MenubarActiveRect.x + MenubarActiveRect.w * 0.15f;
		IndicatorTarget.y = MenubarActiveRect.y + MenubarActiveRect.h - 3.0f;
		IndicatorTarget.w = MenubarActiveRect.w * 0.70f;
		IndicatorTarget.h = 3.0f;

		const uint64_t IndicatorNode = BuildUiAnimNodeKey(MakeUiScopeHash("menubar_v2_indicator"), static_cast<uint64_t>(ClientState));
		CUiV2AnimationRuntime &AnimRt = GameClient()->UiRuntimeV2()->AnimRuntime();
		const CUIRect IndicatorRect = ResolveUiAnimValueRect(AnimRt, IndicatorNode, IndicatorTarget, ui_curve::EMPHASIZED.m_DurationSec, ui_curve::EMPHASIZED.m_Easing);
		const ColorRGBA IndicatorColor = g_Config.m_QmNewUi != 0 ? MenuUiColorAccent(1.0f) : ui_token::color::ACCENT_PRIMARY;
		IndicatorRect.Draw(IndicatorColor, IGraphics::CORNER_ALL, 1.5f);
	}

	// Draw a 2px ui_color underline below the active tab. The X/W position
	// eases between tabs via the v2 runtime so changing pages glides instead of
	// snapping. Indicator is omitted when there is no determinable active tab.
	if(!UseNewUi && MenubarHaveActive && !Ui()->RenderOnly())
	{
		CUIRect IndicatorTarget;
		IndicatorTarget.x = MenubarActiveRect.x + MenubarActiveRect.w * 0.15f;
		IndicatorTarget.y = MenubarActiveRect.y + MenubarActiveRect.h - 2.0f;
		IndicatorTarget.w = MenubarActiveRect.w * 0.70f;
		IndicatorTarget.h = 2.0f;

		const uint64_t IndicatorNode = BuildUiAnimNodeKey(MakeUiScopeHash("menubar_v2_indicator"), static_cast<uint64_t>(ClientState));
		CUiV2AnimationRuntime &AnimRt = GameClient()->UiRuntimeV2()->AnimRuntime();
		const CUIRect IndicatorRect = ResolveUiAnimValueRect(AnimRt, IndicatorNode, IndicatorTarget, ui_curve::EMPHASIZED.m_DurationSec, ui_curve::EMPHASIZED.m_Easing);
		IndicatorRect.Draw(MenuUiColorAccent(1.0f), IGraphics::CORNER_NONE, 0.0f);
	}

	if(NewPage != -1)
	{
		if(ClientState == IClient::STATE_OFFLINE)
			SetMenuPage(NewPage);
		else
			SetGamePage(NewPage);
	}
}

void CMenus::StartLoading(int Total)
{
	m_LoadingState.m_Current = 0;
	m_LoadingState.m_Total = Total;
}

void CMenus::RenderLoading(const char *pCaption, const char *pContent, int IncreaseCounter)
{
	CUiScopedGaussianBlur GaussianBlurScope(Ui());
	// TODO: not supported right now due to separate render thread

	const int CurLoadRenderCount = m_LoadingState.m_Current;
	m_LoadingState.m_Current += IncreaseCounter;
	dbg_assert(m_LoadingState.m_Current <= m_LoadingState.m_Total, "Invalid progress for RenderLoading");

	// make sure that we don't render for each little thing we load
	// because that will slow down loading if we have vsync
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	if(Now - m_LoadingState.m_LastRender < std::chrono::nanoseconds(1s) / 60l)
		return;

	// need up date this here to get correct
	ms_GuiColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));

	Ui()->MapScreen();

	if(GameClient()->m_MenuBackground.IsLoading())
	{
		// Avoid rendering while loading the menu background as this would otherwise
		// cause the regular menu background to be rendered for a few frames while
		// the menu background is not loaded yet.
		return;
	}
	if(!GameClient()->m_MenuBackground.Render())
	{
		RenderBackground();
	}

	m_LoadingState.m_LastRender = Now;

	CUIRect Box;
	const CUIRect Screen = *Ui()->Screen();
	Screen.Margin(QmUiCenteredMargin(Screen, 160.0f, 320.0f, 180.0f), &Box);

	Graphics()->TextureClear();
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 15.0f);
	Box.Margin(20.0f, &Box);

	CUIRect Label;
	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, pCaption, 24.0f, TEXTALIGN_MC);

	Box.HSplitTop(20.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, pContent, 20.0f, TEXTALIGN_MC);

	if(m_LoadingState.m_Total > 0)
	{
		CUIRect ProgressBar;
		Box.HSplitBottom(30.0f, &Box, nullptr);
		Box.HSplitBottom(25.0f, &Box, &ProgressBar);
		ProgressBar.VMargin(20.0f, &ProgressBar);
		Ui()->RenderProgressBar(ProgressBar, CurLoadRenderCount / (float)m_LoadingState.m_Total);
	}

	Graphics()->SetColor(1.0, 1.0, 1.0, 1.0);

	GameClient()->UpdateAndSwapClient();
}

void CMenus::FinishLoading()
{
	m_LoadingState.m_Current = 0;
	m_LoadingState.m_Total = 0;
}

void CMenus::RenderNews(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_NEWS);

	g_Config.m_UiUnreadNews = false;

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.VSplitLeft(15.0f, nullptr, &MainView);

	CUIRect Label;

	const char *pStr = Client()->News();
	char aLine[256];
	while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
	{
		const int Len = str_length(aLine);
		if(Len > 0 && aLine[0] == '|' && aLine[Len - 1] == '|')
		{
			MainView.HSplitTop(30.0f, &Label, &MainView);
			aLine[Len - 1] = '\0';
			Ui()->DoLabel(&Label, aLine + 1, 20.0f, TEXTALIGN_ML);
		}
		else
		{
			MainView.HSplitTop(20.0f, &Label, &MainView);
			Ui()->DoLabel(&Label, aLine, 15.f, TEXTALIGN_ML);
		}
	}
}

void CMenus::RenderStatistics(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_NEWS);

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);

	CUIRect Window = MainView;
	Window.Margin(20.0f, &Window);
	Window.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);

	CUIRect Header, Content;
	Window.HSplitTop(50.0f, &Header, &Content);
	Header.VMargin(14.0f, &Header);
	CUIRect HeaderTitle, HeaderSubTitle;
	Header.HSplitTop(24.0f, &HeaderTitle, &HeaderSubTitle);
	Ui()->DoLabel(&HeaderTitle, Localize("Stats"), 24.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&HeaderSubTitle, Localize("Client data overview"), 12.0f, TEXTALIGN_ML);

	Content.Margin(12.0f, &Content);

	const int64_t LocalUptimeSeconds = static_cast<int64_t>(std::max(0.0f, Client()->LocalTime()));
	int64_t CurrentTimestamp = time_timestamp();
	int64_t StartupTimestamp = std::max<int64_t>(0, CurrentTimestamp - LocalUptimeSeconds);
	const bool UsingServerTime = GameClient()->m_QmClient.HasQmServerTime();
	if(UsingServerTime)
	{
		CurrentTimestamp = GameClient()->m_QmClient.QmServerTimeNow();
		if(GameClient()->m_QmClient.QmServerSessionStartTime() > 0)
			StartupTimestamp = GameClient()->m_QmClient.QmServerSessionStartTime();
		else
			StartupTimestamp = std::max<int64_t>(0, CurrentTimestamp - LocalUptimeSeconds);
	}
	int64_t UptimeSeconds = std::max<int64_t>(0, CurrentTimestamp - StartupTimestamp);
	if(GameClient()->m_QmClient.HasQmServerPlaytime())
		UptimeSeconds = GameClient()->m_QmClient.QmServerPlaytimeSeconds();

	char aUptime[64];
	str_time(UptimeSeconds * 100, TIME_HOURS, aUptime, sizeof(aUptime));

	const int FinishedMaps = GameClient()->m_QmClient.QmDdnetTotalFinishes();

	char aFinishedMapsText[32];
	if(FinishedMaps >= 0)
		str_format(aFinishedMapsText, sizeof(aFinishedMapsText), "%d", FinishedMaps);
	else
		str_copy(aFinishedMapsText, Localize("Loading"), sizeof(aFinishedMapsText));

	const char *pFavoritePartner = GameClient()->m_QmClient.QmDdnetFavoritePartner();
	char aFavoriteFriendText[160];
	if(pFavoritePartner && pFavoritePartner[0] != '\0')
		str_copy(aFavoriteFriendText, pFavoritePartner, sizeof(aFavoriteFriendText));
	else
		str_copy(aFavoriteFriendText, Localize("Loading"), sizeof(aFavoriteFriendText));

	std::unordered_map<std::string, int> aOnlineFriendCounts;
	for(int ServerIndex = 0; ServerIndex < ServerBrowser()->NumSortedServers(); ++ServerIndex)
	{
		const CServerInfo *pServerInfo = ServerBrowser()->SortedGet(ServerIndex);
		if(pServerInfo == nullptr)
			continue;

		for(int ClientIndex = 0; ClientIndex < pServerInfo->m_NumClients; ++ClientIndex)
		{
			const CServerInfo::CClient &ClientInfo = pServerInfo->m_aClients[ClientIndex];
			if(ClientInfo.m_FriendState != IFriends::FRIEND_PLAYER)
				continue;

			std::string Key(ClientInfo.m_aName);
			Key.push_back('\x1f');
			Key.append(ClientInfo.m_aClan);
			++aOnlineFriendCounts[Key];
		}
	}

	const int TotalFriends = GameClient()->Friends()->NumFriends();
	const int OnlineFriends = static_cast<int>(aOnlineFriendCounts.size());

	char aPointsText[32];
	if(Client()->Points() >= 0)
		str_format(aPointsText, sizeof(aPointsText), "%d", Client()->Points());
	else
		str_copy(aPointsText, Localize("Loading"), sizeof(aPointsText));

	char aFriendsText[32];
	str_format(aFriendsText, sizeof(aFriendsText), "%d", TotalFriends);

	char aOnlineFriendsText[32];
	str_format(aOnlineFriendsText, sizeof(aOnlineFriendsText), "%d", OnlineFriends);

	char aFinishSourceText[32];
	str_copy(aFinishSourceText, Localize("Official"), sizeof(aFinishSourceText));

	auto RenderStatCard = [this](const CUIRect &Rect, const char *pTitle, const char *pValue, const char *pHint) {
		Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.30f), IGraphics::CORNER_ALL, 8.0f);
		CUIRect Inner = Rect;
		Inner.Margin(10.0f, &Inner);

		CUIRect Title, Value, Hint;
		Inner.HSplitTop(16.0f, &Title, &Inner);
		Ui()->DoLabel(&Title, pTitle, 12.0f, TEXTALIGN_ML);

		Inner.HSplitTop(4.0f, nullptr, &Inner);
		Inner.HSplitTop(30.0f, &Value, &Inner);
		SLabelProperties ValueProps;
		ValueProps.m_MaxWidth = static_cast<int>(Value.w);
		Ui()->DoLabel(&Value, pValue, 18.0f, TEXTALIGN_ML, ValueProps);

		if(pHint != nullptr && pHint[0] != '\0')
		{
			Inner.HSplitTop(2.0f, nullptr, &Inner);
			Inner.HSplitTop(14.0f, &Hint, &Inner);
			SLabelProperties HintProps;
			HintProps.m_MaxWidth = static_cast<int>(Hint.w);
			Ui()->DoLabel(&Hint, pHint, 11.0f, TEXTALIGN_ML, HintProps);
		}
	};

	CUIRect TopCards;
	Content.HSplitTop(96.0f, &TopCards, &Content);
	const float CardGap = 8.0f;
	const float CardWidth = (TopCards.w - CardGap * 2.0f) / 3.0f;

	CUIRect CardStart, CardFinished, CardFriend, CardRest;
	TopCards.VSplitLeft(CardWidth, &CardStart, &CardRest);
	CardRest.VSplitLeft(CardGap, nullptr, &CardRest);
	CardRest.VSplitLeft(CardWidth, &CardFinished, &CardRest);
	CardRest.VSplitLeft(CardGap, nullptr, &CardRest);
	CardFriend = CardRest;

	RenderStatCard(CardStart, Localize("Client uptime"), aUptime, nullptr);
	RenderStatCard(CardFinished, Localize("Finished maps"), aFinishedMapsText, nullptr);
	RenderStatCard(CardFriend, Localize("Favorite friend"), aFavoriteFriendText, nullptr);

	Content.HSplitTop(10.0f, nullptr, &Content);

	CUIRect BottomLeft, BottomRight;
	Content.VSplitMid(&BottomLeft, &BottomRight, 6.0f);
	BottomLeft.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.23f), IGraphics::CORNER_ALL, 8.0f);
	BottomRight.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.23f), IGraphics::CORNER_ALL, 8.0f);

	auto RenderInfoRow = [this](CUIRect &View, const char *pLabel, const char *pValue) {
		CUIRect Row;
		View.HSplitTop(24.0f, &Row, &View);
		Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f), IGraphics::CORNER_ALL, 5.0f);
		Row.VMargin(8.0f, &Row);

		CUIRect Label, Value;
		Row.VSplitLeft(Row.w * 0.58f, &Label, &Value);
		Ui()->DoLabel(&Label, pLabel, 12.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Value, pValue, 12.0f, TEXTALIGN_MR);

		View.HSplitTop(4.0f, nullptr, &View);
	};

	CUIRect LeftContent = BottomLeft;
	LeftContent.Margin(10.0f, &LeftContent);
	CUIRect LeftTitle;
	LeftContent.HSplitTop(20.0f, &LeftTitle, &LeftContent);
	Ui()->DoLabel(&LeftTitle, Localize("Overview"), 14.0f, TEXTALIGN_ML);
	LeftContent.HSplitTop(6.0f, nullptr, &LeftContent);
	RenderInfoRow(LeftContent, Localize("DDNet points"), aPointsText);
	RenderInfoRow(LeftContent, Localize("Total friends"), aFriendsText);
	RenderInfoRow(LeftContent, Localize("Online friends"), aOnlineFriendsText);
	RenderInfoRow(LeftContent, Localize("Finish data source"), aFinishSourceText);

	CUIRect RightContent = BottomRight;
	RightContent.Margin(10.0f, &RightContent);
	CUIRect RightTitle, RightBody;
	RightContent.HSplitTop(20.0f, &RightTitle, &RightBody);
	Ui()->DoLabel(&RightTitle, Localize("Stats notes"), 14.0f, TEXTALIGN_ML);
	RightBody.HSplitTop(6.0f, nullptr, &RightBody);

	char aNowTime[64];
	str_timestamp_ex((time_t)CurrentTimestamp, aNowTime, sizeof(aNowTime), FORMAT_SPACE);
	char aInfoText[512];
	str_format(aInfoText, sizeof(aInfoText),
		Localize("Current time: %s\n- Uptime and playtime are calculated from official JSON data\n- Same as above\n- Same as above above\n"),
		aNowTime);

	SLabelProperties InfoProps;
	InfoProps.m_MaxWidth = static_cast<int>(RightBody.w);
	Ui()->DoLabel(&RightBody, aInfoText, 12.0f, TEXTALIGN_TL, InfoProps);
}

void CMenus::OnInterfacesInit(CGameClient *pClient)
{
	CComponentInterfaces::OnInterfacesInit(pClient);
	m_MenusIngameTouchControls.OnInterfacesInit(pClient);
	m_MenusSettingsControls.OnInterfacesInit(pClient);
	m_MenusStart.OnInterfacesInit(pClient);
	m_CommunityIcons.OnInterfacesInit(pClient);
}

void CMenus::OnInit()
{
	GameClient()->FrameScheduler()->Reset();

	if(g_Config.m_ClShowWelcome)
	{
		m_Popup = POPUP_LANGUAGE;
		m_CreateDefaultFavoriteCommunities = true;
	}

	if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 &&
		(size_t)(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1) >= ServerBrowser()->FavoriteCommunities().size())
	{
		// Reset page to internet when there is no favorite community for this page.
		g_Config.m_UiPage = PAGE_INTERNET;
	}

	if(g_Config.m_ClSkipStartMenu)
	{
		m_ShowStart = false;
	}
	m_MenuPage = g_Config.m_UiPage;

	m_RefreshButton.Init(Ui(), -1);
	m_ConnectButton.Init(Ui(), -1);

	Console()->Chain("add_favorite", ConchainFavoritesUpdate, this);
	Console()->Chain("remove_favorite", ConchainFavoritesUpdate, this);
	Console()->Chain("add_friend", ConchainFriendlistUpdate, this);
	Console()->Chain("remove_friend", ConchainFriendlistUpdate, this);
	Console()->Chain("friend_category_add", ConchainFriendlistUpdate, this);
	Console()->Chain("friend_category_rename", ConchainFriendlistUpdate, this);
	Console()->Chain("friend_category_remove", ConchainFriendlistUpdate, this);
	Console()->Chain("set_friend_category", ConchainFriendlistUpdate, this);

	Console()->Chain("add_excluded_community", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_community", ConchainCommunitiesUpdate, this);
	Console()->Chain("add_excluded_country", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_country", ConchainCommunitiesUpdate, this);
	Console()->Chain("add_excluded_type", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_type", ConchainCommunitiesUpdate, this);

	Console()->Chain("ui_page", ConchainUiPageUpdate, this);

	Console()->Chain("snd_enable", ConchainUpdateMusicState, this);
	Console()->Chain("snd_enable_music", ConchainUpdateMusicState, this);
	Console()->Chain("cl_background_entities", ConchainBackgroundEntities, this);

	Console()->Chain("cl_assets_entities", ConchainAssetsEntities, this);
	Console()->Chain("cl_asset_game", ConchainAssetGame, this);
	Console()->Chain("cl_asset_emoticons", ConchainAssetEmoticons, this);
	Console()->Chain("cl_asset_particles", ConchainAssetParticles, this);
	Console()->Chain("cl_asset_hud", ConchainAssetHud, this);
	Console()->Chain("cl_asset_extras", ConchainAssetExtras, this);

	Console()->Chain("demo_play", ConchainDemoPlay, this);
	Console()->Chain("demo_speed", ConchainDemoSpeed, this);

	m_TextureBlob = Graphics()->LoadTexture("blob.png", IStorage::TYPE_ALL);

	m_IsInit = true;
	LoadSettingsRuntimeCacheMetadata();

	// load menu images
	m_vMenuImages.clear();
	Storage()->ListDirectory(IStorage::TYPE_ALL, "menuimages", MenuImageScan, this);

	m_CommunityIcons.Load();

	// Quad for the direction arrows above the player
	m_DirectionQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadContainerAddSprite(m_DirectionQuadContainerIndex, 0.f, 0.f, 22.f);
	Graphics()->QuadContainerUpload(m_DirectionQuadContainerIndex);

	// Prewarm settings pages caches
	PrewarmSettingsPages();
}

void CMenus::EnsureSettingsBindCache()
{
	// NOLINTNEXTLINE(readability-identifier-naming)
	extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
	// NOLINTNEXTLINE(readability-identifier-naming)
	extern bool g_CommandBindCacheInitialized;

	if(g_CommandBindCacheInitialized)
		return;

	g_CommandBindCache.clear();
	g_CommandBindCache.reserve(64);
	for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; ++Mod)
	{
		for(int KeyId = 0; KeyId < KEY_LAST; ++KeyId)
		{
			const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
			if(!pBind[0])
				continue;
			g_CommandBindCache.try_emplace(pBind, KeyId, Mod);
		}
	}
	g_CommandBindCacheInitialized = true;
}

void CMenus::PrewarmSettingsPages()
{
	if(g_Config.m_QmSettingsPrewarm == 0)
		return;

	EnsureSettingsBindCache();

	// Preload skin list to avoid lag when first entering settings
	GameClient()->m_Skins.SkinList(0);
}

void CMenus::ConchainBackgroundEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CMenus *pSelf = (CMenus *)pUserData;
		char aNormalized[IO_MAX_PATH_LENGTH];
		NormalizeBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities, aNormalized, sizeof(aNormalized));
		if(str_comp(aNormalized, pSelf->GameClient()->m_Background.MapName()) != 0)
			pSelf->GameClient()->m_Background.LoadBackground();
	}
}

void CMenus::ConchainUpdateMusicState(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	auto *pSelf = (CMenus *)pUserData;
	if(pResult->NumArguments())
		pSelf->UpdateMusicState();
}

void CMenus::UpdateMusicState()
{
	const bool ShouldPlay = Client()->State() == IClient::STATE_OFFLINE && g_Config.m_SndEnable && g_Config.m_SndMusic;
	if(ShouldPlay && !GameClient()->m_Sounds.IsPlaying(SOUND_MENU))
		GameClient()->m_Sounds.Enqueue(CSounds::CHN_MUSIC, SOUND_MENU);
	else if(!ShouldPlay && GameClient()->m_Sounds.IsPlaying(SOUND_MENU))
		GameClient()->m_Sounds.Stop(SOUND_MENU);
}

void CMenus::PopupMessage(const char *pTitle, const char *pMessage, const char *pButtonLabel, int NextPopup, FPopupButtonCallback pfnButtonCallback)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	str_copy(m_aPopupTitle, pTitle);
	str_copy(m_aPopupMessage, pMessage);
	str_copy(m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, pButtonLabel);
	m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup = NextPopup;
	m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback = pfnButtonCallback;
	m_Popup = POPUP_MESSAGE;
}

void CMenus::PopupConfirm(const char *pTitle, const char *pMessage, const char *pConfirmButtonLabel, const char *pCancelButtonLabel,
	FPopupButtonCallback pfnConfirmButtonCallback, int ConfirmNextPopup, FPopupButtonCallback pfnCancelButtonCallback, int CancelNextPopup)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	str_copy(m_aPopupTitle, pTitle);
	str_copy(m_aPopupMessage, pMessage);
	str_copy(m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, pConfirmButtonLabel);
	m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup = ConfirmNextPopup;
	m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback = pfnConfirmButtonCallback;
	str_copy(m_aPopupButtons[BUTTON_CANCEL].m_aLabel, pCancelButtonLabel);
	m_aPopupButtons[BUTTON_CANCEL].m_NextPopup = CancelNextPopup;
	m_aPopupButtons[BUTTON_CANCEL].m_pfnCallback = pfnCancelButtonCallback;
	m_Popup = POPUP_CONFIRM;
}

void CMenus::PopupWarning(const char *pTopic, const char *pBody, const char *pButton, std::chrono::nanoseconds Duration)
{
	// no multiline support for console
	std::string BodyStr = pBody;
	std::replace(BodyStr.begin(), BodyStr.end(), '\n', ' ');
	log_warn("client", "%s: %s", pTopic, BodyStr.c_str());

	Ui()->SetActiveItem(nullptr);

	str_copy(m_aMessageTopic, pTopic);
	str_copy(m_aMessageBody, pBody);
	str_copy(m_aMessageButton, pButton);
	m_Popup = POPUP_WARNING;
	SetActive(true);

	m_PopupWarningDuration = Duration;
	m_PopupWarningLastTime = time_get_nanoseconds();
}

bool CMenus::CanDisplayWarning() const
{
	return m_Popup == POPUP_NONE;
}

void CMenus::Render()
{
	CUiScopedGaussianBlur GaussianBlurScope(Ui());
	CPerfTimer RenderTimer;
	m_MenuUiPerfScrollActive = false;
	Ui()->MapScreen();
	Ui()->SetMouseSlow(false);

	static int s_Frame = 0;
	if(s_Frame == 0)
	{
		RefreshBrowserTab(true);
		s_Frame++;
	}
	else if(s_Frame == 1)
	{
		UpdateMusicState();
		s_Frame++;
	}
	else
	{
		m_CommunityIcons.Update();
	}

	// Initially add DDNet as favorite community and select its tab.
	// This must be delayed until the DDNet info is available.
	if(m_CreateDefaultFavoriteCommunities &&
		ServerBrowser()->DDNetInfoAvailable())
	{
		m_CreateDefaultFavoriteCommunities = false;
		if(ServerBrowser()->Community(IServerBrowser::COMMUNITY_DDNET) != nullptr)
		{
			ServerBrowser()->FavoriteCommunitiesFilter().Clear();
			ServerBrowser()->FavoriteCommunitiesFilter().Add(IServerBrowser::COMMUNITY_DDNET);
			SetMenuPage(PAGE_FAVORITE_COMMUNITY_1);
			ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITE_COMMUNITY_1);
		}
	}
	if(m_JoinTutorial.m_Queued && m_Popup == POPUP_NONE)
	{
		const char *pAddr = ServerBrowser()->GetTutorialServer();
		if(pAddr)
		{
			Client()->Connect(pAddr);
		}
		else
		{
			m_Popup = POPUP_JOIN_TUTORIAL;
		}
		m_JoinTutorial.m_Queued = false;
	}

	// Determine the client state once before rendering because it can change
	// while rendering which causes frames with broken user interface.
	const IClient::EClientState ClientState = Client()->State();
	const int VisibleMenuPage = ClientState == IClient::STATE_ONLINE || ClientState == IClient::STATE_DEMOPLAYBACK ? m_GamePage : m_MenuPage;
	if(VisibleMenuPage != PAGE_SETTINGS)
		m_SettingsCardDeckDisplayState.LeaveSettings();

	if(ClientState == IClient::STATE_ONLINE || ClientState == IClient::STATE_DEMOPLAYBACK)
	{
		ms_ColorTabbarInactive = ms_ColorTabbarInactiveIngame;
		ms_ColorTabbarActive = ms_ColorTabbarActiveIngame;
		ms_ColorTabbarHover = ms_ColorTabbarHoverIngame;
	}
	else
	{
		if(!GameClient()->m_MenuBackground.Render())
		{
			RenderBackground();
		}
		ms_ColorTabbarInactive = ms_ColorTabbarInactiveOutgame;
		ms_ColorTabbarActive = ms_ColorTabbarActiveOutgame;
		ms_ColorTabbarHover = ms_ColorTabbarHoverOutgame;
	}

	CUIRect Screen = *Ui()->Screen();
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK || m_Popup != POPUP_NONE)
	{
		Screen.Margin(10.0f, &Screen);
	}

	switch(ClientState)
	{
	case IClient::STATE_QUITTING:
	case IClient::STATE_RESTARTING:
		// Render nothing except menu background. This should not happen for more than one frame.
		{
			const double TotalDurationMs = RenderTimer.ElapsedMs();
			LogPerfStage(Client(), "menus_render_total", TotalDurationMs, true, "state=shutdown");
			RecordSettingsPerfWindowFrame(TotalDurationMs);
		}
		return;

	case IClient::STATE_CONNECTING:
	{
		CPerfTimer StageTimer;
		RenderPopupConnecting(Screen);
		LogPerfStage(Client(), "popup_connecting", StageTimer.ElapsedMs());
	}
	break;

	case IClient::STATE_LOADING:
	{
		CPerfTimer StageTimer;
		RenderPopupLoading(Screen);
		LogPerfStage(Client(), "popup_loading", StageTimer.ElapsedMs());
	}
	break;

	case IClient::STATE_OFFLINE:
		if(m_Popup != POPUP_NONE)
		{
			CPerfTimer StageTimer;
			RenderPopupFullscreen(Screen);
			LogPerfStage(Client(), "popup_fullscreen", StageTimer.ElapsedMs());
		}
		else if(m_ShowStart)
		{
			CPerfTimer StageTimer;
			m_MenusStart.RenderStartMenu(Screen);
			LogPerfStage(Client(), "start_menu", StageTimer.ElapsedMs());
		}
		else
		{
			CPerfTimer ShellTimer;
			CUIRect TabBar, MainView;
			const bool UseNewUi = g_Config.m_QmNewUi != 0;
			Screen.HSplitTop(MenuMenubarHeight(UseNewUi), &TabBar, &MainView);
			if(UseNewUi)
				MainView.HSplitTop(6.0f, nullptr, &MainView);
			const CUIRect MainViewClip = MainView;
			const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("menu_page_switch"));
			const bool TransitionActive = TransitionStrength > 0.0f && m_MenuPageTransitionDirection != 0.0f;
			const bool ContentTransitionActive = TransitionActive && m_MenuPage != PAGE_SETTINGS;
			const char *pPageName = MenuPageName(m_MenuPage);
			if(m_MenuPage != PAGE_SETTINGS)
				PrepareSettingsTabLabelCache(MainView.w);
			if(ContentTransitionActive)
			{
				ApplyUiSwitchOffset(MainView, TransitionStrength, m_MenuPageTransitionDirection, false, 0.04f, 18.0f, 48.0f);
				Ui()->ClipEnable(&MainViewClip);
			}

			std::optional<CScopedMenuTextVisibleGuard> TextVisibleGuard;
			if(m_MenuPage == PAGE_SETTINGS)
				TextVisibleGuard.emplace(this);

			CPerfTimer ContentTimer;
			const bool ScrollInputActive =
				Input()->KeyPress(KEY_MOUSE_WHEEL_UP) ||
				Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) ||
				Input()->KeyPress(KEY_MOUSE_WHEEL_LEFT) ||
				Input()->KeyPress(KEY_MOUSE_WHEEL_RIGHT);
			const bool CanPrewarmSettings = SettingsRuntimeWarmupShouldRun(
				g_Config.m_QmSettingsPrewarm != 0,
				m_MenuPage == PAGE_SETTINGS,
				Ui()->ActiveItem() != nullptr,
				Ui()->HotItem() != nullptr,
				ScrollInputActive,
				m_SettingsPageSwitchActive || TransitionActive,
				m_SettingsScrollActive);
			if(CanPrewarmSettings)
				PrewarmVisibleSettingsResources(MainView);
			if(m_MenuPage == PAGE_NEWS)
			{
				RenderNews(MainView);
			}
			else if(m_MenuPage == PAGE_STATS)
			{
				RenderStatistics(MainView);
			}
			else if((m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5) || m_MenuPage == PAGE_FAVORITE_MAPS)
			{
				RenderServerbrowser(MainView, true);
			}
			else if(m_MenuPage == PAGE_DEMOS)
			{
				RenderDemoBrowser(MainView);
			}
			else if(m_MenuPage == PAGE_SETTINGS)
			{
				RenderSettings(MainView);
			}
			else
			{
				dbg_assert_failed("Invalid m_MenuPage: %d", m_MenuPage);
			}
			char aContentExtra[128];
			str_format(aContentExtra, sizeof(aContentExtra), "page=%s transition=%d", pPageName, TransitionActive ? 1 : 0);
			LogPerfStage(Client(), "offline_page_content", ContentTimer.ElapsedMs(), TransitionActive, aContentExtra);
			if(Client()->State() != ClientState)
			{
				if(ContentTransitionActive)
					Ui()->ClipDisable();
				const double TotalDurationMs = RenderTimer.ElapsedMs();
				LogPerfStage(Client(), "menus_render_total", TotalDurationMs, true, "state=changed_during_offline_content");
				RecordSettingsPerfWindowFrame(TotalDurationMs);
				return;
			}

			if(ContentTransitionActive)
			{
				Ui()->ClipDisable();
			}
			{
				CPerfTimer StageTimer;
				RenderMenubar(TabBar, ClientState);
				char aMenubarExtra[128];
				str_format(aMenubarExtra, sizeof(aMenubarExtra), "page=%s state=%s", pPageName, ClientStateName(ClientState));
				LogPerfStage(Client(), "menu_menubar", StageTimer.ElapsedMs(), TransitionActive, aMenubarExtra);
			}
		}
		break;

	case IClient::STATE_ONLINE:
		if(m_Popup != POPUP_NONE)
		{
			CPerfTimer StageTimer;
			RenderPopupFullscreen(Screen);
			LogPerfStage(Client(), "popup_fullscreen", StageTimer.ElapsedMs());
		}
		else
		{
			CPerfTimer ShellTimer;
			CUIRect TabBar, MainView;
			const bool UseNewUi = g_Config.m_QmNewUi != 0;
			Screen.HSplitTop(MenuMenubarHeight(UseNewUi), &TabBar, &MainView);
			if(UseNewUi)
				MainView.HSplitTop(6.0f, nullptr, &MainView);
			const CUIRect MainViewClip = MainView;
			const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("game_page_switch"));
			const bool TransitionActive = TransitionStrength > 0.0f && m_GamePageTransitionDirection != 0.0f;
			const bool ContentTransitionActive = TransitionActive && m_GamePage != PAGE_SETTINGS;
			const char *pPageName = GamePageName(m_GamePage);
			const char *pOperationName = SettingsPerfActiveOperation();
			if(pOperationName == nullptr || pOperationName[0] == '\0' || str_comp(pOperationName, "none") == 0)
				pOperationName = "ingame_esc_open";
			char aEscPerfExtra[160];
			str_format(aEscPerfExtra, sizeof(aEscPerfExtra), "operation=%s context=online page=%s tab=none frame=%" PRIu64, pOperationName, pPageName, Client()->PerfFrame());
			if(m_GamePage != PAGE_SETTINGS)
				PrepareSettingsTabLabelCache(MainView.w);
			if(ContentTransitionActive)
			{
				ApplyUiSwitchOffset(MainView, TransitionStrength, m_GamePageTransitionDirection, false, 0.04f, 18.0f, 48.0f);
				Ui()->ClipEnable(&MainViewClip);
			}

			std::optional<CScopedMenuTextVisibleGuard> TextVisibleGuard;
			if(m_GamePage == PAGE_SETTINGS)
				TextVisibleGuard.emplace(this);
			LogPerfStage(Client(), "ingame_esc_menu_shell", ShellTimer.ElapsedMs(), false, aEscPerfExtra);

			CPerfTimer ContentTimer;
			const bool ScrollInputActive =
				Input()->KeyPress(KEY_MOUSE_WHEEL_UP) ||
				Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) ||
				Input()->KeyPress(KEY_MOUSE_WHEEL_LEFT) ||
				Input()->KeyPress(KEY_MOUSE_WHEEL_RIGHT);
			const bool CanPrewarmSettings = SettingsRuntimeWarmupShouldRun(
				g_Config.m_QmSettingsPrewarm != 0,
				m_GamePage == PAGE_SETTINGS,
				Ui()->ActiveItem() != nullptr,
				Ui()->HotItem() != nullptr,
				ScrollInputActive,
				m_SettingsPageSwitchActive || TransitionActive,
				m_SettingsScrollActive);
			if(CanPrewarmSettings)
				PrewarmVisibleSettingsResources(MainView);
			if(m_GamePage == PAGE_GAME)
			{
				CPerfTimer StageTimer;
				RenderGame(MainView);
				RenderIngameHint();
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_PLAYERS)
			{
				CPerfTimer StageTimer;
				RenderPlayers(MainView);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_SERVER_INFO)
			{
				CPerfTimer StageTimer;
				RenderServerInfo(MainView);
				const double StageDurationMs = StageTimer.ElapsedMs();
				LogPerfStage(Client(), "ingame_server_info_layout", StageDurationMs, TransitionActive, aEscPerfExtra);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageDurationMs, TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_NETWORK)
			{
				CPerfTimer StageTimer;
				RenderInGameNetwork(MainView);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_GHOST)
			{
				CPerfTimer StageTimer;
				RenderGhost(MainView);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_UNFINISHED_MAPS)
			{
				CPerfTimer StageTimer;
				RenderUnfinishedMaps(MainView);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_CALLVOTE)
			{
				CPerfTimer StageTimer;
				RenderServerControl(MainView);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_DEMOS)
			{
				CPerfTimer StageTimer;
				RenderDemoBrowser(MainView);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else if(m_GamePage == PAGE_SETTINGS)
			{
				CPerfTimer StageTimer;
				RenderSettings(MainView);
				LogPerfStage(Client(), "ingame_esc_tab_content", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);
			}
			else
			{
				dbg_assert_failed("Invalid m_GamePage: %d", m_GamePage);
			}
			char aContentExtra[128];
			str_format(aContentExtra, sizeof(aContentExtra), "page=%s transition=%d", pPageName, TransitionActive ? 1 : 0);
			LogPerfStage(Client(), "ingame_page_content", ContentTimer.ElapsedMs(), TransitionActive, aContentExtra);
			if(Client()->State() != ClientState)
			{
				if(ContentTransitionActive)
					Ui()->ClipDisable();
				const double TotalDurationMs = RenderTimer.ElapsedMs();
				LogPerfStage(Client(), "menus_render_total", TotalDurationMs, true, "state=changed_during_ingame_content");
				RecordSettingsPerfWindowFrame(TotalDurationMs);
				return;
			}

			if(ContentTransitionActive)
			{
				Ui()->ClipDisable();
			}
			{
				CPerfTimer StageTimer;
				RenderMenubar(TabBar, ClientState);
				const double StageDurationMs = StageTimer.ElapsedMs();
				char aMenubarExtra[128];
				str_format(aMenubarExtra, sizeof(aMenubarExtra), "page=%s state=%s", pPageName, ClientStateName(ClientState));
				LogPerfStage(Client(), "menu_menubar", StageDurationMs, TransitionActive, aMenubarExtra);
			}
		}
		break;

	case IClient::STATE_DEMOPLAYBACK:
		if(m_Popup != POPUP_NONE)
		{
			CPerfTimer StageTimer;
			RenderPopupFullscreen(Screen);
			LogPerfStage(Client(), "popup_fullscreen", StageTimer.ElapsedMs());
		}
		else
		{
			CPerfTimer StageTimer;
			RenderDemoPlayer(Screen);
			LogPerfStage(Client(), "demo_player", StageTimer.ElapsedMs());
		}
		break;
	}

	{
		CPerfTimer StageTimer;
		Ui()->RenderPopupMenus();
		LogPerfStage(Client(), "popup_menus", StageTimer.ElapsedMs());
	}

	// Prevent UI elements from being hovered while a key reader is active
	if(GameClient()->m_KeyBinder.IsActive())
	{
		Ui()->SetHotItem(nullptr);
	}

	// Handle this escape hotkey after popup menus
	if(!m_ShowStart && ClientState == IClient::STATE_OFFLINE && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_ShowStart = true;
	}

	char aTotalExtra[96];
	str_format(aTotalExtra, sizeof(aTotalExtra), "state=%s active=%d", ClientStateName(ClientState), IsActive() ? 1 : 0);
	const double TotalDurationMs = RenderTimer.ElapsedMs();
	LogPerfStage(Client(), "menus_render_total", TotalDurationMs, false, aTotalExtra);
	RecordSettingsPerfWindowFrame(TotalDurationMs);
}

void CMenus::RenderPopupFullscreen(CUIRect Screen)
{
	char aBuf[1536];
	const char *pTitle = "";
	const char *pExtraText = "";
	const char *pButtonText = "";
	bool TopAlign = false;

	ColorRGBA BgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	if(m_Popup == POPUP_MESSAGE || m_Popup == POPUP_CONFIRM)
	{
		pTitle = m_aPopupTitle;
		pExtraText = m_aPopupMessage;
		TopAlign = true;
	}
	else if(m_Popup == POPUP_DISCONNECTED)
	{
		pTitle = Localize("Disconnected");
		pExtraText = Client()->ErrorString();
		pButtonText = Localize("Ok");
		if(Client()->ReconnectTime() > 0)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Reconnect in %d sec"), (int)((Client()->ReconnectTime() - time_get()) / time_freq()) + 1);
			pTitle = Client()->ErrorString();
			pExtraText = aBuf;
			pButtonText = Localize("Abort");
		}
	}
	else if(m_Popup == POPUP_RENAME_DEMO)
	{
		dbg_assert(m_DemolistSelectedIndex >= 0, "m_DemolistSelectedIndex invalid for POPUP_RENAME_DEMO");
		pTitle = m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir ? Localize("Rename folder") : (DemoBrowserBrowsingScreenshots() ? Localize("Rename screenshot") : Localize("Rename demo"));
	}
#if defined(CONF_VIDEORECORDER)
	else if(m_Popup == POPUP_RENDER_DEMO)
	{
		pTitle = Localize("Render demo");
	}
	else if(m_Popup == POPUP_RENDER_DONE)
	{
		pTitle = Localize("Render complete");
	}
#endif
	else if(m_Popup == POPUP_PASSWORD)
	{
		pTitle = Localize("Password incorrect");
		pButtonText = Localize("Try again");
	}
	else if(m_Popup == POPUP_RESTART)
	{
		pTitle = Localize("Restart");
		pExtraText = Localize("Are you sure that you want to restart?");
	}
	else if(m_Popup == POPUP_QUIT)
	{
		pTitle = Localize("Quit");
		pExtraText = Localize("Are you sure that you want to quit?");
	}
	else if(m_Popup == POPUP_FIRST_LAUNCH)
	{
		pTitle = Localize("Welcome to DDNet");
		str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s",
			Localize("DDraceNetwork is a cooperative online game where the goal is for you and your group of tees to reach the finish line of the map. As a newcomer you should start on Novice servers, which host the easiest maps. Consider the ping to choose a server close to you."),
			Localize("Use k key to kill (restart), q to pause and watch other players. See settings for other key binds."),
			Localize("It's recommended that you check the settings to adjust them to your liking before joining a server."),
			Localize("Please enter your nickname below."));
		pExtraText = aBuf;
		pButtonText = Localize("Ok");
		TopAlign = true;
	}
	else if(m_Popup == POPUP_JOIN_TUTORIAL)
	{
		pTitle = Localize("Joining Tutorial server");
	}
	else if(m_Popup == POPUP_POINTS)
	{
		pTitle = Localize("Existing Player");
		if(Client()->InfoState() == IClient::EInfoState::SUCCESS && Client()->Points() > 50)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Your nickname '%s' is already used (%d points). Do you still want to use it?"), Client()->PlayerName(), Client()->Points());
			pExtraText = aBuf;
			TopAlign = true;
		}
		else
		{
			pExtraText = Localize("Checking for existing player with your name");
		}
	}
	else if(m_Popup == POPUP_WARNING)
	{
		BgColor = ColorRGBA(0.5f, 0.0f, 0.0f, 0.7f);
		pTitle = m_aMessageTopic;
		pExtraText = m_aMessageBody;
		pButtonText = m_aMessageButton;
		TopAlign = true;
	}
	else if(m_Popup == POPUP_SAVE_SKIN)
	{
		pTitle = Localize("Save skin");
		pExtraText = Localize("Are you sure you want to save your skin? If a skin with this name already exists, it will be replaced.");
	}

	CUIRect Box, Part;
	Box = Screen;
	if(m_Popup != POPUP_FIRST_LAUNCH)
	{
		Box.Margin(QmUiCenteredMargin(Box, 150.0f, 300.0f, 300.0f), &Box);
	}

	// Background
	Box.Draw(BgColor, IGraphics::CORNER_ALL, 15.0f);

	// Title
	{
		CUIRect Title;
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitTop(24.0f, &Title, &Box);
		Box.HSplitTop(20.0f, nullptr, &Box);
		Title.VMargin(20.0f, &Title);

		const float TitleFontSize = 24.0f;
		if(TextRender()->TextWidth(TitleFontSize, pTitle) > Title.w)
			Ui()->DoLabel(&Title, pTitle, TitleFontSize, TEXTALIGN_ML, {.m_MaxWidth = Title.w});
		else
			Ui()->DoLabel(&Title, pTitle, TitleFontSize, TEXTALIGN_MC);
	}

	// Extra text (optional)
	if(m_Popup != POPUP_JOIN_TUTORIAL)
	{
		CUIRect ExtraText;
		Box.HSplitTop(24.0f, &ExtraText, &Box);
		ExtraText.VMargin(20.0f, &ExtraText);
		if(pExtraText[0] != '\0')
		{
			const float ExtraTextFontSize = m_Popup == POPUP_FIRST_LAUNCH ? 16.0f : 20.0f;

			if(TopAlign)
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_TL, {.m_MaxWidth = ExtraText.w});
			else if(TextRender()->TextWidth(ExtraTextFontSize, pExtraText) > ExtraText.w)
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_ML, {.m_MaxWidth = ExtraText.w});
			else
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_MC);
		}
	}

	if(m_Popup == POPUP_MESSAGE || m_Popup == POPUP_CONFIRM)
	{
		CUIRect ButtonBar;
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &ButtonBar);
		ButtonBar.VMargin(100.0f, &ButtonBar);

		if(m_Popup == POPUP_MESSAGE)
		{
			static CButtonContainer s_ButtonConfirm;
			if(DoButton_Menu(&s_ButtonConfirm, m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, 0, &ButtonBar) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback)();
			}
		}
		else if(m_Popup == POPUP_CONFIRM)
		{
			CUIRect CancelButton, ConfirmButton;
			ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, 40.0f);

			static CButtonContainer s_ButtonCancel;
			if(DoButton_Menu(&s_ButtonCancel, m_aPopupButtons[BUTTON_CANCEL].m_aLabel, 0, &CancelButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			{
				m_Popup = m_aPopupButtons[BUTTON_CANCEL].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CANCEL].m_pfnCallback)();
			}

			static CButtonContainer s_ButtonConfirm;
			if(DoButton_Menu(&s_ButtonConfirm, m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, 0, &ConfirmButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback)();
			}
		}
	}
	else if(m_Popup == POPUP_QUIT || m_Popup == POPUP_RESTART)
	{
		CUIRect Yes, No;
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		// additional info
		Box.VMargin(20.f, &Box);
		if(GameClient()->Editor()->HasUnsavedData())
		{
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s", Localize("There's an unsaved map in the editor, you might want to save it."), Localize("Continue anyway?"));
			Ui()->DoLabel(&Box, aBuf, 20.0f, TEXTALIGN_ML, {.m_MaxWidth = Part.w - 20.0f});
		}
		else if(GameClient()->m_TouchControls.HasEditingChanges() || m_MenusIngameTouchControls.UnsavedChanges())
		{
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s", Localize("There's an unsaved change in the touch controls editor, you might want to save it."), Localize("Continue anyway?"));
			Ui()->DoLabel(&Box, aBuf, 20.0f, TEXTALIGN_ML, {.m_MaxWidth = Part.w - 20.0f});
		}

		// buttons
		Part.VMargin(80.0f, &Part);
		Part.VSplitMid(&No, &Yes);
		Yes.VMargin(20.0f, &Yes);
		No.VMargin(20.0f, &No);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("No"), 0, &No) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonTryAgain;
		if(DoButton_Menu(&s_ButtonTryAgain, Localize("Yes"), 0, &Yes) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(m_Popup == POPUP_RESTART)
			{
				m_Popup = POPUP_NONE;
				Client()->Restart();
			}
			else
			{
				m_Popup = POPUP_NONE;
				Client()->Quit();
			}
		}
	}
	else if(m_Popup == POPUP_PASSWORD)
	{
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);
		Part.VMargin(100.0f, &Part);

		CUIRect TryAgain, Abort;
		Part.VSplitMid(&Abort, &TryAgain, 40.0f);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			m_Popup = POPUP_NONE;
		}

		const NETADDR *pServerAddr = Client()->ServerAddress();
		char aAddr[NETADDR_MAXSTRSIZE] = "";
		if(pServerAddr)
			net_addr_str(pServerAddr, aAddr, sizeof(aAddr), true);

		static CButtonContainer s_ButtonTryAgain;
		if(DoButton_Menu(&s_ButtonTryAgain, Localize("Try again"), 0, &TryAgain) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			Client()->Connect(aAddr, g_Config.m_Password);
		}

		Box.VMargin(60.0f, &Box);
		Box.HSplitBottom(32.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);

		CUIRect Label, TextBox;
		Part.VSplitLeft(100.0f, &Label, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Password"), 18.0f, TEXTALIGN_ML);
		IUiContext PasswordPopupTextInputCtx;
		PasswordPopupTextInputCtx.m_pUi = Ui();
		PasswordPopupTextInputCtx.m_ScopeHash = MakeUiScopeHash("password_popup_text_input");
		ui_widget::SInputFieldOptions PasswordInputOptions;
		PasswordInputOptions.m_Clearable = true;
		PasswordInputOptions.m_FontSize = 12.0f;
		ui_widget::InputField(PasswordPopupTextInputCtx, &m_PasswordInput, TextBox, PasswordInputOptions);

		Box.HSplitBottom(32.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);

		CUIRect Address;
		Part.VSplitLeft(100.0f, &Label, &Address);
		Address.VSplitLeft(20.0f, nullptr, &Address);
		Ui()->DoLabel(&Label, Localize("Address"), 18.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Address, aAddr, 18.0f, TEXTALIGN_ML);

		const CServerBrowser::CServerEntry *pEntry = pServerAddr ? ServerBrowser()->Find(*pServerAddr) : nullptr;
		if(pEntry != nullptr && pEntry->m_GotInfo)
		{
			const CCommunity *pCommunity = ServerBrowser()->Community(pEntry->m_Info.m_aCommunityId);
			const CCommunityIcon *pIcon = pCommunity == nullptr ? nullptr : m_CommunityIcons.Find(pCommunity->Id());

			Box.HSplitBottom(32.0f, &Box, nullptr);
			Box.HSplitBottom(24.0f, &Box, &Part);

			CUIRect Name;
			Part.VSplitLeft(100.0f, &Label, &Name);
			Name.VSplitLeft(20.0f, nullptr, &Name);
			if(pIcon != nullptr)
			{
				CUIRect Icon;
				static char s_CommunityTooltipButtonId;
				Name.VSplitLeft(2.5f * Name.h, &Icon, &Name);
				m_CommunityIcons.Render(pIcon, Icon, true);
				Ui()->DoButtonLogic(&s_CommunityTooltipButtonId, 0, &Icon, BUTTONFLAG_NONE);
				GameClient()->m_Tooltips.DoToolTip(&s_CommunityTooltipButtonId, &Icon, pCommunity->Name());
			}

			Ui()->DoLabel(&Label, Localize("Name"), 18.0f, TEXTALIGN_ML);
			Ui()->DoLabel(&Name, pEntry->m_Info.m_aName, 18.0f, TEXTALIGN_ML);
		}
	}
	else if(m_Popup == POPUP_LANGUAGE)
	{
		CUIRect Button;
		Screen.Margin(QmUiCenteredMargin(Screen, 150.0f, 300.0f, 300.0f), &Box);
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Button);
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.VMargin(20.0f, &Box);
		const bool Activated = RenderLanguageSelection(Box);
		Button.VMargin(120.0f, &Button);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, Localize("Ok"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Activated)
			m_Popup = POPUP_FIRST_LAUNCH;
	}
	else if(m_Popup == POPUP_RENAME_DEMO)
	{
		CUIRect Label, TextBox, Ok, Abort;
		IUiContext DemoRenameTextInputCtx;
		DemoRenameTextInputCtx.m_pUi = Ui();
		DemoRenameTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		DemoRenameTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		DemoRenameTextInputCtx.m_ScopeHash = MakeUiScopeHash("demo_rename_text_input");
		DemoRenameTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&Abort, &Ok);

		Ok.VMargin(20.0f, &Ok);
		Abort.VMargin(20.0f, &Abort);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			// rename demo
			CDemoItem *pSelectedDemoItem = m_vpFilteredDemos[m_DemolistSelectedIndex];
			const bool BrowsingScreenshots = DemoBrowserBrowsingScreenshots();
			char aBufOld[IO_MAX_PATH_LENGTH];
			str_format(aBufOld, sizeof(aBufOld), "%s/%s", m_aCurrentDemoFolder, pSelectedDemoItem->m_aFilename);
			char aBufNew[IO_MAX_PATH_LENGTH];
			str_format(aBufNew, sizeof(aBufNew), "%s/%s", m_aCurrentDemoFolder, m_DemoRenameInput.GetString());
			if(!pSelectedDemoItem->m_IsDir)
			{
				char aNameWithoutExt[IO_MAX_PATH_LENGTH];
				char aExtension[IO_MAX_PATH_LENGTH];
				fs_split_file_extension(pSelectedDemoItem->m_aFilename, aNameWithoutExt, sizeof(aNameWithoutExt), aExtension, sizeof(aExtension));
				if(aExtension[0] != '\0' && str_endswith_nocase(aBufNew, aExtension) == nullptr)
					str_append(aBufNew, aExtension);
			}

			if(str_comp(aBufOld, aBufNew) == 0)
			{
				// Nothing to rename, also same capitalization
			}
			else if(!str_valid_filename(m_DemoRenameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(str_utf8_comp_nocase(aBufOld, aBufNew) != 0 && // Allow renaming if it only changes capitalization to support case-insensitive filesystems
				Storage()->FileExists(aBufNew, pSelectedDemoItem->m_StorageType))
			{
				PopupMessage(Localize("Error"), BrowsingScreenshots ? Localize("A screenshot with this name already exists") : Localize("A demo with this name already exists"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(Storage()->FolderExists(aBufNew, pSelectedDemoItem->m_StorageType))
			{
				PopupMessage(Localize("Error"), Localize("A folder with this name already exists"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(Storage()->RenameFile(aBufOld, aBufNew, pSelectedDemoItem->m_StorageType))
			{
				str_copy(m_aCurrentDemoSelectionName, m_DemoRenameInput.GetString());
				if(!pSelectedDemoItem->m_IsDir)
					fs_split_file_extension(m_DemoRenameInput.GetString(), m_aCurrentDemoSelectionName, sizeof(m_aCurrentDemoSelectionName));
				DemolistPopulate();
				DemolistOnUpdate(false);
			}
			else
			{
				PopupMessage(Localize("Error"), pSelectedDemoItem->m_IsDir ? Localize("Unable to rename the folder") : (BrowsingScreenshots ? Localize("Unable to rename the screenshot") : Localize("Unable to rename the demo")), Localize("Ok"), POPUP_RENAME_DEMO);
			}
		}

		Box.HSplitBottom(60.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(60.0f, nullptr, &Label);
		Label.VSplitLeft(120.0f, nullptr, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		TextBox.VSplitRight(60.0f, &TextBox, nullptr);
		Ui()->DoLabel(&Label, Localize("New name:"), 18.0f, TEXTALIGN_ML);
		ui_widget::SInputFieldOptions RenameInputOptions;
		RenameInputOptions.m_pPlaceholder = Localize("New name");
		RenameInputOptions.m_FontSize = 12.0f;
		ui_widget::InputField(DemoRenameTextInputCtx, &m_DemoRenameInput, TextBox, RenameInputOptions);
	}
#if defined(CONF_VIDEORECORDER)
	else if(m_Popup == POPUP_RENDER_DEMO)
	{
		CUIRect Row, Ok, Abort;
		IUiContext DemoRenderTextInputCtx;
		DemoRenderTextInputCtx.m_pUi = Ui();
		DemoRenderTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		DemoRenderTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		DemoRenderTextInputCtx.m_ScopeHash = MakeUiScopeHash("demo_render_text_input");
		DemoRenderTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

		Box.VMargin(60.0f, &Box);
		Box.HMargin(20.0f, &Box);
		Box.HSplitBottom(24.0f, &Box, &Row);
		Box.HSplitBottom(40.0f, &Box, nullptr);
		Row.VMargin(40.0f, &Row);
		Row.VSplitMid(&Abort, &Ok, 40.0f);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			m_DemoRenderInput.Clear();
			m_HasPendingDemoRenderSource = false;
			m_Popup = POPUP_NONE;
		}

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			// render video
			char aVideoPath[IO_MAX_PATH_LENGTH];
			str_format(aVideoPath, sizeof(aVideoPath), "videos/%s", m_DemoRenderInput.GetString());
			if(!str_endswith(aVideoPath, ".mp4"))
				str_append(aVideoPath, ".mp4");

			if(!str_valid_filename(m_DemoRenderInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_RENDER_DEMO);
			}
			else if(Storage()->FolderExists(aVideoPath, IStorage::TYPE_SAVE))
			{
				PopupMessage(Localize("Error"), Localize("A folder with this name already exists"), Localize("Ok"), POPUP_RENDER_DEMO);
			}
			else if(Storage()->FileExists(aVideoPath, IStorage::TYPE_SAVE))
			{
				char aMessage[128 + IO_MAX_PATH_LENGTH];
				str_format(aMessage, sizeof(aMessage), Localize("File '%s' already exists, do you want to overwrite it?"), m_DemoRenderInput.GetString());
				PopupConfirm(Localize("Replace video"), aMessage, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDemoReplaceVideo, POPUP_NONE, &CMenus::DefaultButtonCallback, POPUP_RENDER_DEMO);
			}
			else
			{
				PopupConfirmDemoReplaceVideo();
			}
		}

		CUIRect ShowChatCheckbox, UseSoundsCheckbox;
		Box.HSplitBottom(20.0f, &Box, &Row);
		Box.HSplitBottom(10.0f, &Box, nullptr);
		Row.VSplitMid(&ShowChatCheckbox, &UseSoundsCheckbox, 20.0f);

		if(DoButton_CheckBox(&g_Config.m_ClVideoShowChat, Localize("Show chat"), g_Config.m_ClVideoShowChat, &ShowChatCheckbox))
			g_Config.m_ClVideoShowChat ^= 1;

		if(DoButton_CheckBox(&g_Config.m_ClVideoSndEnable, Localize("Use sounds"), g_Config.m_ClVideoSndEnable, &UseSoundsCheckbox))
			g_Config.m_ClVideoSndEnable ^= 1;

		CUIRect ShowHudButton;
		Box.HSplitBottom(20.0f, &Box, &Row);
		Row.VSplitMid(&Row, &ShowHudButton, 20.0f);

		if(DoButton_CheckBox(&g_Config.m_ClVideoShowhud, Localize("Show ingame HUD"), g_Config.m_ClVideoShowhud, &ShowHudButton))
			g_Config.m_ClVideoShowhud ^= 1;

		// slowdown
		CUIRect SlowDownButton;
		Row.VSplitLeft(20.0f, &SlowDownButton, &Row);
		Row.VSplitLeft(5.0f, nullptr, &Row);
		static CButtonContainer s_SlowDownButton;
		if(Ui()->DoButton_FontIcon(&s_SlowDownButton, FONT_ICON_BACKWARD, 0, &SlowDownButton, BUTTONFLAG_LEFT))
			m_Speed = std::clamp(m_Speed - 1, 0, (int)(std::size(DEMO_SPEEDS) - 1));

		// paused
		CUIRect PausedButton;
		Row.VSplitLeft(20.0f, &PausedButton, &Row);
		Row.VSplitLeft(5.0f, nullptr, &Row);
		static CButtonContainer s_PausedButton;
		if(Ui()->DoButton_FontIcon(&s_PausedButton, FONT_ICON_PAUSE, 0, &PausedButton, BUTTONFLAG_LEFT))
			m_StartPaused ^= 1;

		// fastforward
		CUIRect FastForwardButton;
		Row.VSplitLeft(20.0f, &FastForwardButton, &Row);
		Row.VSplitLeft(8.0f, nullptr, &Row);
		static CButtonContainer s_FastForwardButton;
		if(Ui()->DoButton_FontIcon(&s_FastForwardButton, FONT_ICON_FORWARD, 0, &FastForwardButton, BUTTONFLAG_LEFT))
			m_Speed = std::clamp(m_Speed + 1, 0, (int)(std::size(DEMO_SPEEDS) - 1));

		// speed meter
		char aBuffer[128];
		const char *pPaused = m_StartPaused ? Localize("(paused)") : "";
		str_format(aBuffer, sizeof(aBuffer), "%s: ×%g %s", Localize("Speed"), DEMO_SPEEDS[m_Speed], pPaused);
		Ui()->DoLabel(&Row, aBuffer, 12.8f, TEXTALIGN_ML);
		Box.HSplitBottom(16.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Row);

		CUIRect Label, TextBox;
		Row.VSplitLeft(110.0f, &Label, &TextBox);
		TextBox.VSplitLeft(10.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Video name:"), 12.8f, TEXTALIGN_ML);
		ui_widget::SInputFieldOptions RenderInputOptions;
		RenderInputOptions.m_pPlaceholder = Localize("Video name");
		RenderInputOptions.m_FontSize = 12.8f;
		ui_widget::InputField(DemoRenderTextInputCtx, &m_DemoRenderInput, TextBox, RenderInputOptions);

		// Warn about disconnect if online
		if(Client()->State() == IClient::STATE_ONLINE)
		{
			Box.HSplitBottom(10.0f, &Box, nullptr);
			Box.HSplitBottom(20.0f, &Box, &Row);
			SLabelProperties LabelProperties;
			LabelProperties.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f));
			Ui()->DoLabel(&Row, Localize("You will be disconnected from the server."), 12.8f, TEXTALIGN_MC, LabelProperties);
		}
	}
	else if(m_Popup == POPUP_RENDER_DONE)
	{
		CUIRect Ok, OpenFolder;

		char aFilePath[IO_MAX_PATH_LENGTH];
		char aSaveFolder[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "videos", aSaveFolder, sizeof(aSaveFolder));
		str_format(aFilePath, sizeof(aFilePath), "%s/%s.mp4", aSaveFolder, m_DemoRenderInput.GetString());

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&OpenFolder, &Ok);

		Ok.VMargin(20.0f, &Ok);
		OpenFolder.VMargin(20.0f, &OpenFolder);

		static CButtonContainer s_ButtonOpenFolder;
		if(DoButton_Menu(&s_ButtonOpenFolder, Localize("Videos directory"), 0, &OpenFolder))
		{
			Client()->ViewFile(aSaveFolder);
		}

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			m_DemoRenderInput.Clear();
		}

		Box.HSplitBottom(160.f, &Box, &Part);
		Part.VMargin(20.0f, &Part);

		str_format(aBuf, sizeof(aBuf), Localize("Video was saved to '%s'"), aFilePath);

		SLabelProperties MessageProps;
		MessageProps.m_MaxWidth = (int)Part.w;
		Ui()->DoLabel(&Part, aBuf, 18.0f, TEXTALIGN_TL, MessageProps);
	}
#endif
	else if(m_Popup == POPUP_FIRST_LAUNCH)
	{
		CUIRect Label, TextBox, Skip, Join;
		IUiContext FirstLaunchTextInputCtx;
		FirstLaunchTextInputCtx.m_pUi = Ui();
		FirstLaunchTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		FirstLaunchTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		FirstLaunchTextInputCtx.m_ScopeHash = MakeUiScopeHash("first_launch_text_input");
		FirstLaunchTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);
		Part.VSplitMid(&Skip, &Join);
		Skip.VMargin(20.0f, &Skip);
		Join.VMargin(20.0f, &Join);

		static CButtonContainer s_JoinTutorialButton;
		if(DoButton_Menu(&s_JoinTutorialButton, Localize("Join Tutorial Server"), 0, &Join) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			Client()->RequestDDNetInfo();
			m_Popup = g_Config.m_BrIndicateFinished ? POPUP_POINTS : POPUP_NONE;
			JoinTutorial();
		}

		static CButtonContainer s_SkipTutorialButton;
		if(DoButton_Menu(&s_SkipTutorialButton, Localize("Skip Tutorial"), 0, &Skip) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			Client()->RequestDDNetInfo();
			m_Popup = g_Config.m_BrIndicateFinished ? POPUP_POINTS : POPUP_NONE;
		}

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(30.0f, nullptr, &Part);
		str_format(aBuf, sizeof(aBuf), "%s\n(%s)",
			Localize("Show DDNet map finishes in server browser"),
			Localize("transmits your player name to info.ddnet.org"));

		if(DoButton_CheckBox(&g_Config.m_BrIndicateFinished, aBuf, g_Config.m_BrIndicateFinished, &Part))
			g_Config.m_BrIndicateFinished ^= 1;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(60.0f, nullptr, &Label);
		Label.VSplitLeft(100.0f, nullptr, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		TextBox.VSplitRight(60.0f, &TextBox, nullptr);
		Ui()->DoLabel(&Label, Localize("Nickname"), 16.0f, TEXTALIGN_ML);
		static CLineInput s_PlayerNameInput(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_PlayerNameInput.SetEmptyText(Client()->PlayerName());
		ui_widget::SInputFieldOptions PlayerNameInputOptions;
		PlayerNameInputOptions.m_pPlaceholder = Client()->PlayerName();
		PlayerNameInputOptions.m_FontSize = 12.0f;
		ui_widget::InputField(FirstLaunchTextInputCtx, &s_PlayerNameInput, TextBox, PlayerNameInputOptions);
	}
	else if(m_Popup == POPUP_JOIN_TUTORIAL)
	{
		CUIRect ButtonBar, StatusLabel, ProgressLabel, ProgressIndicator;
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &ButtonBar);
		ButtonBar.VMargin(120.0f, &ButtonBar);
		Box.HSplitBottom(20.0f, &StatusLabel, nullptr);
		StatusLabel.VMargin(20.0f, &StatusLabel);
		StatusLabel.HSplitMid(&StatusLabel, &ProgressLabel);
		ProgressLabel.VSplitLeft(50.0f, &ProgressIndicator, &ProgressLabel);

		if(m_JoinTutorial.m_Status == CJoinTutorial::EStatus::REFRESHING)
		{
			if(ServerBrowser()->IsGettingServerlist() ||
				Client()->InfoState() == IClient::EInfoState::LOADING)
			{
				// Still refreshing
			}
			else if(ServerBrowser()->IsServerlistError() ||
				Client()->InfoState() == IClient::EInfoState::ERROR)
			{
				m_JoinTutorial.m_Status = CJoinTutorial::EStatus::SERVER_LIST_ERROR;
			}
			else
			{
				const char *pAddr = ServerBrowser()->GetTutorialServer();
				if(pAddr)
				{
					Client()->Connect(pAddr);
				}
				else
				{
					m_JoinTutorial.m_Status = CJoinTutorial::EStatus::NO_TUTORIAL_AVAILABLE;
				}
			}
		}

		const char *pStatusLabel = nullptr;
		switch(m_JoinTutorial.m_Status)
		{
		case CJoinTutorial::EStatus::REFRESHING:
			pStatusLabel = Localize("Getting server list from master server");
			break;
		case CJoinTutorial::EStatus::SERVER_LIST_ERROR:
			pStatusLabel = Localize("Could not get server list from master server");
			break;
		case CJoinTutorial::EStatus::NO_TUTORIAL_AVAILABLE:
			pStatusLabel = Localize("There are no Tutorial servers available");
			break;
		}
		if(pStatusLabel != nullptr)
		{
			Ui()->DoLabel(&StatusLabel, pStatusLabel, 20.0f, TEXTALIGN_ML);
		}

		const char *pProgressLabel = nullptr;
		bool ProgressDeterminate = true;
		const float LastStateChangeSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(time_get_nanoseconds() - m_JoinTutorial.m_StateChange).count();
		constexpr float RefreshDelay = 5.0f;

		if(m_JoinTutorial.m_Status == CJoinTutorial::EStatus::REFRESHING)
		{
			pProgressLabel = Localize("Please wait…");
			ProgressDeterminate = false;
		}
		else if(!m_JoinTutorial.m_TryRefresh)
		{
			if(!m_JoinTutorial.m_TriedRefresh)
			{
				m_JoinTutorial.m_TryRefresh = true;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::NOT_TRIED)
			{
				m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::TRY;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
		}

		if(m_JoinTutorial.m_TryRefresh)
		{
			if(LastStateChangeSeconds >= RefreshDelay)
			{
				// Activate internet tab before joining tutorial to make sure the server info
				// for the tutorial servers is available.
				GameClient()->m_Menus.SetMenuPage(CMenus::PAGE_INTERNET);
				GameClient()->m_Menus.RefreshBrowserTab(true);
				m_JoinTutorial.m_Status = CJoinTutorial::EStatus::REFRESHING;
				m_JoinTutorial.m_TryRefresh = false;
				m_JoinTutorial.m_TriedRefresh = true;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else
			{
				pProgressLabel = Localize("Retrying…");
			}
		}

		const auto &&ShowFinalErrorMessage = [&]() {
			PopupMessage(Localize("Error joining Tutorial server"), Localize("Could not find a Tutorial server. Check your internet connection."), Localize("Ok"));
		};
		const auto &&RunServer = [&]() {
			char aMotd[256];
			str_copy(aMotd, "sv_motd \"");
			char *pDst = aMotd + str_length(aMotd);
			str_escape(&pDst, Localize("You're playing on a local server because no online Tutorial server could be found.\n\nYour record will only be saved locally."), aMotd + sizeof(aMotd) - 1);
			str_append(aMotd, "\"");
			if(GameClient()->m_LocalServer.RunServer({"sv_register 0", "sv_map Tutorial", aMotd}))
			{
				m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::WAITING_START;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else
			{
				ShowFinalErrorMessage();
			}
		};
		if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::TRY)
		{
			if(LastStateChangeSeconds >= RefreshDelay)
			{
				if(GameClient()->m_LocalServer.IsServerRunning())
				{
					GameClient()->m_LocalServer.KillServer();
					m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::WAITING_STOP;
					m_JoinTutorial.m_StateChange = time_get_nanoseconds();
				}
				else
				{
					RunServer();
				}
			}
			else
			{
				pProgressLabel = Localize("Could not find online Tutorial server.\nStarting and connecting to local server…");
			}
		}
		else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::WAITING_STOP)
		{
			if(LastStateChangeSeconds >= 5.0f)
			{
				ShowFinalErrorMessage();
			}
			else
			{
				if(!GameClient()->m_LocalServer.IsServerRunning())
				{
					RunServer();
				}

				pProgressLabel = Localize("Waiting for local server to stop…");
				ProgressDeterminate = false;
			}
		}
		else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::WAITING_START)
		{
			if(LastStateChangeSeconds >= 5.0f)
			{
				ShowFinalErrorMessage();
			}
			else
			{
				if(LastStateChangeSeconds >= 2.0f &&
					GameClient()->m_LocalServer.IsServerRunning())
				{
					Client()->Connect("localhost");
				}

				pProgressLabel = Localize("Waiting for local server to start…");
				ProgressDeterminate = false;
			}
		}

		if(pProgressLabel != nullptr)
		{
			Ui()->RenderProgressSpinner(ProgressIndicator.Center(), 12.0f, {.m_Progress = ProgressDeterminate ? (LastStateChangeSeconds / RefreshDelay) : -1.0f});
			Ui()->DoLabel(&ProgressLabel, pProgressLabel, 20.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &ButtonBar) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
		}
	}
	else if(m_Popup == POPUP_POINTS)
	{
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		if(Client()->InfoState() == IClient::EInfoState::SUCCESS && Client()->Points() > 50)
		{
			CUIRect Yes, No;
			Part.VSplitMid(&No, &Yes, 40.0f);
			static CButtonContainer s_ButtonNo;
			if(DoButton_Menu(&s_ButtonNo, Localize("No"), 0, &No) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			{
				m_Popup = POPUP_FIRST_LAUNCH;
			}

			static CButtonContainer s_ButtonYes;
			if(DoButton_Menu(&s_ButtonYes, Localize("Yes"), 0, &Yes) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = POPUP_NONE;
			}
		}
		else
		{
			static CButtonContainer s_Button;
			if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &Part) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) ||
				Client()->InfoState() == IClient::EInfoState::SUCCESS)
			{
				m_Popup = POPUP_NONE;
			}
			if(Client()->InfoState() == IClient::EInfoState::ERROR)
			{
				PopupMessage(Localize("Error checking player name"), Localize("Could not check for existing player with your name. Check your internet connection."), Localize("Ok"));
			}
		}
	}
	else if(m_Popup == POPUP_WARNING)
	{
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, pButtonText, 0, &Part) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || (m_PopupWarningDuration > 0s && time_get_nanoseconds() - m_PopupWarningLastTime >= m_PopupWarningDuration))
		{
			m_Popup = POPUP_NONE;
			SetActive(false);
		}
	}
	else if(m_Popup == POPUP_SAVE_SKIN)
	{
		CUIRect Label, TextBox, Yes, No;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&No, &Yes);

		Yes.VMargin(20.0f, &Yes);
		No.VMargin(20.0f, &No);

		static CButtonContainer s_ButtonNo;
		if(DoButton_Menu(&s_ButtonNo, Localize("No"), 0, &No) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonYes;
		if(DoButton_Menu(&s_ButtonYes, Localize("Yes"), m_SkinNameInput.IsEmpty() ? 1 : 0, &Yes) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(!str_valid_filename(m_SkinNameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else if(CSkins7::IsSpecialSkin(m_SkinNameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("Unable to save the skin with a reserved name"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else if(!GameClient()->m_Skins7.SaveSkinfile(m_SkinNameInput.GetString(), m_Dummy))
			{
				PopupMessage(Localize("Error"), Localize("Unable to save the skin"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else
			{
				m_Popup = POPUP_NONE;
				m_SkinList7LastRefreshTime = std::nullopt;
			}
		}

		Box.HSplitBottom(60.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VMargin(60.0f, &Label);
		Label.VSplitLeft(100.0f, &Label, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Name"), 18.0f, TEXTALIGN_ML);
		IUiContext SaveSkinTextInputCtx;
		SaveSkinTextInputCtx.m_pUi = Ui();
		SaveSkinTextInputCtx.m_ScopeHash = MakeUiScopeHash("save_skin_text_input");
		ui_widget::SInputFieldOptions SaveSkinInputOptions;
		SaveSkinInputOptions.m_Clearable = true;
		SaveSkinInputOptions.m_FontSize = 12.0f;
		ui_widget::InputField(SaveSkinTextInputCtx, &m_SkinNameInput, TextBox, SaveSkinInputOptions);
	}
	else
	{
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, pButtonText, 0, &Part) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(m_Popup == POPUP_DISCONNECTED && Client()->ReconnectTime() > 0)
				Client()->SetReconnectTime(0);
			m_Popup = POPUP_NONE;
		}
	}

	if(m_Popup == POPUP_NONE)
		Ui()->SetActiveItem(nullptr);
}

void CMenus::RenderPopupConnecting(CUIRect Screen)
{
	const float FontSize = 20.0f;

	CUIRect Box, Label;
	Screen.Margin(QmUiCenteredMargin(Screen, 150.0f, 300.0f, 300.0f), &Box);
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 15.0f);
	Box.Margin(20.0f, &Box);

	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, Localize("Connecting to"), 24.0f, TEXTALIGN_MC);

	Box.HSplitTop(20.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Label, &Box);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Label, Client()->ConnectAddressString(), FontSize, TEXTALIGN_MC, Props);

	if(time_get() - Client()->StateStartTime() > time_freq())
	{
		const char *pConnectivityLabel = "";
		switch(Client()->UdpConnectivity(Client()->ConnectNetTypes()))
		{
		case IClient::CONNECTIVITY_UNKNOWN:
			break;
		case IClient::CONNECTIVITY_CHECKING:
			pConnectivityLabel = Localize("Trying to determine UDP connectivity…");
			break;
		case IClient::CONNECTIVITY_UNREACHABLE:
			pConnectivityLabel = Localize("UDP seems to be filtered.");
			break;
		case IClient::CONNECTIVITY_DIFFERING_UDP_TCP_IP_ADDRESSES:
			pConnectivityLabel = Localize("UDP and TCP IP addresses seem to be different. Try disabling VPN, proxy or network accelerators.");
			break;
		case IClient::CONNECTIVITY_REACHABLE:
			pConnectivityLabel = Localize("No answer from server yet.");
			break;
		}
		if(pConnectivityLabel[0] != '\0')
		{
			Box.HSplitTop(20.0f, nullptr, &Box);
			Box.HSplitTop(24.0f, &Label, &Box);
			SLabelProperties ConnectivityLabelProps;
			ConnectivityLabelProps.m_MaxWidth = Label.w;
			if(TextRender()->TextWidth(FontSize, pConnectivityLabel) > Label.w)
				Ui()->DoLabel(&Label, pConnectivityLabel, FontSize, TEXTALIGN_ML, ConnectivityLabelProps);
			else
				Ui()->DoLabel(&Label, pConnectivityLabel, FontSize, TEXTALIGN_MC);
		}
	}

	CUIRect Button;
	Box.HSplitBottom(24.0f, &Box, &Button);
	Button.VMargin(100.0f, &Button);

	static CButtonContainer s_Button;
	if(DoButton_Menu(&s_Button, Localize("Abort"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Client()->Disconnect();
		Ui()->SetActiveItem(nullptr);
		RefreshBrowserTab(true);
	}
}

void CMenus::RenderPopupLoading(CUIRect Screen)
{
	char aTitle[256];
	char aLabel1[128];
	char aLabel2[128];
	if(Client()->MapDownloadTotalsize() > 0)
	{
		const int64_t Now = time_get();
		if(Now - m_DownloadLastCheckTime >= time_freq())
		{
			if(m_DownloadLastCheckSize > Client()->MapDownloadAmount())
			{
				// map downloaded restarted
				m_DownloadLastCheckSize = 0;
			}

			// update download speed
			const float Diff = (Client()->MapDownloadAmount() - m_DownloadLastCheckSize) / ((int)((Now - m_DownloadLastCheckTime) / time_freq()));
			const float StartDiff = m_DownloadLastCheckSize - 0.0f;
			if(StartDiff + Diff > 0.0f)
				m_DownloadSpeed = (Diff / (StartDiff + Diff)) * (Diff / 1.0f) + (StartDiff / (Diff + StartDiff)) * m_DownloadSpeed;
			else
				m_DownloadSpeed = 0.0f;
			m_DownloadLastCheckTime = Now;
			m_DownloadLastCheckSize = Client()->MapDownloadAmount();
		}

		str_format(aTitle, sizeof(aTitle), "%s: %s", Localize("Downloading map"), Client()->MapDownloadName());

		str_format(aLabel1, sizeof(aLabel1), Localize("%d/%d KiB (%.1f KiB/s)"), Client()->MapDownloadAmount() / 1024, Client()->MapDownloadTotalsize() / 1024, m_DownloadSpeed / 1024.0f);

		const int SecondsLeft = maximum(1, m_DownloadSpeed > 0.0f ? static_cast<int>((Client()->MapDownloadTotalsize() - Client()->MapDownloadAmount()) / m_DownloadSpeed) : 1);
		const int MinutesLeft = SecondsLeft / 60;
		if(MinutesLeft > 0)
		{
			str_format(aLabel2, sizeof(aLabel2), MinutesLeft == 1 ? Localize("%i minute left") : Localize("%i minutes left"), MinutesLeft);
		}
		else
		{
			str_format(aLabel2, sizeof(aLabel2), SecondsLeft == 1 ? Localize("%i second left") : Localize("%i seconds left"), SecondsLeft);
		}
	}
	else
	{
		str_copy(aTitle, Localize("Connected"));
		switch(Client()->LoadingStateDetail())
		{
		case IClient::LOADING_STATE_DETAIL_INITIAL:
			str_copy(aLabel1, Localize("Getting game info"));
			break;
		case IClient::LOADING_STATE_DETAIL_LOADING_MAP:
			str_copy(aLabel1, Localize("Loading map file from storage"));
			break;
		case IClient::LOADING_STATE_DETAIL_LOADING_DEMO:
			str_copy(aLabel1, Localize("Loading demo file from storage"));
			break;
		case IClient::LOADING_STATE_DETAIL_SENDING_READY:
			str_copy(aLabel1, Localize("Requesting to join the game"));
			break;
		case IClient::LOADING_STATE_DETAIL_GETTING_READY:
			str_copy(aLabel1, Localize("Sending initial client info"));
			break;
		default:
			dbg_assert_failed("Invalid loading state %d for RenderPopupLoading", static_cast<int>(Client()->LoadingStateDetail()));
		}
		aLabel2[0] = '\0';
	}

	const float FontSize = 20.0f;

	CUIRect Box, Label;
	Screen.Margin(QmUiCenteredMargin(Screen, 150.0f, 300.0f, 300.0f), &Box);
	Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 15.0f);
	Box.Margin(20.0f, &Box);

	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, aTitle, 24.0f, TEXTALIGN_MC);

	Box.HSplitTop(20.0f, nullptr, &Box);
	Box.HSplitTop(24.0f, &Label, &Box);
	Ui()->DoLabel(&Label, aLabel1, FontSize, TEXTALIGN_MC);

	if(aLabel2[0] != '\0')
	{
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitTop(24.0f, &Label, &Box);
		SLabelProperties ExtraTextProps;
		ExtraTextProps.m_MaxWidth = Label.w;
		if(TextRender()->TextWidth(FontSize, aLabel2) > Label.w)
			Ui()->DoLabel(&Label, aLabel2, FontSize, TEXTALIGN_ML, ExtraTextProps);
		else
			Ui()->DoLabel(&Label, aLabel2, FontSize, TEXTALIGN_MC);
	}

	if(Client()->MapDownloadTotalsize() > 0)
	{
		CUIRect ProgressBar;
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitTop(24.0f, &ProgressBar, &Box);
		ProgressBar.VMargin(20.0f, &ProgressBar);
		Ui()->RenderProgressBar(ProgressBar, Client()->MapDownloadAmount() / (float)Client()->MapDownloadTotalsize());
	}

	CUIRect Button;
	Box.HSplitBottom(24.0f, &Box, &Button);
	Button.VMargin(100.0f, &Button);

	static CButtonContainer s_Button;
	if(DoButton_Menu(&s_Button, Localize("Abort"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Client()->Disconnect();
		Ui()->SetActiveItem(nullptr);
		RefreshBrowserTab(true);
	}
}

#if defined(CONF_VIDEORECORDER)
void CMenus::PopupConfirmDemoReplaceVideo()
{
	char aBuf[IO_MAX_PATH_LENGTH];
	char aDemoFilename[IO_MAX_PATH_LENGTH];
	const char *pDemoFolder = m_HasPendingDemoRenderSource ? m_aPendingDemoRenderFolder : m_aCurrentDemoFolder;
	const char *pDemoSelectionName = m_HasPendingDemoRenderSource ? m_aPendingDemoRenderSelectionName : m_aCurrentDemoSelectionName;
	const int DemoStorageType = m_HasPendingDemoRenderSource ? m_PendingDemoRenderStorageType : m_DemolistStorageType;
	str_copy(aDemoFilename, pDemoSelectionName);
	if(str_endswith_nocase(aDemoFilename, ".demo") == nullptr)
		str_append(aDemoFilename, ".demo", sizeof(aDemoFilename));
	str_format(aBuf, sizeof(aBuf), "%s/%s", pDemoFolder, aDemoFilename);
	char aVideoName[IO_MAX_PATH_LENGTH];
	str_copy(aVideoName, m_DemoRenderInput.GetString());
	const char *pError = Client()->DemoPlayer_Render(aBuf, DemoStorageType, aVideoName, m_Speed, m_StartPaused);
	m_HasPendingDemoRenderSource = false;
	m_vDemoCutSegments.clear();
	g_Config.m_ClDemoSliceBegin = -1;
	g_Config.m_ClDemoSliceEnd = -1;
	m_Speed = DEMO_SPEED_INDEX_DEFAULT;
	m_StartPaused = false;
	m_LastPauseChange = -1.0f;
	m_LastSpeedChange = -1.0f;
	if(pError)
	{
		m_DemoRenderInput.Clear();
		PopupMessage(Localize("Error loading demo"), pError, Localize("Ok"));
	}
}
#endif

void CMenus::RenderThemeSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)
{
	CPerfTimer RenderTimer;
	static CListBox s_ListBox;
	auto &MenuBackground = GameClient()->m_MenuBackground;
	const SSettingsContentMetrics Metrics = pMetrics != nullptr ? *pMetrics : ResolveSettingsContentMetrics(MainView.w);
	s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
	s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
	s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);

	const float HeaderHeight = Metrics.m_LineHeight;
	const float HeaderSpacing = Metrics.m_LineSpacing;
	CUIRect HeaderView = MainView;
	CUIRect Header, HeaderRow;
	HeaderView.HSplitTop(HeaderHeight + HeaderSpacing, &Header, nullptr);
	Header.HSplitTop(HeaderHeight, &HeaderRow, nullptr);

	s_ListBox.DoHeader(&MainView, Localize("Theme"), HeaderHeight, HeaderSpacing);

	static CButtonContainer s_RefreshButton;
	CUIRect RefreshButton;
	HeaderRow.VSplitRight(80.0f * Metrics.m_UiScale, nullptr, &RefreshButton);
	RefreshButton.VMargin(Metrics.m_LineSpacing * 0.5f, &RefreshButton);
	if(DoButton_Menu(&s_RefreshButton, Localize("Refresh"), 0, &RefreshButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), nullptr, Metrics.m_BodySize))
	{
		MenuBackground.RefreshThemes();
		s_ListBox.ResetScroll();
	}

	const std::vector<CTheme> &vThemes = MenuBackground.GetThemes();

	int SelectedTheme = -1;
	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		if(str_comp(vThemes[i].m_Name.c_str(), g_Config.m_ClMenuMap) == 0)
		{
			SelectedTheme = i;
			break;
		}
	}
	const int OldSelected = SelectedTheme;

	s_ListBox.DoStart(Metrics.m_ListRowHeight, vThemes.size(), 1, 3, SelectedTheme);

	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		const CTheme &Theme = vThemes[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&Theme.m_Name, i == SelectedTheme);

		if(!Item.m_Visible)
			continue;

		CUIRect Icon, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h * 2.0f, &Icon, &Label);

		// draw icon if it exists
		if(Theme.m_IconTexture.IsValid())
		{
			Icon.VMargin(6.0f, &Icon);
			Icon.HMargin(3.0f, &Icon);
			Graphics()->TextureSet(Theme.m_IconTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Icon.x, Icon.y, Icon.w, Icon.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		char aName[IO_MAX_PATH_LENGTH];
		if(Theme.m_Name.empty())
			str_copy(aName, "(none)");
		else if(str_comp(Theme.m_Name.c_str(), "auto") == 0)
			str_copy(aName, "(seasons)");
		else if(str_comp(Theme.m_Name.c_str(), "rand") == 0)
			str_copy(aName, "(random)");
		else
			str_copy(aName, Theme.m_Name.c_str());

		Ui()->DoLabel(&Label, aName, Metrics.m_BodySize, TEXTALIGN_ML);
	}

	SelectedTheme = s_ListBox.DoEnd();

	if(OldSelected != SelectedTheme)
	{
		const CTheme &Theme = vThemes[SelectedTheme];
		str_copy(g_Config.m_ClMenuMap, Theme.m_Name.c_str());
		GameClient()->m_MenuBackground.LoadMenuBackground(Theme.m_HasDay, Theme.m_HasNight);
	}
	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "themes=%d selected=%d", (int)vThemes.size(), SelectedTheme);
	LogPerfStage(Client(), "theme_selection_total", RenderTimer.ElapsedMs(), false, aExtra);
}

void CMenus::SetActive(bool Active)
{
	if(Active != m_MenuActive)
	{
		Ui()->SetHotItem(nullptr);
		Ui()->SetActiveItem(nullptr);
		MarkMenuInteraction();
	}
	m_MenuActive = Active;
	if(!m_MenuActive)
	{
		if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
			FinalizeTeeListDrainPerfSession();
		ClearQmClientSettingsSearchInputs();

		if(m_NeedSendinfo)
		{
			GameClient()->SendInfo(false);
			m_NeedSendinfo = false;
		}

		if(m_NeedSendDummyinfo)
		{
			GameClient()->SendDummyInfo(false);
			m_NeedSendDummyinfo = false;
		}

		if(Client()->State() == IClient::STATE_ONLINE)
		{
			GameClient()->OnRelease();
		}
	}
	else if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		GameClient()->OnRelease();
	}
}

void CMenus::MarkMenuInteraction()
{
	m_LastMenuInteractionTime = time_get_nanoseconds();
}

bool CMenus::IsSettingsPageActive() const
{
	if(!IsActive())
		return false;
	if(Client()->State() == IClient::STATE_ONLINE)
		return m_GamePage == PAGE_SETTINGS;
	return m_MenuPage == PAGE_SETTINGS;
}

const char *CMenus::CurrentQmUiPerfPage() const
{
	if(!IsSettingsPageActive())
		return nullptr;

	switch(SettingsCanonicalPage(g_Config.m_UiSettingsPage))
	{
	case SETTINGS_GENERAL: return "settings:general";
	case SETTINGS_TEE: return "settings:tee";
	case SETTINGS_APPEARANCE: return "settings:appearance";
	case SETTINGS_CONTROLS: return "settings:controls";
	case SETTINGS_GRAPHICS: return "settings:graphics";
	case SETTINGS_SOUND: return "settings:sound";
	case SETTINGS_DDNET: return "settings:ddnet";
	case SETTINGS_ASSETS: return "settings:assets";
	case SETTINGS_TCLIENT: return "settings:tclient";
	case SETTINGS_QMCLIENT: return "settings:qmclient";
	case SETTINGS_SEARCH: return "settings:search";
	default: return "settings:unknown";
	}
}

const char *CMenus::CurrentQmUiPerfOperation() const
{
	if(!IsSettingsPageActive())
		return nullptr;

	switch(SettingsCanonicalPage(g_Config.m_UiSettingsPage))
	{
	case SETTINGS_GENERAL: return "settings_general";
	case SETTINGS_TEE: return "settings_tee";
	case SETTINGS_APPEARANCE: return "settings_appearance";
	case SETTINGS_CONTROLS: return "settings_controls";
	case SETTINGS_GRAPHICS: return "settings_graphics";
	case SETTINGS_SOUND: return "settings_sound";
	case SETTINGS_DDNET: return "settings_ddnet";
	case SETTINGS_ASSETS: return "settings_assets";
	case SETTINGS_TCLIENT: return "settings_tclient";
	case SETTINGS_QMCLIENT: return "settings_qmclient";
	case SETTINGS_SEARCH: return "settings_search";
	default: return "settings_unknown";
	}
}

int CMenus::IdleRenderFrameRate() const
{
	if(!IsActive() || !InterfacesInitialized())
		return 0;

	const IClient::EClientState ClientState = Client()->State();
	if(ClientState == IClient::STATE_CONNECTING || ClientState == IClient::STATE_LOADING || ClientState == IClient::STATE_QUITTING || ClientState == IClient::STATE_RESTARTING)
		return 0;

	if(Ui()->ActiveItem() != nullptr || CLineInput::GetActiveInput() != nullptr || GameClient()->m_KeyBinder.IsActive() || Ui()->IsPopupOpen())
		return 0;
	// 性能采样期间解除空闲节流，普通设置页空闲时仍限制功耗。
	if(IsSettingsPageActive() && (g_Config.m_QmPerfDebug != 0 || m_SettingsPerfWindowTracker.HasActiveWindow()))
		return 0;

	if(GameClient()->m_MenuBackground.IsLoading())
		return 0;

	const SUiV2PerfStats &UiRuntimeStats = GameClient()->UiRuntimeV2()->LastStats();
	if(UiRuntimeStats.m_ActiveAnimCount > 0 || UiRuntimeStats.m_QueuedAnimCount > 0)
		return 0;

	if(time_get_nanoseconds() - m_LastMenuInteractionTime < MENU_IDLE_INTERACTION_GRACE_TIME)
		return 0;

	return maximum(MENU_IDLE_REFRESH_RATE, g_Config.m_GfxScreenRefreshRate);
}

CMenus::SSettingsScrollRegionFrame CMenus::BeginSettingsScrollRegion(CScrollRegion &ScrollRegion, CUIRect *pView, const CScrollRegionParams &Params, float PreviousOffsetY)
{
	SSettingsScrollRegionFrame Frame;
	Frame.m_PreviousOffsetY = PreviousOffsetY;
	ScrollRegion.Begin(pView, &Frame.m_BeginOffset, &Params);
	Frame.m_FinalOffsetY = Frame.m_BeginOffset.y;
	return Frame;
}

void CMenus::FinishSettingsScrollRegion(CScrollRegion &ScrollRegion, SSettingsScrollRegionFrame &Frame, const CUIRect *pEndRect, int Page, bool TrackScrollActive)
{
	if(pEndRect != nullptr)
		ScrollRegion.AddRect(*pEndRect);
	ScrollRegion.End();
	Frame.m_FinalOffsetY = ScrollRegion.ScrollbarShown() ? ScrollRegion.ContentScrollOffsetY() : 0.0f;

	if(TrackScrollActive)
		m_SettingsScrollActive = m_SettingsScrollActive || absolute(Frame.m_FinalOffsetY - Frame.m_PreviousOffsetY) > 0.01f;

	if(Page >= 0)
	{
		m_SettingsRuntimeMetadata.m_LastScrollPage = Page;
		m_SettingsRuntimeMetadata.m_LastScrollY = Frame.m_FinalOffsetY;
		m_SettingsRuntimeMetadata.m_Valid = true;
	}
}

CMenus::SQmSettingsCardStyle CMenus::QmSettingsCardStyle(float UiScale) const
{
	SQmSettingsCardStyle Style;
	const SUiTheme Theme = ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(g_Config.m_QmUiFocusColor), ColorHSLA(g_Config.m_QmUiAccentColor), ColorHSLA(g_Config.m_QmUiSelectedColor));
	const ui_widget::SCardProps CardProps = ui_widget::QmClientCardProps(UiScale, &Theme);
	const SQmScrollContainerStyle ScrollStyle = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);
	Style.m_Padding = CardProps.m_Padding;
	Style.m_Spacing = std::clamp(16.0f * UiScale, 10.0f, 16.0f);
	Style.m_CornerRadius = CardProps.m_Radius;
	Style.m_ScrollbarWidth = ScrollStyle.m_ScrollbarWidth;
	Style.m_ScrollbarMargin = ScrollStyle.m_ScrollbarMargin;
	Style.m_GlassColor = CardProps.m_FillColor;
	Style.m_HighlightColor = CardProps.m_HighlightColor;
	Style.m_HairlineColor = CardProps.m_BorderColor;
	return Style;
}

CScrollRegionParams CMenus::QmSettingsScrollRegionParams(float UiScale) const
{
	SQmScrollRequest Request;
	Request.m_Profile = EQmScrollProfile::SETTINGS_OUTER;
	return QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(Request, UiScale, 0.0f));
}

float CMenus::SettingsPageUiScale(float ContentWidth) const
{
	return m_SettingsShellLayoutValid ? m_SettingsShellLayout.m_UiScale : ResolveSettingsUiScale(ContentWidth);
}

SSettingsContentMetrics CMenus::CurrentSettingsContentMetrics() const
{
	return m_SettingsContentMetrics;
}

SSettingsPageLayoutFrame CMenus::SettingsPageLayout(const CUIRect &ContentView, float UiScale) const
{
	SSettingsPageLayoutFrame Page = ResolveSettingsPageLayout(ContentView, false, m_SettingsShellLayoutValid ? m_SettingsShellLayout.m_UiScale : UiScale);
	if(!m_SettingsShellLayoutValid)
		return Page;

	const float OffsetX = ContentView.x - m_SettingsShellLayout.m_ContentRect.x;
	const float OffsetY = ContentView.y - m_SettingsShellLayout.m_ContentRect.y;
	auto OffsetRect = [OffsetX, OffsetY](CUIRect Rect) {
		Rect.x += OffsetX;
		Rect.y += OffsetY;
		return Rect;
	};
	Page.m_CardGap = m_SettingsShellLayout.m_CardGap;
	const CUIRect ShellScrollViewport = OffsetRect(m_SettingsShellLayout.m_ScrollViewport);
	Page.m_ScrollViewport.x = ShellScrollViewport.x;
	Page.m_ScrollViewport.w = ShellScrollViewport.w;
	return ResolveSettingsPageLayoutForScrollViewport(Page, Page.m_ScrollViewport, m_SettingsShellLayout.m_UiScale);
}

bool CMenus::SetSettingsPageFromCardTab(const char *pTab)
{
	if(pTab == nullptr || pTab[0] == '\0')
		return false;
	if(str_comp(pTab, "general") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	else if(str_comp(pTab, "player") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_PLAYER;
	else if(str_comp(pTab, "tee") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_TEE;
	else if(str_comp(pTab, "tee7") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_TEE;
	else if(str_comp(pTab, "visual") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;
	}
	else if(str_comp(pTab, "function") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_FUNCTION;
	}
	else if(str_comp(pTab, "hud") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_HUD;
	}
	else if(str_comp(pTab, "qmclient-overview") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_VISUAL;
	}
	else if(str_comp(pTab, "qmclient-contributors") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_CONTRIBUTORS;
	}
	else if(str_comp(pTab, "tclient") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
		m_TClientSettingsTab = 0;
	}
	else if(str_comp(pTab, "tclient-bind-wheel") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
		m_TClientSettingsTab = 1;
	}
	else if(str_comp(pTab, "tclient-warlist") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
		m_TClientSettingsTab = 2;
	}
	else if(str_comp(pTab, "tclient-chat-binds") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
		m_TClientSettingsTab = 3;
	}
	else if(str_comp(pTab, "tclient-status-bar") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
		m_TClientSettingsTab = 4;
	}
	else if(str_comp(pTab, "tclient-info") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
		m_TClientSettingsTab = 5;
	}
	else if(str_comp(pTab, "tclient-profiles") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_PROFILES;
	else if(str_comp(pTab, "tclient-configs") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_CONFIG;
	}
	else if(str_comp(pTab, "graphics") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_GRAPHICS;
	else if(str_comp(pTab, "sound") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_SOUND;
	else if(str_comp(pTab, "ddnet") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_DDNET;
	else if(str_comp(pTab, "controls") == 0)
		g_Config.m_UiSettingsPage = SETTINGS_CONTROLS;
	else if(str_comp(pTab, "appearance-hud") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE;
		m_AppearanceSettingsTab = APPEARANCE_TAB_HUD;
	}
	else if(str_comp(pTab, "appearance-chat") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE;
		m_AppearanceSettingsTab = APPEARANCE_TAB_CHAT;
	}
	else if(str_comp(pTab, "appearance-name-plate") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE;
		m_AppearanceSettingsTab = APPEARANCE_TAB_NAME_PLATE;
	}
	else if(str_comp(pTab, "appearance-hook-collision") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE;
		m_AppearanceSettingsTab = APPEARANCE_TAB_HOOK_COLLISION;
	}
	else if(str_comp(pTab, "appearance-info-messages") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE;
		m_AppearanceSettingsTab = APPEARANCE_TAB_INFO_MESSAGES;
	}
	else if(str_comp(pTab, "appearance-laser") == 0)
	{
		g_Config.m_UiSettingsPage = SETTINGS_APPEARANCE;
		m_AppearanceSettingsTab = APPEARANCE_TAB_LASER;
	}
	else
		return false;
	return true;
}

void CMenus::NavigateToSettingsCard(const qm_card_registry::SCardNavigationTarget &Target)
{
	if(Target.m_pStableId == nullptr || Target.m_pStableId[0] == '\0' || !SetSettingsPageFromCardTab(Target.m_pTab))
		return;
	m_SettingsCardDeck.RequestReveal(Target.m_pStableId);
}

void CMenus::RequestSettingsCardFocus(const char *pStableId)
{
	if(pStableId == nullptr || pStableId[0] == '\0')
		return;
	m_SettingsCardFocusStableId = pStableId;
}

void CMenus::StartSettingsPerfFixedWindow(const char *pOperation, const char *pContext, const char *pPage, const char *pTab, int MaxFrames)
{
	if(!PerfDebugEnabled())
		return;
	const SQmSettingsPerfWindowFrameResult Interrupted = m_SettingsPerfWindowTracker.StartFixedFrameWindow(
		pOperation,
		pContext,
		pPage,
		pTab,
		MaxFrames,
		g_Config.m_GfxVsync != 0 || g_Config.m_GfxRefreshRate > 0,
		Client()->PerfFrame());
	if(Interrupted.m_ShouldFlush)
		LogSettingsPerfWindowSummary(Interrupted.m_Summary);
}

void CMenus::StartSettingsPerfScrollWindow(const char *pOperation, const char *pContext, const char *pPage, const char *pTab)
{
	if(!PerfDebugEnabled())
		return;
	m_MenuUiPerfScrollActive = true;
	const SQmSettingsPerfWindowFrameResult Interrupted = m_SettingsPerfWindowTracker.EnsureScrollWindow(
		pOperation,
		pContext,
		pPage,
		pTab,
		0.250f,
		g_Config.m_GfxVsync != 0 || g_Config.m_GfxRefreshRate > 0,
		Client()->PerfFrame());
	if(Interrupted.m_ShouldFlush)
		LogSettingsPerfWindowSummary(Interrupted.m_Summary);
}

void CMenus::RecordSettingsPerfWindowFrame(double MenuDurationMs)
{
	if(!PerfDebugEnabled())
		return;
	if(Ui()->ConsumeMenuUiFirstWheelPerf())
		StartSettingsPerfScrollWindow("dropdown_first_wheel", SettingsPerfContextName(), "dropdown", "none");
	const SQmSettingsPerfWindowFrameResult Result = m_SettingsPerfWindowTracker.RecordFrame(Client()->RenderFrameTime(), MenuDurationMs, m_MenuUiPerfScrollActive, Client()->PerfFrame());
	if(Result.m_ShouldFlush)
		LogSettingsPerfWindowSummary(Result.m_Summary);
}

void CMenus::LogSettingsPerfWindowSummary(const SQmSettingsPerfWindowSummary &Summary)
{
	if(!PerfDebugEnabled() || Summary.m_SampleFrames <= 0)
		return;

	char aPayload[1024];
	str_format(aPayload, sizeof(aPayload),
		"event=fps_summary operation=%s context=%s page=%s tab=%s sample_frames=%d sample_seconds=%.3f fps_avg=%.3f fps_min=%.3f fps_1pct_low=%.3f fps_1pct_source=real_sampled fps_max=%.3f frame_ms_avg=%.3f frame_ms_p95=%.3f frame_ms_p99=%.3f frame_ms_max=%.3f menu_ms_max=%.3f window_start_frame=%" PRIu64 " window_end_frame=%" PRIu64 " cap_limited=%d",
		Summary.m_aOperation,
		Summary.m_aContext,
		Summary.m_aPage,
		Summary.m_aTab,
		Summary.m_SampleFrames,
		Summary.m_SampleSeconds,
		Summary.m_FpsAvg,
		Summary.m_FpsMin,
		Summary.m_FpsOnePctLow,
		Summary.m_FpsMax,
		Summary.m_FrameMsAvg,
		Summary.m_FrameMsP95,
		Summary.m_FrameMsP99,
		Summary.m_FrameMsMax,
		Summary.m_MenuMsMax,
		Summary.m_WindowStartFrame,
		Summary.m_WindowEndFrame,
		Summary.m_CapLimited ? 1 : 0);
	QmPerfLogPayload("perf/fps", aPayload, Client());
}

const char *CMenus::SettingsPerfContextName() const
{
	return Client()->State() == IClient::STATE_ONLINE ? "online" : "offline";
}

const char *CMenus::SettingsPerfActiveOperation() const
{
	return m_SettingsPerfWindowTracker.ActiveOperation();
}

const char *CMenus::SettingsPerfStableTextScope(int Page) const
{
	(void)Page;
	if(str_comp(SettingsPerfActiveOperation(), "ingame_esc_open") == 0)
		return "target_settings";

	const char *pActivePage = m_SettingsPerfWindowTracker.ActivePage();
	if(pActivePage == nullptr || pActivePage[0] == '\0')
		return "settings";

	char aPage[32];
	str_copy(aPage, SettingsPageCacheKey(Page, -1).c_str(), sizeof(aPage));
	return str_comp(pActivePage, aPage) == 0 ? "target_settings" : "settings";
}

void CMenus::OnReset()
{
	ResetReportScan();
	ResetDemoScreenshotPreview();
	ClearQmClientSettingsSearchInputs();
	InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
}

void CMenus::OnShutdown()
{
	if(m_SettingsPerfWindowTracker.HasActiveWindow())
	{
		const SQmSettingsPerfWindowSummary Summary = m_SettingsPerfWindowTracker.FinishActiveWindow();
		LogSettingsPerfWindowSummary(Summary);
	}
	SaveSettingsRuntimeCacheMetadata();
	InvalidateSettingsTextPool();
	ClearSettingsAssetsCardMetadataCache();
	ClearSettingsTeePreviewCache();
	ClearSettingsLanguageRowCache();
	ResetDemoScreenshotPreview();
	m_CommunityIcons.Shutdown();
}

CUIElement &CMenus::SettingsTextElement(int Page, int Tab, const char *pTextId)
{
	SMenuTextStyleKey StyleKey;
	return MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, -1, pTextId, StyleKey);
}

CUIElement &CMenus::SettingsTextElement(int Page, int Tab, const char *pTextId, const SMenuTextStyleKey &StyleKey)
{
	return MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, -1, pTextId, StyleKey);
}

void CMenus::CollectMenuTextPlanItem(EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect *pRect, float FontSize, int Align, const SLabelProperties &LabelProps, const SMenuTextStyleKey &StyleKey)
{
	if(!m_MenuTextPlanCollecting || m_pMenuTextPlanCollection == nullptr || pTextId == nullptr || pTextId[0] == '\0' || pText == nullptr || pText[0] == '\0' || pRect == nullptr || FontSize <= 0.0f || pRect->w <= 0.0f || pRect->h <= 0.0f)
		return;

	SMenuTextPlanItem Item;
	Item.m_Scope = Scope;
	Item.m_Page = Page;
	Item.m_Tab = Tab;
	Item.m_Subtab = Subtab;
	Item.m_TextId = pTextId;
	Item.m_Text = pText;
	Item.m_Rect = *pRect;
	Item.m_FontSize = FontSize;
	Item.m_Align = Align;
	Item.m_LabelProps = LabelProps;
	Item.m_StyleKey = StyleKey;
	Item.m_StyleMode = MENU_TEXT_STYLE_EXACT;
	Item.m_SourceTag = "visible-wrapper";
	m_pMenuTextPlanCollection->push_back(Item);
}

CMenus::SMenuTextPlanItem CMenus::AddStableTextLabel(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, float FontSize, int Align, const SLabelProperties &LabelProps, const char *pSourceTag) const
{
	SMenuTextPlanItem Item;
	Item.m_Page = Page;
	Item.m_Tab = Tab;
	Item.m_Subtab = Subtab;
	Item.m_TextId = pTextId != nullptr ? pTextId : "";
	Item.m_Text = pText != nullptr ? pText : "";
	Item.m_Rect = Rect;
	Item.m_FontSize = FontSize;
	Item.m_Align = Align;
	Item.m_LabelProps = LabelProps;
	Item.m_StyleMode = MENU_TEXT_STYLE_RECT;
	Item.m_SourceTag = pSourceTag != nullptr ? pSourceTag : "label";
	return Item;
}

CMenus::SMenuTextPlanItem CMenus::AddStableTextDefault(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, float Width, float Height, float FontSize, int Align, const char *pSourceTag) const
{
	SMenuTextPlanItem Item = AddStableTextLabel(Page, Tab, Subtab, pTextId, pText, CUIRect{0.0f, 0.0f, Width, Height}, FontSize, Align, {}, pSourceTag != nullptr ? pSourceTag : "default-style");
	Item.m_StyleMode = MENU_TEXT_STYLE_DEFAULT;
	Item.m_StyleKey = {};
	return Item;
}

CMenus::SMenuTextStyleKey CMenus::BuildSettingsShellTitleTextStyle(const CUIRect &Rect, CUIRect *pOutLabel) const
{
	CUIRect Label = Rect;
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	if(pOutLabel != nullptr)
		*pOutLabel = Label;
	return BuildMenuTextStyleKey(&Label, 16.0f, TEXTALIGN_MC, Props);
}

CMenus::SMenuTextPlanItem CMenus::AddStableTextCheckbox(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, const char *pSourceTag) const
{
	CUIRect Box, Label;
	Rect.VSplitLeft(Rect.h, &Box, &Label);
	Label.VSplitLeft(5.0f, nullptr, &Label);
	Box.Margin(2.0f, &Box);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_MinimumFontSize = Box.h * CUi::ms_FontmodHeight * 0.7f;
	return AddStableTextLabel(Page, Tab, Subtab, pTextId, pText, Label, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML, Props, pSourceTag != nullptr ? pSourceTag : "checkbox");
}

CMenus::SMenuTextPlanItem CMenus::AddStableTextScrollbar(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, unsigned Flags, const char *pSourceTag) const
{
	const CUIRect Label = ui_widget::SliderInputFieldLabelRect(Rect, pText != nullptr && pText[0] != '\0', Flags);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	const float FontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Label, FontSize, TEXTALIGN_ML, Props);
	SMenuTextPlanItem Item = AddStableTextLabel(Page, Tab, Subtab, pTextId, pText, Label, FontSize, TEXTALIGN_ML, Props, pSourceTag != nullptr ? pSourceTag : "scrollbar");
	Item.m_StyleMode = MENU_TEXT_STYLE_EXACT;
	Item.m_StyleKey = StyleKey;
	return Item;
}

CMenus::SMenuTextPlanItem CMenus::AddStableTextButton(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, const char *pSourceTag) const
{
	CUIRect Text = Rect;
	Text.HMargin(Rect.h >= 20.0f ? 2.0f : 1.0f, &Text);
	SLabelProperties Props;
	Props.m_MaxWidth = Text.w;
	return AddStableTextLabel(Page, Tab, Subtab, pTextId, pText, Text, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC, Props, pSourceTag != nullptr ? pSourceTag : "button");
}

int CMenus::DoIngameMenuTab(CButtonContainer *pButtonContainer, int Page, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, int Corners)
{
	if(pTextId == nullptr)
		return DoButton_MenuTab(pButtonContainer, pText, Checked, pRect, Corners);
	CUIRect Text = *pRect;
	const float ContentScale = g_Config.m_QmNewUi != 0 ? MENU_MENUBAR_CONTENT_SCALE_NEW : 1.0f;
	Text.HMargin(2.0f * ContentScale, &Text);
	SLabelProperties Props;
	Props.m_MaxWidth = Text.w;
	const float FontSize = g_Config.m_QmNewUi != 0 ? ui_token::settings::TAB_FONT_SIZE * ContentScale : Text.h * CUi::ms_FontmodHeight;
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Text, FontSize, TEXTALIGN_MC, Props);
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, pText, &Text, FontSize, TEXTALIGN_MC, Props, StyleKey);
		return 0;
	}
	CUIElement &TextElement = MenuTextElement(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, StyleKey);
	if(g_Config.m_QmNewUi != 0)
		return DoMenuTabV2(pButtonContainer, pText, Checked != 0, pRect, Corners, nullptr, nullptr, nullptr, nullptr, &TextElement, ContentScale);
	return DoButton_MenuTab(pButtonContainer, pText, Checked, pRect, Corners, nullptr, nullptr, nullptr, nullptr, 10.0f, nullptr, &TextElement);
}

int CMenus::DoIngameMenuButton(int Page, const char *pTextId, CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Flags, int Corners, float Rounding)
{
	if(pTextId == nullptr)
		return DoButton_Menu(pButtonContainer, pText, Checked, pRect, Flags, nullptr, Corners, Rounding);

	CUIRect Text = MenuButtonTextRect(pRect, 0.0f, 0.0f);
	SLabelProperties Props;
	Props.m_MaxWidth = Text.w;
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Text, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC, Props);
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, pText, &Text, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC, Props, StyleKey);
		return 0;
	}
	CUIElement &TextElement = MenuTextElement(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, StyleKey);
	Text = MenuButtonTextRect(pRect, 0.0f, 0.0f);
	const int Result = DoButton_Menu(pButtonContainer, "", Checked, pRect, Flags, nullptr, Corners, Rounding);
	CUIElement::SUIElementRect *pElementRect = TextElement.Rect(0);
	const bool HadReadyContainer = pElementRect->m_UITextContainer.Valid();
	DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, TextElement, &Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC, Props);
	if(&TextElement != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid())
	{
		CountMenuTextImmediateFallback();
		Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC, Props);
	}
	return Result;
}

int CMenus::DoIngameMenuCheckBox(int Page, const char *pTextId, const void *pId, const char *pText, int Checked, const CUIRect *pRect)
{
	if(pTextId == nullptr)
		return DoButton_CheckBox(pId, pText, Checked, pRect);

	CUIRect Box, Label;
	pRect->VSplitLeft(pRect->h, &Box, &Label);
	Label.VSplitLeft(5.0f, nullptr, &Label);
	Box.Margin(2.0f, &Box);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_MinimumFontSize = Box.h * CUi::ms_FontmodHeight * 0.7f;
	if(m_MenuTextPlanCollecting)
	{
		DoIngameMenuLabel(Page, pTextId, &Label, pText, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML, Props);
		return 0;
	}

	const int Result = DoButton_CheckBox_Common_WithLabelElement(pId, "", Checked ? "X" : "", pRect, BUTTONFLAG_LEFT, nullptr);
	DoIngameMenuLabel(Page, pTextId, &Label, pText, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML, Props);
	return Result;
}

void CMenus::DoIngameMenuLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)
{
	if(pTextId == nullptr)
	{
		Ui()->DoLabel(pRect, pText, Size, Align, LabelProps);
		return;
	}
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(pRect, Size, Align, LabelProps);
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, pText, pRect, Size, Align, LabelProps, StyleKey);
		return;
	}
	CUIElement &Element = MenuTextElement(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, StyleKey);
	CUIElement::SUIElementRect *pElementRect = Element.Rect(0);
	const bool HadReadyContainer = pElementRect->m_UITextContainer.Valid();
	DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, Element, pRect, pText, Size, Align, LabelProps);
	if(&Element != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid() && pRect != nullptr)
	{
		CountMenuTextImmediateFallback();
		Ui()->DoLabel(pRect, pText, Size, Align, LabelProps);
	}
}

void CMenus::DoIngameMenuTitleLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)
{
	if(pTextId == nullptr)
	{
		Ui()->DoLabel(pRect, pText, Size, Align, LabelProps);
		return;
	}
	const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(pRect, Size, Align, LabelProps);
	if(m_MenuTextPlanCollecting)
	{
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, pText, pRect, Size, Align, LabelProps, StyleKey);
		return;
	}
	CUIElement &Element = MenuTextElement(MENU_TEXT_SCOPE_INGAME, Page, -1, -1, pTextId, StyleKey);
	CUIElement::SUIElementRect *pElementRect = Element.Rect(0);
	const bool HadReadyContainer = pElementRect->m_UITextContainer.Valid();
	DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, Element, pRect, pText, Size, Align, LabelProps);
	if(&Element != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid() && pRect != nullptr)
	{
		CountMenuTextImmediateFallback();
		Ui()->DoLabel(pRect, pText, Size, Align, LabelProps);
	}
}

CUIElement &CMenus::MenuTextElement(EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const SMenuTextStyleKey &StyleKey)
{
	const uint64_t LanguageHash = str_quickhash(g_Config.m_ClLanguagefile);
	const uint64_t FontHash = str_quickhash(g_Config.m_TcCustomFont);
	if(m_MenuTextPoolLanguageHash == 0 || m_MenuTextPoolFontHash == 0)
	{
		m_MenuTextPoolLanguageHash = LanguageHash;
		m_MenuTextPoolFontHash = FontHash;
	}
	else if(m_MenuTextPoolLanguageHash != LanguageHash || m_MenuTextPoolFontHash != FontHash)
	{
		const char *pReason = m_MenuTextPoolLanguageHash != LanguageHash ? "language" : "font";
		InvalidateMenuTextPool(pReason);
		m_MenuTextPoolLanguageHash = LanguageHash;
		m_MenuTextPoolFontHash = FontHash;
	}

	if(m_MenuTextPlanCollecting)
	{
		m_MenuTextPlanPendingItem = {};
		m_MenuTextPlanPendingItem.m_Scope = Scope;
		m_MenuTextPlanPendingItem.m_Page = Page;
		m_MenuTextPlanPendingItem.m_Tab = Tab;
		m_MenuTextPlanPendingItem.m_Subtab = Subtab;
		m_MenuTextPlanPendingItem.m_TextId = pTextId != nullptr ? pTextId : "";
		m_MenuTextPlanPendingItem.m_StyleKey = StyleKey;
		m_MenuTextPlanPendingItem.m_StyleMode = MENU_TEXT_STYLE_EXACT;
		m_MenuTextPlanPendingItem.m_SourceTag = "visible-wrapper";
		m_MenuTextPlanPendingActive = true;
		if(!m_MenuTextFallbackElement.IsRegistered())
			m_MenuTextFallbackElement.Init(Ui(), 1);
		return m_MenuTextFallbackElement;
	}

	const std::string Key = MenuTextCacheKey(Scope, Page, Tab, Subtab, pTextId, StyleKey);
	auto It = m_MenuTextPool.find(Key);
	const uint64_t CurrentFrame = Client()->PerfFrame();
	const bool HasDescriptor = m_SettingsMenuTextPlannedDescriptors.find(MenuTextDescriptorKey(Scope, Page, Tab, Subtab, pTextId)) != m_SettingsMenuTextPlannedDescriptors.end();
	const bool KeyPlanned = m_SettingsMenuTextPlannedKeys.find(Key) != m_SettingsMenuTextPlannedKeys.end();
	if(m_MenuTextPoolVisibleGuard)
	{
		if(m_MenuTextStableCandidatesThisFrame == 0)
		{
			m_MenuTextStableScopeThisFrame = Scope;
			m_MenuTextStablePageThisFrame = Page;
			m_MenuTextStableTabThisFrame = Tab;
			m_MenuTextStableSubtabThisFrame = Subtab;
		}
		++m_MenuTextStableCandidatesThisFrame;
		if(HasDescriptor)
			++m_MenuTextStablePlannedThisFrame;
		else
			++m_MenuTextStableUnplannedThisFrame;
		if(It != m_MenuTextPool.end() && It->second.m_Generation == m_MenuTextPoolGeneration)
		{
			++m_MenuTextStablePoolHitsThisFrame;
			if(It->second.m_Built)
				++m_MenuTextStableHitsThisFrame;
		}
	}
	if(It == m_MenuTextPool.end())
	{
		if(m_MenuTextPoolVisibleGuard)
		{
			++m_MenuTextStableMissesThisFrame;
			LogSettingsTextPoolCoverageGap(Client(), "settings_text_miss", Scope, SettingsPerfStableTextScope(Page), Page, Tab, Subtab, Key.c_str(), "missing", HasDescriptor ? (KeyPlanned ? "not_built" : "key_mismatch") : "missing_descriptor", SettingsPerfActiveOperation(), m_MenuTextCoverageFrame);
			if(!m_MenuTextFallbackElement.IsRegistered())
				m_MenuTextFallbackElement.Init(Ui(), 1);
			return m_MenuTextFallbackElement;
		}
		TrimMenuTextPoolForInsert(CurrentFrame);
		It = m_MenuTextPool.try_emplace(Key).first;
		It->second.m_Element.Init(Ui(), 1);
		It->second.m_StyleKey = StyleKey;
		It->second.m_Generation = m_MenuTextPoolGeneration;
	}
	else if(It->second.m_Generation != m_MenuTextPoolGeneration)
	{
		if(m_MenuTextPoolVisibleGuard)
		{
			++m_MenuTextStableStalesThisFrame;
			LogSettingsTextPoolCoverageGap(Client(), "settings_text_stale", Scope, SettingsPerfStableTextScope(Page), Page, Tab, Subtab, Key.c_str(), m_MenuTextPoolLastStaleReason.empty() ? "style" : m_MenuTextPoolLastStaleReason.c_str(), KeyPlanned ? "stale_generation" : (HasDescriptor ? "key_mismatch" : "missing_descriptor"), SettingsPerfActiveOperation(), m_MenuTextCoverageFrame);
			if(!m_MenuTextFallbackElement.IsRegistered())
				m_MenuTextFallbackElement.Init(Ui(), 1);
			return m_MenuTextFallbackElement;
		}
		Ui()->ResetUIElement(It->second.m_Element);
		It->second.m_StyleKey = StyleKey;
		It->second.m_Generation = m_MenuTextPoolGeneration;
		It->second.m_Built = false;
	}
	It->second.m_LastUsedFrame = CurrentFrame;
	return It->second.m_Element;
}

int CMenus::TrimMenuTextPoolForInsert(uint64_t CurrentFrame)
{
	int Evictions = 0;
	if(CurrentFrame != m_MenuTextLastTrimFrame)
	{
		m_MenuTextLastTrimFrame = CurrentFrame;
		for(auto It = m_MenuTextPool.begin(); It != m_MenuTextPool.end();)
		{
			const bool Expired = CurrentFrame > It->second.m_LastUsedFrame && CurrentFrame - It->second.m_LastUsedFrame > QM_MENU_TEXT_CACHE_MAX_AGE_FRAMES;
			if(!Expired)
			{
				++It;
				continue;
			}
			RemoveMenuTextContainerBuildRequest(It->second.m_Element);
			Ui()->ResetUIElement(It->second.m_Element);
			It = m_MenuTextPool.erase(It);
			++Evictions;
		}
	}
	while(m_MenuTextPool.size() >= QM_MENU_TEXT_CACHE_CAPACITY)
	{
		const auto Oldest = std::min_element(m_MenuTextPool.begin(), m_MenuTextPool.end(), [](const auto &A, const auto &B) {
			return A.second.m_LastUsedFrame < B.second.m_LastUsedFrame;
		});
		if(Oldest == m_MenuTextPool.end())
			break;
		RemoveMenuTextContainerBuildRequest(Oldest->second.m_Element);
		Ui()->ResetUIElement(Oldest->second.m_Element);
		m_MenuTextPool.erase(Oldest);
		++Evictions;
	}
	return Evictions;
}

void CMenus::DoSettingsLabelStreamed(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)
{
	DoMenuLabelStreamed(MENU_TEXT_SCOPE_SETTINGS, Element, pRect, pText, Size, Align, LabelProps, StrLen, pReadCursor, Render);
}

bool CMenus::MenuTextContainerNeedsBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, int StrLen, const CTextCursor *pReadCursor)
{
	if(pRect == nullptr || pText == nullptr)
		return false;
	CUIElement::SUIElementRect *pElementRect = Element.Rect(0);
	const bool TextChanged =
		(StrLen > 0 && (StrLen != (int)pElementRect->m_Text.size() || str_comp_num(pElementRect->m_Text.c_str(), pText, StrLen) != 0)) ||
		(StrLen != 0 && StrLen < 0 && str_comp(pElementRect->m_Text.c_str(), pText) != 0);
	const int ReadCursorGlyphCount = pReadCursor == nullptr ? -1 : pReadCursor->m_GlyphCount;
	const bool SizeChanged = pElementRect->m_Width != pRect->w || pElementRect->m_Height != pRect->h;
	const bool NeedsBuild =
		(!pElementRect->m_UITextContainer.Valid() && pText[0] != '\0' && StrLen != 0) ||
		TextChanged ||
		SizeChanged ||
		pElementRect->m_ReadCursorGlyphCount != ReadCursorGlyphCount;
	return NeedsBuild;
}

bool CMenus::RequestMenuTextContainerBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, int StrLen, const CTextCursor *pReadCursor)
{
	(void)Size;
	(void)Align;
	const bool NeedsBuild = MenuTextContainerNeedsBuild(Element, pRect, pText, StrLen, pReadCursor);
	if(!NeedsBuild)
		return true;
	if(m_pSettingsTextPrebuildBudget != nullptr)
		return *m_pSettingsTextPrebuildBudget > 0;
	if(m_CurrentSettingsUiFrameBudget.m_TextContainerTokens <= 0)
		return false;
	--m_CurrentSettingsUiFrameBudget.m_TextContainerTokens;
	return true;
}

void CMenus::QueueMenuTextContainerBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor)
{
	if(pRect == nullptr || pText == nullptr)
		return;
	for(SMenuTextContainerBuildRequest &Request : m_vMenuTextContainerBuildRequests)
	{
		if(Request.m_pElement == &Element)
		{
			Request.m_Text = pText;
			Request.m_Rect = *pRect;
			Request.m_Size = Size;
			Request.m_Align = Align;
			Request.m_LabelProps = LabelProps;
			Request.m_StrLen = StrLen;
			Request.m_ReadCursorGlyphCount = pReadCursor == nullptr ? -1 : pReadCursor->m_GlyphCount;
			return;
		}
	}
	SMenuTextContainerBuildRequest Request;
	Request.m_pElement = &Element;
	Request.m_Text = pText;
	Request.m_Rect = *pRect;
	Request.m_Size = Size;
	Request.m_Align = Align;
	Request.m_LabelProps = LabelProps;
	Request.m_StrLen = StrLen;
	Request.m_ReadCursorGlyphCount = pReadCursor == nullptr ? -1 : pReadCursor->m_GlyphCount;
	m_vMenuTextContainerBuildRequests.push_back(std::move(Request));
}

void CMenus::DrainMenuTextContainerBuildRequests()
{
	while(m_CurrentSettingsUiFrameBudget.m_TextContainerTokens > 0 && !m_vMenuTextContainerBuildRequests.empty())
	{
		SMenuTextContainerBuildRequest Request = std::move(m_vMenuTextContainerBuildRequests.front());
		m_vMenuTextContainerBuildRequests.erase(m_vMenuTextContainerBuildRequests.begin());
		if(Request.m_pElement == nullptr)
			continue;
		bool TextContainerRecreated = false;
		--m_CurrentSettingsUiFrameBudget.m_TextContainerTokens;
		DrainMenuTextContainerBuild(*Request.m_pElement, &Request.m_Rect, Request.m_Text.c_str(), Request.m_Size, Request.m_Align, Request.m_LabelProps, Request.m_StrLen, nullptr, false, &TextContainerRecreated);
		if(TextContainerRecreated)
		{
			for(auto &[Key, Entry] : m_MenuTextPool)
			{
				(void)Key;
				if(&Entry.m_Element == Request.m_pElement)
				{
					Entry.m_Built = true;
					Entry.m_Generation = m_MenuTextPoolGeneration;
					break;
				}
			}
		}
	}
}

void CMenus::RemoveMenuTextContainerBuildRequest(const CUIElement &Element)
{
	m_vMenuTextContainerBuildRequests.erase(
		std::remove_if(m_vMenuTextContainerBuildRequests.begin(), m_vMenuTextContainerBuildRequests.end(), [&Element](const SMenuTextContainerBuildRequest &Request) {
			return Request.m_pElement == &Element;
		}),
		m_vMenuTextContainerBuildRequests.end());
}

void CMenus::DrainMenuTextContainerBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render, bool *pTextContainerRecreated)
{
	Ui()->DoLabelStreamed(*Element.Rect(0), pRect, pText, Size, Align, LabelProps, StrLen, pReadCursor, Render, pTextContainerRecreated);
}

void CMenus::CountMenuTextImmediateFallback()
{
	if(m_MenuTextPoolVisibleGuard)
		++m_MenuTextStableFallbackImmediateThisFrame;
}

void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)
{
	(void)Scope;
	if(pText == nullptr)
		return;

	if(m_MenuTextPlanCollecting)
	{
		if(m_MenuTextPlanPendingActive)
		{
			SMenuTextPlanItem Item = m_MenuTextPlanPendingItem;
			Item.m_Text = pText;
			Item.m_Rect = pRect != nullptr ? *pRect : CUIRect{};
			Item.m_FontSize = Size;
			Item.m_Align = Align;
			Item.m_LabelProps = LabelProps;
			if(m_pMenuTextPlanCollection != nullptr && SettingsMenuTextPlanItemBuildable(Item))
				m_pMenuTextPlanCollection->push_back(Item);
			m_MenuTextPlanPendingActive = false;
		}
		return;
	}

	if(&Element == &m_MenuTextFallbackElement)
	{
		if(Render && m_MenuTextPoolVisibleGuard)
			++m_MenuTextStableFallbackImmediateThisFrame;
		if(Render)
			Ui()->DoLabel(pRect, pText, Size, Align, LabelProps);
		return;
	}

	CUIElement::SUIElementRect *pElementRect = Element.Rect(0);
	if(pElementRect->m_TextColor != TextRender()->GetTextColor() || pElementRect->m_TextOutlineColor != TextRender()->GetTextOutlineColor())
	{
		pElementRect->m_TextColor = TextRender()->GetTextColor();
		pElementRect->m_TextOutlineColor = TextRender()->GetTextOutlineColor();
	}

	const bool NeedsBuild = MenuTextContainerNeedsBuild(Element, pRect, pText, StrLen, pReadCursor);
	if(NeedsBuild && m_pSettingsTextPrebuildBudget == nullptr)
	{
		if(m_MenuTextPoolVisibleGuard)
			++m_MenuTextStableBuildQueuedThisFrame;
		QueueMenuTextContainerBuild(Element, pRect, pText, Size, Align, LabelProps, StrLen, pReadCursor);
		if(Render && pElementRect->m_UITextContainer.Valid() && pRect != nullptr)
		{
			Ui()->RenderLabelTextContainerAligned(*pElementRect, pRect, Align);
		}
		return;
	}

	if(m_pSettingsTextPrebuildBudget != nullptr)
	{
		if(MenuTextContainerNeedsBuild(Element, pRect, pText, StrLen, pReadCursor))
		{
			SSettingsWarmupFrameBudget Budget{};
			Budget.m_MaxTextContainers = *m_pSettingsTextPrebuildBudget;
			if(!SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::TEXT_CONTAINER))
				return;
			*m_pSettingsTextPrebuildBudget = Budget.m_MaxTextContainers;
			bool TextContainerRecreated = false;
			DrainMenuTextContainerBuild(Element, pRect, pText, Size, Align, LabelProps, StrLen, pReadCursor, Render, &TextContainerRecreated);
			if(TextContainerRecreated)
			{
				for(auto &[Key, Entry] : m_MenuTextPool)
				{
					(void)Key;
					if(&Entry.m_Element == &Element)
					{
						Entry.m_Built = true;
						Entry.m_Generation = m_MenuTextPoolGeneration;
						break;
					}
				}
			}
			if(m_pActiveSettingsTextPerfStats != nullptr)
			{
				if(TextContainerRecreated)
					++m_pActiveSettingsTextPerfStats->m_New;
				else
					++m_pActiveSettingsTextPerfStats->m_Reused;
			}
			return;
		}
	}

	bool TextContainerRecreated = false;
	if(Render && pElementRect->m_UITextContainer.Valid() && pRect != nullptr)
	{
		if(m_MenuTextPoolVisibleGuard)
			++m_MenuTextStableRenderReadyHitsThisFrame;
		Ui()->RenderLabelTextContainerAligned(*pElementRect, pRect, Align);
	}
	if(m_pActiveSettingsTextPerfStats != nullptr)
	{
		if(TextContainerRecreated)
			++m_pActiveSettingsTextPerfStats->m_New;
		else
			++m_pActiveSettingsTextPerfStats->m_Reused;
	}
	if(m_MenuTextPoolVisibleGuard)
	{
		if(TextContainerRecreated)
			++m_MenuTextStableTextNewThisFrame;
		else
		{
			++m_MenuTextStableTextReusedThisFrame;
			++m_MenuTextStableReusedThisFrame;
		}
	}
}

int CMenus::SettingsTextContainerCount()
{
	int Count = 0;
	for(auto &[Key, Entry] : m_MenuTextPool)
	{
		(void)Key;
		const CUIElement::SUIElementRect *pRect = Entry.m_Element.Rect(0);
		if(pRect != nullptr && pRect->m_UITextContainer.Valid())
			++Count;
	}
	return Count;
}

int CMenus::MenuTextPoolSizeForTesting() const
{
	return (int)m_MenuTextPool.size();
}

CMenus::CScopedMenuTextVisibleGuard::CScopedMenuTextVisibleGuard(CMenus *pMenus) :
	m_pMenus(pMenus),
	m_Previous(pMenus->m_MenuTextPoolVisibleGuard)
{
	m_pMenus->EnsureSettingsMenuTextPlanReadyForVisible();
	m_pMenus->m_MenuTextPoolVisibleGuard = true;
	if(!m_Previous)
	{
		// Stack bottom: clear per-frame counters (defense against cross-frame accumulation).
		// Nested guards inherit the parent's accumulated counters and add their own on top,
		// so only the outermost guard flushes the log on destruct (see v2 roadmap §4.2.2
		// double-bookkeeping fix). Without this gate, inner guard wipes outer's accumulation
		// and both destructors log — producing 2x duplicate settings_text_usage payloads.
		++m_pMenus->m_MenuTextCoverageFrame;
		m_pMenus->m_MenuTextStableCandidatesThisFrame = 0;
		m_pMenus->m_MenuTextStableHitsThisFrame = 0;
		m_pMenus->m_MenuTextStablePoolHitsThisFrame = 0;
		m_pMenus->m_MenuTextStableRenderReadyHitsThisFrame = 0;
		m_pMenus->m_MenuTextStableBuildQueuedThisFrame = 0;
		m_pMenus->m_MenuTextStableFallbackImmediateThisFrame = 0;
		m_pMenus->m_MenuTextStableReusedThisFrame = 0;
		m_pMenus->m_MenuTextStableTextNewThisFrame = 0;
		m_pMenus->m_MenuTextStableTextReusedThisFrame = 0;
		m_pMenus->m_MenuTextStableScopeThisFrame = CMenus::MENU_TEXT_SCOPE_SETTINGS;
		m_pMenus->m_MenuTextStablePageThisFrame = -1;
		m_pMenus->m_MenuTextStableTabThisFrame = -1;
		m_pMenus->m_MenuTextStableSubtabThisFrame = -1;
		m_pMenus->m_MenuTextStableMissesThisFrame = 0;
		m_pMenus->m_MenuTextStableStalesThisFrame = 0;
		m_pMenus->m_MenuTextStablePlannedThisFrame = 0;
		m_pMenus->m_MenuTextStableUnplannedThisFrame = 0;
	}
}

CMenus::CScopedMenuTextVisibleGuard::~CScopedMenuTextVisibleGuard()
{
	if(!m_Previous && m_pMenus->m_MenuTextStableCandidatesThisFrame > 0)
	{
		const int Page = m_pMenus->m_MenuTextStablePageThisFrame;
		const CMenus::EMenuTextScope Scope = m_pMenus->m_MenuTextStableScopeThisFrame;
		LogSettingsTextPoolUsage(m_pMenus->Client(), Scope, m_pMenus->SettingsPerfStableTextScope(Page), Page, m_pMenus->m_MenuTextStableTabThisFrame, m_pMenus->m_MenuTextStableSubtabThisFrame, m_pMenus->SettingsPerfActiveOperation(), m_pMenus->m_MenuTextCoverageFrame,
			m_pMenus->m_MenuTextStableCandidatesThisFrame, m_pMenus->m_MenuTextStableHitsThisFrame, m_pMenus->m_MenuTextStableReusedThisFrame,
			m_pMenus->m_MenuTextStableMissesThisFrame, m_pMenus->m_MenuTextStableStalesThisFrame, m_pMenus->m_MenuTextStableTextNewThisFrame, m_pMenus->m_MenuTextStableTextReusedThisFrame,
			m_pMenus->m_MenuTextStablePlannedThisFrame, m_pMenus->m_MenuTextStableUnplannedThisFrame, m_pMenus->m_MenuTextStablePoolHitsThisFrame, m_pMenus->m_MenuTextStableRenderReadyHitsThisFrame,
			m_pMenus->m_MenuTextStableBuildQueuedThisFrame, m_pMenus->m_MenuTextStableFallbackImmediateThisFrame);
	}
	m_pMenus->m_MenuTextPoolVisibleGuard = m_Previous;
}

bool CMenus::PrebuildSettingsTextPlanItem(const SMenuTextPlanItem &Item, int &RemainingBudget)
{
	if(RemainingBudget <= 0 || !SettingsMenuTextPlanItemBuildable(Item))
		return false;

	const SMenuTextStyleKey StyleKey = SettingsMenuTextPlanStyleKey(Item);
	CUIElement &Element = MenuTextElement(Item.m_Scope, Item.m_Page, Item.m_Tab, Item.m_Subtab, Item.m_TextId.c_str(), StyleKey);
	CUIElement::SUIElementRect *pRect = Element.Rect(0);
	const bool NeedsBuild =
		!pRect->m_UITextContainer.Valid() ||
		pRect->m_Width != Item.m_Rect.w ||
		pRect->m_Height != Item.m_Rect.h ||
		pRect->m_Text != Item.m_Text;
	if(NeedsBuild)
	{
		SSettingsWarmupFrameBudget Budget{};
		Budget.m_MaxTextContainers = RemainingBudget;
		if(!SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::TEXT_CONTAINER))
			return false;
		RemainingBudget = Budget.m_MaxTextContainers;
	}

	bool TextContainerRecreated = false;
	DrainMenuTextContainerBuild(Element, &Item.m_Rect, Item.m_Text.c_str(), Item.m_FontSize, Item.m_Align, Item.m_LabelProps, -1, nullptr, false, &TextContainerRecreated);
	if(pRect->m_UITextContainer.Valid())
	{
		for(auto &[Key, Entry] : m_MenuTextPool)
		{
			(void)Key;
			if(&Entry.m_Element == &Element)
			{
				Entry.m_Built = true;
				Entry.m_Generation = m_MenuTextPoolGeneration;
				break;
			}
		}
	}
	return true;
}

void CMenus::BuildBaseSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)
{
	static constexpr int s_aBaseSettingsPages[] = {
		SETTINGS_GENERAL,
		SETTINGS_TEE,
		SETTINGS_APPEARANCE,
		SETTINGS_CONTROLS,
		SETTINGS_GRAPHICS,
		SETTINGS_SOUND,
		SETTINGS_ASSETS,
		SETTINGS_DDNET,
	};

	const int PreviousSettingsPage = g_Config.m_UiSettingsPage;
	const bool PreviousCollecting = m_MenuTextPlanCollecting;
	std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
	const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
	SMenuTextPlanItem PreviousPendingItem;
	if(PreviousPendingActive)
		PreviousPendingItem = m_MenuTextPlanPendingItem;
	const int PreviousTextContextPage = m_SettingsTextContextPage;
	const int PreviousTextContextTab = m_SettingsTextContextTab;
	const int PreviousTextContextSubtab = m_SettingsTextContextSubtab;

	m_MenuTextPlanCollecting = true;
	m_pMenuTextPlanCollection = &vItems;
	m_MenuTextPlanPendingActive = false;
	Ui()->BeginRenderOnly();
	for(const int Page : s_aBaseSettingsPages)
	{
		g_Config.m_UiSettingsPage = Page;
		RenderSettings(MainView);
	}
	Ui()->EndRenderOnly();

	m_SettingsTextContextSubtab = PreviousTextContextSubtab;
	m_SettingsTextContextTab = PreviousTextContextTab;
	m_SettingsTextContextPage = PreviousTextContextPage;
	if(PreviousPendingActive)
		m_MenuTextPlanPendingItem = PreviousPendingItem;
	m_MenuTextPlanPendingActive = PreviousPendingActive;
	m_pMenuTextPlanCollection = pPreviousCollection;
	m_MenuTextPlanCollecting = PreviousCollecting;
	g_Config.m_UiSettingsPage = PreviousSettingsPage;
}

void CMenus::BuildIngameMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)
{
	static constexpr const char *s_apRequiredIngameTabTextIds[] = {
		"ingame-tab-game",
		"ingame-tab-players",
		"ingame-tab-server-info",
		"ingame-tab-browser",
		"ingame-tab-ghost",
		"ingame-tab-call-vote",
	};
	(void)s_apRequiredIngameTabTextIds;
	const int PreviousGamePage = m_GamePage;
	const bool PreviousControlPageOpening = m_ControlPageOpening;
	const bool PreviousCollecting = m_MenuTextPlanCollecting;
	std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
	const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
	SMenuTextPlanItem PreviousPendingItem;
	if(PreviousPendingActive)
		PreviousPendingItem = m_MenuTextPlanPendingItem;

	CUIRect TabBar, ContentView;
	const bool UseNewUi = g_Config.m_QmNewUi != 0;
	MainView.HSplitTop(MenuMenubarHeight(UseNewUi), &TabBar, &ContentView);
	if(UseNewUi)
		ContentView.HSplitTop(6.0f, nullptr, &ContentView);

	m_MenuTextPlanCollecting = true;
	m_pMenuTextPlanCollection = &vItems;
	m_MenuTextPlanPendingActive = false;
	Ui()->BeginRenderOnly();

	m_GamePage = PAGE_SERVER_INFO;
	RenderMenubar(TabBar, IClient::STATE_ONLINE);
	RenderServerInfo(ContentView);

	Ui()->EndRenderOnly();
	if(PreviousPendingActive)
		m_MenuTextPlanPendingItem = PreviousPendingItem;
	m_MenuTextPlanPendingActive = PreviousPendingActive;
	m_pMenuTextPlanCollection = pPreviousCollection;
	m_MenuTextPlanCollecting = PreviousCollecting;
	m_ControlPageOpening = PreviousControlPageOpening;
	m_GamePage = PreviousGamePage;
}

void CMenus::BuildSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems)
{
	const CUIRect Screen = Ui()->Screen() != nullptr ? *Ui()->Screen() : CUIRect{0.0f, 0.0f, 900.0f, 700.0f};
	CUIRect SettingsMainView = MenuTextSettingsContentView(Screen);
	if(str_comp(SettingsPerfActiveOperation(), "ingame_esc_open") == 0)
		BuildIngameMenuTextPlan(vItems, Screen);
	BuildSettingsMenuTextPlan(vItems, SettingsMainView);
}

void CMenus::BuildSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)
{
	const bool PreferQmClient = SettingsCanonicalPage(m_SettingsRuntimeMetadata.m_LastPage) == SETTINGS_QMCLIENT;
	if(PreferQmClient)
	{
		BuildQmClientSettingsMenuTextPlan(vItems, MainView, m_SettingsRuntimeMetadata.m_LastQmTab);
		BuildTClientSettingsMenuTextPlan(vItems, MainView, m_SettingsRuntimeMetadata.m_LastTClientTab);
	}
	else
	{
		BuildTClientSettingsMenuTextPlan(vItems, MainView, m_SettingsRuntimeMetadata.m_LastTClientTab);
		BuildQmClientSettingsMenuTextPlan(vItems, MainView, m_SettingsRuntimeMetadata.m_LastQmTab);
	}
	constexpr int NumTClientTextPlanTabs = 6;
	for(int Tab = 0; Tab < NumTClientTextPlanTabs; ++Tab)
	{
		if(Tab != CanonicalizeTClientCacheTab(m_SettingsRuntimeMetadata.m_LastTClientTab))
			BuildTClientSettingsMenuTextPlan(vItems, MainView, Tab);
	}
	for(int Tab = 0; Tab < NUMBER_OF_QMCLIENT_SETTINGS_TABS; ++Tab)
	{
		if(Tab != std::clamp(m_SettingsRuntimeMetadata.m_LastQmTab, 0, NUMBER_OF_QMCLIENT_SETTINGS_TABS - 1))
			BuildQmClientSettingsMenuTextPlan(vItems, MainView, Tab);
	}
	BuildBaseSettingsMenuTextPlan(vItems, MainView);
}

void CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)
{
	const char *pOperation = pOperationOverride != nullptr ? pOperationOverride : SettingsPerfActiveOperation();
	const std::string Operation = pOperation != nullptr ? pOperation : "";
	const bool IngameEscOperation = str_comp(pOperation, "ingame_esc_open") == 0;
	if(!m_SettingsMenuTextPlanCollectionDirty &&
		m_SettingsMenuTextPlanCollectionGeneration == m_MenuTextPoolGeneration &&
		m_SettingsMenuTextPlanCollectionOperation == Operation &&
		!m_vSettingsMenuTextPlanCollectionUnits.empty())
		return;

	m_vSettingsMenuTextPrebuildPlan.clear();
	m_vSettingsMenuTextPlanCollectionUnits.clear();
	m_SettingsMenuTextPlannedDescriptors.clear();
	m_SettingsMenuTextPlannedKeys.clear();
	m_SettingsMenuTextPlanCursor = 0;
	m_SettingsMenuTextPlanCollectionCursor = 0;
	m_SettingsMenuTextPlanGeneration = m_MenuTextPoolGeneration;
	m_SettingsMenuTextPlanCollectionGeneration = m_MenuTextPoolGeneration;
	m_SettingsMenuTextPlanCollectionOperation = Operation;
	m_SettingsMenuTextPlanMetadataDirty = false;
	m_SettingsMenuTextPlanCollectionDirty = false;
	m_SettingsMenuTextPlanCollectionComplete = false;

	if(IngameEscOperation)
		m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_INGAME_ESC, -1, -1});

	const int CurrentPage = SettingsCanonicalPage(g_Config.m_UiSettingsPage);
	int CurrentTab = -1;
	if(CurrentPage == SETTINGS_TCLIENT)
		CurrentTab = m_TClientSettingsTab;
	else if(CurrentPage == SETTINGS_QMCLIENT)
		CurrentTab = m_QmClientSettingsTab;
	m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS, CurrentPage, CurrentTab});

	const bool PreferQmClient = SettingsCanonicalPage(m_SettingsRuntimeMetadata.m_LastPage) == SETTINGS_QMCLIENT;
	const int LastTClientTab = CanonicalizeTClientCacheTab(m_SettingsRuntimeMetadata.m_LastTClientTab);
	const int LastQmClientTab = std::clamp(m_SettingsRuntimeMetadata.m_LastQmTab, 0, NUMBER_OF_QMCLIENT_SETTINGS_TABS - 1);
	if(PreferQmClient)
	{
		m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB, SETTINGS_QMCLIENT, LastQmClientTab});
		m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_TCLIENT_TAB, SETTINGS_TCLIENT, LastTClientTab});
	}
	else
	{
		m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_TCLIENT_TAB, SETTINGS_TCLIENT, LastTClientTab});
		m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB, SETTINGS_QMCLIENT, LastQmClientTab});
	}

	constexpr int NumTClientTextPlanTabs = 6;
	for(int Tab = 0; Tab < NumTClientTextPlanTabs; ++Tab)
	{
		if(Tab != LastTClientTab)
			m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_TCLIENT_TAB, SETTINGS_TCLIENT, Tab});
	}
	for(int Tab = 0; Tab < NUMBER_OF_QMCLIENT_SETTINGS_TABS; ++Tab)
	{
		if(Tab != LastQmClientTab)
			m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB, SETTINGS_QMCLIENT, Tab});
	}

	static constexpr int s_aBaseSettingsPages[] = {
		SETTINGS_GENERAL,
		SETTINGS_TEE,
		SETTINGS_APPEARANCE,
		SETTINGS_CONTROLS,
		SETTINGS_GRAPHICS,
		SETTINGS_SOUND,
		SETTINGS_ASSETS,
		SETTINGS_DDNET,
	};
	for(const int Page : s_aBaseSettingsPages)
		m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_BASE_PAGE, Page, -1});
}

void CMenus::CollectSettingsMenuTextPlanUnit(const SSettingsMenuTextPlanCollectionUnit &Unit, CUIRect Screen, CUIRect SettingsMainView)
{
	switch(Unit.m_Kind)
	{
	case MENU_TEXT_PLAN_UNIT_VISIBLE_SETTINGS:
		BuildVisibleSettingsMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, SettingsMainView);
		break;
	case MENU_TEXT_PLAN_UNIT_TCLIENT_TAB:
		BuildTClientSettingsMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, SettingsMainView, Unit.m_Tab);
		break;
	case MENU_TEXT_PLAN_UNIT_QMCLIENT_TAB:
		BuildQmClientSettingsMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, SettingsMainView, Unit.m_Tab);
		break;
	case MENU_TEXT_PLAN_UNIT_BASE_PAGE:
	{
		const int PreviousSettingsPage = g_Config.m_UiSettingsPage;
		const bool PreviousCollecting = m_MenuTextPlanCollecting;
		std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
		const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
		SMenuTextPlanItem PreviousPendingItem;
		if(PreviousPendingActive)
			PreviousPendingItem = m_MenuTextPlanPendingItem;
		g_Config.m_UiSettingsPage = Unit.m_Page;
		m_MenuTextPlanCollecting = true;
		m_pMenuTextPlanCollection = &m_vSettingsMenuTextPrebuildPlan;
		m_MenuTextPlanPendingActive = false;
		Ui()->BeginRenderOnly();
		RenderSettings(SettingsMainView);
		Ui()->EndRenderOnly();
		if(PreviousPendingActive)
			m_MenuTextPlanPendingItem = PreviousPendingItem;
		m_MenuTextPlanPendingActive = PreviousPendingActive;
		m_pMenuTextPlanCollection = pPreviousCollection;
		m_MenuTextPlanCollecting = PreviousCollecting;
		g_Config.m_UiSettingsPage = PreviousSettingsPage;
		break;
	}
	case MENU_TEXT_PLAN_UNIT_INGAME_ESC:
		BuildIngameMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, Screen);
		break;
	}
}

bool CMenus::AdvanceSettingsMenuTextPlanCollection(int Budget, const char *pOperationOverride)
{
	m_SettingsMenuTextLastCollectionStats = {};
	m_SettingsMenuTextLastCollectionStats.m_Budget = maximum(Budget, 0);
	if(Budget > 0 && Ui()->Screen() != nullptr)
	{
		const int CurrentScreenBucket = round_to_int(Ui()->Screen()->w);
		if(s_LastPlanCollectionScreenBucket >= 0 && s_LastPlanCollectionScreenBucket != CurrentScreenBucket)
			m_SettingsMenuTextPlanCollectionDirty = true;
		s_LastPlanCollectionScreenBucket = CurrentScreenBucket;
	}
	PrepareSettingsMenuTextPlanCollectionUnits(pOperationOverride);
	m_SettingsMenuTextLastCollectionStats.m_UnitsTotal = (int)m_vSettingsMenuTextPlanCollectionUnits.size();
	if(Budget <= 0 || Ui()->Screen() == nullptr)
	{
		m_SettingsMenuTextLastCollectionStats.m_UnitsDone = (int)m_SettingsMenuTextPlanCollectionCursor;
		m_SettingsMenuTextLastCollectionStats.m_Remaining = maximum(0, (int)m_vSettingsMenuTextPlanCollectionUnits.size() - (int)m_SettingsMenuTextPlanCollectionCursor);
		m_SettingsMenuTextLastCollectionStats.m_Complete = m_SettingsMenuTextPlanCollectionComplete;
		m_SettingsMenuTextLastCollectionStats.m_Dirty = m_SettingsMenuTextPlanCollectionDirty;
		return m_SettingsMenuTextPlanCollectionComplete;
	}

	const CUIRect Screen = *Ui()->Screen();
	CUIRect SettingsMainView = MenuTextSettingsContentView(Screen);
	int RemainingBudget = Budget;
	while(RemainingBudget > 0 && m_SettingsMenuTextPlanCollectionCursor < m_vSettingsMenuTextPlanCollectionUnits.size())
	{
		const size_t PreviousItemCount = m_vSettingsMenuTextPrebuildPlan.size();
		CollectSettingsMenuTextPlanUnit(m_vSettingsMenuTextPlanCollectionUnits[m_SettingsMenuTextPlanCollectionCursor], Screen, SettingsMainView);
		for(size_t ItemIndex = PreviousItemCount; ItemIndex < m_vSettingsMenuTextPrebuildPlan.size(); ++ItemIndex)
		{
			const SMenuTextPlanItem &Item = m_vSettingsMenuTextPrebuildPlan[ItemIndex];
			if(!SettingsMenuTextPlanItemBuildable(Item))
				continue;
			const SMenuTextStyleKey StyleKey = SettingsMenuTextPlanStyleKey(Item);
			m_SettingsMenuTextPlannedDescriptors.insert(MenuTextDescriptorKey(Item.m_Scope, Item.m_Page, Item.m_Tab, Item.m_Subtab, Item.m_TextId.c_str()));
			m_SettingsMenuTextPlannedKeys.insert(MenuTextCacheKey(Item.m_Scope, Item.m_Page, Item.m_Tab, Item.m_Subtab, Item.m_TextId.c_str(), StyleKey));
		}
		++m_SettingsMenuTextPlanCollectionCursor;
		--RemainingBudget;
	}
	m_SettingsMenuTextPlanCollectionComplete = m_SettingsMenuTextPlanCollectionCursor >= m_vSettingsMenuTextPlanCollectionUnits.size();
	m_SettingsMenuTextLastCollectionStats.m_UnitsDone = (int)m_SettingsMenuTextPlanCollectionCursor;
	m_SettingsMenuTextLastCollectionStats.m_Remaining = maximum(0, (int)m_vSettingsMenuTextPlanCollectionUnits.size() - (int)m_SettingsMenuTextPlanCollectionCursor);
	m_SettingsMenuTextLastCollectionStats.m_Complete = m_SettingsMenuTextPlanCollectionComplete;
	m_SettingsMenuTextLastCollectionStats.m_Dirty = m_SettingsMenuTextPlanCollectionDirty;
	return m_SettingsMenuTextPlanCollectionComplete;
}

void CMenus::BuildVisibleSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)
{
	const int Page = SettingsCanonicalPage(g_Config.m_UiSettingsPage);
	if(Page == SETTINGS_TCLIENT)
	{
		BuildTClientSettingsMenuTextPlan(vItems, MainView, m_TClientSettingsTab);
		return;
	}
	if(Page == SETTINGS_QMCLIENT)
	{
		BuildQmClientSettingsMenuTextPlan(vItems, MainView, m_QmClientSettingsTab);
		return;
	}

	const int PreviousSettingsPage = g_Config.m_UiSettingsPage;
	const bool PreviousCollecting = m_MenuTextPlanCollecting;
	std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
	const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
	SMenuTextPlanItem PreviousPendingItem;
	if(PreviousPendingActive)
		PreviousPendingItem = m_MenuTextPlanPendingItem;
	g_Config.m_UiSettingsPage = Page;
	m_MenuTextPlanCollecting = true;
	m_pMenuTextPlanCollection = &vItems;
	m_MenuTextPlanPendingActive = false;
	Ui()->BeginRenderOnly();
	RenderSettings(MainView);
	Ui()->EndRenderOnly();
	if(PreviousPendingActive)
		m_MenuTextPlanPendingItem = PreviousPendingItem;
	m_MenuTextPlanPendingActive = PreviousPendingActive;
	m_pMenuTextPlanCollection = pPreviousCollection;
	m_MenuTextPlanCollecting = PreviousCollecting;
	g_Config.m_UiSettingsPage = PreviousSettingsPage;
}

int CMenus::CountMissingSettingsMenuTextPlanItems() const
{
	if(m_SettingsMenuTextPlanCollectionDirty || !m_SettingsMenuTextPlanCollectionComplete)
		return maximum(0, (int)m_vSettingsMenuTextPlanCollectionUnits.size() - (int)m_SettingsMenuTextPlanCollectionCursor);
	return maximum(0, (int)m_vSettingsMenuTextPrebuildPlan.size() - (int)m_SettingsMenuTextPlanCursor);
}

void CMenus::PrebuildSettingsMenuTextPool(int Budget, const char *pScopeOverride, const char *pOperationOverride)
{
	PrebuildSettingsTextPoolForLoading(Budget, pOperationOverride);
	const int Built = m_SettingsMenuTextLastPrebuildStats.m_Built;
	const int Reused = m_SettingsMenuTextLastPrebuildStats.m_Reused;
	const int RemainingMissing = m_SettingsMenuTextLastPrebuildStats.m_Remaining;
	const size_t Cursor = m_SettingsMenuTextPlanCursor;
	(void)Cursor;
	const char *pActiveOperation = SettingsPerfActiveOperation();
	const char *pOperation = pOperationOverride != nullptr ? pOperationOverride : pActiveOperation;
	const char *pPhase = "before_target";
	const char *pTargetScope = "target_settings";
	const char *pScope = pScopeOverride != nullptr ? pScopeOverride : (pActiveOperation[0] != '\0' && str_comp(pActiveOperation, "none") != 0 ? pTargetScope : "settings");
	if(PerfDebugEnabled())
	{
		char aPayload[256];
		// Keep the emitted contract searchable: phase=before_target scope=target_settings.
		str_format(aPayload, sizeof(aPayload), "event=settings_text_prebuild built=%d reused=%d remaining=%d budget=%d phase=%s scope=%s operation=%s",
			Built, Reused, RemainingMissing, Budget, pPhase, pScope, pOperation);
		QmPerfLogPayload("perf/settings-text", aPayload, Client(), CurrentQmUiPerfPage() != nullptr ? CurrentQmUiPerfPage() : "settings");
		str_format(aPayload, sizeof(aPayload), "event=settings_text_plan_collection units_done=%d units_total=%d remaining=%d budget=%d complete=%d dirty=%d phase=%s scope=%s operation=%s",
			m_SettingsMenuTextLastCollectionStats.m_UnitsDone, m_SettingsMenuTextLastCollectionStats.m_UnitsTotal, m_SettingsMenuTextLastCollectionStats.m_Remaining, m_SettingsMenuTextLastCollectionStats.m_Budget,
			m_SettingsMenuTextLastCollectionStats.m_Complete ? 1 : 0, m_SettingsMenuTextLastCollectionStats.m_Dirty ? 1 : 0, pPhase, pScope, pOperation);
		QmPerfLogPayload("perf/settings-text", aPayload, Client(), CurrentQmUiPerfPage() != nullptr ? CurrentQmUiPerfPage() : "settings");
	}
}

void CMenus::EnsureSettingsMenuTextPlanReadyForVisible()
{
	if(Ui()->Screen() == nullptr)
		return;
	if(!m_SettingsMenuTextPlanMetadataDirty && m_SettingsMenuTextPlanGeneration == m_MenuTextPoolGeneration && !m_vSettingsMenuTextPrebuildPlan.empty() && !m_SettingsMenuTextPlannedDescriptors.empty())
		return;

	// InvalidateMenuTextPool 在 visible guard 激活时不会 clear 这些容器，
	// rebuild 前必须先清空，否则会在旧数据上 append 导致 plan items 重复
	// （内存增长 + prebuild remaining telemetry 偏差）。
	m_vSettingsMenuTextPrebuildPlan.clear();
	m_SettingsMenuTextPlannedDescriptors.clear();
	m_SettingsMenuTextPlannedKeys.clear();
	const CUiRenderOnlyScope RenderOnly(Ui());
	std::vector<SMenuTextPlanItem> vVisibleItems;
	const CUIRect Screen = *Ui()->Screen();
	CUIRect SettingsMainView = MenuTextSettingsContentView(Screen);
	if(str_comp(SettingsPerfActiveOperation(), "ingame_esc_open") == 0)
		BuildIngameMenuTextPlan(vVisibleItems, Screen);
	else
		BuildVisibleSettingsMenuTextPlan(vVisibleItems, SettingsMainView);
	m_SettingsMenuTextPlanGeneration = m_MenuTextPoolGeneration;
	for(const SMenuTextPlanItem &Item : vVisibleItems)
	{
		if(!SettingsMenuTextPlanItemBuildable(Item))
			continue;
		const SMenuTextStyleKey StyleKey = SettingsMenuTextPlanStyleKey(Item);
		m_SettingsMenuTextPlannedDescriptors.insert(MenuTextDescriptorKey(Item.m_Scope, Item.m_Page, Item.m_Tab, Item.m_Subtab, Item.m_TextId.c_str()));
		m_SettingsMenuTextPlannedKeys.insert(MenuTextCacheKey(Item.m_Scope, Item.m_Page, Item.m_Tab, Item.m_Subtab, Item.m_TextId.c_str(), StyleKey));
		m_vSettingsMenuTextPrebuildPlan.push_back(Item);
	}
	m_SettingsMenuTextPlanMetadataDirty = false;
}

void CMenus::PrebuildIngameEscTextPoolBeforeOpen(int Budget)
{
	if(Budget <= 0)
		return;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameId = GameClient()->PerfFrameOrOne();
	str_copy(Input.m_aOperation, SettingsPerfActiveOperation(), sizeof(Input.m_aOperation));
	str_copy(Input.m_aPage, "settings", sizeof(Input.m_aPage));
	str_copy(Input.m_aTab, "ingame_esc", sizeof(Input.m_aTab));
	str_copy(Input.m_aContext, SettingsPerfContextName(), sizeof(Input.m_aContext));
	Input.m_FrameMsAverage = (float)GameClient()->m_QmMonitoring.Snapshot().m_Performance.m_FrameTimeMs;
	Input.m_FrameMsP95 = Input.m_FrameMsAverage;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_BackgroundBacklog = maximum(Budget, 0) + CountMissingSettingsMenuTextPlanItems();
	Input.m_WindowActive = true;
	const SSettingsAdaptiveBudgetOutput AdaptiveBudget = BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::SettingsText, "stable_text_ingame_esc", Input);
	PrebuildSettingsMenuTextPool(minimum(Budget, maximum(1, AdaptiveBudget.m_TextPrebuildTokens)), "target_settings", "ingame_esc_open");
}

int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)
{
	m_SettingsMenuTextLastPrebuildStats = {};
	m_SettingsMenuTextLastPrebuildStats.m_Budget = maximum(Budget, 0);
	if(Budget <= 0 || Ui()->Screen() == nullptr)
		return maximum(Budget, 0);

	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameId = GameClient()->PerfFrameOrOne();
	str_copy(Input.m_aOperation, pOperationOverride != nullptr ? pOperationOverride : SettingsPerfActiveOperation(), sizeof(Input.m_aOperation));
	str_copy(Input.m_aPage, "settings", sizeof(Input.m_aPage));
	str_copy(Input.m_aTab, "stable_text", sizeof(Input.m_aTab));
	str_copy(Input.m_aContext, GameClient()->ClientStateOnline() ? "online" : "offline", sizeof(Input.m_aContext));
	Input.m_FrameMsAverage = (float)GameClient()->m_QmMonitoring.Snapshot().m_Performance.m_FrameTimeMs;
	Input.m_FrameMsP95 = Input.m_FrameMsAverage;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_ScrollActive = m_SettingsScrollActive;
	Input.m_PostScrollRecoveryFrames = m_SettingsPostScrollRecoveryFrames;
	Input.m_BackgroundBacklog = maximum(Budget, 0) + CountMissingSettingsMenuTextPlanItems() + SettingsTextPlanCollectionRemaining();
	Input.m_WindowActive = true;
	const SSettingsAdaptiveBudgetOutput AdaptiveBudget = BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::SettingsText, "stable_text_prebuild", Input);

	const CUiRenderOnlyScope RenderOnly(Ui());
	const int PlanCollectionBudget = maximum(1, minimum(Budget, AdaptiveBudget.m_TextPrebuildTokens));
	AdvanceSettingsMenuTextPlanCollection(PlanCollectionBudget, pOperationOverride);
	int RemainingBudget = minimum(Budget, maximum(1, AdaptiveBudget.m_TextPrebuildTokens));
	if(m_SettingsMenuTextPlanGeneration != m_MenuTextPoolGeneration)
	{
		m_SettingsMenuTextPlanGeneration = m_MenuTextPoolGeneration;
		m_SettingsMenuTextPlanCursor = 0;
	}

	while(m_SettingsMenuTextPlanCursor < m_vSettingsMenuTextPrebuildPlan.size())
	{
		const SMenuTextPlanItem &Item = m_vSettingsMenuTextPrebuildPlan[m_SettingsMenuTextPlanCursor];
		if(!SettingsMenuTextPlanItemBuildable(Item))
		{
			++m_SettingsMenuTextPlanCursor;
			continue;
		}
		const SMenuTextStyleKey StyleKey = SettingsMenuTextPlanStyleKey(Item);
		const std::string Key = MenuTextCacheKey(Item.m_Scope, Item.m_Page, Item.m_Tab, Item.m_Subtab, Item.m_TextId.c_str(), StyleKey);
		const auto It = m_MenuTextPool.find(Key);
		const bool AlreadyReady = It != m_MenuTextPool.end() && It->second.m_Built && It->second.m_Generation == m_MenuTextPoolGeneration;
		if(AlreadyReady)
		{
			++m_SettingsMenuTextLastPrebuildStats.m_Reused;
			++m_SettingsMenuTextPlanCursor;
			continue;
		}
		if(RemainingBudget <= 0)
			break;
		const int BeforeBudget = RemainingBudget;
		const bool Built = PrebuildSettingsTextPlanItem(Item, RemainingBudget);
		if(!Built)
		{
			++m_SettingsMenuTextPlanCursor;
			continue;
		}
		(void)BeforeBudget;
		++m_SettingsMenuTextLastPrebuildStats.m_Built;
		++m_SettingsMenuTextPlanCursor;
	}
	m_SettingsMenuTextLastPrebuildStats.m_Remaining = CountMissingSettingsMenuTextPlanItems();
	return RemainingBudget;
}

void CMenus::LogSettingsAdaptiveBudget(const char *pSource, const SSettingsAdaptiveBudgetInput &Input, const SSettingsAdaptiveBudgetOutput &Output) const
{
	if(!PerfDebugEnabled())
		return;
	char aPayload[768];
	str_format(aPayload, sizeof(aPayload),
		"event=settings_adaptive_budget source=%s frame=%" PRIu64 " operation=%s page=%s tab=%s subtab=%d context=%s mode=%s reason=%s frame_ms_avg=%.3f frame_ms_p95=%.3f target_ms=%.3f visible_tokens=%d prefetch_tokens=%d background_tokens=%d gpu_upload_tokens=%d resource_upload_tokens=%d text_tokens=%d text_container_tokens=%d glyph_rasterize_tokens=%d glyph_upload_tokens=%d paragraph_layout_tokens=%d metadata_layout_tokens=%d preview_artifact_tokens=%d texture_upload_tokens=%d card_draw_tokens=%d demo_tokens=%d backlog=%d scroll=%d jump_scroll=%d tab_switch_first_frame=%d frame_pressure=%d text_create_ewma_ms=%.3f text_upload_ewma_ms=%.3f glyph_raster_ewma_ms=%.3f glyph_upload_ewma_ms=%.3f text_scroll_cap=%d text_idle_cap=%d",
		pSource != nullptr ? pSource : "unknown",
		Input.m_FrameId,
		Input.m_aOperation,
		Input.m_aPage,
		Input.m_aTab,
		Input.m_Subtab,
		Input.m_aContext,
		SettingsAdaptiveBudgetModeName(Output.m_Mode),
		SettingsAdaptiveBudgetReasonName(Output.m_Reason),
		Input.m_FrameMsAverage,
		Input.m_FrameMsP95,
		Input.m_TargetFrameMs,
		Output.m_VisibleTokens,
		Output.m_PrefetchTokens,
		Output.m_BackgroundTokens,
		Output.m_GpuUploadTokens,
		Output.m_ResourceUploadTokens,
		Output.m_TextPrebuildTokens,
		Output.m_TextContainerTokens,
		Output.m_GlyphRasterizeTokens,
		Output.m_GlyphUploadTokens,
		Output.m_ParagraphLayoutTokens,
		Output.m_MetadataLayoutTokens,
		Output.m_PreviewArtifactTokens,
		Output.m_TextureUploadTokens,
		Output.m_CardDrawTokens,
		Output.m_DemoMetadataTokens,
		Input.m_BackgroundBacklog + Input.m_VisibleWaiting,
		Input.m_ScrollActive ? 1 : 0,
		Input.m_JumpScrollActive ? 1 : 0,
		Input.m_TabSwitchFirstFrame ? 1 : 0,
		Input.m_FramePressure ? 1 : 0,
		Input.m_TextContainerCreateMsEwma,
		Input.m_TextContainerUploadMsEwma,
		Input.m_GlyphRasterizeMsEwma,
		Input.m_GlyphUploadMsEwma,
		Input.m_TextScrollHardCap,
		Input.m_TextIdleHardCap);
	QmPerfLogPayload("perf/settings-resource", aPayload, Client(), CurrentQmUiPerfPage() != nullptr ? CurrentQmUiPerfPage() : "settings");
}

void CMenus::PrepareSettingsAdaptiveBudgetInput(SSettingsAdaptiveBudgetInput &Input)
{
	if(Input.m_FrameId == 0)
		Input.m_FrameId = Client()->PerfFrame();
	if(Input.m_aOperation[0] == '\0')
		str_copy(Input.m_aOperation, SettingsPerfActiveOperation(), sizeof(Input.m_aOperation));
	if(Input.m_aPage[0] == '\0')
		str_copy(Input.m_aPage, CurrentQmUiPerfPage() != nullptr ? CurrentQmUiPerfPage() : "settings", sizeof(Input.m_aPage));
	if(Input.m_aContext[0] == '\0')
		str_copy(Input.m_aContext, SettingsPerfContextName(), sizeof(Input.m_aContext));
	const SQmTextRuntimeBudgetSnapshot TextSnapshot = TextRender()->QmTextRuntimeBudgetSnapshot();
	m_TextContainerCreateMsEwma = QmPerfEwma(m_TextContainerCreateMsEwma, (float)TextSnapshot.m_TextContainerCreateMs, 0.20f);
	m_TextContainerUploadMsEwma = QmPerfEwma(m_TextContainerUploadMsEwma, (float)TextSnapshot.m_TextContainerUploadMs, 0.20f);
	m_GlyphRasterizeMsEwma = QmPerfEwma(m_GlyphRasterizeMsEwma, (float)TextSnapshot.m_GlyphRasterizeMs, 0.20f);
	m_GlyphUploadMsEwma = QmPerfEwma(m_GlyphUploadMsEwma, (float)TextSnapshot.m_GlyphUploadMs, 0.20f);
	Input.m_TextContainerCreateMsEwma = m_TextContainerCreateMsEwma;
	Input.m_TextContainerUploadMsEwma = m_TextContainerUploadMsEwma;
	Input.m_GlyphRasterizeMsEwma = m_GlyphRasterizeMsEwma;
	Input.m_GlyphUploadMsEwma = m_GlyphUploadMsEwma;
	Input.m_TextScrollHardCap = 2;
	Input.m_TextPressureHardCap = 2;
	Input.m_TextIdleHardCap = 64;
	Input.m_GlyphScrollHardCap = 1;
	Input.m_GlyphPressureHardCap = 1;
	Input.m_GlyphIdleHardCap = 8;
}

SSettingsAdaptiveBudgetOutput CMenus::BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer Consumer, const char *pSource, SSettingsAdaptiveBudgetInput Input)
{
	PrepareSettingsAdaptiveBudgetInput(Input);
	m_CurrentSettingsUiFrameBudget = GameClient()->FrameScheduler()->ComputeBudget(Consumer, Input);
	LogSettingsAdaptiveBudget(pSource, Input, m_CurrentSettingsUiFrameBudget);
	return m_CurrentSettingsUiFrameBudget;
}

void CMenus::LogSettingsUiBudget(const char *pPage, const SSettingsUiBudgetFrame &Frame) const
{
	if(!PerfDebugEnabled())
		return;
	char aPayload[384];
	// Telemetry contract: event=settings_ui_budget.
	str_format(aPayload, sizeof(aPayload),
		"event=settings_ui_budget page=%s operation=%s frame=%" PRIu64 " tab=%d subtab=%d layout_ms=%.3f text_ms=%.3f text_new=%d text_reused=%d draw_calls=%d vertices=%d indices=%d heap_allocs=%d visible_widgets=%d",
		pPage != nullptr ? pPage : "settings",
		SettingsPerfActiveOperation(),
		(uint64_t)Client()->PerfFrame(),
		Frame.m_Tab,
		Frame.m_Subtab,
		Frame.m_LayoutMs,
		Frame.m_TextMs,
		Frame.m_TextNew,
		Frame.m_TextReused,
		Frame.m_DrawCalls,
		Frame.m_Vertices,
		Frame.m_Indices,
		Frame.m_HeapAllocs,
		Frame.m_VisibleWidgets);
	QmPerfLogPayload("perf/ui_budget", aPayload, Client(), pPage != nullptr ? pPage : "settings");
}

void CMenus::InvalidateSettingsTextPool()
{
	InvalidateMenuTextPool("style");
}

void CMenus::InvalidateMenuTextPool(const char *pReason)
{
	m_MenuTextPoolLastStaleReason = pReason != nullptr ? pReason : "style";
	for(auto &[Key, Entry] : m_MenuTextPool)
	{
		(void)Key;
		Entry.m_Generation = 0;
	}
	m_MenuTextPoolLanguageHash = 0;
	m_MenuTextPoolFontHash = 0;
	m_MenuTextPoolLayoutHash = 0;
	m_MenuTextPoolThemeHash = 0;
	m_SettingsMenuTextPlanMetadataDirty = true;
	if(!m_MenuTextPoolVisibleGuard)
	{
		m_vMenuTextContainerBuildRequests.clear();
		for(auto &[Key, Entry] : m_MenuTextPool)
		{
			(void)Key;
			Ui()->ResetUIElement(Entry.m_Element);
		}
		m_MenuTextPool.clear();
		m_MenuTextLastTrimFrame = ~uint64_t{0};
		m_vSettingsMenuTextPrebuildPlan.clear();
		m_vSettingsMenuTextPlanCollectionUnits.clear();
		m_SettingsMenuTextPlannedDescriptors.clear();
		m_SettingsMenuTextPlannedKeys.clear();
	}
	m_SettingsMenuTextPlanCursor = 0;
	m_SettingsMenuTextPlanCollectionCursor = 0;
	m_SettingsMenuTextPlanGeneration = 0;
	m_SettingsMenuTextPlanCollectionGeneration = 0;
	m_SettingsMenuTextPlanCollectionOperation.clear();
	m_SettingsMenuTextPlanCollectionDirty = true;
	m_SettingsMenuTextPlanCollectionComplete = false;
	m_SettingsMenuTextLastPrebuildStats = {};
	m_SettingsMenuTextLastCollectionStats = {};
	m_SettingsMenuTextLastCollectionStats.m_Dirty = true;
	++m_MenuTextPoolGeneration;
	if(PerfDebugEnabled())
	{
		char aPayload[160];
		str_format(aPayload, sizeof(aPayload), "event=settings_text_stale reason=%s", pReason != nullptr ? pReason : "style");
		QmPerfLogPayload("perf/settings-text", aPayload, Client(), "settings");
	}
}

void CMenus::InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason Reason)
{
	const bool ClearsText = SettingsInvalidationClearsTextPool(Reason);
	const bool ClearsResource = SettingsInvalidationClearsResourcePlan(Reason);
	const bool ClearsSection =
		Reason == ESettingsInvalidationReason::LANGUAGE_CHANGED ||
		Reason == ESettingsInvalidationReason::FONT_CHANGED ||
		Reason == ESettingsInvalidationReason::BACKEND_CHANGED ||
		Reason == ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED ||
		Reason == ESettingsInvalidationReason::DPI_CHANGED ||
		Reason == ESettingsInvalidationReason::UI_SCALE_CHANGED ||
		Reason == ESettingsInvalidationReason::CONFIG_HASH_CHANGED ||
		Reason == ESettingsInvalidationReason::SECTION_SIZE_CHANGED;
	LogSettingsInvalidatePerf(Reason, ClearsText, ClearsSection, false, ClearsResource);

	if(ClearsText)
		InvalidateMenuTextPool(MenuTextInvalidationReasonName(Reason));

	if(ClearsSection)
	{
		InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::CONFIG);
	}

	if(ClearsResource)
	{
		InvalidateSettingsAssetResourcePlan();
		ClearSettingsAssetsCardMetadataCache();
	}

	if(Reason == ESettingsInvalidationReason::LANGUAGE_CHANGED ||
		Reason == ESettingsInvalidationReason::FONT_CHANGED ||
		Reason == ESettingsInvalidationReason::BACKEND_CHANGED)
		ClearSettingsLanguageRowCache();
	if(Reason == ESettingsInvalidationReason::BACKEND_CHANGED)
		ClearSettingsTeePreviewCache();
}

bool CMenus::PrewarmSettingsPageResources(int Page, int Tab, const CUIRect &ContentView)
{
	Page = SettingsCanonicalPage(Page);
	if(Page == SETTINGS_GENERAL)
	{
		std::vector<int> vCountryCodes;
		const int NumLanguages = (int)g_Localization.Languages().size();
		vCountryCodes.reserve(NumLanguages);
		for(int i = 0; i < NumLanguages; ++i)
		{
			const auto &Language = g_Localization.Languages()[i];
			if(str_comp(Language.m_Filename.c_str(), g_Config.m_ClLanguagefile) == 0)
			{
				vCountryCodes.push_back(Language.m_CountryCode);
				break;
			}
		}
		for(int i = 0; i < NumLanguages; ++i)
			vCountryCodes.push_back(g_Localization.Languages()[i].m_CountryCode);
		return GameClient()->m_CountryFlags.PrewarmByCountryCodesReady(BuildSettingsCountryFlagWarmupPlan(vCountryCodes));
	}
	else if(Page == SETTINGS_TEE)
	{
		const int TeeWarmupEntries = SettingsTeeSkinListFirstPageWarmupEntries(ContentView.h);
		std::vector<int> vIndices;
		vIndices.reserve(GameClient()->m_CountryFlags.Num());
		for(int i = 0; i < (int)GameClient()->m_CountryFlags.Num(); ++i)
			vIndices.push_back(i);
		const bool FlagsReady = GameClient()->m_CountryFlags.PrewarmByIndicesReady(vIndices);
		const bool TeeReady = GameClient()->m_Skins.PrewarmPlayerPreviewReady(m_Dummy ? 1 : 0, TeeWarmupEntries, true);
		return FlagsReady && TeeReady;
	}
	else if(Page == SETTINGS_ASSETS)
	{
		return PrewarmSettingsAssetResources();
	}
	return true;
}

void CMenus::PrewarmVisibleSettingsResources(CUIRect MainView)
{
	CUIRect ContentView;
	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	if(g_Config.m_QmNewUi != 0)
	{
		const SSettingsShellLayoutFrame Shell = ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f);
		ContentView = Shell.m_ContentRect;
	}
	else
	{
		ContentView = MainView;
		const float TabBarWidth = std::clamp(ContentView.w * 0.14f, 108.0f, 120.0f);
		ContentView.VSplitRight(TabBarWidth, &ContentView, nullptr);
		ContentView.Margin(std::clamp(ContentView.w * 0.02f, 12.0f, 20.0f), &ContentView);
	}
	if(g_Config.m_QmNewUi == 0 && NeedRestart)
	{
		ContentView.HSplitBottom(20.0f, &ContentView, nullptr);
		ContentView.HSplitBottom(10.0f, &ContentView, nullptr);
	}

	const int Page = SettingsCanonicalPage(g_Config.m_UiSettingsPage);
	int Tab = -1;
	if(Page == SETTINGS_TCLIENT)
		Tab = m_TClientSettingsTab;
	else if(Page == SETTINGS_QMCLIENT)
		Tab = m_QmClientSettingsTab;
	else if(Page == SETTINGS_ASSETS)
		Tab = CurrentSettingsAssetsTab();

	(void)PrewarmSettingsPageResources(Page, Tab, ContentView);
	// Immediate-mode settings pages must not be rendered from the idle prewarm
	// pass, otherwise they paint a second copy before the real settings shell.

	// 接力推进文本池 plan collection + prebuild。loading 阶段只调一次且 budget 受限
	// (实测 plan units_done=1/21, prebuild built=1, complete=0)；idle prewarm 必须持续推进
	// 直到 complete + remaining=0，否则切 tab/进设置首帧要现场创建文本容器 (text_new 爆发，
	// 实测 settings_page_content 单次 355ms)。上限交给 adaptive budget 按帧压力收紧实际 token：
	// idle 帧 (设置页空闲) 多推进，交互/滚动帧少推进或不推进。
	if(CountMissingSettingsMenuTextPlanItems() > 0)
	{
		PrebuildSettingsTextPoolForLoading(64, "settings_idle");
	}
}

bool CMenus::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_MenuActive)
		return false;

	MarkMenuInteraction();
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CMenus::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive() && GameClient()->m_HudEditor.IsActive())
		return false;

	// Escape key is always handled to activate/deactivate menu
	if((Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE) || IsActive())
	{
		MarkMenuInteraction();
		Ui()->OnInput(Event);
		return true;
	}
	return false;
}

void CMenus::OnStateChange(int NewState, int OldState)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	if(OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_OFFLINE)
	{
		TextRender()->DeleteTextContainer(m_MotdTextContainerIndex);
		TextRender()->DeleteTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex);
		m_IngameMotdParagraphCache.m_Valid = false;
		m_IngameMotdParagraphCache.m_Pending = false;
		for(auto &[Key, Entry] : m_SnapshotTextCache)
		{
			(void)Key;
			if(Entry.m_Element.IsRegistered())
				TextRender()->DeleteTextContainer(Entry.m_Element.Rect(0)->m_UITextContainer);
		}
		m_SnapshotTextCache.clear();
		m_SnapshotTextLastReadyByScope.clear();
		m_SnapshotTextPending.clear();
	}

	if(NewState == IClient::STATE_OFFLINE)
	{
		ResetReportScan();
		if(OldState >= IClient::STATE_ONLINE && NewState < IClient::STATE_QUITTING)
			UpdateMusicState();
		m_Popup = POPUP_NONE;
		if(Client()->ErrorString() && Client()->ErrorString()[0] != 0)
		{
			if(str_find(Client()->ErrorString(), "password"))
			{
				m_Popup = POPUP_PASSWORD;
				m_PasswordInput.SelectAll();
				Ui()->SetActiveItem(&m_PasswordInput);
			}
			else
			{
				m_Popup = POPUP_DISCONNECTED;
			}
		}
	}
	else if(NewState == IClient::STATE_LOADING)
	{
		m_DownloadLastCheckTime = time_get();
		m_DownloadLastCheckSize = 0;
		m_DownloadSpeed = 0.0f;
	}
	else if(NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
	{
		if(m_Popup != POPUP_WARNING)
		{
			m_Popup = POPUP_NONE;
			SetActive(false);
		}
	}
}

void CMenus::OnWindowResize()
{
	TextRender()->DeleteTextContainer(m_MotdTextContainerIndex);
	TextRender()->DeleteTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex);
	m_IngameMotdParagraphCache.m_Valid = false;
	m_IngameMotdParagraphCache.m_Pending = false;
	for(auto &[Key, Entry] : m_SnapshotTextCache)
	{
		(void)Key;
		if(Entry.m_Element.IsRegistered())
			TextRender()->DeleteTextContainer(Entry.m_Element.Rect(0)->m_UITextContainer);
	}
	m_SnapshotTextCache.clear();
	m_SnapshotTextLastReadyByScope.clear();
	m_SnapshotTextPending.clear();
	InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED);
}

void CMenus::OnRender()
{
	CPerfTimer FrameTimer;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		SetActive(true);

	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_ServerMode == CGameClient::SERVERMODE_PUREMOD)
	{
		Client()->Disconnect();
		SetActive(true);
		PopupMessage(Localize("Disconnected"), Localize("The server is running a non-standard tuning on a pure game type."), Localize("Ok"));
	}

	if(!IsActive())
	{
		m_SettingsCardDeckDisplayState.LeaveSettings();
		if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			if(Client()->State() == IClient::STATE_ONLINE)
			{
				StartSettingsPerfFixedWindow("ingame_esc_open", "online", GamePageName(m_GamePage), "none", 30);
				m_IngameEscOpenFrame = Client()->PerfFrame();
				m_IngameServerInfoBackgroundPrepareRequested = true;
			}
			SetActive(true);
		}
		else if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
		{
			Ui()->ClearHotkeys();
			return;
		}
	}

	Ui()->StartCheck();
	UpdateColors();

	{
		CPerfTimer StageTimer;
		Ui()->Update();
		LogPerfStage(Client(), "ui_update", StageTimer.ElapsedMs());
	}

	if(IsActive())
		Ui()->DoBackButton();

	{
		CPerfTimer StageTimer;
		Render();
		LogPerfStage(Client(), "render_body", StageTimer.ElapsedMs());
	}

	{
		CPerfTimer StageTimer;
		DrainMenuTextContainerBuildRequests();
		LogPerfStage(Client(), "menu_text_runtime_drain", StageTimer.ElapsedMs());
	}

	{
		CPerfTimer StageTimer;
		if(IsActive() && Client()->State() == IClient::STATE_ONLINE)
		{
			if(m_GamePage == PAGE_SERVER_INFO)
				DrainIngameUiSnapshotTextRuntime();
			else if(m_IngameServerInfoBackgroundPrepareRequested && Client()->PerfFrame() > m_IngameEscOpenFrame)
			{
				PrepareIngameServerInfoTextRuntime();
				if(!m_SnapshotTextPending.empty())
					DrainIngameUiSnapshotTextRuntime();
				else
					DrainIngameUiTextRuntime(false);
				m_IngameServerInfoBackgroundPrepareRequested = !m_SnapshotTextPending.empty() || m_IngameMotdParagraphCache.m_Pending;
			}
			else
				DrainIngameUiSnapshotTextRuntime();
		}
		else
		{
			DrainIngameUiSnapshotTextRuntime();
		}
		LogPerfStage(Client(), "ingame_text_runtime_drain", StageTimer.ElapsedMs());
	}

	if(IsActive())
	{
		CPerfTimer StageTimer;
		Ui()->RenderBackButton();
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
		LogPerfStage(Client(), "cursor_render", StageTimer.ElapsedMs());
	}

	// render debug information
	if(g_Config.m_Debug)
		Ui()->DebugRender(2.0f, Ui()->Screen()->h - 12.0f);

	if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		SetActive(false);

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();

	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "state=%s active=%d", ClientStateName(Client()->State()), IsActive() ? 1 : 0);
	LogPerfStage(Client(), "menus_onrender_total", FrameTimer.ElapsedMs(), false, aExtra);
	{
		CPerfTimer StageTimer;
		TextRender()->FlushQmTextRuntimeBudgetLog();
		LogPerfStage(Client(), "telemetry_flush", StageTimer.ElapsedMs());
	}
}

void CMenus::UpdateColors()
{
	ms_GuiColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));

	ms_ColorTabbarInactiveOutgame = MenuUiColorSurface(0.45f, 0.16f);
	ms_ColorTabbarActiveOutgame = MenuUiColorSurface(0.70f, 0.16f);
	ms_ColorTabbarHoverOutgame = MenuUiColorSurface(0.62f, 0.20f);

	ms_ColorTabbarInactiveIngame = MenuUiColorSurface(0.45f, 0.16f);
	ms_ColorTabbarActiveIngame = MenuUiColorSurface(0.70f, 0.16f);
	ms_ColorTabbarHoverIngame = MenuUiColorSurface(0.62f, 0.20f);
}

void CMenus::RenderBackground()
{
	const float ScreenHeight = 300.0f;
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);

	// render background color
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(ms_GuiColor.WithAlpha(1.0f));
	const IGraphics::CQuadItem BackgroundQuadItem = IGraphics::CQuadItem(0, 0, ScreenWidth, ScreenHeight);
	Graphics()->QuadsDrawTL(&BackgroundQuadItem, 1);
	Graphics()->QuadsEnd();

	// 空菜单地图即显式的 "(none)" 主题。DDNet 默认棋盘格为黑色，
	// 在黑色 UI 颜色上不可见；保留同一程序化图案但改用对比色。
	const bool NoMenuTheme = g_Config.m_ClMenuMap[0] == '\0';
	const float GuiLuminance = (ms_GuiColor.r + ms_GuiColor.g + ms_GuiColor.b) / 3.0f;
	const ColorRGBA CheckerColor = NoMenuTheme ? (GuiLuminance < 0.5f ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.10f)) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.045f);

	// render the tiles
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(CheckerColor);
	const float Size = 15.0f;
	const float OffsetTime = std::fmod(GameClient()->GlobalTimeOrZero() * 0.15f, 2.0f);
	IGraphics::CQuadItem aCheckerItems[64];
	size_t NumCheckerItems = 0;
	const int NumItemsWidth = std::ceil(ScreenWidth / Size);
	const int NumItemsHeight = std::ceil(ScreenHeight / Size);
	for(int y = -2; y < NumItemsHeight; y++)
	{
		for(int x = 0; x < NumItemsWidth + 4; x += 2)
		{
			aCheckerItems[NumCheckerItems] = IGraphics::CQuadItem((x - 2 * OffsetTime + (y & 1)) * Size, (y + OffsetTime) * Size, Size, Size);
			NumCheckerItems++;
			if(NumCheckerItems == std::size(aCheckerItems))
			{
				Graphics()->QuadsDrawTL(aCheckerItems, NumCheckerItems);
				NumCheckerItems = 0;
			}
		}
	}
	if(NumCheckerItems != 0)
		Graphics()->QuadsDrawTL(aCheckerItems, NumCheckerItems);
	Graphics()->QuadsEnd();

	// render border fade
	Graphics()->TextureSet(m_TextureBlob);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem BlobQuadItem = IGraphics::CQuadItem(-100, -100, ScreenWidth + 200, ScreenHeight + 200);
	Graphics()->QuadsDrawTL(&BlobQuadItem, 1);
	Graphics()->QuadsEnd();

	// restore screen
	Ui()->MapScreen();
}

int CMenus::DoButton_CheckBox_Tristate(const void *pId, const char *pText, TRISTATE Checked, const CUIRect *pRect)
{
	switch(Checked)
	{
	case TRISTATE::NONE:
		return DoButton_CheckBox_Common(pId, pText, "", pRect, BUTTONFLAG_LEFT);
	case TRISTATE::SOME:
		return DoButton_CheckBox_Common(pId, pText, "O", pRect, BUTTONFLAG_LEFT);
	case TRISTATE::ALL:
		return DoButton_CheckBox_Common(pId, pText, "X", pRect, BUTTONFLAG_LEFT);
	default:
		dbg_assert_failed("Invalid tristate. Checked: %d", static_cast<int>(Checked));
	}
}

int CMenus::MenuImageScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	const char *pExtension = ".png";
	CMenuImage MenuImage;
	CMenus *pSelf = static_cast<CMenus *>(pUser);
	if(IsDir || !str_endswith(pName, pExtension) || str_length(pName) - str_length(pExtension) >= (int)sizeof(MenuImage.m_aName))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "menuimages/%s", pName);

	CImageInfo Info;
	if(!pSelf->Graphics()->LoadPng(Info, aPath, DirType))
	{
		char aError[IO_MAX_PATH_LENGTH + 64];
		str_format(aError, sizeof(aError), "Failed to load menu image from '%s'", aPath);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "menus", aError);
		return 0;
	}
	if(Info.m_Format != CImageInfo::FORMAT_RGBA)
	{
		Info.Free();
		char aError[IO_MAX_PATH_LENGTH + 64];
		str_format(aError, sizeof(aError), "Failed to load menu image from '%s': must be an RGBA image", aPath);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "menus", aError);
		return 0;
	}

	MenuImage.m_OrgTexture = pSelf->Graphics()->LoadTextureRaw(Info, 0, aPath);

	ConvertToGrayscale(Info);
	MenuImage.m_GreyTexture = pSelf->Graphics()->LoadTextureRawMove(Info, 0, aPath);

	str_truncate(MenuImage.m_aName, sizeof(MenuImage.m_aName), pName, str_length(pName) - str_length(pExtension));
	pSelf->m_vMenuImages.push_back(MenuImage);

	pSelf->RenderLoading(Localize("Loading DDNet Client"), Localize("Loading menu images"), 0);

	return 0;
}

const CMenus::CMenuImage *CMenus::FindMenuImage(const char *pName)
{
	for(auto &Image : m_vMenuImages)
		if(str_comp(Image.m_aName, pName) == 0)
			return &Image;
	return nullptr;
}

void CMenus::SetMenuPage(int NewPage)
{
	const int OldPage = m_MenuPage;
	if(OldPage != NewPage)
		MarkMenuInteraction();
	if(OldPage == PAGE_SETTINGS && NewPage != PAGE_SETTINGS)
	{
		ClearQmClientSettingsSearchInputs();
	}
	if(PerfDebugEnabled() && OldPage != NewPage)
	{
		char aPayload[160];
		str_format(aPayload, sizeof(aPayload), "event=page_switch from=%s to=%s dur_ms=%.3f source=menu_page_switch", MenuPageName(OldPage), MenuPageName(NewPage), 0.0);
		QmPerfLogPayload("perf/interaction", aPayload, Client());
	}
	m_MenuPage = NewPage;
	auto IsBrowserPage = [](int Page) {
		return (Page >= PAGE_INTERNET && Page <= PAGE_FAVORITE_COMMUNITY_5) || Page == PAGE_FAVORITE_MAPS;
	};
	auto BrowserPageVisualOrder = [](int Page) {
		if(Page == PAGE_INTERNET)
			return 0;
		if(Page == PAGE_LAN)
			return 1;
		if(Page == PAGE_FAVORITES)
			return 2;
		if(Page == PAGE_FAVORITE_MAPS)
			return 3;
		if(Page >= PAGE_FAVORITE_COMMUNITY_1 && Page <= PAGE_FAVORITE_COMMUNITY_5)
			return 4 + Page - PAGE_FAVORITE_COMMUNITY_1;
		return Page;
	};
	const bool OldIsBrowser = IsBrowserPage(OldPage);
	const bool NewIsBrowser = IsBrowserPage(NewPage);
	if(OldIsBrowser && NewIsBrowser && OldPage != NewPage)
	{
		m_BrowserTabTransitionDirection = BrowserPageVisualOrder(NewPage) > BrowserPageVisualOrder(OldPage) ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(UiAnimNodeKey("browser_page_switch"), MENU_SWITCH_DURATION);
	}
	else
	{
		m_BrowserTabTransitionDirection = 0.0f;
	}
	if(OldPage != NewPage && !(OldIsBrowser && NewIsBrowser))
	{
		m_MenuPageTransitionDirection = NewPage > OldPage ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(UiAnimNodeKey("menu_page_switch"), MENU_SWITCH_DURATION);
	}
	else
	{
		m_MenuPageTransitionDirection = 0.0f;
	}
	if(IsBrowserPage(NewPage))
	{
		g_Config.m_UiPage = NewPage;
		bool ForceRefresh = false;
		if(m_ForceRefreshLanPage && NewPage == PAGE_LAN)
		{
			ForceRefresh = true;
			m_ForceRefreshLanPage = false;
		}
		if(OldPage != NewPage || ForceRefresh)
		{
			RefreshBrowserTab(ForceRefresh);
		}
	}
	if(OldPage != NewPage && NewPage == PAGE_SETTINGS)
	{
		m_SettingsPerfLastPage = -1;
		m_SettingsPerfLastTClientTab = -1;
		m_SettingsPerfLastQmClientTab = -1;
		const std::string PageName = SettingsPageCacheKey(g_Config.m_UiSettingsPage, -1);
		StartSettingsPerfFixedWindow("settings_open", "offline", PageName.c_str(), "none", 30);
	}
	else if(OldPage == PAGE_SETTINGS && NewPage != PAGE_SETTINGS)
	{
		m_SettingsPerfLastPage = -1;
		m_SettingsPerfLastTClientTab = -1;
		m_SettingsPerfLastQmClientTab = -1;
	}
}

void CMenus::SetGamePage(int NewPage)
{
	// "Unfinished maps" is no longer exposed in navigation.
	if(NewPage == PAGE_UNFINISHED_MAPS)
		NewPage = PAGE_GAME;

	const int OldPage = m_GamePage;
	if(OldPage != NewPage)
		MarkMenuInteraction();
	if(PerfDebugEnabled() && OldPage != NewPage)
	{
		char aPayload[160];
		str_format(aPayload, sizeof(aPayload), "event=page_switch from=%s to=%s dur_ms=%.3f source=game_page_switch", GamePageName(OldPage), GamePageName(NewPage), 0.0);
		QmPerfLogPayload("perf/interaction", aPayload, Client());
	}
	m_GamePage = NewPage;
	if(OldPage != NewPage)
	{
		m_GamePageTransitionDirection = NewPage > OldPage ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(UiAnimNodeKey("game_page_switch"), MENU_SWITCH_DURATION);
	}
	else
	{
		m_GamePageTransitionDirection = 0.0f;
	}
	if(OldPage != NewPage && NewPage == PAGE_SETTINGS)
	{
		m_SettingsPerfLastPage = -1;
		m_SettingsPerfLastTClientTab = -1;
		m_SettingsPerfLastQmClientTab = -1;
		const std::string PageName = SettingsPageCacheKey(g_Config.m_UiSettingsPage, -1);
		StartSettingsPerfFixedWindow("settings_open", "online", PageName.c_str(), "none", 30);
	}
	else if(OldPage != NewPage && NewPage == PAGE_SERVER_INFO)
	{
		m_IngameServerInfoBackgroundPrepareRequested = false;
		StartSettingsPerfFixedWindow("ingame_server_info", "online", "game", "server_info", 30);
	}
	else if(OldPage == PAGE_SETTINGS && NewPage != PAGE_SETTINGS)
	{
		m_SettingsPerfLastPage = -1;
		m_SettingsPerfLastTClientTab = -1;
		m_SettingsPerfLastQmClientTab = -1;
	}
}

void CMenus::RefreshBrowserTab(bool Force)
{
	CPerfTimer Timer;
	if(g_Config.m_UiPage == PAGE_INTERNET)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_INTERNET)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(IServerBrowser::TYPE_INTERNET);
			UpdateCommunityCache(true);
		}
	}
	else if(g_Config.m_UiPage == PAGE_LAN)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_LAN)
		{
			ServerBrowser()->Refresh(IServerBrowser::TYPE_LAN);
			UpdateCommunityCache(true);
		}
	}
	else if(g_Config.m_UiPage == PAGE_FAVORITES)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_FAVORITES)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITES);
			UpdateCommunityCache(true);
		}
	}
	else if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5)
	{
		const int BrowserType = g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1 + IServerBrowser::TYPE_FAVORITE_COMMUNITY_1;
		if(Force || ServerBrowser()->GetCurrentType() != BrowserType)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(BrowserType);
			UpdateCommunityCache(true);
		}
	}

	char aExtra[128];
	str_format(aExtra, sizeof(aExtra), "page=%s force=%d current_type=%d", MenuPageName(g_Config.m_UiPage), Force ? 1 : 0, ServerBrowser()->GetCurrentType());
	LogPerfStage(Client(), "refresh_browser_tab", Timer.ElapsedMs(), Force, aExtra);
}

void CMenus::ForceRefreshLanPage()
{
	m_ForceRefreshLanPage = true;
}

void CMenus::SetShowStart(bool ShowStart)
{
	if(m_ShowStart != ShowStart)
		MarkMenuInteraction();
	m_ShowStart = ShowStart;
}

void CMenus::ShowQuitPopup()
{
	m_Popup = POPUP_QUIT;
}

void CMenus::JoinTutorial()
{
	m_JoinTutorial.m_Queued = true;
	m_JoinTutorial.m_Status = CJoinTutorial::EStatus::REFRESHING;
	m_JoinTutorial.m_TryRefresh = false;
	m_JoinTutorial.m_TriedRefresh = false;
	m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::NOT_TRIED;
	m_JoinTutorial.m_StateChange = time_get_nanoseconds();
}
