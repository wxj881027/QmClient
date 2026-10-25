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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("ping-circle", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplatePingCircle, "tclient-nameplate-ping-circle", Localize("Show ping colored circle in nameplates"), &g_Config.m_QmNameplatePingCircle, &Row, LineSize);
	}
	if(Flow.Control("country", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCountry, "tclient-nameplate-country", Localize("Show country flags in nameplates"), &g_Config.m_QmNameplateCountry, &Row, LineSize);
	}
	if(Flow.Control("skins", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateSkins, "tclient-nameplate-skins", Localize("Show skin names in nameplate"), &g_Config.m_QmNameplateSkins, &Row, LineSize);
	}
	if(Flow.Control("freeze-stars", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, "tclient-freeze-stars", Localize("Freeze stars"), &g_Config.m_ClFreezeStars, &Row, LineSize);
	}
	if(Flow.Control("color-freeze", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmColorFreeze, "tclient-color-freeze", Localize("Use colored skins for frozen tees"), &g_Config.m_QmColorFreeze, &Row, LineSize);
	}
	if(Flow.Control("freeze-katana", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFreezeKatana, "tclient-freeze-katana", Localize("Show katana on frozen players"), &g_Config.m_QmFreezeKatana, &Row, LineSize);
	}
	if(Flow.Control("white-feet", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWhiteFeet, "tclient-white-feet", Localize("Render all custom colored feet as white feet skin"), &g_Config.m_QmWhiteFeet, &Row, LineSize);
	}
	if(g_Config.m_QmWhiteFeet)
	{
		if(Flow.Control("white-feet-skin", LineSize, Row, MarginSmall))
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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	const SSettingsContentMetrics ContentMetrics = Ctx.m_Metrics;
	const float RadioHeight = ResolveSettingsRadioRowLayout(Content, 3, ContentMetrics).m_Height;
	if(Flow.Control("tiny-tee-mode", RadioHeight, Row, MarginSmall))
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
	if(g_Config.m_QmTinyTees > 0)
	{
		if(Flow.Control("tiny-tee-size", LineSize, Row, MarginSmall))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tiny-tee-size", &g_Config.m_QmTinyTeeSize, &g_Config.m_QmTinyTeeSize, &Row, Localize("Tiny Tee Size"), 85, 115);
		}
	}
	if(Flow.Control("jelly-toggle", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTee, "tclient-enable-jelly-tee", Localize("Enable Jelly Tee"), &g_Config.m_QmJellyTee, &Row, LineSize);
	}
	if(g_Config.m_QmJellyTee)
	{
		if(Flow.Control("jelly-others", LineSize, Row, MarginSmall))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTeeOthers, "tclient-jelly-others", Localize("Jelly others"), &g_Config.m_QmJellyTeeOthers, &Row, LineSize);
		}
		if(Flow.Control("jelly-strength", LineSize, Row, MarginSmall))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-strength", &g_Config.m_QmJellyTeeStrength, &g_Config.m_QmJellyTeeStrength, &Row, Localize("Jelly strength"), 0, 1000);
		}
		if(Flow.Control("jelly-duration", LineSize, Row, MarginSmall))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-jelly-duration", &g_Config.m_QmJellyTeeDuration, &g_Config.m_QmJellyTeeDuration, &Row, Localize("Jelly duration"), 1, 500);
		}
	}
	if(Flow.Control("fake-ctf-flags", RadioHeight, Row, MarginSmall))
	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_QmFakeCtfFlags;
		if(DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, "tclient-fake-ctf-flags-label", Localize("Fake CTF flags"), s_vButtonContainers, {"tclient-fake-ctf-flags-none", "tclient-fake-ctf-flags-red", "tclient-fake-ctf-flags-blue"}, {Localize("None"), Localize("Red"), Localize("Blue")}, {0, 1, 2}, Value, ContentMetrics, nullptr, &g_Config.m_QmFakeCtfFlags))
		{
			if(Flow.Input() && !Ui()->RenderOnly())
				g_Config.m_QmFakeCtfFlags = Value;
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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("enabled", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerIndicator, "tclient-player-indicator-enabled", Localize("Show any enabled Indicators"), &g_Config.m_QmPlayerIndicator, &Row, LineSize);
	}
	if(Flow.Control("hide-visible", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorHideVisible, "tclient-indicator-hide-visible", Localize("Hide indicator for tees on your screen"), &g_Config.m_QmIndicatorHideVisible, &Row, LineSize);
	}
	if(Flow.Control("freeze-only", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerIndicatorFreeze, "tclient-player-indicator-freeze-only", Localize("Show only freeze Players"), &g_Config.m_QmPlayerIndicatorFreeze, &Row, LineSize);
	}
	if(Flow.Control("team-only", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorTeamOnly, "tclient-indicator-team-only", Localize("Only show after joining a team"), &g_Config.m_QmIndicatorTeamOnly, &Row, LineSize);
	}
	if(Flow.Control("tees", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorTees, "tclient-indicator-tees", Localize("Render tiny tees instead of circles"), &g_Config.m_QmIndicatorTees, &Row, LineSize);
	}
	if(Flow.Control("warlist", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicator, "tclient-warlist-indicator", Localize("Use warlist groups for indicator"), &g_Config.m_QmWarListIndicator, &Row, LineSize);
	}
	if(Flow.Control("radius", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-radius", &g_Config.m_QmIndicatorRadius, &g_Config.m_QmIndicatorRadius, &Row, Localize("Indicator size"), 1, 16);
	}
	if(Flow.Control("opacity", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-opacity", &g_Config.m_QmIndicatorOpacity, &g_Config.m_QmIndicatorOpacity, &Row, Localize("Indicator opacity"), 0, 100);
	}
	if(Flow.Control("variable-distance", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmIndicatorVariableDistance, "tclient-indicator-variable-distance", Localize("Change indicator offset based on distance to other tees"), &g_Config.m_QmIndicatorVariableDistance, &Row, LineSize);
	}
	if(g_Config.m_QmIndicatorVariableDistance)
	{
		if(Flow.Control("offset", LineSize, Row, MarginSmall))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset", &g_Config.m_QmIndicatorOffset, &g_Config.m_QmIndicatorOffset, &Row, Localize("Indicator min offset"), 16, 200);
		}
		if(Flow.Control("max-offset", LineSize, Row, MarginSmall))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset-max", &g_Config.m_QmIndicatorOffsetMax, &g_Config.m_QmIndicatorOffsetMax, &Row, Localize("Indicator max offset"), 16, 200);
		}
		if(Flow.Control("max-distance", LineSize, Row, MarginSmall))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-max-distance", &g_Config.m_QmIndicatorMaxDistance, &g_Config.m_QmIndicatorMaxDistance, &Row, Localize("Indicator max distance"), 500, 7000);
		}
	}
	else
	{
		if(Flow.Control("offset", LineSize, Row, MarginSmall))
		{
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-indicator-offset", &g_Config.m_QmIndicatorOffset, &g_Config.m_QmIndicatorOffset, &Row, Localize("Indicator offset"), 16, 200);
		}
	}
	if(g_Config.m_QmWarListIndicator)
	{
		if(Flow.Control("warlist-colors", LineSize, Row, MarginSmall))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorColors, "tclient-warlist-indicator-colors", Localize("Use warlist colors instead of regular colors"), &g_Config.m_QmWarListIndicatorColors, &Row, LineSize);
		}
		if(Flow.Control("warlist-all", LineSize, Row, MarginSmall))
		{
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorAll, "tclient-warlist-indicator-all", Localize("Show all warlist groups"), &g_Config.m_QmWarListIndicatorAll, &Row, LineSize);
		}
		if(Flow.Control("warlist-enemy", LineSize, Row, MarginSmall))
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorEnemy, "tclient-warlist-indicator-enemy", aBuf, &g_Config.m_QmWarListIndicatorEnemy, &Row, LineSize);
		}
		if(Flow.Control("warlist-team", LineSize, Row, MarginSmall))
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
			DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWarListIndicatorTeam, "tclient-warlist-indicator-team", aBuf, &g_Config.m_QmWarListIndicatorTeam, &Row, LineSize);
		}
	}
	if(!g_Config.m_QmWarListIndicatorColors || !g_Config.m_QmWarListIndicator)
	{
		if(Flow.Control("alive-color", LineSize, Row, MarginSmall))
		{
			static CButtonContainer s_IndicatorAliveColorId;
			DoLine_ColorPicker(&s_IndicatorAliveColorId, CurrentSettingsContentMetrics(), &Row, Localize("Indicator alive color"), &g_Config.m_QmIndicatorAlive, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		}
		if(Flow.Control("freeze-color", LineSize, Row, MarginSmall))
		{
			static CButtonContainer s_IndicatorDeadColorId;
			DoLine_ColorPicker(&s_IndicatorDeadColorId, CurrentSettingsContentMetrics(), &Row, Localize("Indicator in freeze color"), &g_Config.m_QmIndicatorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		}
		if(Flow.Control("saved-color", LineSize, Row, MarginSmall))
		{
			static CButtonContainer s_IndicatorSavedColorId;
			DoLine_ColorPicker(&s_IndicatorSavedColorId, CurrentSettingsContentMetrics(), &Row, Localize("Indicator safe color"), &g_Config.m_QmIndicatorSaved, ColorRGBA(0.0f, 0.0f, 0.0f), false);
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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("show-frozen-hud", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowFrozenHud, "tclient-show-frozen-hud", Localize("Show tee status bar"), &g_Config.m_QmShowFrozenHud, &Row, LineSize);
	}
	if(Flow.Control("frozen-hud-skins", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowFrozenHudSkins, "tclient-frozen-hud-skins", Localize("Use custom skins instead of the ninja tee"), &g_Config.m_QmShowFrozenHudSkins, &Row, LineSize);
	}
	if(Flow.Control("frozen-hud-team-only", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFrozenHudTeamOnly, "tclient-frozen-hud-team-only", Localize("Only show after joining a team"), &g_Config.m_QmFrozenHudTeamOnly, &Row, LineSize);
	}
	if(Flow.Control("frozen-max-rows", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-max-rows", &g_Config.m_QmFrozenMaxRows, &g_Config.m_QmFrozenMaxRows, &Row, Localize("Maximum rows"), 1, 6);
	}
	if(Flow.Control("frozen-hud-tee-size", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-frozen-hud-tee-size", &g_Config.m_QmFrozenHudTeeSize, &g_Config.m_QmFrozenHudTeeSize, &Row, Localize("Tee size"), 8, 27);
	}
	if(Flow.Control("show-frozen-text", LineSize, Row, MarginSmall))
	{
		if(DoTClientSettingsButton_CheckBox(&g_Config.m_QmShowFrozenText, "tclient-show-frozen-text", Localize("Show the number of tees still alive"), g_Config.m_QmShowFrozenText >= 1, &Row))
		{
			if(Flow.Input() && !Ui()->RenderOnly())
				g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText >= 1 ? 0 : 1;
		}
	}
	if(g_Config.m_QmShowFrozenText)
	{
		if(Flow.Control("show-frozen-count", LineSize, Row, MarginSmall))
		{
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_CountFrozenText, &Row, &g_Config.m_QmShowFrozenText);
			if(DoTClientSettingsButton_CheckBox(&s_CountFrozenText, "tclient-show-frozen-count-text", Localize("Show the number of frozen tees"), g_Config.m_QmShowFrozenText == 2, &Row))
			{
				if(Flow.Input() && !Ui()->RenderOnly())
					g_Config.m_QmShowFrozenText = g_Config.m_QmShowFrozenText != 2 ? 2 : 1;
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

float CMenus::LayoutTClientTileOutlinesCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("outline-enabled", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmOutline, "tclient-outline-enabled", Localize("Show all enabled outlines"), &g_Config.m_QmOutline, &Row, LineSize);
	}
	if(Flow.Control("outline-entities", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmOutlineEntities, "tclient-outline-entities", Localize("Only show outlines in the entities layer"), &g_Config.m_QmOutlineEntities, &Row, LineSize);
	}
	if(Flow.Control("outline-opacity", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-opacity", &g_Config.m_QmOutlineAlpha, &g_Config.m_QmOutlineAlpha, &Row, Localize("Outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	if(Flow.Control("outline-solid-opacity", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-solid-opacity", &g_Config.m_QmOutlineSolidAlpha, &g_Config.m_QmOutlineSolidAlpha, &Row, Localize("Solid tile outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	auto OutlineType = [&](const char *pId, CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, unsigned int ColorDefault) {
		const std::string ColorId = std::string(pId) + "-color";
		if(Flow.Control(ColorId.c_str(), LineSize, Row, MarginSmall))
			DoLine_ColorPicker(&ButtonContainer, CurrentSettingsContentMetrics(), &Row, pName, &Color, ColorDefault, true, &Enable, true);
		const std::string WidthId = std::string(pId) + "-width";
		if(Flow.Control(WidthId.c_str(), LineSize, Row, MarginSmall))
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-outline-width", &Width, &Width, &Row, Localize("Width", "Outlines"), 1, 16);
	};
	static CButtonContainer s_aOutlineButtonContainers[5];
	OutlineType("solid", s_aOutlineButtonContainers[0], Localize("Solid"), g_Config.m_QmOutlineSolid, g_Config.m_QmOutlineWidthSolid, g_Config.m_QmOutlineColorSolid, DefaultConfig::QmOutlineColorSolid);
	OutlineType("freeze", s_aOutlineButtonContainers[1], Localize("Freeze"), g_Config.m_QmOutlineFreeze, g_Config.m_QmOutlineWidthFreeze, g_Config.m_QmOutlineColorFreeze, DefaultConfig::QmOutlineColorFreeze);
	if(Flow.Control("deep-freeze-color", LineSize, Row, MarginSmall))
	{
		static CButtonContainer s_OutlineDeepFreezeColorId;
		DoLine_ColorPicker(&s_OutlineDeepFreezeColorId, CurrentSettingsContentMetrics(), &Row, Localize("Deep freeze color"), &g_Config.m_QmOutlineColorDeepFreeze, DefaultConfig::QmOutlineColorDeepFreeze, false, nullptr, true);
	}
	OutlineType("unfreeze", s_aOutlineButtonContainers[2], Localize("Unfreeze"), g_Config.m_QmOutlineUnfreeze, g_Config.m_QmOutlineWidthUnfreeze, g_Config.m_QmOutlineColorUnfreeze, DefaultConfig::QmOutlineColorUnfreeze);
	if(Flow.Control("deep-unfreeze-color", LineSize, Row, MarginSmall))
	{
		static CButtonContainer s_OutlineDeepUnfreezeColorId;
		DoLine_ColorPicker(&s_OutlineDeepUnfreezeColorId, CurrentSettingsContentMetrics(), &Row, Localize("Deep unfreeze color"), &g_Config.m_QmOutlineColorDeepUnfreeze, DefaultConfig::QmOutlineColorDeepUnfreeze, false, nullptr, true);
	}
	OutlineType("kill", s_aOutlineButtonContainers[3], Localize("Kill"), g_Config.m_QmOutlineKill, g_Config.m_QmOutlineWidthKill, g_Config.m_QmOutlineColorKill, DefaultConfig::QmOutlineColorKill);
	OutlineType("tele", s_aOutlineButtonContainers[4], Localize("Tele"), g_Config.m_QmOutlineTele, g_Config.m_QmOutlineWidthTele, g_Config.m_QmOutlineColorTele, DefaultConfig::QmOutlineColorTele);
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}

float CMenus::LayoutTClientGhostToolsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, CUIRect &Content, bool Render)
{
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("show-others-ghosts", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowOthersGhosts, "tclient-show-others-ghosts", Localize("Show unpredicted ghosts for other players"), &g_Config.m_QmShowOthersGhosts, &Row, LineSize);
	}
	if(Flow.Control("swap-ghosts", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmSwapGhosts, "tclient-swap-ghosts", Localize("Swap ghosts with regular players"), &g_Config.m_QmSwapGhosts, &Row, LineSize);
	}
	if(Flow.Control("predicted-ghost-opacity", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-predicted-ghost-opacity", &g_Config.m_QmPredGhostsAlpha, &g_Config.m_QmPredGhostsAlpha, &Row, Localize("Predicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	if(Flow.Control("unpredicted-ghost-opacity", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-unpredicted-ghost-opacity", &g_Config.m_QmUnpredGhostsAlpha, &g_Config.m_QmUnpredGhostsAlpha, &Row, Localize("Unpredicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}
	if(Flow.Control("hide-frozen-ghosts", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmHideFrozenGhosts, "tclient-hide-frozen-ghosts", Localize("Hide ghosts of frozen players"), &g_Config.m_QmHideFrozenGhosts, &Row, LineSize);
	}
	if(Flow.Control("render-ghost-as-circle", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRenderGhostAsCircle, "tclient-render-ghost-as-circle", Localize("Render ghosts as circles"), &g_Config.m_QmRenderGhostAsCircle, &Row, LineSize);
	}
	if(Flow.Control("toggle-ghost-key", LineSize, Row, MarginSmall))
	{
		static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
		DoLine_KeyReader(Row, s_ReaderButtonGhost, s_ClearButtonGhost, Localize("Toggle ghost key"), "toggle qm_show_others_ghosts 0 1");
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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("rainbow-tees", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowTees, "tclient-rainbow-tees", Localize("Rainbow Tees"), &g_Config.m_QmRainbowTees, &Row, LineSize);
	}
	if(Flow.Control("rainbow-weapons", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowWeapon, "tclient-rainbow-weapons", Localize("Rainbow weapons"), &g_Config.m_QmRainbowWeapon, &Row, LineSize);
	}
	if(Flow.Control("rainbow-hook", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowHook, "tclient-rainbow-hook", Localize("Rainbow hook"), &g_Config.m_QmRainbowHook, &Row, LineSize);
	}
	if(Flow.Control("rainbow-others", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowOthers, "tclient-rainbow-others", Localize("Rainbow others"), &g_Config.m_QmRainbowOthers, &Row, LineSize);
	}
	if(Flow.Control("rainbow-mode", LineSize, Row, MarginSmall))
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
	if(Flow.Control("rainbow-speed", LineSize, Row, MarginSmall))
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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("tee-trail-enabled", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrail, "tclient-tee-trail-enabled", Localize("Enable tee trails"), &g_Config.m_QmTeeTrail, &Row, LineSize);
	}
	if(Flow.Control("tee-trail-others", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailOthers, "tclient-tee-trail-others", Localize("Show other tees' trails"), &g_Config.m_QmTeeTrailOthers, &Row, LineSize);
	}
	if(Flow.Control("tee-trail-fade", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailFade, "tclient-tee-trail-fade", Localize("Fade trail alpha"), &g_Config.m_QmTeeTrailFade, &Row, LineSize);
	}
	if(Flow.Control("tee-trail-taper", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailTaper, "tclient-tee-trail-taper", Localize("Taper trail width"), &g_Config.m_QmTeeTrailTaper, &Row, LineSize);
	}
	if(Flow.Control("tee-trail-style-colors", LineSize, Row, MarginSmall))
	{
		DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmTeeTrailStyleColors, "tclient-tee-trail-style-colors", Localize("Use style colors"), &g_Config.m_QmTeeTrailStyleColors, &Row, LineSize);
	}
	if(Flow.Control("color-mode", LineSize, Row, MarginSmall))
	{
		const char *apNames[] = {Localize("Solid"), Localize("Tee"), Localize("Rainbow"), Localize("Speed")};
		s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
		const int Old = g_Config.m_QmTeeTrailColorMode - 1;
		const int New = DoSettingsDropDown(&Row, Old, apNames, std::size(apNames), s_TrailDropDownState, {}, &g_Config.m_QmTeeTrailColorMode);
		if(Flow.Input() && !Ui()->RenderOnly() && New != Old)
			g_Config.m_QmTeeTrailColorMode = New + 1;
	}
	if(Flow.Control("trail-style", LineSize, Row, MarginSmall))
	{
		const char *apNames[] = {Localize("Original"), Localize("Manga: Ink Slash"), Localize("Magic: Spirit Script"), Localize("Pixel: Layer Shift")};
		s_TrailStyleDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailStyleDropDownScrollRegion;
		const int Old = qm_tee_trail::ResolveStyle(g_Config.m_QmTeeTrailStyle);
		const int New = DoSettingsDropDown(&Row, Old, apNames, std::size(apNames), s_TrailStyleDropDownState, {}, &g_Config.m_QmTeeTrailStyle);
		if(Flow.Input() && !Ui()->RenderOnly() && New != Old)
			g_Config.m_QmTeeTrailStyle = New;
	}
	if(g_Config.m_QmTeeTrailColorMode == CTrails::COLORMODE_SOLID)
	{
		if(Flow.Control("solid-color", LineSize, Row, MarginSmall))
		{
			static CButtonContainer s_TeeTrailColor;
			DoLine_ColorPicker(&s_TeeTrailColor, CurrentSettingsContentMetrics(), &Row, Localize("Tee trail color"), &g_Config.m_QmTeeTrailColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		}
	}
	if(Flow.Control("tee-trail-width", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-width", &g_Config.m_QmTeeTrailWidth, &g_Config.m_QmTeeTrailWidth, &Row, Localize("Trail width"), 0, 20);
	}
	if(Flow.Control("tee-trail-length", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-length", &g_Config.m_QmTeeTrailLength, &g_Config.m_QmTeeTrailLength, &Row, Localize("Trail length"), 0, 200);
	}
	if(Flow.Control("tee-trail-alpha", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-tee-trail-alpha", &g_Config.m_QmTeeTrailAlpha, &g_Config.m_QmTeeTrailAlpha, &Row, Localize("Trail alpha"), 0, 100);
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
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	const float StartY = Content.y;
	std::vector<SSettingsContentRowDeclaration> vRows;
	std::optional<CSettingsContentLayoutScope> MeasureScope;
	if(!Render && CSettingsContentLayoutScope::Current() == nullptr)
		MeasureScope.emplace(vRows);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("color", LineSize, Row, MarginSmall))
	{
		static CButtonContainer s_BgDrawColor;
		DoLine_ColorPicker(&s_BgDrawColor, CurrentSettingsContentMetrics(), &Row, Localize("Color"), &g_Config.m_QmBgDrawColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	}
	if(Flow.Control("fade-time", LineSize, Row, MarginSmall))
	{
		if(g_Config.m_QmBgDrawFadeTime == 0)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_QmBgDrawFadeTime, &g_Config.m_QmBgDrawFadeTime, &Row, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds (never)"));
		else
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-fade-time", &g_Config.m_QmBgDrawFadeTime, &g_Config.m_QmBgDrawFadeTime, &Row, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, 0, Localize(" seconds"));
	}
	if(Flow.Control("width", LineSize, Row, MarginSmall))
	{
		DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-bg-draw-width", &g_Config.m_QmBgDrawWidth, &g_Config.m_QmBgDrawWidth, &Row, Localize("Width"), 1, 50);
	}
	if(Flow.Control("draw-key", LineSize, Row, MarginSmall))
	{
		static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
		DoLine_KeyReader(Row, s_ReaderButtonDraw, s_ClearButtonDraw, Localize("Draw where mouse is"), "+bg_draw");
	}
	if(MeasureScope)
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve(vRows, Content.w, 0, {})->Height();
	}
	return Content.y - StartY;
}
