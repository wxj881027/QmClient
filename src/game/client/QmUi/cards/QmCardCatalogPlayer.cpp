/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsIconOptions.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/message_gradient.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/tee_color_code.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/qmclient/tee_skin_apply.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
	void LogPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}
}

uint64_t CMenus::BuildPlayerSettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards)
{
	const CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	CPerfTimer RenderTimer;

	const SSettingsContentMetrics PlayerMetrics = Ctx.m_Metrics;
	const float UiScale = PlayerMetrics.m_UiScale;
	const float BodySize = PlayerMetrics.m_BodySize;
	const IUiContext PlayerCardCtx = Ctx.m_UiContext;
	const qm_card_registry::SCardDefault *pIdentityDefault = qm_card_registry::FindByStableId("deck:player-identity");
	const qm_card_registry::SCardDefault *pCountryDefault = qm_card_registry::FindByStableId("deck:player-country");
	dbg_assert(pIdentityDefault != nullptr && pCountryDefault != nullptr, "player settings cards must be registered");
	if(pIdentityDefault == nullptr || pCountryDefault == nullptr)
		return 0;

	// 子 Tab 已由设置壳层的 Card Deck 统一处理入场；页面内部不再叠加横向位移动效。
	const auto DrawAnimatedContent = [](CUIRect Content, auto &&DrawContent) { DrawContent(Content); };

	int *pCountry = m_Dummy ? &g_Config.m_ClDummyCountry : &g_Config.m_PlayerCountry;
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	const IUiContext PlayerIdentityTextInputCtx = SettingsUiContext("settings_player_identity_text_inputs", UiScale);
	if(!m_Dummy)
	{
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	static CLineInputBuffered<25> s_FlagFilterInput;
	class CCountryFlagEntry
	{
	public:
		const CCountryFlags::CCountryFlag *m_pFlag;
		std::optional<std::pair<int, int>> m_NameMatch;
	};
	static std::vector<CCountryFlagEntry> s_vFilteredFlags;
	s_vFilteredFlags.clear();
	s_vFilteredFlags.reserve(GameClient()->m_CountryFlags.Num());
	for(size_t i = 0; i < GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = GameClient()->m_CountryFlags.GetByIndex(i);
		if(!s_FlagFilterInput.IsEmpty())
		{
			const char *pNameMatchEnd;
			const char *pNameMatchStart = str_utf8_find_nocase(Entry.m_aCountryCodeString, s_FlagFilterInput.GetString(), &pNameMatchEnd);
			if(pNameMatchStart != nullptr)
			{
				s_vFilteredFlags.emplace_back(&Entry, std::make_pair<int, int>(pNameMatchStart - Entry.m_aCountryCodeString, pNameMatchEnd - pNameMatchStart));
			}
		}
		else
		{
			s_vFilteredFlags.emplace_back(&Entry, std::nullopt);
		}
	}
	const IUiContext PlayerFlagSearchCtx = SettingsUiContext("settings_player_flag_search", UiScale);

	const bool RenderOnly = Ctx.m_ReadOnly;
	const auto BuildDefinitions = [this, pIdentityDefault, pCountryDefault, DrawAnimatedContent, PlayerIdentityTextInputCtx, PlayerFlagSearchCtx, pCountry, UiScale, BodySize, PlayerMetrics](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(2);
		const SSettingsCardSpec IdentitySpec{pIdentityDefault->m_pStableId, Localize(pIdentityDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pIdentityDefault)};
		const SSettingsCardSpec CountrySpec{pCountryDefault->m_pStableId, Localize(pCountryDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pCountryDefault)};
		const auto AddCard = [&vCards](const SSettingsCardSpec &Spec, float ContentHeight, FSettingsCardRender Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Measure = [ContentHeight](float) { return ContentHeight; };
			Definition.m_Render = Render;
			vCards.push_back(Definition);
		};
		AddCard(IdentitySpec, ResolveSettingsRowsHeight(2, PlayerMetrics.m_LineHeight, PlayerMetrics.m_LineSpacing), [this, DrawAnimatedContent, PlayerIdentityTextInputCtx, UiScale, BodySize, PlayerMetrics](CUIRect Content) {
			CUIRect Label, NameRow, ClanRow;
			char aBuf[128];
			Content.HSplitTop(PlayerMetrics.m_LineHeight, &NameRow, &Content);
			DrawAnimatedContent(NameRow, [this, &Label, &PlayerIdentityTextInputCtx, &aBuf, UiScale, BodySize](CUIRect Row) {
				Row.VSplitLeft(80.0f * UiScale, &Label, &Row);
				Row.VSplitLeft(150.0f * UiScale, &Row, nullptr);
				str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
				Ui()->DoLabel(&Label, aBuf, BodySize, TEXTALIGN_ML);
				if(ui_widget::InputField(PlayerIdentityTextInputCtx, &s_NameInput, Row, Client()->PlayerName(), BodySize))
					SetNeedSendInfo(m_Dummy);
			});
			Content.HSplitTop(PlayerMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(PlayerMetrics.m_LineHeight, &ClanRow, &Content);
			DrawAnimatedContent(ClanRow, [this, &Label, &PlayerIdentityTextInputCtx, &aBuf, UiScale, BodySize](CUIRect Row) {
				Row.VSplitLeft(80.0f * UiScale, &Label, &Row);
				Row.VSplitLeft(150.0f * UiScale, &Row, nullptr);
				str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
				Ui()->DoLabel(&Label, aBuf, BodySize, TEXTALIGN_ML);
				if(ui_widget::InputField(PlayerIdentityTextInputCtx, &s_ClanInput, Row, "", BodySize))
					SetNeedSendInfo();
			});
		});
		AddCard(CountrySpec, 520.0f * UiScale, [this, pCountry, PlayerFlagSearchCtx, UiScale, BodySize](CUIRect Content) {
			const bool MenuUiPerfEnabled = QmPerfEnabled();
			const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
			CUIRect QuickSearch, Label;
			Content.HSplitBottom(20.0f * UiScale, &Content, &QuickSearch);
			Content.HSplitBottom(5.0f * UiScale, &Content, nullptr);
			QuickSearch.VSplitLeft(minimum(220.0f * UiScale, QuickSearch.w), &QuickSearch, nullptr);
			int SelectedOld = -1;
			static CListBox s_ListBox;
			s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);
			s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_ListBox.DoStart(48.0f * UiScale, s_vFilteredFlags.size(), 10, 2, SelectedOld, &Content);
			int VisibleFlags = 0;
			for(size_t i = 0; i < s_vFilteredFlags.size(); i++)
			{
				const CCountryFlagEntry &Entry = s_vFilteredFlags[i];
				if(Entry.m_pFlag->m_CountryCode == *pCountry)
					SelectedOld = i;
				const CListboxItem Item = s_ListBox.DoNextItem(&Entry.m_pFlag->m_CountryCode, SelectedOld >= 0 && (size_t)SelectedOld == i);
				if(!Item.m_Visible)
					continue;
				++VisibleFlags;
				CUIRect FlagRect;
				Item.m_Rect.Margin(5.0f * UiScale, &FlagRect);
				FlagRect.HSplitBottom(12.0f * UiScale, &FlagRect, &Label);
				Label.HSplitTop(2.0f * UiScale, nullptr, &Label);
				const float OldWidth = FlagRect.w;
				FlagRect.w = FlagRect.h * 2.0f;
				FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
				GameClient()->m_CountryFlags.Render(Entry.m_pFlag->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
				if(Entry.m_pFlag->m_Texture.IsValid() || Entry.m_pFlag->m_CountryCode == -1)
				{
					SLabelProperties Props;
					Props.m_MaxWidth = Label.w - 5.0f;
					if(Entry.m_NameMatch.has_value())
					{
						const auto [MatchStart, MatchLength] = Entry.m_NameMatch.value();
						Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
					}
					Ui()->DoLabel(&Label, Entry.m_pFlag->m_aCountryCodeString, 10.0f * UiScale, TEXTALIGN_MC, Props);
				}
			}
			const int NewSelected = s_ListBox.DoEnd();
			const bool FlagListScrollActive = QmMenuUiScrollPerfActive(s_ListBox.WheelConsumedThisFrame(), s_ListBox.ScrollbarActive(), s_ListBox.ScrollbarAnimating());
			if(FlagListScrollActive)
			{
				StartSettingsPerfScrollWindow("flags_grid_scroll", SettingsPerfContextName(), "settings:player", "none");
				SQmMenuUiFramePerf MenuUiPerf;
				MenuUiPerf.m_pPage = "settings:player";
				MenuUiPerf.m_pOperation = "flags_grid_scroll";
				MenuUiPerf.m_ItemsTotal = (int)s_vFilteredFlags.size();
				MenuUiPerf.m_ItemsVisible = VisibleFlags;
				MenuUiPerf.m_ItemsProcessed = VisibleFlags;
				MenuUiPerf.m_ItemsSkipped = maximum(0, (int)s_vFilteredFlags.size() - VisibleFlags);
				MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? (float)std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0f;
				QmLogMenuUiFramePerf(MenuUiPerf, Client());
			}
			if(SelectedOld != NewSelected && NewSelected >= 0 && NewSelected < (int)s_vFilteredFlags.size())
			{
				*pCountry = s_vFilteredFlags[NewSelected].m_pFlag->m_CountryCode;
				SetNeedSendInfo();
			}
			ui_widget::SInputFieldOptions FlagSearchOptions;
			FlagSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
			FlagSearchOptions.m_Clearable = true;
			FlagSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
			FlagSearchOptions.m_FontSize = BodySize;
			ui_widget::InputField(PlayerFlagSearchCtx, &s_FlagFilterInput, QuickSearch, FlagSearchOptions);
		});
	};
	const uint64_t PlayerLayoutRevision =
		((uint64_t)(RenderOnly ? 1 : 0) << 63) |
		((uint64_t)(m_Dummy ? 1 : 0) << 62);
	if(pCards != nullptr)
	{
		BuildDefinitions(*pCards);
	}
	return PlayerLayoutRevision;
}
