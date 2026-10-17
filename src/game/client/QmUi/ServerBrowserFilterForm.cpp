/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SecondaryPanel.h"
#include "ServerBrowserFilterLayout.h"
#include "SettingsPageLayout.h"
#include "UiForms.h"

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/countryflags.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderServerbrowserFilterForm(CUIRect &View)
{
	SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Ui()->Screen()->w);
	// 沿用浏览器原有字号与行高，短按钮通过横排节省空间。
	Metrics.m_LineHeight = 18.0f;
	Metrics.m_BodySize = 14.0f * CUi::ms_FontmodHeight;
	Metrics.m_LineSpacing = 3.0f;
	Metrics.m_InputHeight = Metrics.m_LineHeight;
	Metrics.m_SectionGap = 6.0f;
	const auto Entry = [](int *pValue, const char *pTextId) {
		SSettingsToggleEntry Result{pValue, pTextId, Localize(pTextId)};
		Result.m_ShowCheckmark = true;
		return Result;
	};
	const auto LabelHeight = [&](const char *pText) {
		return std::max(Metrics.m_BodySize, TextRender()->TextBoundingBox(Metrics.m_BodySize, pText, -1, std::max(1.0f, View.w)).m_H);
	};
	const auto DrawLabel = [&](const char *pText) {
		CUIRect Label;
		View.HSplitTop(LabelHeight(pText), &Label, &View);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EnableWidthCheck = false;
		Props.m_MinimumFontSize = Metrics.m_BodySize;
		Ui()->DoLabel(&Label, pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
		View.HSplitTop(Metrics.m_LineSpacing, nullptr, &View);
	};
	const auto DrawSection = [&](const char *pText) {
		View.HSplitTop(Metrics.m_SectionGap, nullptr, &View);
		DrawLabel(pText);
	};

	DrawLabel(Localize("Common filters"));
	DoSettingsToggleGroup(-1, -1, -1, View, {
							Entry(&g_Config.m_BrFilterEmpty, Localizable("Has people playing")),
							Entry(&g_Config.m_BrFilterFull, Localizable("Server not full")),
							Entry(&g_Config.m_BrFilterFriends, Localizable("Show friends only")),
							Entry(&g_Config.m_BrFilterPw, Localizable("No password")),
							Entry(&g_Config.m_BrFilterLogin, Localizable("No login required")),
						},
		Metrics);

	View.HSplitTop(Metrics.m_SectionGap, nullptr, &View);
	const char *pGameTypes = Localize("Game types:");
	const SSettingsToggleEntry Strict = Entry(&g_Config.m_BrFilterGametypeStrict, Localizable("Strict gametype filter"));
	const auto StrictSize = MeasureServerBrowserFilterStrictButton(*TextRender(), Strict.m_pLabel, Metrics);
	// 标题与严格匹配只有在完整放得下时同行，长译文使用独立按钮行。
	if(TextRender()->TextWidth(Metrics.m_BodySize, pGameTypes) + Metrics.m_LineSpacing + StrictSize.m_Width <= View.w)
	{
		CUIRect Header, StrictButton, Label;
		View.HSplitTop(StrictSize.m_Height, &Header, &View);
		Header.VSplitRight(StrictSize.m_Width, &Label, &StrictButton);
		Ui()->DoLabel(&Label, pGameTypes, Metrics.m_BodySize, TEXTALIGN_ML);
		DoSettingsToggleButton(-1, -1, -1, StrictButton, Strict, Metrics);
		View.HSplitTop(Metrics.m_LineSpacing, nullptr, &View);
	}
	else
	{
		DrawLabel(pGameTypes);
		DoSettingsToggleGroup(-1, -1, -1, View, {Strict}, Metrics);
	}
	CUIRect Input;
	View.HSplitTop(Metrics.m_InputHeight, &Input, &View);
	static CLineInput s_GametypeInput(g_Config.m_BrFilterGametype, sizeof(g_Config.m_BrFilterGametype));
	ui_widget::SInputFieldOptions InputOptions;
	InputOptions.m_FontSize = Metrics.m_BodySize;
	if(ui_widget::InputField(SettingsUiContext("server_browser_filter_game_type"), &s_GametypeInput, Input, InputOptions).m_Changed)
		Client()->ServerBrowserUpdate();

	DrawSection(Localize("Server address:"));
	View.HSplitTop(Metrics.m_InputHeight, &Input, &View);
	static CLineInput s_ServerAddressInput(g_Config.m_BrFilterServerAddress, sizeof(g_Config.m_BrFilterServerAddress));
	if(ui_widget::InputField(SettingsUiContext("server_browser_filter_address"), &s_ServerAddressInput, Input, InputOptions).m_Changed)
		Client()->ServerBrowserUpdate();

	View.HSplitTop(Metrics.m_SectionGap, nullptr, &View);
	{
		const SSettingsToggleEntry Country = Entry(&g_Config.m_BrFilterCountry, Localizable("Player country:"));
		const float FlagWidth = Metrics.m_LineHeight * 2.0f;
		CUIRect CountryContent = View;
		CountryContent.w = std::max(0.0f, View.w - FlagWidth - Metrics.m_LineSpacing);
		const float Top = View.y;
		DoSettingsToggleGroup(-1, -1, -1, CountryContent, {Country}, Metrics);
		CUIRect Row;
		View.HSplitTop(CountryContent.y - Top, &Row, &View);
		CUIRect Flag{Row.x + Row.w - FlagWidth, Row.y, FlagWidth, Metrics.m_LineHeight};
		GameClient()->m_CountryFlags.Render(g_Config.m_BrFilterCountryIndex, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &g_Config.m_BrFilterCountryIndex ? 1.0f : (g_Config.m_BrFilterCountry ? 0.9f : 0.5f)), Flag.x, Flag.y, Flag.w, Flag.h);
		if(Ui()->DoButtonLogic(&g_Config.m_BrFilterCountryIndex, 0, &Flag, BUTTONFLAG_LEFT))
		{
			static SPopupMenuId s_PopupCountryId;
			static SPopupCountrySelectionContext s_PopupCountryContext;
			s_PopupCountryContext.m_pMenus = this;
			s_PopupCountryContext.m_Selection = g_Config.m_BrFilterCountryIndex;
			s_PopupCountryContext.m_New = true;
			const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*Ui()->Screen());
			Ui()->DoPopupMenu(&s_PopupCountryId, Flag.x, Flag.y + Flag.h, PanelRect.w, PanelRect.h, &s_PopupCountryContext, PopupCountrySelection, ui_widget::SecondaryPanelProperties());
		}
	}

	DrawSection(Localize("Player count"));
	DoSettingsToggleGroup(-1, -1, -1, View, {
							Entry(&g_Config.m_BrFilterSpectators, Localizable("Count players only")),
							Entry(&g_Config.m_BrFilterConnectingPlayers, Localizable("Filter connecting players")),
						},
		Metrics);

	if(ServerBrowser()->CommunityCache().AnyRanksAvailable())
	{
		DrawSection(Localize("Map progress"));
		const bool WasIndicatingFinished = g_Config.m_BrIndicateFinished != 0;
		const SSettingsToggleEntry Finished = Entry(&g_Config.m_BrIndicateFinished, Localizable("Indicate map finish"));
		if(WasIndicatingFinished)
			DoSettingsToggleGroup(-1, -1, -1, View, {Finished, Entry(&g_Config.m_BrFilterUnfinishedMap, Localizable("Unfinished map"))}, Metrics);
		else
			DoSettingsToggleGroup(-1, -1, -1, View, {Finished}, Metrics);
		if(!g_Config.m_BrIndicateFinished)
			g_Config.m_BrFilterUnfinishedMap = 0;
		else if(!WasIndicatingFinished)
			ServerBrowser()->Refresh(ServerBrowser()->GetCurrentType());
	}
}
