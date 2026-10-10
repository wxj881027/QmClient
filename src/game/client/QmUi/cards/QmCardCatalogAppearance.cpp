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
#include <game/client/components/qmclient/chat_gradient.h>
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

uint64_t CMenus::BuildAppearanceSettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards, int AppearanceTab)
{
	const SSettingsPageLayoutFrame AppearancePage = Ctx.m_Page;
	const CUIRect MainView = AppearancePage.m_ContentViewport;
	CUIRect LeftView, RightView, Button;
	const SSettingsContentMetrics AppearanceMetrics = Ctx.m_Metrics;
	const float AppearanceUiScale = AppearanceMetrics.m_UiScale;
	const float AppearanceBodySize = AppearanceMetrics.m_BodySize;

	const float LineSize = AppearanceMetrics.m_LineHeight;
	const float ColorPickerRowHeight = AppearanceMetrics.m_ButtonHeight + AppearanceMetrics.m_LineSpacing;
	const float HeadlineFontSize = AppearanceMetrics.m_HeadlineSize;
	const float HeadlineHeight = AppearanceMetrics.m_LineHeight + AppearanceMetrics.m_LineSpacing * 2.0f;
	const float MarginSmall = AppearanceMetrics.m_LineSpacing;
	const float MarginBetweenViews = AppearanceMetrics.m_SectionGap * 2.0f;

	CUIRect ContentView = MainView;
	auto DoAppearanceHeading = [this, AppearanceTab](CUIRect &View, const char *pTextId, const char *pText, float FontSize, float LineHeight) {
		CUIRect Heading;
		View.HSplitTop(LineHeight, &Heading, &View);
		CUIElement &HeadingElement = SettingsTextElement(SETTINGS_APPEARANCE, AppearanceTab, pTextId);
		DoSettingsLabelStreamed(HeadingElement, &Heading, pText, FontSize, TEXTALIGN_ML);
	};
	const IUiContext AppearanceCardCtx = Ctx.m_UiContext;
	const auto DoAppearanceNumericField = [this, AppearanceCardCtx, AppearanceBodySize](int Tab, const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u, const char *pSuffix = "", const char *pMaxText = nullptr) {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_pScale = pScale;
		Options.m_Flags = Flags;
		Options.m_pMaxText = pMaxText;
		Options.m_FontSize = AppearanceBodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_APPEARANCE, Tab, -1, pTextId, Rect, pLabel, Flags, Options))
			return false;
		return ui_widget::NumericField(AppearanceCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
	};
	const char *const aAppearanceIds[] = {
		"deck:appearance-hud-main", "deck:appearance-hud-ddrace", "deck:appearance-chat-settings", "deck:appearance-chat-messages", "deck:appearance-chat-preview", "deck:appearance-name-plate-settings", "deck:appearance-name-plate-preview", "deck:appearance-hook-collision-main", "deck:appearance-hook-collision-preview", "deck:appearance-info-messages", "deck:appearance-laser-enhanced", "deck:appearance-laser-colors", "deck:appearance-laser-preview"};
	std::array<const qm_card_registry::SCardDefault *, std::size(aAppearanceIds)> aAppearanceDefaults{};
	for(size_t i = 0; i < std::size(aAppearanceIds); ++i)
	{
		aAppearanceDefaults[i] = qm_card_registry::FindByStableId(aAppearanceIds[i]);
		dbg_assert(aAppearanceDefaults[i] != nullptr, "appearance settings card must be registered");
		if(aAppearanceDefaults[i] == nullptr)
			return 0;
	}
	const auto CardSpec = [aAppearanceDefaults](size_t Index) { return SSettingsCardSpec{aAppearanceDefaults[Index]->m_pStableId, Localize(aAppearanceDefaults[Index]->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*aAppearanceDefaults[Index])}; };
	const bool RenderOnly = Ctx.m_ReadOnly;
	const auto ResolveNamePlatePreviewMeasureRevision = [this]() {
		const int aInputs[] = {
			g_Config.m_QmNameplateShowScope,
			g_Config.m_ClNamePlatesClan,
			g_Config.m_ClNamePlatesFriendMark,
			g_Config.m_ClNamePlatesIds,
			g_Config.m_ClNamePlatesIdsSeparateLine,
			g_Config.m_ClNamePlatesStrong,
			g_Config.m_Debug,
			g_Config.m_ClNamePlatesSize,
			g_Config.m_ClNamePlatesClanSize,
			g_Config.m_ClNamePlatesIdsSize,
			g_Config.m_ClNamePlatesCoordsSize,
			g_Config.m_ClDirectionSize,
			g_Config.m_ClNamePlatesStrongSize,
			g_Config.m_QmNameplateCoords,
			g_Config.m_QmNameplateCoordsOwn,
			g_Config.m_QmNameplateCoordX,
			g_Config.m_QmNameplateCoordY,
			g_Config.m_ClShowDirection,
			g_Config.m_QmNameplateHookStrongWeakScope,
			g_Config.m_QmNameplateFreeMove,
			g_Config.m_QmNameplateFreeMoveX,
			g_Config.m_QmNameplateFreeMoveY,
			g_Config.m_ClDummy,
			g_Config.m_ClNamePlatesOffset,
			g_Config.m_QmNameplateNameOffsetX,
			g_Config.m_QmNameplateNameOffsetY,
			g_Config.m_QmNameplateClanOffsetX,
			g_Config.m_QmNameplateClanOffsetY,
			g_Config.m_QmNameplateHookOffsetX,
			g_Config.m_QmNameplateHookOffsetY,
			g_Config.m_QmNameplateCoordsOffsetX,
			g_Config.m_QmNameplateCoordsOffsetY,
			g_Config.m_QmNameplateKeysOffsetX,
			g_Config.m_QmNameplateKeysOffsetY,
			g_Config.m_ClNamePlatesTeamcolors,
			g_Config.m_QmNameplateTextEffects,
			g_Config.m_QmNameplateTextBorderRange,
			g_Config.m_QmNameplateTextGlowRange,
		};
		uint64_t Revision = 1469598103934665603ull;
		for(const int Input : aInputs)
			Revision = (Revision ^ static_cast<uint64_t>(static_cast<uint32_t>(Input))) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_QmCustomFont)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_QmCustomFontCjk)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_QmCustomFontIcons)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(Client()->PlayerName())) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(Client()->DummyName())) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_PlayerClan)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_ClDummyClan)) * 1099511628211ull;
		return Revision;
	};
	const uint64_t NamePlatePreviewMeasureRevision = AppearanceTab == APPEARANCE_TAB_NAME_PLATE ? ResolveNamePlatePreviewMeasureRevision() : 0;
	const auto HudCoreToggles = [this, AppearanceMetrics, RenderOnly](CUIRect &Content, ESettingsToggleGroupPass Pass = ESettingsToggleGroupPass::RENDER) {
		return DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, APPEARANCE_TAB_HUD, Content,
			{
				{&g_Config.m_ClShowhudHealthAmmo, "appearance-show-health-shields-ammo", Localize("Show health, shields and ammo")},
				{&g_Config.m_ClShowhudScore, "appearance-show-score", Localize("Show score")},
				{&g_Config.m_ClShowLocalTimeAlways, "appearance-show-local-time-always", Localize("Show local time always")},
				{&g_Config.m_ClSpecCursor, "appearance-show-spectator-cursor", Localize("Show spectator cursor")},
				{&g_Config.m_ClShowVotesAfterVoting, "appearance-show-votes-after-voting", Localize("Show votes window after voting")},
			},
			AppearanceMetrics, !RenderOnly, Pass);
	};
	const auto HudStyleToggles = [this, AppearanceMetrics, RenderOnly](CUIRect &Content, ESettingsToggleGroupPass Pass = ESettingsToggleGroupPass::RENDER) {
		return DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, APPEARANCE_TAB_HUD, Content,
			{
				{&g_Config.m_ClHudRainbowColors, "appearance-hud-rainbow-colors", Localize("HUD rainbow colors")},
				{&g_Config.m_ClShowhudJumpsIndicator, "appearance-show-jumps-indicator", Localize("Show jumps indicator")},
			},
			AppearanceMetrics, !RenderOnly, Pass);
	};
	const auto HudStatusToggles = [this, AppearanceMetrics, RenderOnly](CUIRect &Content, ESettingsToggleGroupPass Pass = ESettingsToggleGroupPass::RENDER) {
		return DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, APPEARANCE_TAB_HUD, Content,
			{
				{&g_Config.m_ClShowhudSpectatorCount, "appearance-show-spectator-count", Localize("Show number of spectators")},
				{&g_Config.m_ClShowhudDummyActions, "appearance-show-dummy-actions", Localize("Show dummy actions")},
			},
			AppearanceMetrics, !RenderOnly, Pass);
	};
	const auto HudPositionToggles = [this, AppearanceMetrics, RenderOnly](CUIRect &Content, ESettingsToggleGroupPass Pass = ESettingsToggleGroupPass::RENDER) {
		return DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, APPEARANCE_TAB_HUD, Content,
			{
				{&g_Config.m_ClShowhudPlayerPosition, "appearance-show-player-position", Localize("Show player position")},
				{&g_Config.m_ClShowhudPlayerSpeed, "appearance-show-player-speed", Localize("Show player speed")},
				{&g_Config.m_ClShowhudPlayerAngle, "appearance-show-player-target-angle", Localize("Show player target angle")},
			},
			AppearanceMetrics, !RenderOnly, Pass);
	};
	const auto ChatDisplayToggles = [this, AppearanceMetrics, RenderOnly](CUIRect &Content, ESettingsToggleGroupPass Pass = ESettingsToggleGroupPass::RENDER) {
		return DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, APPEARANCE_TAB_CHAT, Content,
			{
				{&g_Config.m_ClChatTeamColors, "appearance-chat-team-colors", Localize("Show names in chat in team colors")},
				{&g_Config.m_ClShowChatFriends, "appearance-chat-friends-only", Localize("Show only chat messages from friends")},
				{&g_Config.m_ClShowChatTeamMembersOnly, "appearance-chat-team-members-only", Localize("Show only chat messages from team members")},
			},
			AppearanceMetrics, !RenderOnly, Pass);
	};
	const auto ChatStorageToggles = [this, AppearanceMetrics, RenderOnly](CUIRect &Content, ESettingsToggleGroupPass Pass = ESettingsToggleGroupPass::RENDER) {
		return DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, APPEARANCE_TAB_CHAT, Content,
			{
				{&g_Config.m_QmChatSaveDraft, "appearance-chat-save-draft", Localize("Save unsent chat draft")},
				{&g_Config.m_QmChatLogAutoSave, "appearance-chat-log-auto-save", Localize("Auto save chat log")},
			},
			AppearanceMetrics, !RenderOnly, Pass);
	};

	const auto BuildDefinitions = [=, this](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(std::size(aAppearanceIds));
		const auto AddCard = [&vCards, &CardSpec](size_t Index, float ContentHeight, FSettingsCardRender Render) {
			const SSettingsCardSpec Spec = CardSpec(Index);
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Measure = [ContentHeight](float) { return maximum(0.0f, ContentHeight); };
			Definition.m_Render = std::move(Render);
			Definition.m_MeasureRevision = static_cast<uint64_t>(maximum(0.0f, ContentHeight) * 1000.0f);
			vCards.push_back(std::move(Definition));
		};
		const auto AddMeasuredCard = [&vCards, &CardSpec](size_t Index, FSettingsCardMeasure Measure, FSettingsCardRender Render, uint64_t MeasureRevision) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = CardSpec(Index);
			Definition.m_Measure = std::move(Measure);
			Definition.m_Render = std::move(Render);
			Definition.m_MeasureRevision = MeasureRevision;
			vCards.push_back(std::move(Definition));
		};

		const auto AddToggleCard = [this, &vCards, &CardSpec](size_t Index, const FSettingsCardRenderMeasured &Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = CardSpec(Index);
			Definition.m_RenderMeasured = Render;
			Definition.m_Render = [Render](CUIRect Content) { Render(Content); };
			Definition.m_Measure = [this, Render](float Width) { return qm_card_catalog::QmCardRenderHook::MeasureContent(this, Render, Width); };
			vCards.push_back(std::move(Definition));
		};

		if(AppearanceTab == APPEARANCE_TAB_HUD)
		{
			CPerfTimer HudShellTimer;
			LogPerfStage(Client(), "appearance_hud_text_cache", HudShellTimer.ElapsedMs(), false, "page=appearance tab=hud section=text_cache");
			LogPerfStage(Client(), "appearance_hud_tab_shell", HudShellTimer.ElapsedMs(), false, "page=appearance tab=hud");
			AddToggleCard(0, [=, this](CUIRect &ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				CPerfTimer HudCoreTimer;
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhud, "appearance-show-ingame-hud", Localize("Show ingame HUD"), &g_Config.m_ClShowhud, &LeftView, LineSize, MarginSmall, AppearanceBodySize);
				HudCoreToggles(LeftView);
				LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
				CUIRect ScoreboardTitle;
				LeftView.HSplitTop(HeadlineHeight, &ScoreboardTitle, &LeftView);
				CUIElement &ScoreboardTitleText = SettingsTextElement(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, "appearance-scoreboard-title");
				DoSettingsLabelStreamed(ScoreboardTitleText, &ScoreboardTitle, Localize("Scoreboard"), HeadlineFontSize, TEXTALIGN_ML);
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
				ColorRGBA GreenDefault(0.78f, 1.0f, 0.8f, 1.0f);
				static CButtonContainer s_AuthedColor, s_SameClanColor, s_FriendsListFriendColor, s_FriendsListClanColor;
				DoLine_ColorPicker(&s_AuthedColor, AppearanceMetrics, &LeftView, Localize("Authed name color in scoreboard"), &g_Config.m_ClAuthedPlayerColor, GreenDefault, false);
				DoLine_ColorPicker(&s_SameClanColor, AppearanceMetrics, &LeftView, Localize("Same clan color in scoreboard"), &g_Config.m_ClSameClanColor, GreenDefault, false);
				DoLine_ColorPicker(&s_FriendsListFriendColor, AppearanceMetrics, &LeftView, Localize("Friend color in friends list"), &g_Config.m_ClFriendsListFriendColor, ColorRGBA(0.949f, 0.806f, 0.368f), false);
				DoLine_ColorPicker(&s_FriendsListClanColor, AppearanceMetrics, &LeftView, Localize("Clan color in friends list"), &g_Config.m_ClFriendsListClanColor, ColorRGBA(0.336f, 0.231f, 0.867f), false);
				ContentRect = LeftView;
				LogPerfStage(Client(), "appearance_hud_core_section", HudCoreTimer.ElapsedMs(), false, "page=appearance tab=hud section=core");
			});
			AddToggleCard(1, [=, this](CUIRect &ContentRect) mutable {
				CUIRect RightView = ContentRect;
				CPerfTimer HudDdraceTimer;
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowIds, "appearance-show-client-ids", Localize("Show client IDs (scoreboard, chat, spectating)"), &g_Config.m_ClShowIds, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudDDRace, "appearance-show-ddrace-hud", Localize("Show DDRace HUD"), &g_Config.m_ClShowhudDDRace, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				if(g_Config.m_ClShowhudDDRace)
				{
					HudStyleToggles(RightView);
				}
				HudStatusToggles(RightView);
				// 卡键/锤子/分身控制/分身同步四个状态开关与自定义 bind 状态列表已迁移到 QmClient → HUD → DDRace HUD Pro 卡片
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				HudPositionToggles(RightView);
				LogPerfStage(Client(), "appearance_hud_ddrace_section", HudDdraceTimer.ElapsedMs(), false, "page=appearance tab=hud section=ddrace");
				CPerfTimer HudFreezeBarsTimer;
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowFreezeBars, "appearance-show-freeze-bars", Localize("Show freeze bars"), &g_Config.m_ClShowFreezeBars, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				if(g_Config.m_ClShowFreezeBars)
				{
					RightView.HSplitTop(LineSize, &Button, &RightView);
					DoAppearanceNumericField(APPEARANCE_TAB_HUD, "appearance-freeze-bars-alpha-inside-freeze", &g_Config.m_ClFreezeBarsAlphaInsideFreeze, &g_Config.m_ClFreezeBarsAlphaInsideFreeze, Button, Localize("Opacity of freeze bars inside freeze"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				ContentRect = RightView;
				LogPerfStage(Client(), "appearance_hud_freeze_bars_section", HudFreezeBarsTimer.ElapsedMs(), false, "page=appearance tab=hud section=freeze_bars");
			});
			vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_ClShowhudDDRace != 0) | (static_cast<uint64_t>(g_Config.m_ClShowFreezeBars != 0) << 1);
			vCards.back().m_PreLayoutInput = [this, LineSize, MarginSmall, HudStyleToggles, HudStatusToggles, HudPositionToggles](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CUIRect RightView = Content;
				CUIRect Row;
				const auto NextRow = [&]() {
					RightView.HSplitTop(LineSize, &Row, &RightView);
					RightView.HSplitTop(MarginSmall, nullptr, &RightView);
					return Row;
				};
				const auto ProcessToggle = [this](CUIRect ToggleRow, int *pValue) {
					if(!Ui()->DoButtonLogic(pValue, 0, &ToggleRow, BUTTONFLAG_LEFT))
						return false;
					*pValue ^= 1;
					return true;
				};
				NextRow();
				bool Changed = ProcessToggle(NextRow(), &g_Config.m_ClShowhudDDRace);
				if(g_Config.m_ClShowhudDDRace)
					HudStyleToggles(RightView, ESettingsToggleGroupPass::LAYOUT);
				HudStatusToggles(RightView, ESettingsToggleGroupPass::LAYOUT);
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				HudPositionToggles(RightView, ESettingsToggleGroupPass::LAYOUT);
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				Changed = ProcessToggle(NextRow(), &g_Config.m_ClShowFreezeBars) || Changed;
				return Changed;
			};
		}
		else if(AppearanceTab == APPEARANCE_TAB_CHAT)
		{
			CChat *pChat = &GameClient()->m_Chat;
			static int s_AppearanceAlwaysShowChat = 0;
			// ***** Chat ***** //
			SSettingsCardDefinition ChatSettingsDefinition;
			ChatSettingsDefinition.m_Spec = CardSpec(2);
			ChatSettingsDefinition.m_RenderMeasured = [=, this](CUIRect &ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				const auto NextChatRow = [&](CUIRect &Row) {
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
				};
				// General chat settings
				NextChatRow(Button);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClShowChat, "appearance-show-chat", Localize("Show chat"), g_Config.m_ClShowChat, &Button))
				{
					g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
				}
				if(g_Config.m_ClShowChat)
				{
					NextChatRow(Button);
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &s_AppearanceAlwaysShowChat, "appearance-always-show-chat", Localize("Always show chat"), g_Config.m_ClShowChat == 2, &Button))
						g_Config.m_ClShowChat = g_Config.m_ClShowChat != 2 ? 2 : 1;
				}

				ChatDisplayToggles(LeftView);
				ChatStorageToggles(LeftView);
				if(g_Config.m_QmChatLogAutoSave)
				{
					NextChatRow(Button);
					DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-log-keep-days", &g_Config.m_QmChatLogKeepDays, &g_Config.m_QmChatLogKeepDays, Button, Localize("Chat log retention days"), 0, 3650, &CUi::ms_LinearScrollbarScale, 0, "", Localize("Days"));
				}

				if(DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClChatOld, "appearance-use-old-chat-style", Localize("Use old chat style"), &g_Config.m_ClChatOld, &LeftView, LineSize, 0.0f, AppearanceBodySize))
					GameClient()->m_Chat.RebuildChat();
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

				// Profanity censor checkbox intentionally stays disabled here.

				NextChatRow(Button);
				if(DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-font-size", &g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, Button, Localize("Chat font size"), 10, 100))
				{
					pChat->EnsureCoherentWidth();
					pChat->RebuildChat();
				}

				NextChatRow(Button);
				if(DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-width", &g_Config.m_ClChatWidth, &g_Config.m_ClChatWidth, Button, Localize("Chat width"), 120, 400))
				{
					pChat->EnsureCoherentFontSize();
					pChat->RebuildChat();
				}

				static CButtonContainer s_BackgroundColor;
				DoLine_ColorPicker(&s_BackgroundColor, AppearanceMetrics, &LeftView, Localize("Chat background color"), &g_Config.m_ClChatBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ClChatBackgroundColor, true)), false, nullptr, true);
				ContentRect = LeftView;
			};
			const auto ChatRender = ChatSettingsDefinition.m_RenderMeasured;
			ChatSettingsDefinition.m_Render = [ChatRender](CUIRect Content) { ChatRender(Content); };
			ChatSettingsDefinition.m_Measure = [this, ChatRender](float Width) { return qm_card_catalog::QmCardRenderHook::MeasureContent(this, ChatRender, Width); };

			ChatSettingsDefinition.m_MeasureRevision =
				(static_cast<uint64_t>(g_Config.m_ClShowChat != 0) << 0) |
				(static_cast<uint64_t>(g_Config.m_QmChatLogAutoSave != 0) << 1);
			ChatSettingsDefinition.m_PreLayoutInput = [this, LineSize, MarginSmall, ChatDisplayToggles, ChatStorageToggles](CUIRect ContentRect) {
				if(m_MenuTextPlanCollecting)
					return false;
				auto NextRow = [LineSize, MarginSmall](CUIRect &Content, CUIRect &Row) {
					Content.HSplitTop(LineSize, &Row, &Content);
					Content.HSplitTop(MarginSmall, nullptr, &Content);
				};
				CUIRect Row;
				NextRow(ContentRect, Row);
				if(Ui()->DoButtonLogic(&g_Config.m_ClShowChat, 0, &Row, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
					return true;
				}
				if(g_Config.m_ClShowChat)
					NextRow(ContentRect, Row);
				ChatDisplayToggles(ContentRect, ESettingsToggleGroupPass::LAYOUT);
				return ChatStorageToggles(ContentRect, ESettingsToggleGroupPass::INPUT);
			};
			vCards.push_back(std::move(ChatSettingsDefinition));
			const float ChatMessagesMinCardHeight = ResolveAppearanceChatMessagesHeight(AppearanceMetrics);
			AddCard(3, ChatMessagesMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				char aBuf[128];
				CUIRect RightView = ContentRect;
				// ***** Messages ***** //
				// Message Colors and extra settings
				static CButtonContainer s_SystemMessageReset, s_SystemMessageAdd, s_SystemMessageRemove;
				static unsigned s_aSystemMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-system-message", Localize("System message"), &g_Config.m_ClMessageSystemColor, g_Config.m_ClMessageSystemGradient, sizeof(g_Config.m_ClMessageSystemGradient), ColorRGBA(1.0f, 1.0f, 0.5f), &s_SystemMessageReset, &s_SystemMessageAdd, &s_SystemMessageRemove, s_aSystemMessageColorValues, EQmChatGradientRole::SYSTEM, true, &g_Config.m_ClShowChatSystem, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);
				if(DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_QmChatHideSystemPrefix, "appearance-chat-hide-system-prefix", Localize("Hide system message prefix"), &g_Config.m_QmChatHideSystemPrefix, &RightView, LineSize, MarginSmall, AppearanceBodySize))
				{
					pChat->RebuildChat();
					ConfigManager()->Save();
				}

				static CButtonContainer s_HighlightedMessageReset, s_HighlightedMessageAdd, s_HighlightedMessageRemove;
				static unsigned s_aHighlightedMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-highlighted-message", Localize("Highlighted message"), &g_Config.m_ClMessageHighlightColor, g_Config.m_ClMessageHighlightGradient, sizeof(g_Config.m_ClMessageHighlightGradient), ColorRGBA(1.0f, 0.5f, 0.5f), &s_HighlightedMessageReset, &s_HighlightedMessageAdd, &s_HighlightedMessageRemove, s_aHighlightedMessageColorValues, EQmChatGradientRole::HIGHLIGHT, true, nullptr, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_TeamMessageReset, s_TeamMessageAdd, s_TeamMessageRemove;
				static unsigned s_aTeamMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-team-message", Localize("Team message"), &g_Config.m_ClMessageTeamColor, g_Config.m_ClMessageTeamGradient, sizeof(g_Config.m_ClMessageTeamGradient), ColorRGBA(0.65f, 1.0f, 0.65f), &s_TeamMessageReset, &s_TeamMessageAdd, &s_TeamMessageRemove, s_aTeamMessageColorValues, EQmChatGradientRole::TEAM, true, nullptr, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_FriendMessageReset, s_FriendMessageAdd, s_FriendMessageRemove;
				static unsigned s_aFriendMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-friend-message", Localize("Friend message"), &g_Config.m_ClMessageFriendColor, g_Config.m_ClMessageFriendGradient, sizeof(g_Config.m_ClMessageFriendGradient), ColorRGBA(1.0f, 0.137f, 0.137f), &s_FriendMessageReset, &s_FriendMessageAdd, &s_FriendMessageRemove, s_aFriendMessageColorValues, EQmChatGradientRole::FRIEND, true, &g_Config.m_ClMessageFriend, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_NormalMessageReset, s_NormalMessageAdd, s_NormalMessageRemove;
				static unsigned s_aNormalMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-normal-message", Localize("Normal message"), &g_Config.m_ClMessageColor, g_Config.m_ClMessageGradient, sizeof(g_Config.m_ClMessageGradient), ColorRGBA(1.0f, 1.0f, 1.0f), &s_NormalMessageReset, &s_NormalMessageAdd, &s_NormalMessageRemove, s_aNormalMessageColorValues, EQmChatGradientRole::NORMAL, true, nullptr, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				str_format(aBuf, sizeof(aBuf), "%s (echo)", Localize("Client message"));
				static CButtonContainer s_ClientMessageReset, s_ClientMessageAdd, s_ClientMessageRemove;
				static unsigned s_aClientMessageColorValues[CMessageGradient::MAX_COLORS];
				// TClient
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-client-message", aBuf, &g_Config.m_ClMessageClientColor, g_Config.m_ClMessageClientGradient, sizeof(g_Config.m_ClMessageClientGradient), ColorRGBA(0.5f, 0.78f, 1.0f), &s_ClientMessageReset, &s_ClientMessageAdd, &s_ClientMessageRemove, s_aClientMessageColorValues, EQmChatGradientRole::CLIENT, true, &g_Config.m_QmShowChatClient, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_FriendMessageHeartReset;
				const unsigned OldFriendMessageHeartColor = g_Config.m_ClMessageFriendHeartColor;
				DoLine_ColorPicker(&s_FriendMessageHeartReset, AppearanceMetrics, &RightView, Localize("Friend heart"), &g_Config.m_ClMessageFriendHeartColor, ColorRGBA(1.0f, 0.0f, 0.0f), true);
				if(g_Config.m_ClMessageFriendHeartColor != OldFriendMessageHeartColor)
				{
					pChat->RebuildChat();
					ConfigManager()->Save();
				}
				RenderQmChatGradientSettings(RightView, *pChat, AppearanceMetrics);
			});
			const auto MeasureChatPreview = [this, pChat, MarginSmall](float ContentWidth) {
				const float RealFontSize = pChat->FontSize() * 2.0f;
				const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? pChat->MessagePaddingX() : 0.0f) * 2.0f;
				const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? pChat->MessagePaddingY() : 0.0f) * 2.0f;
				const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? pChat->MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0.0f) * 2.0f;
				const float ConfiguredLineWidth = g_Config.m_ClChatWidth * 2.0f - RealMsgPaddingX * 1.5f - RealMsgPaddingTee;
				const float CardLineWidth = maximum(RealFontSize, ContentWidth - 2.0f * MarginSmall - RealMsgPaddingX * 1.5f - RealMsgPaddingTee);
				const float LineWidth = maximum(RealFontSize, minimum(ConfiguredLineWidth, CardLineWidth));
				char aPlayerName[64];
				str_copy(aPlayerName, Client()->PlayerName());
				float Height = 2.0f * MarginSmall;
				const auto AddPreviewLine = [&](const char *pName, const char *pText, bool AppendNameSeparator = true) {
					char aLine[384];
					str_format(aLine, sizeof(aLine), "%s%s%s", pName, AppendNameSeparator && pName[0] != '\0' ? ": " : "", pText);
					Height += maximum(RealFontSize, TextRender()->TextBoundingBox(RealFontSize, aLine, -1, LineWidth).m_H) + RealMsgPaddingY;
				};
				if(g_Config.m_ClShowChatSystem)
				{
					char aSystemText[128];
					str_format(aSystemText, sizeof(aSystemText), "'%s' entered and joined the game", aPlayerName);
					AddPreviewLine(CChat::SystemMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0), aSystemText, false);
				}
				if(!g_Config.m_ClShowChatFriends)
				{
					if(!g_Config.m_ClShowChatTeamMembersOnly)
					{
						char aHighlightText[128];
						str_format(aHighlightText, sizeof(aHighlightText), "Hey, how are you %s?", aPlayerName);
						AddPreviewLine("Random Tee", aHighlightText);
					}
					AddPreviewLine("Your Teammate", "Let's speedrun this!");
				}
				if(!g_Config.m_ClShowChatTeamMembersOnly)
					AddPreviewLine("Friend", "Hello there");
				if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
					AddPreviewLine("Spammer", "Hey fools, I'm spamming here!");
				if(g_Config.m_QmShowChatClient)
					AddPreviewLine("", "Echo command executed");
				return maximum(Height, 2.0f * MarginSmall + RealFontSize + RealMsgPaddingY);
			};
			const uint64_t ChatPreviewMeasureRevision =
				(static_cast<uint64_t>(g_Config.m_ClChatOld != 0) << 0) |
				(static_cast<uint64_t>(g_Config.m_ClShowChatSystem != 0) << 1) |
				(static_cast<uint64_t>(g_Config.m_ClShowChatFriends != 0) << 2) |
				(static_cast<uint64_t>(g_Config.m_ClShowChatTeamMembersOnly != 0) << 3) |
				(static_cast<uint64_t>(g_Config.m_QmShowChatClient != 0) << 4) |
				(static_cast<uint64_t>(g_Config.m_QmChatHideSystemPrefix != 0) << 5) |
				(static_cast<uint64_t>(std::clamp(g_Config.m_ClChatFontSize, 0, 255)) << 8) |
				(static_cast<uint64_t>(std::clamp(g_Config.m_ClChatWidth, 0, 1023)) << 16);
			AddMeasuredCard(4, MeasureChatPreview, [=, this](CUIRect ContentRect) mutable {
			char aBuf[128];
			CUIRect PreviewView = ContentRect;
			// ***** Chat Preview ***** //
			PreviewView.Draw(ColorRGBA(1, 1, 1, 0.1f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
			PreviewView.Margin(MarginSmall, &PreviewView);

			ColorRGBA SystemColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			ColorRGBA HighlightedColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
			ColorRGBA TeamColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			ColorRGBA FriendColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			ColorRGBA FriendHeartColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageFriendHeartColor));
			ColorRGBA NormalColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageColor));
			ColorRGBA ClientColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			ColorRGBA DefaultNameColor(0.8f, 0.8f, 0.8f, 1.0f);

			const float RealFontSize = pChat->FontSize() * 2;
			const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? pChat->MessagePaddingX() : 0) * 2;
			const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? pChat->MessagePaddingY() : 0) * 2;
			const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? pChat->MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0) * 2;
			const float RealOffsetY = RealFontSize + RealMsgPaddingY;

			const float X = RealMsgPaddingX / 2.0f + PreviewView.x;
			float Y = PreviewView.y;
			const float ConfiguredLineWidth = g_Config.m_ClChatWidth * 2.0f - RealMsgPaddingX * 1.5f - RealMsgPaddingTee;
			const float CardLineWidth = maximum(RealFontSize, PreviewView.w - RealMsgPaddingX * 1.5f - RealMsgPaddingTee);
			float LineWidth = maximum(RealFontSize, minimum(ConfiguredLineWidth, CardLineWidth));

			str_copy(aBuf, Client()->PlayerName());

			const CAnimState *pIdleState = CAnimState::GetIdle();
			const float RealTeeSize = pChat->MessageTeeSize() * 2;
			const float RealTeeSizeHalved = pChat->MessageTeeSize();
			constexpr float TWSkinUnreliableOffset = -0.25f;
			const float OffsetTeeY = RealTeeSizeHalved;
			const float FullHeightMinusTee = RealOffsetY - RealTeeSize;

			struct SPreviewLine
			{
				int m_ClientId;
				bool m_Team;
				char m_aName[64];
				char m_aText[256];
				bool m_Friend;
				bool m_Player;
				bool m_Client;
				bool m_Highlighted;
				int m_TimesRepeated;

				CTeeRenderInfo m_RenderInfo;
			};

			static std::vector<SPreviewLine> s_vLines;

			enum ELineFlag
			{
				FLAG_TEAM = 1 << 0,
				FLAG_FRIEND = 1 << 1,
				FLAG_HIGHLIGHT = 1 << 2,
				FLAG_CLIENT = 1 << 3
			};
			enum
			{
				PREVIEW_SYS,
				PREVIEW_HIGHLIGHT,
				PREVIEW_TEAM,
				PREVIEW_FRIEND,
				PREVIEW_SPAMMER,
				PREVIEW_CLIENT
			};
			auto &&SetPreviewLine = [](int Index, int ClientId, const char *pName, const char *pText, int Flag, int Repeats) {
				SPreviewLine *pLine;
				if((int)s_vLines.size() <= Index)
				{
					s_vLines.emplace_back();
					pLine = &s_vLines.back();
				}
				else
				{
					pLine = &s_vLines[Index];
				}
				pLine->m_ClientId = ClientId;
				pLine->m_Team = Flag & FLAG_TEAM;
				pLine->m_Friend = Flag & FLAG_FRIEND;
				pLine->m_Player = ClientId >= 0;
				pLine->m_Highlighted = Flag & FLAG_HIGHLIGHT;
				pLine->m_Client = Flag & FLAG_CLIENT;
				pLine->m_TimesRepeated = Repeats;
				str_copy(pLine->m_aName, pName);
				str_copy(pLine->m_aText, pText);
			};
			auto &&SetLineSkin = [RealTeeSize](int Index, const CSkin *pSkin) {
				if(Index >= (int)s_vLines.size())
					return;
				s_vLines[Index].m_RenderInfo.m_Size = RealTeeSize;
				s_vLines[Index].m_RenderInfo.Apply(pSkin);
			};

			auto &&RenderPreview = [&](int LineIndex, int x, int y, bool Render = true) {
				if(LineIndex >= (int)s_vLines.size())
					return vec2(0, 0);
				CTextCursor LocalCursor;
				LocalCursor.SetPosition(vec2(x, y));
				LocalCursor.m_FontSize = RealFontSize;
				LocalCursor.m_Flags = Render ? TEXTFLAG_RENDER : 0;
				LocalCursor.m_LineWidth = LineWidth;
				const auto &Line = s_vLines[LineIndex];

				char aClientId[16] = "";
				if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
				{
					GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_FORCE);
				}

				char aCount[12];
				if(Line.m_ClientId < 0)
					str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
				else
					str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

				if(Line.m_Player)
				{
					LocalCursor.m_X += RealMsgPaddingTee;

					if(Line.m_Friend && g_Config.m_ClMessageFriend)
					{
						if(Render)
							TextRender()->TextColor(FriendHeartColor);
						TextRender()->TextEx(&LocalCursor, "♥ ", -1);
					}
				}

				ColorRGBA NameColor;
				if(Line.m_Team)
					NameColor = CalculateNameColor(color_cast<ColorHSLA>(TeamColor));
				else if(Line.m_Player)
					NameColor = DefaultNameColor;
				else if(Line.m_Client)
					NameColor = ClientColor;
				else
					NameColor = SystemColor;

				if(Render)
					TextRender()->TextColor(NameColor);

				TextRender()->TextEx(&LocalCursor, aClientId);
				TextRender()->TextEx(&LocalCursor, Line.m_aName);

				if(Line.m_TimesRepeated > 0)
				{
					if(Render)
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
					TextRender()->TextEx(&LocalCursor, aCount, -1);
				}

				if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
				{
					if(Render)
						TextRender()->TextColor(NameColor);
					TextRender()->TextEx(&LocalCursor, ": ", -1);
				}

				CTextCursor AppendCursor = LocalCursor;
				AppendCursor.m_LongestLineWidth = 0.0f;
				if(!g_Config.m_ClChatOld)
				{
					AppendCursor.m_StartX = LocalCursor.m_X;
					AppendCursor.m_LineWidth -= LocalCursor.m_LongestLineWidth;
				}

				if(Render)
				{
					if(Line.m_Highlighted)
						TextRender()->TextColor(HighlightedColor);
					else if(Line.m_Friend && g_Config.m_ClMessageFriend)
						TextRender()->TextColor(FriendColor);
					else if(Line.m_Team)
						TextRender()->TextColor(TeamColor);
					else if(Line.m_Player)
						TextRender()->TextColor(NormalColor);
				}

				const EQmChatGradientRole Role = Line.m_Highlighted                          ? EQmChatGradientRole::HIGHLIGHT :
								 Line.m_Friend && g_Config.m_ClMessageFriend ? EQmChatGradientRole::FRIEND :
								 Line.m_Team                                 ? EQmChatGradientRole::TEAM :
								 Line.m_Player                               ? EQmChatGradientRole::NORMAL :
								 Line.m_Client                               ? EQmChatGradientRole::CLIENT :
													       EQmChatGradientRole::SYSTEM;
				const auto Gradient = QmChatGradientStyle(g_Config, Role, TextRender()->GetTextColor());
				const CQmChatGradientPaint Paint(TextRender(), AppendCursor, Line.m_aText, &Gradient);
				TextRender()->TextEx(&AppendCursor, Line.m_aText, -1);
				if(Render)
					TextRender()->TextColor(TextRender()->DefaultTextColor());

				return vec2{LocalCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth, AppendCursor.Height() + RealMsgPaddingY};
			};

			// Set preview lines
			{
				char aLineBuilder[128];

				str_format(aLineBuilder, sizeof(aLineBuilder), "'%s' entered and joined the game", aBuf);
				SetPreviewLine(PREVIEW_SYS, -1, CChat::SystemMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0), aLineBuilder, 0, 0);

				str_format(aLineBuilder, sizeof(aLineBuilder), "Hey, how are you %s?", aBuf);
				SetPreviewLine(PREVIEW_HIGHLIGHT, 7, "Random Tee", aLineBuilder, FLAG_HIGHLIGHT, 0);

				SetPreviewLine(PREVIEW_TEAM, 11, "Your Teammate", "Let's speedrun this!", FLAG_TEAM, 0);
				SetPreviewLine(PREVIEW_FRIEND, 8, "Friend", "Hello there", FLAG_FRIEND, 0);
				SetPreviewLine(PREVIEW_SPAMMER, 9, "Spammer", "Hey fools, I'm spamming here!", 0, 5);
				SetPreviewLine(PREVIEW_CLIENT, -1, "— ", "Echo command executed", FLAG_CLIENT, 0);
			}

			SetLineSkin(1, GameClient()->m_Skins.Find("pinky"));
			SetLineSkin(2, GameClient()->m_Skins.Find("default"));
			SetLineSkin(3, GameClient()->m_Skins.Find("cammostripes"));
			SetLineSkin(4, GameClient()->m_Skins.Find("beast"));

			// Backgrounds first
			if(!g_Config.m_ClChatOld)
			{
				const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true));

				float TempY = Y;
				const float RealBackgroundRounding = pChat->MessageRounding() * 2.0f;

				auto &&RenderMessageBackground = [&](int LineIndex) {
					const auto Size = RenderPreview(LineIndex, 0, 0, false);
					const CUIRect MessageBackground{PreviewView.x, TempY - RealMsgPaddingY / 2.0f, PreviewView.w, Size.y};
					DrawRoundedSurface(Ui(), MessageBackground, BackgroundColor, ColorRGBA(), RealBackgroundRounding);
					return Size.y;
				};

				if(g_Config.m_ClShowChatSystem)
				{
					TempY += RenderMessageBackground(PREVIEW_SYS);
				}

				if(!g_Config.m_ClShowChatFriends)
				{
					if(!g_Config.m_ClShowChatTeamMembersOnly)
						TempY += RenderMessageBackground(PREVIEW_HIGHLIGHT);
					TempY += RenderMessageBackground(PREVIEW_TEAM);
				}

				if(!g_Config.m_ClShowChatTeamMembersOnly)
					TempY += RenderMessageBackground(PREVIEW_FRIEND);

				if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
				{
					TempY += RenderMessageBackground(PREVIEW_SPAMMER);
				}

				if(g_Config.m_QmShowChatClient)
				{
					TempY += RenderMessageBackground(PREVIEW_CLIENT);
				}

			}

			// System
			if(g_Config.m_ClShowChatSystem)
			{
				Y += RenderPreview(PREVIEW_SYS, X, Y).y;
			}

			if(!g_Config.m_ClShowChatFriends)
			{
				// Highlighted
				if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
					RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_HIGHLIGHT].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				if(!g_Config.m_ClShowChatTeamMembersOnly)
					Y += RenderPreview(PREVIEW_HIGHLIGHT, X, Y).y;

				// Team
				if(!g_Config.m_ClChatOld)
					RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_TEAM].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				Y += RenderPreview(PREVIEW_TEAM, X, Y).y;
			}

			// Friend
			if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_FRIEND].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			if(!g_Config.m_ClShowChatTeamMembersOnly)
				Y += RenderPreview(PREVIEW_FRIEND, X, Y).y;

			// Normal
			if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
			{
				if(!g_Config.m_ClChatOld)
					RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_SPAMMER].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				Y += RenderPreview(PREVIEW_SPAMMER, X, Y).y;
			}
			// Client
			if(g_Config.m_QmShowChatClient)
			{
				Y += RenderPreview(PREVIEW_CLIENT, X, Y).y;
			}

					TextRender()->TextColor(TextRender()->DefaultTextColor());
					PreviewView.y = maximum(PreviewView.y, Y + MarginSmall); }, ChatPreviewMeasureRevision);
		}
		else if(AppearanceTab == APPEARANCE_TAB_NAME_PLATE)
		{
			qm_card_catalog::SQmCardBuildContext CardBuild;
			CardBuild.m_pMenus = this;
			CardBuild.m_ReadOnly = RenderOnly;
			CardBuild.m_Page = AppearancePage;
			CardBuild.m_Metrics = AppearanceMetrics;
			CardBuild.m_UiContext = AppearanceCardCtx;
			qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::NameplateCardStableIds(), vCards);
			const float NamePlatePreviewMinAreaHeight = std::clamp(190.0f * AppearanceUiScale, 160.0f, 210.0f);
			const float NamePlatePreviewControlsHeight = ResolveSettingsRowsHeight(2, LineSize, MarginSmall) + MarginSmall + AppearanceMetrics.m_ButtonHeight;
			const auto ResolveNamePlatePreviewCardHeight = [this, NamePlatePreviewMinAreaHeight, NamePlatePreviewControlsHeight, MarginSmall](float) {
				return maximum(NamePlatePreviewMinAreaHeight, GameClient()->m_NamePlates.MeasurePreviewAreaHeight()) + MarginSmall + NamePlatePreviewControlsHeight;
			};

			AddMeasuredCard(6, ResolveNamePlatePreviewCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				CUIRect PreviewArea, Controls;
				RightView.HSplitBottom(NamePlatePreviewControlsHeight, &PreviewArea, &Controls);
				PreviewArea.HSplitBottom(MarginSmall, &PreviewArea, nullptr);
				PreviewArea.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_ALL, ui_token::radius::CARD);
				const auto NextPreviewControl = [&](CUIRect &Row) {
					Controls.HSplitTop(LineSize, &Row, &Controls);
					if(Controls.h > 0.0f)
						Controls.HSplitTop(MarginSmall, nullptr, &Controls);
				};

				NextPreviewControl(Button);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, &m_DummyNamePlatePreview, g_Config.m_ClDummy ? "appearance-preview-player-nameplate" : "appearance-preview-dummy-nameplate", g_Config.m_ClDummy ? Localize("Preview player nameplate") : Localize("Preview dummy nameplate"), m_DummyNamePlatePreview, &Button))
					m_DummyNamePlatePreview = !m_DummyNamePlatePreview;

				NextPreviewControl(Button);
				const bool NameplateFreeMoveEnabled = g_Config.m_QmNameplateFreeMove != 0 || g_Config.m_QmNameplateFreeMoveX != 0 || g_Config.m_QmNameplateFreeMoveY != 0;
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, &g_Config.m_QmNameplateFreeMove, "appearance-nameplate-free-move", Localize("Free move"), NameplateFreeMoveEnabled, &Button))
				{
					const int NewValue = NameplateFreeMoveEnabled ? 0 : 1;
					g_Config.m_QmNameplateFreeMove = NewValue;
					g_Config.m_QmNameplateFreeMoveX = 0;
					g_Config.m_QmNameplateFreeMoveY = 0;
				}

				Controls.HSplitTop(AppearanceMetrics.m_ButtonHeight, &Button, &Controls);
				if(Controls.h > 0.0f)
					Controls.HSplitTop(MarginSmall, nullptr, &Controls);
				static CButtonContainer s_NameplateResetLayoutButton;
				if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, APPEARANCE_TAB_NAME_PLATE, &s_NameplateResetLayoutButton, "appearance-nameplate-reset-layout", Localize("Reset layout"), 0, &Button))
				{
					g_Config.m_QmNameplateKeysOffsetX = 0;
					g_Config.m_QmNameplateKeysOffsetY = 0;
					g_Config.m_QmNameplateCoordsOffsetX = 0;
					g_Config.m_QmNameplateCoordsOffsetY = 0;
					g_Config.m_QmNameplateHookOffsetX = 0;
					g_Config.m_QmNameplateHookOffsetY = 0;
					g_Config.m_QmNameplateClanOffsetX = 0;
					g_Config.m_QmNameplateClanOffsetY = 0;
					g_Config.m_QmNameplateNameOffsetX = 0;
					g_Config.m_QmNameplateNameOffsetY = 0;
				}
				int Dummy = g_Config.m_ClDummy != (m_DummyNamePlatePreview ? 1 : 0);
				GameClient()->m_NamePlates.RenderNamePlatePreview(PreviewArea, Dummy); }, NamePlatePreviewMeasureRevision);
		}
		else if(AppearanceTab == APPEARANCE_TAB_HOOK_COLLISION)
		{
			static int s_AppearanceAlwaysShowHookCollOwn = 0;
			static int s_AppearanceAlwaysShowHookCollOther = 0;
			const auto ResolveHookCollisionLeftMinCardHeight = [LineSize, MarginSmall, HeadlineHeight, ColorPickerRowHeight]() {
				const int RowCount = 5 + (g_Config.m_ClShowHookCollOwn != 0 ? 1 : 0) + (g_Config.m_ClShowHookCollOther != 0 ? 1 : 0);
				return ResolveSettingsRowsHeight(RowCount, LineSize, MarginSmall) + MarginSmall + HeadlineHeight + ColorPickerRowHeight * 4.0f;
			};
			const float HookCollisionRightMinCardHeight =
				(50.0f + 4.0f * MarginSmall) * 5.0f + LineSize;
			AddCard(7, ResolveHookCollisionLeftMinCardHeight(), [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				int RowsRemaining = 5 + (g_Config.m_ClShowHookCollOwn != 0 ? 1 : 0) + (g_Config.m_ClShowHookCollOther != 0 ? 1 : 0);
				const auto NextRow = [&]() {
					CUIRect Row;
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					if(--RowsRemaining > 0)
						LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
					return Row;
				};

				// General hookline settings
				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_ClShowHookCollOwn, "appearance-show-own-hook-collision", Localize("Show own player's hook collision line"), g_Config.m_ClShowHookCollOwn, &Button))
				{
					g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn ? 0 : 1;
				}
				Button = NextRow();
				if(g_Config.m_ClShowHookCollOwn)
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_AppearanceAlwaysShowHookCollOwn, "appearance-always-show-own-hook-collision", Localize("Always show own player's hook collision line"), g_Config.m_ClShowHookCollOwn == 2, &Button))
						g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
				}

				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_ClShowHookCollOther, "appearance-show-other-hook-collision", Localize("Show other players' hook collision lines"), g_Config.m_ClShowHookCollOther, &Button))
				{
					g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther >= 1 ? 0 : 1;
				}
				Button = NextRow();
				if(g_Config.m_ClShowHookCollOther)
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_AppearanceAlwaysShowHookCollOther, "appearance-always-show-other-hook-collision", Localize("Always show other players' hook collision lines"), g_Config.m_ClShowHookCollOther == 2, &Button))
						g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
				}

				Button = NextRow();
				DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-own-width", &g_Config.m_ClHookCollSize, &g_Config.m_ClHookCollSize, Button, Localize("Width of your own hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale);

				Button = NextRow();
				DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-other-width", &g_Config.m_ClHookCollSizeOther, &g_Config.m_ClHookCollSizeOther, Button, Localize("Width of others' hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale);

				Button = NextRow();
				DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-opacity", &g_Config.m_ClHookCollAlpha, &g_Config.m_ClHookCollAlpha, Button, Localize("Hook collision line opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

				static CButtonContainer s_HookCollNoCollResetId, s_HookCollHookableCollResetId, s_HookCollTeeCollResetId, s_HookCollTipColorResetId;
				static int s_HookCollToolTip;

				DoAppearanceHeading(LeftView, "appearance-hook-collision-colors-title", Localize("Colors of the hook collision line, in case of a possible collision with:"), HeadlineFontSize, HeadlineHeight);

				Ui()->RegisterPassiveHotItem(&s_HookCollToolTip, &LeftView);
				GameClient()->m_Tooltips.DoToolTip(&s_HookCollToolTip, &LeftView, Localize("Your movements are not taken into account when calculating the line colors"));
				DoLine_ColorPicker(&s_HookCollNoCollResetId, AppearanceMetrics, &LeftView, Localize("Nothing hookable"), &g_Config.m_ClHookCollColorNoColl, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), false);
				DoLine_ColorPicker(&s_HookCollHookableCollResetId, AppearanceMetrics, &LeftView, Localize("Something hookable"), &g_Config.m_ClHookCollColorHookableColl, ColorRGBA(130.0f / 255.0f, 232.0f / 255.0f, 160.0f / 255.0f, 1.0f), false);
				DoLine_ColorPicker(&s_HookCollTeeCollResetId, AppearanceMetrics, &LeftView, Localize("A Tee"), &g_Config.m_ClHookCollColorTeeColl, ColorRGBA(1.0f, 1.0f, 0.0f, 1.0f), false);
				DoLine_ColorPicker(&s_HookCollTipColorResetId, AppearanceMetrics, &LeftView, Localize("Hook line tip"), &g_Config.m_ClHookCollTipColor, ColorRGBA(1.0f, 1.0f, 0.0f, 0.5f), false, nullptr, true);
			});
			vCards.back().m_Measure = [ResolveHookCollisionLeftMinCardHeight](float) { return ResolveHookCollisionLeftMinCardHeight(); };
			vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_ClShowHookCollOwn != 0) | (static_cast<uint64_t>(g_Config.m_ClShowHookCollOther != 0) << 1);
			vCards.back().m_PreLayoutInput = [this, LineSize, MarginSmall](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CUIRect LeftView = Content;
				int RowsRemaining = 5 + (g_Config.m_ClShowHookCollOwn != 0 ? 1 : 0) + (g_Config.m_ClShowHookCollOther != 0 ? 1 : 0);
				const auto NextRow = [&]() {
					CUIRect Row;
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					if(--RowsRemaining > 0)
						LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
					return Row;
				};
				bool Changed = false;
				CUIRect OwnToggleRow = NextRow();
				if(Ui()->DoButtonLogic(&g_Config.m_ClShowHookCollOwn, g_Config.m_ClShowHookCollOwn != 0, &OwnToggleRow, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 0 ? 0 : 1;
					Changed = true;
				}
				if(g_Config.m_ClShowHookCollOwn)
				{
					CUIRect AlwaysShowRow = NextRow();
					if(Ui()->DoButtonLogic(&s_AppearanceAlwaysShowHookCollOwn, g_Config.m_ClShowHookCollOwn == 2, &AlwaysShowRow, BUTTONFLAG_LEFT))
					{
						g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
						Changed = true;
					}
				}
				CUIRect OtherToggleRow = NextRow();
				if(Ui()->DoButtonLogic(&g_Config.m_ClShowHookCollOther, g_Config.m_ClShowHookCollOther != 0, &OtherToggleRow, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 0 ? 0 : 1;
					Changed = true;
				}
				if(g_Config.m_ClShowHookCollOther)
				{
					CUIRect AlwaysShowRow = NextRow();
					if(Ui()->DoButtonLogic(&s_AppearanceAlwaysShowHookCollOther, g_Config.m_ClShowHookCollOther == 2, &AlwaysShowRow, BUTTONFLAG_LEFT))
					{
						g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
						Changed = true;
					}
				}
				NextRow();
				NextRow();
				NextRow();
				return Changed;
			};
			AddCard(8, HookCollisionRightMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				// ***** Hook collisions preview ***** //
				auto DoHookCollision = [this](const vec2 &Pos, const float &Length, const int &Size, const ColorRGBA &Color, const ColorRGBA &TipColor, const bool &Invert) {
					ColorRGBA ColorModified = Color;
					ColorRGBA TipColorModified = TipColor;
					if(Invert)
						ColorModified = color_invert(ColorModified);
					ColorModified = ColorModified.WithAlpha((float)g_Config.m_ClHookCollAlpha / 100);
					TipColorModified = TipColor.WithMultipliedAlpha((float)g_Config.m_ClHookCollAlpha / 100);
					Graphics()->TextureClear();
					if(Size > 0)
					{
						Graphics()->QuadsBegin();
						Graphics()->SetColor(ColorModified);
						float LineWidth = 0.5f + (float)(Size - 1) * 0.25f;
						IGraphics::CQuadItem QuadItem(Pos.x, Pos.y - LineWidth, Length, LineWidth * 2.f);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						if(TipColor.a > 0.0f)
						{
							Graphics()->SetColor(TipColorModified);
							IGraphics::CQuadItem TipQuadItem(Pos.x + Length, Pos.y - LineWidth, 15.f, LineWidth * 2.f);
							Graphics()->QuadsDrawTL(&TipQuadItem, 1);
						}
						Graphics()->QuadsEnd();
					}
					else
					{
						Graphics()->LinesBegin();
						Graphics()->SetColor(ColorModified);
						IGraphics::CLineItem LineItem(Pos.x, Pos.y, Pos.x + Length, Pos.y);
						Graphics()->LinesDraw(&LineItem, 1);
						if(TipColor.a > 0.0f)
						{
							Graphics()->SetColor(TipColorModified);
							IGraphics::CLineItem TipLineItem(Pos.x + Length, Pos.y, Pos.x + Length + 15.f, Pos.y);
							Graphics()->LinesDraw(&TipLineItem, 1);
						}
						Graphics()->LinesEnd();
					}
				};

				CTeeRenderInfo OwnSkinInfo;
				OwnSkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
				OwnSkinInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
				OwnSkinInfo.m_Size = 50.0f;

				CTeeRenderInfo DummySkinInfo;
				DummySkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummySkin));
				DummySkinInfo.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
				DummySkinInfo.m_Size = 50.0f;

				vec2 TeeRenderPos, DummyRenderPos;

				const float LineLength = 150.f;
				const float LeftMargin = 30.f;

				const int TileScale = 32.0f;

				// 预览 tile 取图：优先预览专用小图（避免菜单会话加载完整 entities），
				// 完整 entities 已在显存时沿用原路径；空白/缺失/解码中则跳过绘制。
				const auto DrawHookPreviewTile = [&](int TileIndex, const CUIRect &Rect) {
					IGraphics::CTextureHandle TileTexture;
					const auto Source = GameClient()->m_MapImages.GetHookPreviewTileSource(TileIndex, TileTexture);
					if(Source == CMapImages::EHookPreviewTileSource::SMALL_TEXTURE)
					{
						Graphics()->TextureClear();
						Graphics()->TextureSet(TileTexture);
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
						Graphics()->QuadsBegin();
						Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
						IGraphics::CQuadItem QuadItem(Rect.x, Rect.y, TileScale, TileScale);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						Graphics()->QuadsEnd();
					}
					else if(Source == CMapImages::EHookPreviewTileSource::FULL_ENTITIES_LOADED)
					{
						Graphics()->TextureClear();
						Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
						RenderMap()->RenderTile(Rect.x, Rect.y, TileIndex, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
					}
				};

				// Toggled via checkbox later, inverts some previews
				static bool s_HookCollPressed = false;

				CUIRect PreviewColl;
				const auto PrepareHookPreviewRow = [&](CUIRect &Preview) {
					Preview.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_ALL, ui_token::radius::CARD);
					Preview.Margin(MarginSmall, &Preview);
				};

				// ***** Unhookable Tile Preview *****
				CUIRect PreviewNoColl;
				RightView.HSplitTop(50.0f, &PreviewNoColl, &RightView);
				PrepareHookPreviewRow(PreviewNoColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewNoColl.x + LeftMargin, PreviewNoColl.y + PreviewNoColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewNoColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				CUIRect NoHookTileRect;
				PreviewNoColl.VSplitRight(LineLength, &PreviewNoColl, &NoHookTileRect);
				NoHookTileRect.VSplitLeft(50.0f, &NoHookTileRect, nullptr);
				NoHookTileRect.Margin(10.0f, &NoHookTileRect);

				// Render unhookable tile
				DrawHookPreviewTile(TILE_NOHOOK, NoHookTileRect);

				// ***** Hookable Tile Preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorHookableColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				CUIRect HookTileRect;
				PreviewColl.VSplitRight(LineLength, &PreviewColl, &HookTileRect);
				HookTileRect.VSplitLeft(50.0f, &HookTileRect, nullptr);
				HookTileRect.Margin(10.0f, &HookTileRect);

				// Render hookable tile
				DrawHookPreviewTile(TILE_SOLID, HookTileRect);

				// ***** Hook Dummy Preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Hook Dummy Reverse Preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSizeOther, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), false);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Hook tip preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollTipColor, true)), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Preview +hookcoll pressed toggle *****
				RightView.HSplitTop(LineSize, &Button, &RightView);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_HookCollPressed, "appearance-preview-hook-collisions-pressed", Localize("Preview 'Hook collisions' being pressed"), s_HookCollPressed, &Button))
					s_HookCollPressed = !s_HookCollPressed;
			});
		}
		else if(AppearanceTab == APPEARANCE_TAB_INFO_MESSAGES)
		{
			const float InfoMessagesMinCardHeight = ResolveSettingsRowsHeight(2, LineSize, MarginSmall) + MarginSmall + ColorPickerRowHeight * 2.0f;
			AddCard(9, InfoMessagesMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				int RowsRemaining = 2;
				const auto NextRow = [&]() {
					CUIRect Row;
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					if(--RowsRemaining > 0)
						LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
					return Row;
				};

				// General info messages settings
				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_INFO_MESSAGES, &g_Config.m_ClShowKillMessages, "appearance-show-kill-messages", Localize("Show kill messages"), g_Config.m_ClShowKillMessages, &Button))
				{
					g_Config.m_ClShowKillMessages ^= 1;
				}

				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_INFO_MESSAGES, &g_Config.m_ClShowFinishMessages, "appearance-show-finish-messages", Localize("Show finish messages"), g_Config.m_ClShowFinishMessages, &Button))
				{
					g_Config.m_ClShowFinishMessages ^= 1;
				}
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

				static CButtonContainer s_KillMessageNormalColorId, s_KillMessageHighlightColorId;
				DoLine_ColorPicker(&s_KillMessageNormalColorId, AppearanceMetrics, &LeftView, Localize("Normal Color"), &g_Config.m_ClKillMessageNormalColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				DoLine_ColorPicker(&s_KillMessageHighlightColorId, AppearanceMetrics, &LeftView, Localize("Highlight Color"), &g_Config.m_ClKillMessageHighlightColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
			});
		}
		else if(AppearanceTab == APPEARANCE_TAB_LASER)
		{
			const float LaserPreviewHeight = std::clamp(56.0f * AppearanceUiScale, 40.0f, 56.0f);
			const auto ResolveLaserEnhancedMinCardHeight = [AppearanceMetrics]() {
				return ResolveAppearanceLaserEnhancedHeight(AppearanceMetrics, g_Config.m_QmLaserEnhanced != 0);
			};
			const float LaserColorMinCardHeight = ResolveAppearanceLaserColorsHeight(AppearanceMetrics);
			const float LaserPreviewMinCardHeight = (LaserPreviewHeight + 2.0f * MarginSmall) * 5.0f;
			AddCard(10, ResolveLaserEnhancedMinCardHeight(), [=, this](CUIRect ContentRect) mutable {
				CUIRect EnhancedCardContent = ContentRect;

				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserEnhanced, "appearance-laser-effect-enhancement", Localize("Laser effect enhancement"), &g_Config.m_QmLaserEnhanced, &EnhancedCardContent, LineSize, 0.0f, AppearanceBodySize);
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
				DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-glow-intensity", &g_Config.m_QmLaserGlowIntensity, &g_Config.m_QmLaserGlowIntensity, Button, Localize("Glow intensity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
				DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-size", &g_Config.m_QmLaserSize, &g_Config.m_QmLaserSize, Button, Localize("Laser size"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
				DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-opacity", &g_Config.m_QmLaserAlpha, &g_Config.m_QmLaserAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserRoundCaps, "appearance-laser-rounded-caps", Localize("Rounded caps"), &g_Config.m_QmLaserRoundCaps, &EnhancedCardContent, LineSize, 0.0f, AppearanceBodySize);
				if(g_Config.m_QmLaserEnhanced)
				{
					EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
					EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
					DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-pulse-speed", &g_Config.m_QmLaserPulseSpeed, &g_Config.m_QmLaserPulseSpeed, Button, Localize("Pulse speed"), 10, 500);
					EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
					EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
					DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-pulse-amplitude", &g_Config.m_QmLaserPulseAmplitude, &g_Config.m_QmLaserPulseAmplitude, Button, Localize("Pulse amplitude"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
			});
			vCards.back().m_Measure = [ResolveLaserEnhancedMinCardHeight](float) { return ResolveLaserEnhancedMinCardHeight(); };
			vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_QmLaserEnhanced != 0);
			vCards.back().m_PreLayoutInput = [this, LineSize](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CUIRect ToggleRow;
				Content.HSplitTop(LineSize, &ToggleRow, &Content);
				if(!Ui()->DoButtonLogic(&g_Config.m_QmLaserEnhanced, 0, &ToggleRow, BUTTONFLAG_LEFT))
					return false;
				g_Config.m_QmLaserEnhanced ^= 1;
				return true;
			};
			AddCard(11, LaserColorMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect ColorCardContent = ContentRect;
				// ***** Weapons ***** //
				DoAppearanceHeading(ColorCardContent, "appearance-weapons-title", Localize("Weapons"), HeadlineFontSize, HeadlineHeight);
				ColorCardContent.HSplitTop(MarginSmall, nullptr, &ColorCardContent);

				// General weapon laser settings
				static CButtonContainer s_LaserRifleOutResetId, s_LaserRifleInResetId, s_LaserShotgunOutResetId, s_LaserShotgunInResetId;

				ColorHSLA LaserRifleOutlineColor = DoLine_ColorPicker(&s_LaserRifleOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Rifle Laser Outline Color"), &g_Config.m_ClLaserRifleOutlineColor, ColorRGBA(0.074402f, 0.074402f, 0.247166f, 1.0f), false);
				ColorHSLA LaserRifleInnerColor = DoLine_ColorPicker(&s_LaserRifleInResetId, AppearanceMetrics, &ColorCardContent, Localize("Rifle Laser Inner Color"), &g_Config.m_ClLaserRifleInnerColor, ColorRGBA(0.498039f, 0.498039f, 1.0f, 1.0f), false);
				ColorHSLA LaserShotgunOutlineColor = DoLine_ColorPicker(&s_LaserShotgunOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Shotgun Laser Outline Color"), &g_Config.m_ClLaserShotgunOutlineColor, ColorRGBA(0.125490f, 0.098039f, 0.043137f, 1.0f), false);
				ColorHSLA LaserShotgunInnerColor = DoLine_ColorPicker(&s_LaserShotgunInResetId, AppearanceMetrics, &ColorCardContent, Localize("Shotgun Laser Inner Color"), &g_Config.m_ClLaserShotgunInnerColor, ColorRGBA(0.570588f, 0.417647f, 0.252941f, 1.0f), false);

				// ***** Entities ***** //
				ColorCardContent.HSplitTop(10.0f, nullptr, &ColorCardContent);
				DoAppearanceHeading(ColorCardContent, "appearance-entities-title", Localize("Entities"), HeadlineFontSize, HeadlineHeight);
				ColorCardContent.HSplitTop(MarginSmall, nullptr, &ColorCardContent);

				// General entity laser settings
				static CButtonContainer s_LaserDoorOutResetId, s_LaserDoorInResetId, s_LaserFreezeOutResetId, s_LaserFreezeInResetId, s_LaserDraggerOutResetId, s_LaserDraggerInResetId;

				ColorHSLA LaserDoorOutlineColor = DoLine_ColorPicker(&s_LaserDoorOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Door Laser Outline Color"), &g_Config.m_ClLaserDoorOutlineColor, ColorRGBA(0.0f, 0.131372f, 0.096078f, 1.0f), false);
				ColorHSLA LaserDoorInnerColor = DoLine_ColorPicker(&s_LaserDoorInResetId, AppearanceMetrics, &ColorCardContent, Localize("Door Laser Inner Color"), &g_Config.m_ClLaserDoorInnerColor, ColorRGBA(0.262745f, 0.760784f, 0.639215f, 1.0f), false);
				ColorHSLA LaserFreezeOutlineColor = DoLine_ColorPicker(&s_LaserFreezeOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Freeze Laser Outline Color"), &g_Config.m_ClLaserFreezeOutlineColor, ColorRGBA(0.131372f, 0.123529f, 0.182352f, 1.0f), false);
				ColorHSLA LaserFreezeInnerColor = DoLine_ColorPicker(&s_LaserFreezeInResetId, AppearanceMetrics, &ColorCardContent, Localize("Freeze Laser Inner Color"), &g_Config.m_ClLaserFreezeInnerColor, ColorRGBA(0.482352f, 0.443137f, 0.564705f, 1.0f), false);
				ColorHSLA LaserDraggerOutlineColor = DoLine_ColorPicker(&s_LaserDraggerOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Dragger Outline Color"), &g_Config.m_ClLaserDraggerOutlineColor, ColorRGBA(0.1640625f, 0.015625f, 0.015625f, 1.0f), false);
				ColorHSLA LaserDraggerInnerColor = DoLine_ColorPicker(&s_LaserDraggerInResetId, AppearanceMetrics, &ColorCardContent, Localize("Dragger Inner Color"), &g_Config.m_ClLaserDraggerInnerColor, ColorRGBA(.8666666f, .3725490f, .3725490f, 1.0f), false);

				static CButtonContainer s_AllToRifleResetId, s_AllToDefaultResetId;

				ColorCardContent.HSplitTop(4 * MarginSmall, nullptr, &ColorCardContent);
				ColorCardContent.HSplitTop(AppearanceMetrics.m_ButtonHeight, &Button, &ColorCardContent);
				if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, APPEARANCE_TAB_LASER, &s_AllToRifleResetId, "appearance-laser-set-all-to-rifle", Localize("Set all to Rifle"), 0, &Button))
				{
					g_Config.m_ClLaserShotgunOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserShotgunInnerColor = g_Config.m_ClLaserRifleInnerColor;
					g_Config.m_ClLaserDoorOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserDoorInnerColor = g_Config.m_ClLaserRifleInnerColor;
					g_Config.m_ClLaserFreezeOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserFreezeInnerColor = g_Config.m_ClLaserRifleInnerColor;
					g_Config.m_ClLaserDraggerOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserDraggerInnerColor = g_Config.m_ClLaserRifleInnerColor;
				}

				// values taken from the CL commands
				ColorCardContent.HSplitTop(2 * MarginSmall, nullptr, &ColorCardContent);
				ColorCardContent.HSplitTop(AppearanceMetrics.m_ButtonHeight, &Button, &ColorCardContent);
				if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, APPEARANCE_TAB_LASER, &s_AllToDefaultResetId, "appearance-laser-reset-defaults", Localize("Reset to defaults"), 0, &Button))
				{
					g_Config.m_ClLaserRifleOutlineColor = 11176233;
					g_Config.m_ClLaserRifleInnerColor = 11206591;
					g_Config.m_ClLaserShotgunOutlineColor = 1866773;
					g_Config.m_ClLaserShotgunInnerColor = 1467241;
					g_Config.m_ClLaserDoorOutlineColor = 7667473;
					g_Config.m_ClLaserDoorInnerColor = 7701379;
					g_Config.m_ClLaserFreezeOutlineColor = 11613223;
					g_Config.m_ClLaserFreezeInnerColor = 12001153;
					g_Config.m_ClLaserDraggerOutlineColor = 57618;
					g_Config.m_ClLaserDraggerInnerColor = 42398;
				}
			});
			AddCard(12, LaserPreviewMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect PreviewCardContent = ContentRect;
				const ColorHSLA LaserRifleOutlineColor = ColorHSLA(g_Config.m_ClLaserRifleOutlineColor);
				const ColorHSLA LaserRifleInnerColor = ColorHSLA(g_Config.m_ClLaserRifleInnerColor);
				const ColorHSLA LaserShotgunOutlineColor = ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor);
				const ColorHSLA LaserShotgunInnerColor = ColorHSLA(g_Config.m_ClLaserShotgunInnerColor);
				const ColorHSLA LaserDoorOutlineColor = ColorHSLA(g_Config.m_ClLaserDoorOutlineColor);
				const ColorHSLA LaserDoorInnerColor = ColorHSLA(g_Config.m_ClLaserDoorInnerColor);
				const ColorHSLA LaserFreezeOutlineColor = ColorHSLA(g_Config.m_ClLaserFreezeOutlineColor);
				const ColorHSLA LaserFreezeInnerColor = ColorHSLA(g_Config.m_ClLaserFreezeInnerColor);
				const ColorHSLA LaserDraggerOutlineColor = ColorHSLA(g_Config.m_ClLaserDraggerOutlineColor);
				const ColorHSLA LaserDraggerInnerColor = ColorHSLA(g_Config.m_ClLaserDraggerInnerColor);
				// ***** Laser Preview ***** //

				CUIRect LaserPreviewRect;
				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);
			});
		}
	};
	uint64_t AppearanceLayoutRevision = static_cast<uint64_t>(AppearanceTab & 0xff);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ NamePlatePreviewMeasureRevision;
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmNameplateEffectAutoLod != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(RenderOnly ? 1 : 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowhudDDRace != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowFreezeBars != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChat != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmChatLogAutoSave != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClChatOld != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChatSystem != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChatFriends != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChatTeamMembersOnly != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmShowChatClient != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(std::clamp(g_Config.m_ClChatFontSize, 0, 255));
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(std::clamp(g_Config.m_ClChatWidth, 0, 1023));
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(str_quickhash(Client()->PlayerName()));
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ qm_card_catalog::NameplateMeasureContentRevision();
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowHookCollOwn != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowHookCollOther != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmLaserEnhanced != 0);
	if(pCards != nullptr)
	{
		BuildDefinitions(*pCards);
	}
	return AppearanceLayoutRevision;
}
