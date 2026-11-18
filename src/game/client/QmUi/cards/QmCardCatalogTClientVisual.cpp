#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/SettingsPersonalization.h>
#include <game/client/QmUi/SettingsRowReveal.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL3/SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

float CMenus::LayoutTClientVisualNameplateCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("ping-circle", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplatePingCircle, "tclient-nameplate-ping-circle", Localize("Show ping colored circle in nameplates"), &g_Config.m_QmNameplatePingCircle, &Row, LineSize);
	}
	if(Flow.Control("country", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCountry, "tclient-nameplate-country", Localize("Show country flags in nameplates"), &g_Config.m_QmNameplateCountry, &Row, LineSize);
	}
	if(Flow.Control("skins", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateSkins, "tclient-nameplate-skins", Localize("Show skin names in nameplate"), &g_Config.m_QmNameplateSkins, &Row, LineSize);
	}
	if(Flow.Control("freeze-stars", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, "tclient-freeze-stars", Localize("Freeze stars"), &g_Config.m_ClFreezeStars, &Row, LineSize);
	}
	if(Flow.Control("color-freeze", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmColorFreeze, "tclient-color-freeze", Localize("Use colored skins for frozen tees"), &g_Config.m_QmColorFreeze, &Row, LineSize);
	}
	if(Flow.Control("freeze-katana", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFreezeKatana, "tclient-freeze-katana", Localize("Show katana on frozen players"), &g_Config.m_QmFreezeKatana, &Row, LineSize);
	}
	if(Flow.Control("white-feet", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWhiteFeet, "tclient-white-feet", Localize("Render all custom colored feet as white feet skin"), &g_Config.m_QmWhiteFeet, &Row, LineSize);
	}
	static CSettingsConditionalRows s_WhiteFeetRows;
	if(s_WhiteFeetRows.Update(g_Config.m_QmWhiteFeet, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope WhiteFeetRevealScope(Ui(), Flow, s_WhiteFeetRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("white-feet-skin", LineSize, Row, RowSpacing))
		{
			Row.VSplitMid(&Row, nullptr);
			static CLineInput s_WhiteFeet(g_Config.m_QmWhiteFeetSkin, sizeof(g_Config.m_QmWhiteFeetSkin));
			s_WhiteFeet.SetEmptyText("x_ninja");
			IUiContext InputCtx = SettingsUiContext("settings_tclient_white_feet_text_inputs", Ctx.m_Metrics.m_UiScale);
			ui_widget::InputField(InputCtx, &s_WhiteFeet, Row, nullptr, EditBoxFontSize);
		}
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientVisualEffectsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	const SSettingsContentMetrics ContentMetrics = Ctx.m_Metrics;
	const float RadioHeight = ResolveSettingsRadioRowLayout(Content, 3, ContentMetrics).m_Height;
	if(Flow.Control("tiny-tee-mode", RadioHeight, Row, RowSpacing))
	{
		int Value = g_Config.m_QmTinyTees ? (g_Config.m_QmTinyTeesOthers ? 2 : 1) : 0;
		if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-smaller-tees-label", Localize("Smaller tees"), s_vTinyTeeModeButtons, {"tclient-smaller-tees-none", "tclient-smaller-tees-self", "tclient-smaller-tees-all"}, {Localize("None"), Localize("Self"), Localize("All")}, {0, 1, 2}, Value, ContentMetrics, nullptr, &g_Config.m_QmTinyTees, &g_Config.m_QmTinyTeesOthers))
		{
			if(Flow.Input() && !Ui()->RenderOnly())
			{
				g_Config.m_QmTinyTees = Value > 0 ? 1 : 0;
				g_Config.m_QmTinyTeesOthers = Value > 1 ? 1 : 0;
			}
		}
	}
	// 开关驱动的参数行：高度按进度长出来、下面的行跟着让位，开/关都不再硬跳。
	const int MotionLevel = g_Config.m_QmUiMotionLevel;
	static CSettingsConditionalRows s_TinyTeeSizeRows;
	if(s_TinyTeeSizeRows.Update(g_Config.m_QmTinyTees > 0, Ctx.m_UiContext.m_FrameDt, MotionLevel))
	{
		const CSettingsContentRevealScope RevealScope(Ui(), Flow, s_TinyTeeSizeRows.Fade(MotionLevel));
		if(Flow.Control("tiny-tee-size", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tiny-tee-size", &g_Config.m_QmTinyTeeSize, &g_Config.m_QmTinyTeeSize, &Row, Localize("Tiny Tee Size"), 85, 115);
		}
	}
	if(Flow.Control("fake-ctf-flags", RadioHeight, Row, RowSpacing))
	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_QmFakeCtfFlags;
		if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-fake-ctf-flags-label", Localize("Fake CTF flags"), s_vButtonContainers, {"tclient-fake-ctf-flags-none", "tclient-fake-ctf-flags-red", "tclient-fake-ctf-flags-blue"}, {Localize("None"), Localize("Red"), Localize("Blue")}, {0, 1, 2}, Value, ContentMetrics, nullptr, &g_Config.m_QmFakeCtfFlags))
		{
			if(Flow.Input() && !Ui()->RenderOnly())
				g_Config.m_QmFakeCtfFlags = Value;
		}
	}
	// Q 弹 Tee 开关放在基础项最后一行：平时只占一个开关，打开后直接展开三项细调，不再经过“个性调整”折叠。
	if(Flow.Control("jelly-toggle", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTee, "tclient-enable-jelly-tee", Localize("Enable Jelly Tee"), &g_Config.m_QmJellyTee, &Row, LineSize);
	}
	static CSettingsConditionalRows s_JellyDetailRows;
	if(s_JellyDetailRows.Update(g_Config.m_QmJellyTee != 0, Ctx.m_UiContext.m_FrameDt, MotionLevel))
	{
		const CSettingsContentRevealScope RevealScope(Ui(), Flow, s_JellyDetailRows.Fade(MotionLevel));
		if(Flow.Control("jelly-others", LineSize, Row, RowSpacing))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTeeOthers, "tclient-jelly-others", Localize("Jelly others"), &g_Config.m_QmJellyTeeOthers, &Row, LineSize);
		}
		if(Flow.Control("jelly-strength", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-strength", &g_Config.m_QmJellyTeeStrength, &g_Config.m_QmJellyTeeStrength, &Row, Localize("Jelly strength"), 0, 1000);
		}
		if(Flow.Control("jelly-duration", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-duration", &g_Config.m_QmJellyTeeDuration, &g_Config.m_QmJellyTeeDuration, &Row, Localize("Jelly duration"), 1, 500);
		}
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientPlayerIndicatorCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("enabled", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerIndicator, "tclient-player-indicator-enabled", Localize("Show any enabled Indicators"), &g_Config.m_QmPlayerIndicator, &Row, LineSize);
	}
	if(Flow.Control("hide-visible", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorHideVisible, "tclient-indicator-hide-visible", Localize("Hide indicator for tees on your screen"), &g_Config.m_QmIndicatorHideVisible, &Row, LineSize);
	}
	if(Flow.Control("freeze-only", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerIndicatorFreeze, "tclient-player-indicator-freeze-only", Localize("Show only freeze Players"), &g_Config.m_QmPlayerIndicatorFreeze, &Row, LineSize);
	}
	if(Flow.Control("team-only", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorTeamOnly, "tclient-indicator-team-only", Localize("Only show after joining a team"), &g_Config.m_QmIndicatorTeamOnly, &Row, LineSize);
	}
	if(Flow.Control("tees", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorTees, "tclient-indicator-tees", Localize("Render tiny tees instead of circles"), &g_Config.m_QmIndicatorTees, &Row, LineSize);
	}
	if(Flow.Control("warlist", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicator, "tclient-warlist-indicator", Localize("Use warlist groups for indicator"), &g_Config.m_QmWarListIndicator, &Row, LineSize);
	}
	float SectionReveal = 0.0f;
	if(qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "tclient:player-indicator", Ctx.m_Metrics, &SectionReveal, Ctx.m_ReadOnly))
	{
		const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);
		if(Flow.Control("radius", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-radius", &g_Config.m_QmIndicatorRadius, &g_Config.m_QmIndicatorRadius, &Row, Localize("Indicator size"), 1, 16);
		}
		if(Flow.Control("opacity", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-opacity", &g_Config.m_QmIndicatorOpacity, &g_Config.m_QmIndicatorOpacity, &Row, Localize("Indicator opacity"), 0, 100);
		}
		if(Flow.Control("variable-distance", LineSize, Row, RowSpacing))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorVariableDistance, "tclient-indicator-variable-distance", Localize("Change indicator offset based on distance to other tees"), &g_Config.m_QmIndicatorVariableDistance, &Row, LineSize);
		}
		// 固定偏移与跟随距离共用同一个 offset 行：行 id、控件和取值范围都不变，只有文案随模式切换。
		// 不能按 if/else 各声明一份——交叉过渡时同一个行 id 会出现两行、在同一位置叠起来；
		// 跟随距离多出来的两个参数行走条件行展开，收起时整块不声明。
		const bool IndicatorVariableDistance = g_Config.m_QmIndicatorVariableDistance != 0;
		if(Flow.Control("offset", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset", &g_Config.m_QmIndicatorOffset, &g_Config.m_QmIndicatorOffset, &Row, IndicatorVariableDistance ? Localize("Indicator min offset") : Localize("Indicator offset"), 16, 200);
		}
		static CSettingsConditionalRows s_IndicatorVariableDistanceRows;
		if(s_IndicatorVariableDistanceRows.Update(IndicatorVariableDistance, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
		{
			const CSettingsContentRevealScope IndicatorVariableDistanceRevealScope(Ui(), Flow, s_IndicatorVariableDistanceRows.Fade(g_Config.m_QmUiMotionLevel));
			if(Flow.Control("max-offset", LineSize, Row, RowSpacing))
			{
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset-max", &g_Config.m_QmIndicatorOffsetMax, &g_Config.m_QmIndicatorOffsetMax, &Row, Localize("Indicator max offset"), 16, 200);
			}
			if(Flow.Control("max-distance", LineSize, Row, RowSpacing))
			{
				DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-max-distance", &g_Config.m_QmIndicatorMaxDistance, &g_Config.m_QmIndicatorMaxDistance, &Row, Localize("Indicator max distance"), 500, 7000);
			}
		}
		static CSettingsConditionalRows s_WarListIndicatorRows;
		if(s_WarListIndicatorRows.Update(g_Config.m_QmWarListIndicator, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
		{
			const CSettingsContentRevealScope WarListIndicatorRevealScope(Ui(), Flow, s_WarListIndicatorRows.Fade(g_Config.m_QmUiMotionLevel));
			if(Flow.Control("warlist-colors", LineSize, Row, RowSpacing))
			{
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorColors, "tclient-warlist-indicator-colors", Localize("Use warlist colors instead of regular colors"), &g_Config.m_QmWarListIndicatorColors, &Row, LineSize);
			}
			if(Flow.Control("warlist-all", LineSize, Row, RowSpacing))
			{
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorAll, "tclient-warlist-indicator-all", Localize("Show all warlist groups"), &g_Config.m_QmWarListIndicatorAll, &Row, LineSize);
			}
			if(Flow.Control("warlist-enemy", LineSize, Row, RowSpacing))
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorEnemy, "tclient-warlist-indicator-enemy", aBuf, &g_Config.m_QmWarListIndicatorEnemy, &Row, LineSize);
			}
			if(Flow.Control("warlist-team", LineSize, Row, RowSpacing))
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
				DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorTeam, "tclient-warlist-indicator-team", aBuf, &g_Config.m_QmWarListIndicatorTeam, &Row, LineSize);
			}
		}
		if(!g_Config.m_QmWarListIndicatorColors || !g_Config.m_QmWarListIndicator)
		{
			if(Flow.Control("alive-color", LineSize, Row, RowSpacing))
			{
				static CButtonContainer s_IndicatorAliveColorId;
				DoLine_ColorPicker(&s_IndicatorAliveColorId, CurrentSettingsContentMetrics(), &Row, Localize("Indicator alive color"), &g_Config.m_QmIndicatorAlive, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmIndicatorAlive)), false);
			}
			if(Flow.Control("freeze-color", LineSize, Row, RowSpacing))
			{
				static CButtonContainer s_IndicatorDeadColorId;
				DoLine_ColorPicker(&s_IndicatorDeadColorId, CurrentSettingsContentMetrics(), &Row, Localize("Indicator in freeze color"), &g_Config.m_QmIndicatorFreeze, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmIndicatorFreeze)), false);
			}
			if(Flow.Control("saved-color", LineSize, Row, RowSpacing))
			{
				static CButtonContainer s_IndicatorSavedColorId;
				DoLine_ColorPicker(&s_IndicatorSavedColorId, CurrentSettingsContentMetrics(), &Row, Localize("Indicator safe color"), &g_Config.m_QmIndicatorSaved, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmIndicatorSaved)), false);
			}
		}
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientTeeStatusBarCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("show-frozen-hud", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowFrozenHud, "tclient-show-frozen-hud", Localize("Show tee status bar"), &g_Config.m_QmShowFrozenHud, &Row, LineSize);
	}
	if(Flow.Control("frozen-hud-skins", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowFrozenHudSkins, "tclient-frozen-hud-skins", Localize("Use custom skins instead of the ninja tee"), &g_Config.m_QmShowFrozenHudSkins, &Row, LineSize);
	}
	if(Flow.Control("frozen-hud-team-only", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFrozenHudTeamOnly, "tclient-frozen-hud-team-only", Localize("Only show after joining a team"), &g_Config.m_QmFrozenHudTeamOnly, &Row, LineSize);
	}
	if(Flow.Control("show-frozen-text", LineSize, Row, RowSpacing))
	{
		if(DoTClientSettingsButton_CheckBox(&g_Config.m_QmShowFrozenText, "tclient-show-frozen-text", Localize("Show the number of tees still alive"), g_Config.m_QmShowFrozenText >= 1, &Row))
		{
			if(Flow.Input() && !Ui()->RenderOnly())
				g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText >= 1 ? 0 : 1;
		}
	}
	static CSettingsConditionalRows s_ShowFrozenCountRows;
	if(s_ShowFrozenCountRows.Update(g_Config.m_QmShowFrozenText, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
	{
		const CSettingsContentRevealScope ShowFrozenCountRevealScope(Ui(), Flow, s_ShowFrozenCountRows.Fade(g_Config.m_QmUiMotionLevel));
		if(Flow.Control("show-frozen-count", LineSize, Row, RowSpacing))
		{
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_CountFrozenText, &Row, &g_Config.m_QmShowFrozenText);
			if(DoTClientSettingsButton_CheckBox(&s_CountFrozenText, "tclient-show-frozen-count-text", Localize("Show the number of frozen tees"), g_Config.m_QmShowFrozenText == 2, &Row))
			{
				if(Flow.Input() && !Ui()->RenderOnly())
					g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText != 2 ? 2 : 1;
			}
		}
	}
	if(Flow.Control("frozen-max-rows", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-max-rows", &g_Config.m_QmFrozenMaxRows, &g_Config.m_QmFrozenMaxRows, &Row, Localize("Maximum rows"), 1, 6);
	}
	if(Flow.Control("frozen-hud-tee-size", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-hud-tee-size", &g_Config.m_QmFrozenHudTeeSize, &g_Config.m_QmFrozenHudTeeSize, &Row, Localize("Tee size"), 8, 27);
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientTileOutlinesCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("outline-enabled", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmOutline, "tclient-outline-enabled", Localize("Show all enabled outlines"), &g_Config.m_QmOutline, &Row, LineSize);
	}
	if(Flow.Control("outline-entities", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmOutlineEntities, "tclient-outline-entities", Localize("Only show outlines in the entities layer"), &g_Config.m_QmOutlineEntities, &Row, LineSize);
	}
	// 全局透明度作用于所有类型，属于主行，不能和某个类型的具体参数混在一起。
	if(Flow.Control("outline-opacity", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-opacity", &g_Config.m_QmOutlineAlpha, &g_Config.m_QmOutlineAlpha, &Row, Localize("Outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	if(Flow.Control("outline-solid-opacity", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-solid-opacity", &g_Config.m_QmOutlineSolidAlpha, &g_Config.m_QmOutlineSolidAlpha, &Row, Localize("Solid tile outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	// 类型行：开关 + 名称 + 常驻色块 + 重置。开关本身就是“是否展开内部配置项”：
	// 和别处一致，打开才出现宽度等细调，不再额外挂一个折叠箭头（两个展开入口会互相打架）。
	// 色块常驻，关着也能看到这个类型配的是什么颜色。
	static CButtonContainer s_aOutlineResetButtons[8];
	// 每个类型的参数行各自记一条显隐轨道：开关打开淡入上浮，关掉后播完再收高度。
	static CSettingsRowReveal s_aTypeReveal[8];
	const int MotionLevel = g_Config.m_QmUiMotionLevel;
	const float FrameDt = Ctx.m_UiContext.m_FrameDt;
	const float SubIndent = ui_token::settings::row::SUB.m_Indent * Ctx.m_Metrics.m_UiScale;
	const auto OutlineType = [&](const int Index, const char *pId, const char *pName, int &Enabled, int &Width, unsigned int &Color, const unsigned int ColorDefault) {
		CUIRect TypeRow;
		if(Flow.Control(pId, LineSize, TypeRow, ui_token::settings::row::PRIMARY.m_Spacing))
		{
			const float Side = std::min(TypeRow.h, 18.0f);
			CUIRect Reset, Swatch, Control;
			TypeRow.VSplitRight(Side, &Control, &Reset);
			Control.VSplitRight(MarginExtraSmall, &Control, nullptr);
			Control.VSplitRight(Side, &Control, &Swatch);
			Control.VSplitRight(RowSpacing, &Control, nullptr);

			const int Checked = Enabled != 0;
			if(DoTClientSettingsButton_CheckBox(&Enabled, pId, pName, Checked, &Control, FontSize) && Flow.Input() && !Ctx.m_ReadOnly)
				Enabled ^= 1;
			// 轮廓颜色不开放 alpha：透明度由本卡片的全局 / Solid 滑条统一控制，
			// 取色器打开时也不能把配置高字节写成 0。
			DoButton_ColorPicker(&Swatch, &Color, false);
			if(Ui()->DoButton_QmIcon(&s_aOutlineResetButtons[Index], EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &Reset, IGraphics::CORNER_ALL))
				Color = ColorDefault;
			GameClient()->m_Tooltips.DoToolTip(&s_aOutlineResetButtons[Index], &Reset, Localize("Reset"));
		}
		if(!s_aTypeReveal[Index].Update(Enabled != 0, FrameDt, MotionLevel))
			return;
		const SQmListEntryVisual RevealVisual = s_aTypeReveal[Index].Visual(MotionLevel);
		// 参数行缩进一级，标签带类型前缀：一屏里出现多组「宽度」时仍能对上号。
		const std::string WidthId = std::string(pId) + "-width";
		char aWidthLabel[128];
		str_format(aWidthLabel, sizeof(aWidthLabel), "%s · %s", pName, Localize("Width", "Outlines"));
		// 高度按进度长出来（下面的类型行跟着让位），内容按最终高度画并套进度透明度，
		// 避免动画中途文字被压扁。
		Flow.BeginReveal(RevealVisual.m_Fade);
		if(Flow.Control(WidthId.c_str(), LineSize, TypeRow, ui_token::settings::row::SUB.m_Spacing))
		{
			std::optional<CUiScopedControlAlphaScale> RevealFade;
			if(RevealVisual.m_Fade < 0.999f)
				RevealFade.emplace(Ui(), RevealVisual.m_Fade);
			TypeRow.h = LineSize;
			TypeRow.x += SubIndent;
			TypeRow.w = std::max(0.0f, TypeRow.w - SubIndent);
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, WidthId.c_str(), &Width, &Width, &TypeRow, aWidthLabel, 1, 16);
		}
		Flow.EndReveal();
	};
	// 细调参数收进“个性调整”折叠行：它是配置层，不能和上面的功能开关长得一样。
	// 这张卡上方已经有一组滑条做视觉分隔，折叠行不再额外画分割线。
	// 展开/收起走布局高度过渡：内容从 0 长出来、下方行跟着让位，收起时先收高度再停声明。
	static CButtonContainer s_OutlineFoldRowButton;
	static CSettingsRowReveal s_OutlineSectionReveal;
	bool &SectionExpanded = qm_card_catalog::SettingsPersonalizationExpanded("tclient:tile-outlines");
	const bool SectionDeclared = s_OutlineSectionReveal.Update(SectionExpanded, FrameDt, MotionLevel);
	const SQmListEntryVisual SectionVisual = s_OutlineSectionReveal.Visual(MotionLevel);
	CUIRect FoldRow;
	if(Flow.Control("outline-personalization", LineSize, FoldRow, ui_token::settings::row::FOLD.m_Spacing))
	{
		SSettingsFoldRowStyle FoldStyle = ResolveSettingsFoldRowStyle(
			SettingsCardDeckVisualOptions().m_BorderColor,
			ResolveConfiguredAccentColor().WithAlpha(1.0f));
		const bool Clicked = SettingsFoldRow(Ui(), &s_OutlineFoldRowButton, FoldRow, Localize("Personal adjustments"), Ctx.m_Metrics.m_SmallSize, SectionExpanded, FoldStyle);
		if(Clicked && Flow.Input() && !Ctx.m_ReadOnly)
		{
			SectionExpanded = !SectionExpanded;
			qm_card_catalog::SaveSettingsPersonalizationExpanded();
		}
	}
	Flow.BeginReveal(SectionVisual.m_Fade);
	std::optional<CUiScopedControlAlphaScale> SectionFade;
	if(SectionVisual.m_Fade < 0.999f)
		SectionFade.emplace(Ui(), SectionVisual.m_Fade);
	if(SectionDeclared)
	{
		// 每个图块种类一行、各自开关/宽度/颜色：可钩与不可钩墙体分开，冻结与深度冻结、
		// 解冻与深度解冻也分开，不用再靠“某类型 · 深度颜色”这种隐藏子项。
		OutlineType(0, "solid", Localize("Hookable tiles"), g_Config.m_QmOutlineSolid, g_Config.m_QmOutlineWidthSolid, g_Config.m_QmOutlineColorSolid, DefaultConfig::QmOutlineColorSolid);
		OutlineType(1, "nohook", Localize("Unhookable tiles"), g_Config.m_QmOutlineNoHook, g_Config.m_QmOutlineWidthNoHook, g_Config.m_QmOutlineColorNoHook, DefaultConfig::QmOutlineColorNoHook);
		OutlineType(2, "freeze", Localize("Freeze"), g_Config.m_QmOutlineFreeze, g_Config.m_QmOutlineWidthFreeze, g_Config.m_QmOutlineColorFreeze, DefaultConfig::QmOutlineColorFreeze);
		OutlineType(3, "deep-freeze", Localize("Deep freeze"), g_Config.m_QmOutlineDeepFreeze, g_Config.m_QmOutlineWidthDeepFreeze, g_Config.m_QmOutlineColorDeepFreeze, DefaultConfig::QmOutlineColorDeepFreeze);
		OutlineType(4, "unfreeze", Localize("Unfreeze"), g_Config.m_QmOutlineUnfreeze, g_Config.m_QmOutlineWidthUnfreeze, g_Config.m_QmOutlineColorUnfreeze, DefaultConfig::QmOutlineColorUnfreeze);
		OutlineType(5, "deep-unfreeze", Localize("Deep unfreeze"), g_Config.m_QmOutlineDeepUnfreeze, g_Config.m_QmOutlineWidthDeepUnfreeze, g_Config.m_QmOutlineColorDeepUnfreeze, DefaultConfig::QmOutlineColorDeepUnfreeze);
		OutlineType(6, "kill", Localize("Kill"), g_Config.m_QmOutlineKill, g_Config.m_QmOutlineWidthKill, g_Config.m_QmOutlineColorKill, DefaultConfig::QmOutlineColorKill);
		OutlineType(7, "tele", Localize("Tele"), g_Config.m_QmOutlineTele, g_Config.m_QmOutlineWidthTele, g_Config.m_QmOutlineColorTele, DefaultConfig::QmOutlineColorTele);
	}
	Flow.EndReveal();

	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientGhostToolsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("show-others-ghosts", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowOthersGhosts, "tclient-show-others-ghosts", Localize("Show unpredicted ghosts for other players"), &g_Config.m_QmShowOthersGhosts, &Row, LineSize);
	}
	if(Flow.Control("swap-ghosts", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmSwapGhosts, "tclient-swap-ghosts", Localize("Swap ghosts with regular players"), &g_Config.m_QmSwapGhosts, &Row, LineSize);
	}
	if(Flow.Control("hide-frozen-ghosts", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmHideFrozenGhosts, "tclient-hide-frozen-ghosts", Localize("Hide ghosts of frozen players"), &g_Config.m_QmHideFrozenGhosts, &Row, LineSize);
	}
	if(Flow.Control("ghost-replay-feedback", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmGhostViewFeedback, "tclient-ghost-replay-feedback", Localize("Replay ghost sounds and effects"), &g_Config.m_QmGhostViewFeedback, &Row, LineSize);
	}
	if(Flow.Control("render-ghost-as-circle", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRenderGhostAsCircle, "tclient-render-ghost-as-circle", Localize("Render ghosts as circles"), &g_Config.m_QmRenderGhostAsCircle, &Row, LineSize);
	}
	if(Flow.Control("toggle-ghost-key", LineSize, Row, RowSpacing))
	{
		static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
		DoLine_KeyReader(Row, s_ReaderButtonGhost, s_ClearButtonGhost, Localize("Toggle ghost key"), "toggle qm_show_others_ghosts 0 1");
	}
	if(Flow.Control("predicted-ghost-opacity", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-predicted-ghost-opacity", &g_Config.m_QmPredGhostsAlpha, &g_Config.m_QmPredGhostsAlpha, &Row, Localize("Predicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	if(Flow.Control("unpredicted-ghost-opacity", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-unpredicted-ghost-opacity", &g_Config.m_QmUnpredGhostsAlpha, &g_Config.m_QmUnpredGhostsAlpha, &Row, Localize("Unpredicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientRainbowCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("rainbow-tees", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowTees, "tclient-rainbow-tees", Localize("Rainbow Tees"), &g_Config.m_QmRainbowTees, &Row, LineSize);
	}
	if(Flow.Control("rainbow-weapons", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowWeapon, "tclient-rainbow-weapons", Localize("Rainbow weapons"), &g_Config.m_QmRainbowWeapon, &Row, LineSize);
	}
	if(Flow.Control("rainbow-hook", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowHook, "tclient-rainbow-hook", Localize("Rainbow hook"), &g_Config.m_QmRainbowHook, &Row, LineSize);
	}
	if(Flow.Control("rainbow-others", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowOthers, "tclient-rainbow-others", Localize("Rainbow others"), &g_Config.m_QmRainbowOthers, &Row, LineSize);
	}
	if(Flow.Control("rainbow-mode", LineSize, Row, RowSpacing))
	{
		const char *apNames[] = {Localize("Rainbow"), Localize("Pulse"), Localize("Black"), Localize("Random")};
		static CUi::SDropDownState s_RainbowDropDownState;
		static CScrollRegion s_RainbowDropDownScrollRegion;
		s_RainbowDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RainbowDropDownScrollRegion;
		const int Old = g_Config.m_QmRainbowMode - 1;
		const int New = DoSettingsDropDown(&Row, Old, apNames, std::size(apNames), s_RainbowDropDownState, {}, &g_Config.m_QmRainbowMode);
		if(Flow.Input() && !Ui()->RenderOnly() && Old != New)
			g_Config.m_QmRainbowMode = New + 1;
	}
	if(Flow.Control("rainbow-speed", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-rainbow-speed", &g_Config.m_QmRainbowSpeed, &g_Config.m_QmRainbowSpeed, &Row, Localize("Rainbow speed"), 0, 5000, &CUi::ms_LogarithmicScrollbarScale, 0, "%");
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientTeeTrailsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("tee-trail-enabled", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrail, "tclient-tee-trail-enabled", Localize("Enable tee trails"), &g_Config.m_QmTeeTrail, &Row, LineSize);
	}
	if(Flow.Control("tee-trail-others", LineSize, Row, RowSpacing))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailOthers, "tclient-tee-trail-others", Localize("Show other tees' trails"), &g_Config.m_QmTeeTrailOthers, &Row, LineSize);
	}
	float SectionReveal = 0.0f;
	if(qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "tclient:tee-trails", Ctx.m_Metrics, &SectionReveal, Ctx.m_ReadOnly))
	{
		const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);
		if(Flow.Control("tee-trail-fade", LineSize, Row, RowSpacing))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailFade, "tclient-tee-trail-fade", Localize("Fade trail alpha"), &g_Config.m_QmTeeTrailFade, &Row, LineSize);
		}
		if(Flow.Control("tee-trail-taper", LineSize, Row, RowSpacing))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailTaper, "tclient-tee-trail-taper", Localize("Taper trail width"), &g_Config.m_QmTeeTrailTaper, &Row, LineSize);
		}
		if(Flow.Control("tee-trail-style-colors", LineSize, Row, RowSpacing))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailStyleColors, "tclient-tee-trail-style-colors", Localize("Use style colors"), &g_Config.m_QmTeeTrailStyleColors, &Row, LineSize);
		}
		if(Flow.Control("color-mode", LineSize, Row, RowSpacing))
		{
			const char *apNames[] = {Localize("Solid"), Localize("Tee"), Localize("Rainbow"), Localize("Speed")};
			s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
			const int Old = g_Config.m_QmTeeTrailColorMode - 1;
			const int New = DoSettingsDropDown(&Row, Old, apNames, std::size(apNames), s_TrailDropDownState, {}, &g_Config.m_QmTeeTrailColorMode);
			if(Flow.Input() && !Ui()->RenderOnly() && New != Old)
				g_Config.m_QmTeeTrailColorMode = New + 1;
		}
		if(Flow.Control("trail-style", LineSize, Row, RowSpacing))
		{
			const char *apNames[] = {
				Localize("Original"),
				Localize("Manga: Ink Slash"),
				Localize("Magic: Spirit Script"),
				Localize("Pixel: Layer Shift"),
				Localize("Aurora: Polar Veil"),
				Localize("Ink: Paper Bloom"),
				Localize("Ignis: Ember Spirit"),
				Localize("Stardust: Constellation"),
				Localize("Black Flash: Void Rift"),
			};
			// 下拉框按艺术样式顺序显示；4、5 是旧配置编号，不进入列表，保存值仍写样式编号。
			const int aStyleIds[] = {
				qm_tee_trail::STYLE_ORIGINAL,
				qm_tee_trail::STYLE_MANGA,
				qm_tee_trail::STYLE_MAGIC,
				qm_tee_trail::STYLE_PIXEL,
				qm_tee_trail::STYLE_AURORA,
				qm_tee_trail::STYLE_INK,
				qm_tee_trail::STYLE_IGNIS,
				qm_tee_trail::STYLE_STARDUST,
				qm_tee_trail::STYLE_BLACKFLASH,
			};
			static_assert(std::size(apNames) == std::size(aStyleIds), "trail style names must match ids");
			s_TrailStyleDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailStyleDropDownScrollRegion;
			const int Current = qm_tee_trail::ResolveStyle(g_Config.m_QmTeeTrailStyle);
			int Old = 0;
			for(size_t i = 0; i < std::size(aStyleIds); ++i)
				if(aStyleIds[i] == Current)
					Old = int(i);
			const int New = DoSettingsDropDown(&Row, Old, apNames, std::size(apNames), s_TrailStyleDropDownState, {}, &g_Config.m_QmTeeTrailStyle);
			if(Flow.Input() && !Ui()->RenderOnly() && New != Old && New >= 0 && New < int(std::size(aStyleIds)))
				g_Config.m_QmTeeTrailStyle = aStyleIds[New];
		}
		static CSettingsConditionalRows s_TeeTrailSolidColorRows;
		if(s_TeeTrailSolidColorRows.Update(g_Config.m_QmTeeTrailColorMode == CTrails::COLORMODE_SOLID, Ctx.m_UiContext.m_FrameDt, g_Config.m_QmUiMotionLevel))
		{
			const CSettingsContentRevealScope TeeTrailSolidColorRevealScope(Ui(), Flow, s_TeeTrailSolidColorRows.Fade(g_Config.m_QmUiMotionLevel));
			if(Flow.Control("solid-color", LineSize, Row, RowSpacing))
			{
				static CButtonContainer s_TeeTrailColor;
				DoLine_ColorPicker(&s_TeeTrailColor, CurrentSettingsContentMetrics(), &Row, Localize("Tee trail color"), &g_Config.m_QmTeeTrailColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmTeeTrailColor)), false);
			}
		}
		if(Flow.Control("tee-trail-width", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-width", &g_Config.m_QmTeeTrailWidth, &g_Config.m_QmTeeTrailWidth, &Row, Localize("Trail width"), 0, 20);
		}
		if(Flow.Control("tee-trail-length", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-length", &g_Config.m_QmTeeTrailLength, &g_Config.m_QmTeeTrailLength, &Row, Localize("Trail length"), 0, 200);
		}
		if(Flow.Control("tee-trail-alpha", LineSize, Row, RowSpacing))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-alpha", &g_Config.m_QmTeeTrailAlpha, &g_Config.m_QmTeeTrailAlpha, &Row, Localize("Trail alpha"), 0, 100);
		}
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientBackgroundDrawCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	const CTClientCardMetricsScope CardMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("draw-key", LineSize, Row, RowSpacing))
	{
		static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
		DoLine_KeyReader(Row, s_ReaderButtonDraw, s_ClearButtonDraw, Localize("Draw where mouse is"), "+bg_draw");
	}
	if(Flow.Control("color", LineSize, Row, RowSpacing))
	{
		static CButtonContainer s_BgDrawColor;
		DoLine_ColorPicker(&s_BgDrawColor, CurrentSettingsContentMetrics(), &Row, Localize("Color"), &g_Config.m_QmBgDrawColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmBgDrawColor)), false);
	}
	if(Flow.Control("fade-time", LineSize, Row, RowSpacing))
	{
		if(g_Config.m_QmBgDrawFadeTime == 0)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_QmBgDrawFadeTime, &g_Config.m_QmBgDrawFadeTime, &Row, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds (never)"));
		else
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_QmBgDrawFadeTime, &g_Config.m_QmBgDrawFadeTime, &Row, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds"));
	}
	if(Flow.Control("width", LineSize, Row, RowSpacing))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-width", &g_Config.m_QmBgDrawWidth, &g_Config.m_QmBgDrawWidth, &Row, Localize("Width"), 1, 50);
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}
