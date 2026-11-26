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
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsIconOptions.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogInternal.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/chat_gradient.h>
#include <game/client/components/qmclient/chat_message_filter.h>
#include <game/client/components/qmclient/chat_scrollbar.h>
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
	CUIRect Button{};
	const SSettingsContentMetrics AppearanceMetrics = Ctx.m_Metrics;
	const float AppearanceUiScale = AppearanceMetrics.m_UiScale;
	const float AppearanceBodySize = AppearanceMetrics.m_BodySize;

	const float LineSize = AppearanceMetrics.m_LineHeight;
	const float ColorPickerRowHeight = AppearanceMetrics.m_ButtonHeight + AppearanceMetrics.m_LineSpacing;
	const float HeadlineFontSize = AppearanceMetrics.m_HeadlineSize;
	const float HeadlineHeight = AppearanceMetrics.m_LineHeight + AppearanceMetrics.m_LineSpacing * 2.0f;
	const float RowSpacing = AppearanceMetrics.m_LineSpacing;

	CUIRect ContentView = MainView;
	// 标题行也是普通行：行距由它自己声明，行后不再补 Space。
	// 与 §9.1 的 DoSettingsSectionHeading 同档：字号取 Metrics.m_HeadlineSize、段前距取
	// SectionGap。这里保留局部 lambda 是因为外观卡还要走 SettingsTextElement +
	// DoSettingsLabelStreamed 的文本复用与搜索高亮，包一层 helper 要多传整套页面上下文；
	// 与 helper 的差别只剩标题颜色（这里沿用设置页默认文本色，helper 固定次要色）。
	auto DoAppearanceHeading = [this, AppearanceTab](CUIRect &View, const char *pTextId, const char *pText, float FontSize, float LineHeight, float SpacingBefore) {
		CSettingsContentLayoutFlow Flow(View);
		CUIRect Heading;
		if(!Flow.Control(pTextId, LineHeight, Heading, SpacingBefore))
			return;
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
		"deck:appearance-hud-main", "deck:appearance-hud-ddrace", "deck:appearance-scoreboard", "deck:appearance-chat-settings", "deck:appearance-chat-messages", "deck:appearance-chat-preview", "deck:appearance-name-plate-settings", "deck:appearance-name-plate-preview", "deck:appearance-hook-collision-main", "deck:appearance-hook-collision-preview", "deck:appearance-info-messages", "deck:appearance-laser-enhanced", "deck:appearance-laser-colors", "deck:appearance-laser-preview"};
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
			g_Config.m_QmNameplateKeysLeftOffsetX,
			g_Config.m_QmNameplateKeysLeftOffsetY,
			g_Config.m_QmNameplateKeysUpOffsetX,
			g_Config.m_QmNameplateKeysUpOffsetY,
			g_Config.m_QmNameplateKeysRightOffsetX,
			g_Config.m_QmNameplateKeysRightOffsetY,
			g_Config.m_QmNameplateOffsetsLinked,
			g_Config.m_QmNameplateDummyNameOffsetX,
			g_Config.m_QmNameplateDummyNameOffsetY,
			g_Config.m_QmNameplateDummyClanOffsetX,
			g_Config.m_QmNameplateDummyClanOffsetY,
			g_Config.m_QmNameplateDummyHookOffsetX,
			g_Config.m_QmNameplateDummyHookOffsetY,
			g_Config.m_QmNameplateDummyCoordsOffsetX,
			g_Config.m_QmNameplateDummyCoordsOffsetY,
			g_Config.m_QmNameplateDummyKeysLeftOffsetX,
			g_Config.m_QmNameplateDummyKeysLeftOffsetY,
			g_Config.m_QmNameplateDummyKeysUpOffsetX,
			g_Config.m_QmNameplateDummyKeysUpOffsetY,
			g_Config.m_QmNameplateDummyKeysRightOffsetX,
			g_Config.m_QmNameplateDummyKeysRightOffsetY,
			g_Config.m_QmNameplateDummyWaterHammerOffsetX,
			g_Config.m_QmNameplateDummyWaterHammerOffsetY,
			g_Config.m_QmNameplateDummyWaterHookOffsetX,
			g_Config.m_QmNameplateDummyWaterHookOffsetY,
			g_Config.m_QmNameplateWaterHammerOffsetX,
			g_Config.m_QmNameplateWaterHammerOffsetY,
			g_Config.m_QmNameplateWaterHookOffsetX,
			g_Config.m_QmNameplateWaterHookOffsetY,
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
	const auto BuildDefinitions = [=, this](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(std::size(aAppearanceIds));
		const auto DeclareDefinition = [&Ctx](SSettingsCardDefinition &Definition) {
			const auto Render = Definition.m_Render;
			Definition.m_RenderMeasured = [Render](CUIRect &Rect) { Render(Rect); };
			qm_card_catalog::UseDeclaredModuleContent(Ctx, Definition);
		};
		// 行距由行自己声明（声明阶段记到上一行末尾），行后不再补 Space：卡底不会多留一档。
		const auto DeclaredCheckbox = [this](int Page, int Tab, const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pView, float Height, float Spacing, float FontSize) {
			CSettingsContentLayoutFlow Flow(*pView);
			CUIRect Row;
			return Flow.Control(pTextId, Height, Row, Spacing) && DoSettingsButton_CheckBoxAutoVMarginAndSet(Page, Tab, pId, pTextId, pText, pValue, &Row, Height, 0.0f, FontSize);
		};
		const auto AddCard = [&vCards, &CardSpec, &DeclareDefinition](size_t Index, float ContentHeight, FSettingsCardRender Render) {
			const SSettingsCardSpec Spec = CardSpec(Index);
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			if(Index == 9 || Index == 13)
			{
				const std::string Id = std::string(Spec.m_pStableId) + ":preview";
				Definition.m_Render = [Render = std::move(Render), Id, ContentHeight](CUIRect Rect) {
					CSettingsContentLayoutFlow Flow(Rect);
					CUIRect Block;
					if(Flow.Control(Id.c_str(), ContentHeight, Block))
						Render(Block);
				};
			}
			else
				Definition.m_Render = std::move(Render);
			DeclareDefinition(Definition);
			vCards.push_back(std::move(Definition));
		};
		const auto AddMeasuredCard = [&vCards, &CardSpec, &Ctx](size_t Index, FSettingsCardMeasure Measure, FSettingsCardRender Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = CardSpec(Index);
			Definition.m_Render = std::move(Render);
			qm_settings_content::UseDeclaredBlockContent(Definition, Ctx.m_UiContext.m_pUi, qm_card_catalog::QmCardRenderHook::TextRenderer(Ctx.m_pMenus), std::move(Measure));
			vCards.push_back(std::move(Definition));
		};

		if(AppearanceTab == APPEARANCE_TAB_HUD)
		{
			CPerfTimer HudShellTimer;
			LogPerfStage(Client(), "appearance_hud_text_cache", HudShellTimer.ElapsedMs(), false, "page=appearance tab=hud section=text_cache");
			LogPerfStage(Client(), "appearance_hud_tab_shell", HudShellTimer.ElapsedMs(), false, "page=appearance tab=hud");
			AddCard(0, 0.0f, [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				CSettingsContentLayoutFlow Flow(LeftView);
				CPerfTimer HudCoreTimer;
				DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhud, "appearance-show-ingame-hud", Localize("Show ingame HUD"), &g_Config.m_ClShowhud, &LeftView, LineSize, RowSpacing, AppearanceBodySize);
				// 主 HUD 卡逐行声明：本地时间秒是“始终显示本地时间”的从属行，
				// 必须紧跟父行插入，不能再塞进 DDRace HUD 卡或整组之后。
				const int MotionLevel = g_Config.m_QmUiMotionLevel;
				const float FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
				const auto ToggleRow = [&](const char *pTextId, const char *pText, int *pValue) {
					CUIRect Row;
					if(Flow.Control(pTextId, LineSize, Row, RowSpacing))
						DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, pValue, pTextId, pText, pValue, &Row, LineSize, 0.0f, AppearanceBodySize);
				};
				ToggleRow("appearance-show-health-shields-ammo", Localize("Show health, shields and ammo"), &g_Config.m_ClShowhudHealthAmmo);
				ToggleRow("appearance-show-score", Localize("Show score"), &g_Config.m_ClShowhudScore);
				ToggleRow("appearance-show-local-time-always", Localize("Show local time always"), &g_Config.m_ClShowLocalTimeAlways);
				static CSettingsConditionalRows s_LocalTimeSecondsRows;
				if(s_LocalTimeSecondsRows.Update(g_Config.m_ClShowLocalTimeAlways != 0, FrameDt, MotionLevel))
				{
					const CSettingsContentRevealScope SecondsScope(Ui(), Flow, s_LocalTimeSecondsRows.Fade(MotionLevel));
					ToggleRow("appearance-show-local-time-seconds", Localize("Show seconds in local time"), &g_Config.m_QmShowLocalTimeSeconds);
				}
				ToggleRow("appearance-show-spectator-cursor", Localize("Show spectator cursor"), &g_Config.m_ClSpecCursor);
				ToggleRow("appearance-show-votes-after-voting", Localize("Show votes window after voting"), &g_Config.m_ClShowVotesAfterVoting);
				LogPerfStage(Client(), "appearance_hud_core_section", HudCoreTimer.ElapsedMs(), false, "page=appearance tab=hud section=core");
			});
			AddCard(1, 0.0f, [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				CSettingsContentLayoutFlow Flow(RightView);
				CPerfTimer HudDdraceTimer;
				// 这张卡的每一行都走同一个行距规则：行自己声明 RowSpacing 行距，
				// 不再混用「组」组件（它自带另一套行距），否则相邻行距会一张卡里两种节奏。
				const int MotionLevel = g_Config.m_QmUiMotionLevel;
				const float FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
				int RowIndex = 0;
				const auto CheckboxRow = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
					CUIRect Row;
					const float SpacingBefore = RowIndex++ == 0 ? 0.0f : RowSpacing;
					if(Flow.Control(pTextId, LineSize, Row, SpacingBefore))
						DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, pId, pTextId, pText, pValue, &Row, LineSize, 0.0f, AppearanceBodySize);
				};
				CheckboxRow(&g_Config.m_ClShowIds, "appearance-show-client-ids", Localize("Show client IDs (scoreboard, chat, spectating)"), &g_Config.m_ClShowIds);
				CheckboxRow(&g_Config.m_ClShowhudDDRace, "appearance-show-ddrace-hud", Localize("Show DDRace HUD"), &g_Config.m_ClShowhudDDRace);
				// DDRace HUD 的子项：开关打开才出现，显隐走布局过渡。
				static CSettingsConditionalRows s_DdraceSubRows;
				if(s_DdraceSubRows.Update(g_Config.m_ClShowhudDDRace != 0, FrameDt, MotionLevel))
				{
					const CSettingsContentRevealScope SubRowsScope(Ui(), Flow, s_DdraceSubRows.Fade(MotionLevel));
					CheckboxRow(&g_Config.m_ClHudRainbowColors, "appearance-hud-rainbow-colors", Localize("HUD rainbow colors"), &g_Config.m_ClHudRainbowColors);
					CheckboxRow(&g_Config.m_ClShowhudJumpsIndicator, "appearance-show-jumps-indicator", Localize("Show jumps indicator"), &g_Config.m_ClShowhudJumpsIndicator);
				}
				CheckboxRow(&g_Config.m_ClShowhudSpectatorCount, "appearance-show-spectator-count", Localize("Show number of spectators"), &g_Config.m_ClShowhudSpectatorCount);
				CheckboxRow(&g_Config.m_ClShowhudDummyActions, "appearance-show-dummy-actions", Localize("Show dummy actions"), &g_Config.m_ClShowhudDummyActions);
				// 卡键、锤子、分身控制与同步状态由同页的 DDRace HUD Pro 卡片提供。
				CheckboxRow(&g_Config.m_ClShowhudPlayerPosition, "appearance-show-player-position", Localize("Show player position"), &g_Config.m_ClShowhudPlayerPosition);
				static CSettingsConditionalRows s_DummyPositionRows;
				if(s_DummyPositionRows.Update(g_Config.m_ClShowhudPlayerPosition != 0, FrameDt, MotionLevel))
				{
					const CSettingsContentRevealScope DummyPositionScope(Ui(), Flow, s_DummyPositionRows.Fade(MotionLevel));
					CheckboxRow(&g_Config.m_QmShowhudDummyPosition, "appearance-show-dummy-position", Localize("Show dummy position in movement info HUD"), &g_Config.m_QmShowhudDummyPosition);
				}
				CheckboxRow(&g_Config.m_ClShowhudPlayerSpeed, "appearance-show-player-speed", Localize("Show player speed"), &g_Config.m_ClShowhudPlayerSpeed);
				static CSettingsConditionalRows s_DummySpeedRows;
				if(s_DummySpeedRows.Update(g_Config.m_ClShowhudPlayerSpeed != 0, FrameDt, MotionLevel))
				{
					const CSettingsContentRevealScope DummySpeedScope(Ui(), Flow, s_DummySpeedRows.Fade(MotionLevel));
					CheckboxRow(&g_Config.m_QmShowhudDummySpeed, "appearance-show-dummy-speed", Localize("Show dummy velocity in movement info HUD"), &g_Config.m_QmShowhudDummySpeed);
				}
				CheckboxRow(&g_Config.m_ClShowhudPlayerAngle, "appearance-show-player-target-angle", Localize("Show player target angle"), &g_Config.m_ClShowhudPlayerAngle);
				static CSettingsConditionalRows s_DummyAngleRows;
				if(s_DummyAngleRows.Update(g_Config.m_ClShowhudPlayerAngle != 0, FrameDt, MotionLevel))
				{
					const CSettingsContentRevealScope DummyAngleScope(Ui(), Flow, s_DummyAngleRows.Fade(MotionLevel));
					CheckboxRow(&g_Config.m_QmShowhudDummyAngle, "appearance-show-dummy-target-angle", Localize("Show dummy angle in movement info HUD"), &g_Config.m_QmShowhudDummyAngle);
				}
				LogPerfStage(Client(), "appearance_hud_ddrace_section", HudDdraceTimer.ElapsedMs(), false, "page=appearance tab=hud section=ddrace");
				CPerfTimer HudFreezeBarsTimer;
				CheckboxRow(&g_Config.m_ClShowFreezeBars, "appearance-show-freeze-bars", Localize("Show freeze bars"), &g_Config.m_ClShowFreezeBars);
				// 冻结条不透明度由“显示冻结条”开关直接控制显隐，不再藏在“个性调整”里。
				static CSettingsConditionalRows s_FreezeBarAlphaRows;
				if(s_FreezeBarAlphaRows.Update(g_Config.m_ClShowFreezeBars != 0, FrameDt, MotionLevel))
				{
					const CSettingsContentRevealScope FreezeBarAlphaScope(Ui(), Flow, s_FreezeBarAlphaRows.Fade(MotionLevel));
					CUIRect AlphaRow;
					if(Flow.Control("appearance-freeze-bars-alpha-inside-freeze", LineSize, AlphaRow, RowSpacing))
						DoAppearanceNumericField(APPEARANCE_TAB_HUD, "appearance-freeze-bars-alpha-inside-freeze", &g_Config.m_ClFreezeBarsAlphaInsideFreeze, &g_Config.m_ClFreezeBarsAlphaInsideFreeze, AlphaRow, Localize("Opacity of freeze bars inside freeze"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				LogPerfStage(Client(), "appearance_hud_freeze_bars_section", HudFreezeBarsTimer.ElapsedMs(), false, "page=appearance tab=hud section=freeze_bars");
			});
			// 计分板颜色组独立成卡：原先塞在 HUD 卡的「个性调整」折叠区里，行 id 保持不变，
			// 设置搜索、文本缓存和既有测试都不需要改 key。
			AddCard(2, 0.0f, [=, this](CUIRect ContentRect) mutable {
				CUIRect ColorView = ContentRect;
				CSettingsContentLayoutFlow Flow(ColorView);
				static CButtonContainer s_AuthedColor, s_SameClanColor, s_FriendsListFriendColor, s_FriendsListClanColor;
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClAuthedPlayerColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_AuthedColor, AppearanceMetrics, &ColorRow, Localize("Authed name color in scoreboard"), &g_Config.m_ClAuthedPlayerColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClAuthedPlayerColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClSameClanColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_SameClanColor, AppearanceMetrics, &ColorRow, Localize("Same clan color in scoreboard"), &g_Config.m_ClSameClanColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClSameClanColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClFriendsListFriendColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_FriendsListFriendColor, AppearanceMetrics, &ColorRow, Localize("Friend color in friends list"), &g_Config.m_ClFriendsListFriendColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClFriendsListFriendColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClFriendsListClanColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_FriendsListClanColor, AppearanceMetrics, &ColorRow, Localize("Clan color in friends list"), &g_Config.m_ClFriendsListClanColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClFriendsListClanColor)), false);
				}
			});
		}
		else if(AppearanceTab == APPEARANCE_TAB_CHAT)
		{
			CChat *pChat = &GameClient()->m_Chat;
			const auto ShowChatPreview = [](bool Friend, bool OtherTeam, bool Mentioned = false) {
				const QmChatMessageFilter::SSettings Settings{g_Config.m_QmChatRelatedOnly != 0, g_Config.m_ClShowChatFriends != 0, g_Config.m_ClShowChatTeamMembersOnly != 0};
				QmChatMessageFilter::SMessage Message;
				Message.m_ClientId = 0;
				Message.m_Friend = Friend;
				Message.m_OtherTeam = OtherTeam;
				Message.m_Mentioned = Mentioned;
				return QmChatMessageFilter::ShouldShow(Settings, Message);
			};
			static int s_AppearanceAlwaysShowChat = 0;
			// ***** Chat ***** //
			SSettingsCardDefinition ChatSettingsDefinition;
			ChatSettingsDefinition.m_Spec = CardSpec(3);
			ChatSettingsDefinition.m_Render = [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				CSettingsContentLayoutFlow Flow(LeftView);

				// General chat settings
				if(Flow.Control("appearance-show-chat", LineSize, Button, RowSpacing))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClShowChat, "appearance-show-chat", Localize("Show chat"), g_Config.m_ClShowChat, &Button))
					{
						g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
					}
				}

				static CSettingsConditionalRows s_AppearanceAlwaysShowChatRows;
				if(s_AppearanceAlwaysShowChatRows.Update(g_Config.m_ClShowChat, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
				{
					const CSettingsContentRevealScope AppearanceAlwaysShowChatRevealScope(Ui(), Flow, s_AppearanceAlwaysShowChatRows.Fade(g_Config.m_QmUiMotionLevel));
					if(Flow.Control("appearance-always-show-chat", LineSize, Button, RowSpacing))
					{
						if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &s_AppearanceAlwaysShowChat, "appearance-always-show-chat", Localize("Always show chat"), g_Config.m_ClShowChat == 2, &Button))
							g_Config.m_ClShowChat = g_Config.m_ClShowChat != 2 ? 2 : 1;
					}
				}
				DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, APPEARANCE_TAB_CHAT, LeftView,
					{
						{&g_Config.m_ClChatTeamColors, "appearance-chat-team-colors", Localize("Show names in chat in team colors")},
						{&g_Config.m_QmChatSponsorBadges, "appearance-chat-sponsor-badges", Localize("Show sponsor badges in chat")},
						{&g_Config.m_ClShowChatFriends, "appearance-chat-friends-only", Localize("Show only chat messages from friends")},
						{&g_Config.m_ClShowChatTeamMembersOnly, "appearance-chat-team-members-only", Localize("Show only chat messages from team members")},
						{&g_Config.m_QmChatRelatedOnly, "appearance-chat-related-only", Localize("Only show messages related to me")},
						{&g_Config.m_QmChatScrollbarRight, "appearance-chat-scrollbar-right", Localize("Show the chat scrollbar on the right")},
					},
					AppearanceMetrics, !RenderOnly);
				DoSettingsToggleGroup(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, APPEARANCE_TAB_CHAT, LeftView,
					{
						{&g_Config.m_QmChatSaveDraft, "appearance-chat-save-draft", Localize("Save unsent chat draft")},
						{&g_Config.m_QmChatLogAutoSave, "appearance-chat-log-auto-save", Localize("Auto save chat log")},
					},
					AppearanceMetrics, !RenderOnly);
				static CSettingsConditionalRows s_AppearanceChatLogKeepDaysRows;
				if(s_AppearanceChatLogKeepDaysRows.Update(g_Config.m_QmChatLogAutoSave, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
				{
					const CSettingsContentRevealScope AppearanceChatLogKeepDaysRevealScope(Ui(), Flow, s_AppearanceChatLogKeepDaysRows.Fade(g_Config.m_QmUiMotionLevel));
					if(Flow.Control("appearance-chat-log-keep-days", LineSize, Button, RowSpacing))
					{
						DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-log-keep-days", &g_Config.m_QmChatLogKeepDays, &g_Config.m_QmChatLogKeepDays, Button, Localize("Chat log retention days"), 0, 3650, &CUi::ms_LinearScrollbarScale, 0, "", Localize("Days"));
					}
				}

				float SectionReveal = 0.0f;
				if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, LeftView, "deck:appearance-chat-settings", AppearanceMetrics, &SectionReveal, RenderOnly))
					return;
				const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

				if(DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClChatOld, "appearance-use-old-chat-style", Localize("Use old chat style"), &g_Config.m_ClChatOld, &LeftView, LineSize, RowSpacing, AppearanceBodySize))
					GameClient()->m_Chat.RebuildChat();

				// Profanity censor checkbox intentionally stays disabled here.

				if(Flow.Control("appearance-chat-font-size", LineSize, Button, RowSpacing))
				{
					if(DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-font-size", &g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, Button, Localize("Chat font size"), 10, 100))
					{
						pChat->EnsureCoherentWidth();
						pChat->RebuildChat();
					}
				}

				if(Flow.Control("appearance-chat-width", LineSize, Button, RowSpacing))
				{
					if(DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-width", &g_Config.m_ClChatWidth, &g_Config.m_ClChatWidth, Button, Localize("Chat width"), 140, 400))
					{
						pChat->EnsureCoherentFontSize();
						pChat->RebuildChat();
					}
				}

				if(Flow.Control("appearance-chat-line-spacing", LineSize, Button, RowSpacing))
				{
					// 行间隔不改变每行内容高度，无需重建布局缓存。
					DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-line-spacing", &g_Config.m_QmChatLineSpacing, &g_Config.m_QmChatLineSpacing, Button, Localize("Chat line spacing"), 0, 32, &CUi::ms_LinearScrollbarScale, 0, " px");
				}

				static CButtonContainer s_BackgroundColor;
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClChatBackgroundColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_BackgroundColor, AppearanceMetrics, &ColorRow, Localize("Chat background color"), &g_Config.m_ClChatBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ClChatBackgroundColor, true)), false, nullptr, true);
				}
			};
			DeclareDefinition(ChatSettingsDefinition);
			vCards.push_back(std::move(ChatSettingsDefinition));
			AddCard(4, 0.0f, [=, this](CUIRect ContentRect) mutable {
				char aBuf[128];
				CUIRect RightView = ContentRect;
				CSettingsContentLayoutFlow Flow(RightView);
				bool VisibilityChanged = DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClShowChatSystem, "appearance-chat-show-system", Localize("System message"), &g_Config.m_ClShowChatSystem, &RightView, LineSize, RowSpacing, AppearanceBodySize);
				VisibilityChanged |= DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClMessageFriend, "appearance-chat-show-friend", Localize("Friend message"), &g_Config.m_ClMessageFriend, &RightView, LineSize, RowSpacing, AppearanceBodySize);
				str_format(aBuf, sizeof(aBuf), "%s (echo)", Localize("Client message"));
				VisibilityChanged |= DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_QmShowChatClient, "appearance-chat-show-client", aBuf, &g_Config.m_QmShowChatClient, &RightView, LineSize, RowSpacing, AppearanceBodySize);
				if(VisibilityChanged)
				{
					pChat->RebuildChat();
					ConfigManager()->Save();
				}
				if(DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_QmChatHideSystemPrefix, "appearance-chat-hide-system-prefix", Localize("Hide system message prefix"), &g_Config.m_QmChatHideSystemPrefix, &RightView, LineSize, RowSpacing, AppearanceBodySize))
				{
					pChat->RebuildChat();
					ConfigManager()->Save();
				}

				float SectionReveal = 0.0f;
				if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, RightView, "deck:appearance-chat-messages", AppearanceMetrics, &SectionReveal, RenderOnly))
					return;
				const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);
				// 下面的消息颜色行来自已改成「行自己声明行距」的外部 helper；它们和上面这一行之间的间距
				// 由「个性调整」折叠行自己预留（取较大者），所以这里不再额外补。

				// ***** Messages ***** //
				// Message Colors and extra settings
				static CButtonContainer s_SystemMessageReset, s_SystemMessageAdd, s_SystemMessageRemove;
				static unsigned s_aSystemMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-system-message", Localize("System message"), &g_Config.m_ClMessageSystemColor, g_Config.m_ClMessageSystemGradient, sizeof(g_Config.m_ClMessageSystemGradient), color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClMessageSystemColor)), &s_SystemMessageReset, &s_SystemMessageAdd, &s_SystemMessageRemove, s_aSystemMessageColorValues, EQmChatGradientRole::SYSTEM, true, nullptr, LineSize, RowSpacing, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);
				static CButtonContainer s_HighlightedMessageReset, s_HighlightedMessageAdd, s_HighlightedMessageRemove;
				static unsigned s_aHighlightedMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-highlighted-message", Localize("Highlighted message"), &g_Config.m_ClMessageHighlightColor, g_Config.m_ClMessageHighlightGradient, sizeof(g_Config.m_ClMessageHighlightGradient), color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClMessageHighlightColor)), &s_HighlightedMessageReset, &s_HighlightedMessageAdd, &s_HighlightedMessageRemove, s_aHighlightedMessageColorValues, EQmChatGradientRole::HIGHLIGHT, true, nullptr, LineSize, RowSpacing, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_TeamMessageReset, s_TeamMessageAdd, s_TeamMessageRemove;
				static unsigned s_aTeamMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-team-message", Localize("Team message"), &g_Config.m_ClMessageTeamColor, g_Config.m_ClMessageTeamGradient, sizeof(g_Config.m_ClMessageTeamGradient), color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClMessageTeamColor)), &s_TeamMessageReset, &s_TeamMessageAdd, &s_TeamMessageRemove, s_aTeamMessageColorValues, EQmChatGradientRole::TEAM, true, nullptr, LineSize, RowSpacing, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_FriendMessageReset, s_FriendMessageAdd, s_FriendMessageRemove;
				static unsigned s_aFriendMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-friend-message", Localize("Friend message"), &g_Config.m_ClMessageFriendColor, g_Config.m_ClMessageFriendGradient, sizeof(g_Config.m_ClMessageFriendGradient), color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClMessageFriendColor)), &s_FriendMessageReset, &s_FriendMessageAdd, &s_FriendMessageRemove, s_aFriendMessageColorValues, EQmChatGradientRole::FRIEND, true, nullptr, LineSize, RowSpacing, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_NormalMessageReset, s_NormalMessageAdd, s_NormalMessageRemove;
				static unsigned s_aNormalMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-normal-message", Localize("Normal message"), &g_Config.m_ClMessageColor, g_Config.m_ClMessageGradient, sizeof(g_Config.m_ClMessageGradient), color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClMessageColor)), &s_NormalMessageReset, &s_NormalMessageAdd, &s_NormalMessageRemove, s_aNormalMessageColorValues, EQmChatGradientRole::NORMAL, true, nullptr, LineSize, RowSpacing, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				str_format(aBuf, sizeof(aBuf), "%s (echo)", Localize("Client message"));
				static CButtonContainer s_ClientMessageReset, s_ClientMessageAdd, s_ClientMessageRemove;
				static unsigned s_aClientMessageColorValues[CMessageGradient::MAX_COLORS];
				// TClient
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-client-message", aBuf, &g_Config.m_ClMessageClientColor, g_Config.m_ClMessageClientGradient, sizeof(g_Config.m_ClMessageClientGradient), color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClMessageClientColor)), &s_ClientMessageReset, &s_ClientMessageAdd, &s_ClientMessageRemove, s_aClientMessageColorValues, EQmChatGradientRole::CLIENT, true, nullptr, LineSize, RowSpacing, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_FriendMessageHeartReset;
				const unsigned OldFriendMessageHeartColor = g_Config.m_ClMessageFriendHeartColor;
				{
					CUIRect ColorRow;
					// 行距由行自己声明：行后不再补 Space，下面的渐变设置块首行同样自带行距。
					if(Flow.Control("appearance-color-ClMessageFriendHeartColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_FriendMessageHeartReset, AppearanceMetrics, &ColorRow, Localize("Friend heart"), &g_Config.m_ClMessageFriendHeartColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClMessageFriendHeartColor)), true);
				}
				if(g_Config.m_ClMessageFriendHeartColor != OldFriendMessageHeartColor)
				{
					pChat->RebuildChat();
					ConfigManager()->Save();
				}
				RenderQmChatGradientSettings(RightView, *pChat, AppearanceMetrics);
			});
			const auto MeasureChatPreview = [this, pChat, RowSpacing, ShowChatPreview](float ContentWidth) {
				const float RealFontSize = pChat->FontSize() * 2.0f;
				const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? pChat->MessagePaddingX() : 0.0f) * 2.0f;
				const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? pChat->MessagePaddingY() : 0.0f) * 2.0f;
				const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? pChat->MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0.0f) * 2.0f;
				const float RealLineSpacing = g_Config.m_QmChatLineSpacing * 2.0f;
				const float ConfiguredLineWidth = g_Config.m_ClChatWidth * 2.0f - RealMsgPaddingX * 1.5f - RealMsgPaddingTee;
				const float CardLineWidth = maximum(RealFontSize, ContentWidth - 2.0f * RowSpacing - QM_CHAT_SCROLLBAR_RESERVE * 2.0f - RealMsgPaddingX * 1.5f - RealMsgPaddingTee);
				const float LineWidth = maximum(RealFontSize, minimum(ConfiguredLineWidth, CardLineWidth));
				char aPlayerName[64];
				str_copy(aPlayerName, Client()->PlayerName());
				float Height = 2.0f * RowSpacing;
				const auto AddPreviewLine = [&](const char *pName, const char *pText, bool AppendNameSeparator = true) {
					char aLine[384];
					str_format(aLine, sizeof(aLine), "%s%s%s", pName, AppendNameSeparator && pName[0] != '\0' ? ": " : "", pText);
					Height += maximum(RealFontSize, TextRender()->TextBoundingBox(RealFontSize, aLine, -1, LineWidth).m_H) + RealMsgPaddingY + RealLineSpacing;
				};
				if(g_Config.m_ClShowChatSystem)
				{
					char aSystemText[128];
					str_format(aSystemText, sizeof(aSystemText), "'%s' entered and joined the game", aPlayerName);
					AddPreviewLine(CChat::SystemMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0), aSystemText, false);
				}
				if(ShowChatPreview(false, true, true))
				{
					char aHighlightText[128];
					str_format(aHighlightText, sizeof(aHighlightText), "Hey, how are you %s?", aPlayerName);
					AddPreviewLine("Random Tee", aHighlightText);
				}
				if(ShowChatPreview(false, false))
					AddPreviewLine("Your Teammate", "Let's speedrun this!");
				if(ShowChatPreview(true, true))
					AddPreviewLine("Friend", "Hello there");
				if(ShowChatPreview(false, true))
					AddPreviewLine("Spammer", "Hey fools, I'm spamming here!");
				if(g_Config.m_QmShowChatClient)
					AddPreviewLine(CChat::ClientMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0), "Echo command executed", false);
				return maximum(Height, 2.0f * RowSpacing + RealFontSize + RealMsgPaddingY);
			};
			AddMeasuredCard(5, MeasureChatPreview, [=, this](CUIRect ContentRect) mutable {
				if(Ui()->DrawingSuppressed())
					return;
			// 和头衔卡的「发言预览」共用同一份聊天框预览实现。
				CMenus *pMenus = this;
			CChat *pChat = &pMenus->GameClient()->m_Chat;
			const auto ShowChatPreview = [](bool Friend, bool OtherTeam, bool Mentioned = false) {
				const QmChatMessageFilter::SSettings Settings{g_Config.m_QmChatRelatedOnly != 0, g_Config.m_ClShowChatFriends != 0, g_Config.m_ClShowChatTeamMembersOnly != 0};
				QmChatMessageFilter::SMessage Message;
				Message.m_ClientId = 0;
				Message.m_Friend = Friend;
				Message.m_OtherTeam = OtherTeam;
				Message.m_Mentioned = Mentioned;
				return QmChatMessageFilter::ShouldShow(Settings, Message);
			};
			char aBuf[128];
			CUIRect PreviewView = ContentRect;
			// ***** Chat Preview ***** //
			PreviewView.Draw(ui_token::color::PREVIEW_SURFACE, IGraphics::CORNER_ALL, ui_token::radius::BASE);
			// 底部预留出来的高度不排消息行，调用方在那里画自己的那一行。
			PreviewView.Margin(RowSpacing, &PreviewView);
			const CUIRect PreviewRailBounds = PreviewView;
			const float ScrollbarReserve = std::min(PreviewView.w, QM_CHAT_SCROLLBAR_RESERVE * 2.0f);
			if(g_Config.m_QmChatScrollbarRight)
				PreviewView.VSplitRight(ScrollbarReserve, &PreviewView, nullptr);
			else
				PreviewView.VSplitLeft(ScrollbarReserve, nullptr, &PreviewView);

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
			// 行间隔与游戏内 AdvanceChatLineStack 对齐：按 2 倍缩放换算，堆叠推进时消耗，底色矩形不包含。
			const float RealLineSpacing = g_Config.m_QmChatLineSpacing * 2.0f;

			const float X = RealMsgPaddingX / 2.0f + PreviewView.x;
			float Y = PreviewView.y;
			const float ConfiguredLineWidth = g_Config.m_ClChatWidth * 2.0f - RealMsgPaddingX * 1.5f - RealMsgPaddingTee;
			const float CardLineWidth = maximum(RealFontSize, PreviewView.w - RealMsgPaddingX * 1.5f - RealMsgPaddingTee);
			float LineWidth = maximum(RealFontSize, minimum(ConfiguredLineWidth, CardLineWidth));

			str_copy(aBuf, pMenus->Client()->PlayerName());

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
					pMenus->GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_FORCE);
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
							pMenus->TextRender()->TextColor(FriendHeartColor);
						pMenus->TextRender()->TextEx(&LocalCursor, "♥ ", -1);
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
					pMenus->TextRender()->TextColor(NameColor);

				pMenus->TextRender()->TextEx(&LocalCursor, aClientId);
				pMenus->TextRender()->TextEx(&LocalCursor, Line.m_aName);

				if(Line.m_TimesRepeated > 0)
				{
					if(Render)
						pMenus->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
					pMenus->TextRender()->TextEx(&LocalCursor, aCount, -1);
				}

				if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
				{
					if(Render)
						pMenus->TextRender()->TextColor(NameColor);
					pMenus->TextRender()->TextEx(&LocalCursor, ": ", -1);
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
						pMenus->TextRender()->TextColor(HighlightedColor);
					else if(Line.m_Friend && g_Config.m_ClMessageFriend)
						pMenus->TextRender()->TextColor(FriendColor);
					else if(Line.m_Team)
						pMenus->TextRender()->TextColor(TeamColor);
					else if(Line.m_Player)
						pMenus->TextRender()->TextColor(NormalColor);
				}

				const EQmChatGradientRole Role = Line.m_Highlighted                          ? EQmChatGradientRole::HIGHLIGHT :
								 Line.m_Friend && g_Config.m_ClMessageFriend ? EQmChatGradientRole::FRIEND :
								 Line.m_Team                                 ? EQmChatGradientRole::TEAM :
								 Line.m_Player                               ? EQmChatGradientRole::NORMAL :
								 Line.m_Client                               ? EQmChatGradientRole::CLIENT :
													       EQmChatGradientRole::SYSTEM;
				const auto Gradient = QmChatGradientStyle(g_Config, Role, pMenus->TextRender()->GetTextColor());
				const CQmChatGradientPaint Paint(pMenus->TextRender(), AppendCursor, Line.m_aText, &Gradient);
				pMenus->TextRender()->TextEx(&AppendCursor, Line.m_aText, -1);
				if(Render)
					pMenus->TextRender()->TextColor(pMenus->TextRender()->DefaultTextColor());

				return vec2{LocalCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth, AppendCursor.Height() + RealMsgPaddingY + RealLineSpacing};
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
				SetPreviewLine(PREVIEW_CLIENT, -1, CChat::ClientMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0), "Echo command executed", FLAG_CLIENT, 0);
			}

			SetLineSkin(1, pMenus->GameClient()->m_Skins.Find("pinky"));
			SetLineSkin(2, pMenus->GameClient()->m_Skins.Find("default"));
			SetLineSkin(3, pMenus->GameClient()->m_Skins.Find("cammostripes"));
			SetLineSkin(4, pMenus->GameClient()->m_Skins.Find("beast"));

			// Backgrounds first
			if(!g_Config.m_ClChatOld)
			{
				const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true));

				float TempY = Y;
				// 预览是 2 倍尺度：单行行高按同一倍数折算，圆角与聊天框同一条派生
				// （单行胶囊、多行圆角矩形），预览里多行消息才能看到应有的圆角矩形。
				const float SingleLinePreviewHeight = (pChat->FontSize() + pChat->MessagePaddingY()) * 2.0f;

				auto &&RenderMessageBackground = [&](int LineIndex) {
					const auto Size = RenderPreview(LineIndex, 0, 0, false);
					const CUIRect MessageBackground{PreviewView.x, TempY - RealMsgPaddingY / 2.0f, PreviewView.w, Size.y - RealLineSpacing};
					const bool SingleLine = ui_token::chat::BubbleIsSingleLine(MessageBackground.h, SingleLinePreviewHeight);
					DrawRoundedSurface(pMenus->Ui(), MessageBackground, BackgroundColor, ColorRGBA(), ui_token::chat::BubbleRadius(SingleLine, MessageBackground.h, SingleLinePreviewHeight));
					return Size.y;
				};

				if(g_Config.m_ClShowChatSystem)
				{
					TempY += RenderMessageBackground(PREVIEW_SYS);
				}

				if(ShowChatPreview(false, true, true))
					TempY += RenderMessageBackground(PREVIEW_HIGHLIGHT);
				if(ShowChatPreview(false, false))
					TempY += RenderMessageBackground(PREVIEW_TEAM);

				if(ShowChatPreview(true, true))
					TempY += RenderMessageBackground(PREVIEW_FRIEND);

				if(ShowChatPreview(false, true))
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

			if(ShowChatPreview(false, true, true))
			{
				// Highlighted
				if(!g_Config.m_ClChatOld)
					pMenus->RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_HIGHLIGHT].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				Y += RenderPreview(PREVIEW_HIGHLIGHT, X, Y).y;
			}

			if(ShowChatPreview(false, false))
			{
				// Team
				if(!g_Config.m_ClChatOld)
					pMenus->RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_TEAM].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				Y += RenderPreview(PREVIEW_TEAM, X, Y).y;
			}

			// Friend
			if(!g_Config.m_ClChatOld && ShowChatPreview(true, true))
				pMenus->RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_FRIEND].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			if(ShowChatPreview(true, true))
				Y += RenderPreview(PREVIEW_FRIEND, X, Y).y;

			// Normal
			if(ShowChatPreview(false, true))
			{
				if(!g_Config.m_ClChatOld)
					pMenus->RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_SPAMMER].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				Y += RenderPreview(PREVIEW_SPAMMER, X, Y).y;
			}
			// Client
			if(g_Config.m_QmShowChatClient)
			{
				Y += RenderPreview(PREVIEW_CLIENT, X, Y).y;

				// 滚动条照旧画在预览边缘，和聊天页的观感保持一致。
				const CUIRect Rail = QmChatScrollbarRail(PreviewRailBounds, PreviewRailBounds.y, std::min(PreviewRailBounds.h, std::max(RealFontSize, Y - PreviewRailBounds.y)), g_Config.m_QmChatScrollbarRight != 0, 2.0f);
				const float HandleHeight = QmChatScrollbarHandleHeight(Rail.h, 3, 6, 2.0f);
				const CUIRect Handle = QmChatScrollbarHandle(Rail, HandleHeight, 1.0f);
				QmDrawChatScrollbar(pMenus->Ui(), Rail, Handle, false);
			} });
		}
		else if(AppearanceTab == APPEARANCE_TAB_NAME_PLATE)
		{
			qm_card_catalog::SQmCardBuildContext CardBuild;
			CardBuild.m_pMenus = this;
			CardBuild.m_ReadOnly = RenderOnly;
			CardBuild.m_Page = Ctx.m_Page;
			CardBuild.m_Metrics = AppearanceMetrics;
			CardBuild.m_UiContext = AppearanceCardCtx;
			qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::NameplateCardStableIds(), vCards);
			const float NamePlatePreviewMinAreaHeight = std::clamp(190.0f * AppearanceUiScale, 160.0f, 210.0f);
			const float NamePlatePreviewControlsHeight = ResolveSettingsRowsHeight(4, LineSize, RowSpacing) + RowSpacing + AppearanceMetrics.m_ButtonHeight;
			const auto ResolveNamePlatePreviewCardHeight = [this, NamePlatePreviewMinAreaHeight, NamePlatePreviewControlsHeight, RowSpacing](float) {
				return maximum(NamePlatePreviewMinAreaHeight, GameClient()->m_NamePlates.MeasurePreviewAreaHeight()) + RowSpacing + NamePlatePreviewControlsHeight;
			};

			AddMeasuredCard(7, ResolveNamePlatePreviewCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				CUIRect PreviewArea, Controls;
				RightView.HSplitBottom(NamePlatePreviewControlsHeight, &PreviewArea, &Controls);
				PreviewArea.HSplitBottom(RowSpacing, &PreviewArea, nullptr);
				PreviewArea.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_ALL, ui_token::radius::CARD);
				const auto NextPreviewControl = [&](CUIRect &Row) {
					ConsumeSettingsMeasuredRow(Controls, LineSize, &Row);
					if(Controls.h > 0.0f)
						Controls.HSplitTop(RowSpacing, nullptr, &Controls);
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

				NextPreviewControl(Button);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, &g_Config.m_QmNameplateMultiSelect, "appearance-nameplate-multi-select", Localize("Multi-select move"), g_Config.m_QmNameplateMultiSelect, &Button))
					g_Config.m_QmNameplateMultiSelect = g_Config.m_QmNameplateMultiSelect ? 0 : 1;

				NextPreviewControl(Button);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, &g_Config.m_QmNameplateOffsetsLinked, "appearance-nameplate-offsets-linked", Localize("Share layout with dummy"), g_Config.m_QmNameplateOffsetsLinked, &Button))
					g_Config.m_QmNameplateOffsetsLinked = g_Config.m_QmNameplateOffsetsLinked ? 0 : 1;

				Controls.HSplitTop(AppearanceMetrics.m_ButtonHeight, &Button, &Controls);
				if(Controls.h > 0.0f)
					Controls.HSplitTop(RowSpacing, nullptr, &Controls);
				static CButtonContainer s_NameplateResetLayoutButton;
				if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, APPEARANCE_TAB_NAME_PLATE, &s_NameplateResetLayoutButton, "appearance-nameplate-reset-layout", Localize("Reset layout"), 0, &Button))
				{
					g_Config.m_QmNameplateKeysLeftOffsetX = CConfig::ms_QmNameplateKeysLeftOffsetX;
					g_Config.m_QmNameplateKeysLeftOffsetY = CConfig::ms_QmNameplateKeysLeftOffsetY;
					g_Config.m_QmNameplateKeysUpOffsetX = CConfig::ms_QmNameplateKeysUpOffsetX;
					g_Config.m_QmNameplateKeysUpOffsetY = CConfig::ms_QmNameplateKeysUpOffsetY;
					g_Config.m_QmNameplateKeysRightOffsetX = CConfig::ms_QmNameplateKeysRightOffsetX;
					g_Config.m_QmNameplateKeysRightOffsetY = CConfig::ms_QmNameplateKeysRightOffsetY;
					g_Config.m_QmNameplateDummyNameOffsetX = CConfig::ms_QmNameplateDummyNameOffsetX;
					g_Config.m_QmNameplateDummyNameOffsetY = CConfig::ms_QmNameplateDummyNameOffsetY;
					g_Config.m_QmNameplateDummyClanOffsetX = CConfig::ms_QmNameplateDummyClanOffsetX;
					g_Config.m_QmNameplateDummyClanOffsetY = CConfig::ms_QmNameplateDummyClanOffsetY;
					g_Config.m_QmNameplateDummyHookOffsetX = CConfig::ms_QmNameplateDummyHookOffsetX;
					g_Config.m_QmNameplateDummyHookOffsetY = CConfig::ms_QmNameplateDummyHookOffsetY;
					g_Config.m_QmNameplateDummyCoordsOffsetX = CConfig::ms_QmNameplateDummyCoordsOffsetX;
					g_Config.m_QmNameplateDummyCoordsOffsetY = CConfig::ms_QmNameplateDummyCoordsOffsetY;
					g_Config.m_QmNameplateDummyKeysLeftOffsetX = CConfig::ms_QmNameplateDummyKeysLeftOffsetX;
					g_Config.m_QmNameplateDummyKeysLeftOffsetY = CConfig::ms_QmNameplateDummyKeysLeftOffsetY;
					g_Config.m_QmNameplateDummyKeysUpOffsetX = CConfig::ms_QmNameplateDummyKeysUpOffsetX;
					g_Config.m_QmNameplateDummyKeysUpOffsetY = CConfig::ms_QmNameplateDummyKeysUpOffsetY;
					g_Config.m_QmNameplateDummyKeysRightOffsetX = CConfig::ms_QmNameplateDummyKeysRightOffsetX;
					g_Config.m_QmNameplateDummyKeysRightOffsetY = CConfig::ms_QmNameplateDummyKeysRightOffsetY;
					g_Config.m_QmNameplateDummyWaterHammerOffsetX = CConfig::ms_QmNameplateDummyWaterHammerOffsetX;
					g_Config.m_QmNameplateDummyWaterHammerOffsetY = CConfig::ms_QmNameplateDummyWaterHammerOffsetY;
					g_Config.m_QmNameplateDummyWaterHookOffsetX = CConfig::ms_QmNameplateDummyWaterHookOffsetX;
					g_Config.m_QmNameplateDummyWaterHookOffsetY = CConfig::ms_QmNameplateDummyWaterHookOffsetY;
					g_Config.m_QmNameplateWaterHammerOffsetX = CConfig::ms_QmNameplateWaterHammerOffsetX;
					g_Config.m_QmNameplateWaterHammerOffsetY = CConfig::ms_QmNameplateWaterHammerOffsetY;
					g_Config.m_QmNameplateWaterHookOffsetX = CConfig::ms_QmNameplateWaterHookOffsetX;
					g_Config.m_QmNameplateWaterHookOffsetY = CConfig::ms_QmNameplateWaterHookOffsetY;
					g_Config.m_QmNameplateCoordsOffsetX = CConfig::ms_QmNameplateCoordsOffsetX;
					g_Config.m_QmNameplateCoordsOffsetY = CConfig::ms_QmNameplateCoordsOffsetY;
					g_Config.m_QmNameplateHookOffsetX = CConfig::ms_QmNameplateHookOffsetX;
					g_Config.m_QmNameplateHookOffsetY = CConfig::ms_QmNameplateHookOffsetY;
					g_Config.m_QmNameplateClanOffsetX = CConfig::ms_QmNameplateClanOffsetX;
					g_Config.m_QmNameplateClanOffsetY = CConfig::ms_QmNameplateClanOffsetY;
					g_Config.m_QmNameplateNameOffsetX = CConfig::ms_QmNameplateNameOffsetX;
					g_Config.m_QmNameplateNameOffsetY = CConfig::ms_QmNameplateNameOffsetY;
				}
				int Dummy = g_Config.m_ClDummy != (m_DummyNamePlatePreview ? 1 : 0);
				GameClient()->m_NamePlates.RenderNamePlatePreview(PreviewArea, Dummy); });
		}
		else if(AppearanceTab == APPEARANCE_TAB_HOOK_COLLISION)
		{
			static int s_AppearanceAlwaysShowHookCollOwn = 0;
			static int s_AppearanceAlwaysShowHookCollOther = 0;
			const float HookCollisionRightMinCardHeight =
				(50.0f + 4.0f * RowSpacing) * 5.0f + LineSize;
			AddCard(8, 0.0f, [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				CSettingsContentLayoutFlow Flow(LeftView);

				// General hookline settings
				if(Flow.Control("appearance-show-own-hook-collision", LineSize, Button, RowSpacing))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_ClShowHookCollOwn, "appearance-show-own-hook-collision", Localize("Show own player's hook collision line"), g_Config.m_ClShowHookCollOwn, &Button))
					{
						g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn ? 0 : 1;
					}
				}

				static CSettingsConditionalRows s_AppearanceHookCollOwnRows;
				if(s_AppearanceHookCollOwnRows.Update(g_Config.m_ClShowHookCollOwn, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
				{
					const CSettingsContentRevealScope AppearanceHookCollOwnRevealScope(Ui(), Flow, s_AppearanceHookCollOwnRows.Fade(g_Config.m_QmUiMotionLevel));
					if(Flow.Control("appearance-always-show-own-hook-collision", LineSize, Button, RowSpacing))
					{
						if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_AppearanceAlwaysShowHookCollOwn, "appearance-always-show-own-hook-collision", Localize("Always show own player's hook collision line"), g_Config.m_ClShowHookCollOwn == 2, &Button))
							g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
					}
				}

				if(Flow.Control("appearance-show-other-hook-collision", LineSize, Button, RowSpacing))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_ClShowHookCollOther, "appearance-show-other-hook-collision", Localize("Show other players' hook collision lines"), g_Config.m_ClShowHookCollOther, &Button))
					{
						g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther >= 1 ? 0 : 1;
					}
				}

				static CSettingsConditionalRows s_AppearanceHookCollOtherRows;
				if(s_AppearanceHookCollOtherRows.Update(g_Config.m_ClShowHookCollOther, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
				{
					const CSettingsContentRevealScope AppearanceHookCollOtherRevealScope(Ui(), Flow, s_AppearanceHookCollOtherRows.Fade(g_Config.m_QmUiMotionLevel));
					if(Flow.Control("appearance-always-show-other-hook-collision", LineSize, Button, RowSpacing))
					{
						if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_AppearanceAlwaysShowHookCollOther, "appearance-always-show-other-hook-collision", Localize("Always show other players' hook collision lines"), g_Config.m_ClShowHookCollOther == 2, &Button))
							g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
					}
				}

				if(Flow.Control("appearance-hook-collision-own-width", LineSize, Button, RowSpacing))
				{
					DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-own-width", &g_Config.m_ClHookCollSize, &g_Config.m_ClHookCollSize, Button, Localize("Width of your own hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale);
				}

				if(Flow.Control("appearance-hook-collision-other-width", LineSize, Button, RowSpacing))
				{
					DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-other-width", &g_Config.m_ClHookCollSizeOther, &g_Config.m_ClHookCollSizeOther, Button, Localize("Width of others' hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale);
				}

				if(Flow.Control("appearance-hook-collision-opacity", LineSize, Button, RowSpacing))
				{
					DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-opacity", &g_Config.m_ClHookCollAlpha, &g_Config.m_ClHookCollAlpha, Button, Localize("Hook collision line opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				// 旧版单段线：把分段提示线并回一条，恢复原版 DDNet 的观感。
				if(Flow.Control("appearance-revert-hook-line", LineSize, Button, RowSpacing))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_QmRevertHookLine, "appearance-revert-hook-line", Localize("Old single-segment hook collision line"), g_Config.m_QmRevertHookLine, &Button))
						g_Config.m_QmRevertHookLine ^= 1;
				}

				static CButtonContainer s_HookCollNoCollResetId, s_HookCollHookableCollResetId, s_HookCollTeeCollResetId, s_HookCollTipColorResetId, s_HookCollPressedNoCollResetId, s_HookCollPressedHookableResetId, s_HookCollPressedTeeResetId, s_HookCollPressedTipResetId;
				static int s_HookCollToolTip;

				DoAppearanceHeading(LeftView, "appearance-hook-collision-colors-title", Localize("Colors of the hook collision line, in case of a possible collision with:"), HeadlineFontSize, HeadlineHeight, RowSpacing);

				Ui()->RegisterPassiveHotItem(&s_HookCollToolTip, &LeftView);
				GameClient()->m_Tooltips.DoToolTip(&s_HookCollToolTip, &LeftView, Localize("Your movements are not taken into account when calculating the line colors"));
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClHookCollColorNoColl", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_HookCollNoCollResetId, AppearanceMetrics, &ColorRow, Localize("Nothing hookable"), &g_Config.m_ClHookCollColorNoColl, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClHookCollColorNoColl)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClHookCollColorHookableColl", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_HookCollHookableCollResetId, AppearanceMetrics, &ColorRow, Localize("Something hookable"), &g_Config.m_ClHookCollColorHookableColl, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClHookCollColorHookableColl)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClHookCollColorTeeColl", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_HookCollTeeCollResetId, AppearanceMetrics, &ColorRow, Localize("A Tee"), &g_Config.m_ClHookCollColorTeeColl, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClHookCollColorTeeColl)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClHookCollTipColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_HookCollTipColorResetId, AppearanceMetrics, &ColorRow, Localize("Hook line tip"), &g_Config.m_ClHookCollTipColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClHookCollTipColor, true)), false, nullptr, true);
				}
				{
					// 自定义按下后颜色开关：关闭时按下态沿用自动反色
					if(Flow.Control("appearance-hook-coll-pressed-custom", LineSize, Button, RowSpacing))
					{
						if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_QmHookCollPressedCustom, "appearance-hook-coll-pressed-custom", Localize("Custom pressed hook line colors"), g_Config.m_QmHookCollPressedCustom, &Button))
						{
							g_Config.m_QmHookCollPressedCustom = g_Config.m_QmHookCollPressedCustom ? 0 : 1;
						}
						static int s_HookCollPressedCustomToolTip;
						Ui()->RegisterPassiveHotItem(&s_HookCollPressedCustomToolTip, &Button);
						GameClient()->m_Tooltips.DoToolTip(&s_HookCollPressedCustomToolTip, &Button, Localize("Use the custom colors below while the hook key is held"));
					}
				}
				// 按下后四行取色器仅在"自定义按下后颜色"开启时渲染；关闭时不占用布局，
				// 高度过渡由设置卡片外壳按测量变化自动承载。
				if(g_Config.m_QmHookCollPressedCustom != 0)
				{
					{
						// 按下后颜色（未瞄准可钩物体）：开关开启时始终生效，默认为原色的高亮变体
						CUIRect ColorRow;
						if(Flow.Control("appearance-color-QmHookCollPressedNoColl", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						{
							DoLine_ColorPicker(&s_HookCollPressedNoCollResetId, AppearanceMetrics, &ColorRow, Localize("Pressed: Nothing hookable"), &g_Config.m_QmHookCollPressedNoColl, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmHookCollPressedNoColl, true)), false, nullptr, true);
							static int s_HookCollPressedToolTip;
							Ui()->RegisterPassiveHotItem(&s_HookCollPressedToolTip, &ColorRow);
							GameClient()->m_Tooltips.DoToolTip(&s_HookCollPressedToolTip, &ColorRow, Localize("Pressed hook line color; always used when custom pressed colors are enabled"));
						}
					}
					{
						// 按下后颜色（可钩物体）
						CUIRect ColorRow;
						if(Flow.Control("appearance-color-QmHookCollPressedHookable", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						{
							DoLine_ColorPicker(&s_HookCollPressedHookableResetId, AppearanceMetrics, &ColorRow, Localize("Pressed: Something hookable"), &g_Config.m_QmHookCollPressedHookable, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmHookCollPressedHookable, true)), false, nullptr, true);
							static int s_HookCollPressedToolTip;
							Ui()->RegisterPassiveHotItem(&s_HookCollPressedToolTip, &ColorRow);
							GameClient()->m_Tooltips.DoToolTip(&s_HookCollPressedToolTip, &ColorRow, Localize("Pressed hook line color; always used when custom pressed colors are enabled"));
						}
					}
					{
						// 按下后颜色（其他玩家）
						CUIRect ColorRow;
						if(Flow.Control("appearance-color-QmHookCollPressedTee", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						{
							DoLine_ColorPicker(&s_HookCollPressedTeeResetId, AppearanceMetrics, &ColorRow, Localize("Pressed: A Tee"), &g_Config.m_QmHookCollPressedTee, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmHookCollPressedTee, true)), false, nullptr, true);
							static int s_HookCollPressedToolTip;
							Ui()->RegisterPassiveHotItem(&s_HookCollPressedToolTip, &ColorRow);
							GameClient()->m_Tooltips.DoToolTip(&s_HookCollPressedToolTip, &ColorRow, Localize("Pressed hook line color; always used when custom pressed colors are enabled"));
						}
					}
					{
						// 按下后颜色（辅助线提示）
						CUIRect ColorRow;
						if(Flow.Control("appearance-color-QmHookCollPressedTip", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						{
							DoLine_ColorPicker(&s_HookCollPressedTipResetId, AppearanceMetrics, &ColorRow, Localize("Pressed: Hook line tip"), &g_Config.m_QmHookCollPressedTip, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmHookCollPressedTip, true)), false, nullptr, true);
							static int s_HookCollPressedToolTip;
							Ui()->RegisterPassiveHotItem(&s_HookCollPressedToolTip, &ColorRow);
							GameClient()->m_Tooltips.DoToolTip(&s_HookCollPressedToolTip, &ColorRow, Localize("Pressed hook line color; always used when custom pressed colors are enabled"));
						}
					}
				}
			});
			AddCard(9, HookCollisionRightMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				// ***** Hook collisions preview ***** //
				// PressedColor/PressedTipColor：对应分类的"按下后"配置色（开关开启时始终生效），
				// 与 players.cpp 按下态渲染逻辑保持一致
				auto DoHookCollision = [this](const vec2 &Pos, const float &Length, const int &Size, const ColorRGBA &Color, const ColorRGBA &TipColor, const bool &Invert, unsigned PressedColor = 0, unsigned PressedTipColor = 0) {
					ColorRGBA ColorModified = Color;
					ColorRGBA TipColorModified = TipColor;
					if(Invert)
					{
						// 与 players.cpp 按下态渲染一致：开关开启时取对应分类"按下后"颜色（始终生效），开关关闭沿用自动反色
						if(g_Config.m_QmHookCollPressedCustom)
							ColorModified = color_cast<ColorRGBA>(ColorHSLA(PressedColor, true));
						else
							ColorModified = color_invert(ColorModified);
						// 提示段：与 players.cpp 一致，原提示色可见时才套用按下态颜色
						if(g_Config.m_QmHookCollPressedCustom && TipColor.a > 0.0f)
						{
							TipColorModified = color_cast<ColorRGBA>(ColorHSLA(PressedTipColor, true));
						}
					}
					ColorModified = ColorModified.WithAlpha((float)g_Config.m_ClHookCollAlpha / 100);
					TipColorModified = TipColorModified.WithMultipliedAlpha((float)g_Config.m_ClHookCollAlpha / 100);
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
						RenderMap()->RenderTile(Rect.x, Rect.y, TileIndex, TileScale, ui_token::color::NEUTRAL_WHITE);
					}
				};

				// Toggled via checkbox later, inverts some previews
				static bool s_HookCollPressed = false;

				CUIRect PreviewColl;
				const auto PrepareHookPreviewRow = [&](CUIRect &Preview) {
					Preview.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_ALL, ui_token::radius::CARD);
					Preview.Margin(RowSpacing, &Preview);
				};

				// ***** Unhookable Tile Preview *****
				CUIRect PreviewNoColl;
				RightView.HSplitTop(50.0f, &PreviewNoColl, &RightView);
				PrepareHookPreviewRow(PreviewNoColl);
				RightView.HSplitTop(4 * RowSpacing, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewNoColl.x + LeftMargin, PreviewNoColl.y + PreviewNoColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewNoColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), ui_token::color::COLOR_TRANSPARENT, s_HookCollPressed, g_Config.m_QmHookCollPressedNoColl);
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
				RightView.HSplitTop(4 * RowSpacing, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorHookableColl)), ui_token::color::COLOR_TRANSPARENT, s_HookCollPressed, g_Config.m_QmHookCollPressedHookable);
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
				RightView.HSplitTop(4 * RowSpacing, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ui_token::color::COLOR_TRANSPARENT, s_HookCollPressed, g_Config.m_QmHookCollPressedTee);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Hook Dummy Reverse Preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * RowSpacing, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSizeOther, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ui_token::color::COLOR_TRANSPARENT, false);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Hook tip preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * RowSpacing, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollTipColor, true)), s_HookCollPressed, g_Config.m_QmHookCollPressedNoColl, g_Config.m_QmHookCollPressedTip);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Preview +hookcoll pressed toggle *****
				RightView.HSplitTop(LineSize, &Button, &RightView);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_HookCollPressed, "appearance-preview-hook-collisions-pressed", Localize("Preview 'Hook collisions' being pressed"), s_HookCollPressed, &Button))
					s_HookCollPressed = !s_HookCollPressed;
			});
		}
		else if(AppearanceTab == APPEARANCE_TAB_INFO_MESSAGES)
		{
			AddCard(10, 0.0f, [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				CSettingsContentLayoutFlow Flow(LeftView);

				// General info messages settings
				if(Flow.Control("appearance-show-kill-messages", LineSize, Button, RowSpacing))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_INFO_MESSAGES, &g_Config.m_ClShowKillMessages, "appearance-show-kill-messages", Localize("Show kill messages"), g_Config.m_ClShowKillMessages, &Button))
					{
						g_Config.m_ClShowKillMessages ^= 1;
					}
				}

				if(Flow.Control("appearance-show-finish-messages", LineSize, Button, RowSpacing))
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_INFO_MESSAGES, &g_Config.m_ClShowFinishMessages, "appearance-show-finish-messages", Localize("Show finish messages"), g_Config.m_ClShowFinishMessages, &Button))
					{
						g_Config.m_ClShowFinishMessages ^= 1;
					}
				}

				static CButtonContainer s_KillMessageNormalColorId, s_KillMessageHighlightColorId;
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClKillMessageNormalColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_KillMessageNormalColorId, AppearanceMetrics, &ColorRow, Localize("Normal Color"), &g_Config.m_ClKillMessageNormalColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClKillMessageNormalColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClKillMessageHighlightColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_KillMessageHighlightColorId, AppearanceMetrics, &ColorRow, Localize("Highlight Color"), &g_Config.m_ClKillMessageHighlightColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClKillMessageHighlightColor)), false);
				}
			});
		}
		else if(AppearanceTab == APPEARANCE_TAB_LASER)
		{
			const float LaserPreviewHeight = std::clamp(56.0f * AppearanceUiScale, 40.0f, 56.0f);
			const float LaserPreviewMinCardHeight = (LaserPreviewHeight + 2.0f * RowSpacing) * 5.0f;
			AddCard(11, 0.0f, [=, this](CUIRect ContentRect) mutable {
				CUIRect EnhancedCardContent = ContentRect;
				CSettingsContentLayoutFlow Flow(EnhancedCardContent);

				DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserEnhanced, "appearance-laser-effect-enhancement", Localize("Laser effect enhancement"), &g_Config.m_QmLaserEnhanced, &EnhancedCardContent, LineSize, RowSpacing, AppearanceBodySize);
				float SectionReveal = 0.0f;
				if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, EnhancedCardContent, "deck:appearance-laser-enhanced", AppearanceMetrics, &SectionReveal, RenderOnly))
					return;
				const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);
				if(Flow.Control("appearance-laser-glow-intensity", LineSize, Button, RowSpacing))
				{
					DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-glow-intensity", &g_Config.m_QmLaserGlowIntensity, &g_Config.m_QmLaserGlowIntensity, Button, Localize("Glow intensity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				if(Flow.Control("appearance-laser-size", LineSize, Button, RowSpacing))
				{
					DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-size", &g_Config.m_QmLaserSize, &g_Config.m_QmLaserSize, Button, Localize("Laser size"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				if(Flow.Control("appearance-laser-opacity", LineSize, Button, RowSpacing))
				{
					DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-opacity", &g_Config.m_QmLaserAlpha, &g_Config.m_QmLaserAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				DeclaredCheckbox(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserRoundCaps, "appearance-laser-rounded-caps", Localize("Rounded caps"), &g_Config.m_QmLaserRoundCaps, &EnhancedCardContent, LineSize, RowSpacing, AppearanceBodySize);
				static CSettingsConditionalRows s_AppearanceLaserPulseRows;
				if(s_AppearanceLaserPulseRows.Update(g_Config.m_QmLaserEnhanced, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
				{
					const CSettingsContentRevealScope AppearanceLaserPulseRevealScope(Ui(), Flow, s_AppearanceLaserPulseRows.Fade(g_Config.m_QmUiMotionLevel));
					if(Flow.Control("appearance-laser-pulse-speed", LineSize, Button, RowSpacing))
					{
						DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-pulse-speed", &g_Config.m_QmLaserPulseSpeed, &g_Config.m_QmLaserPulseSpeed, Button, Localize("Pulse speed"), 10, 500);
					}
					if(Flow.Control("appearance-laser-pulse-amplitude", LineSize, Button, RowSpacing))
					{
						DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-pulse-amplitude", &g_Config.m_QmLaserPulseAmplitude, &g_Config.m_QmLaserPulseAmplitude, Button, Localize("Pulse amplitude"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					}
				}
			});
			AddCard(12, 0.0f, [=, this](CUIRect ContentRect) mutable {
				CUIRect ColorCardContent = ContentRect;
				CSettingsContentLayoutFlow Flow(ColorCardContent);
				// ***** Weapons ***** //
				float SectionReveal = 0.0f;
				if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, ColorCardContent, "deck:appearance-laser-colors", AppearanceMetrics, &SectionReveal, RenderOnly))
					return;
				const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

				DoAppearanceHeading(ColorCardContent, "appearance-weapons-title", Localize("Weapons"), HeadlineFontSize, HeadlineHeight, RowSpacing);

				// General weapon laser settings
				static CButtonContainer s_LaserRifleOutResetId, s_LaserRifleInResetId, s_LaserShotgunOutResetId, s_LaserShotgunInResetId;

				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserRifleOutlineColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserRifleOutResetId, AppearanceMetrics, &ColorRow, Localize("Rifle Laser Outline Color"), &g_Config.m_ClLaserRifleOutlineColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserRifleOutlineColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserRifleInnerColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserRifleInResetId, AppearanceMetrics, &ColorRow, Localize("Rifle Laser Inner Color"), &g_Config.m_ClLaserRifleInnerColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserRifleInnerColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserShotgunOutlineColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserShotgunOutResetId, AppearanceMetrics, &ColorRow, Localize("Shotgun Laser Outline Color"), &g_Config.m_ClLaserShotgunOutlineColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserShotgunOutlineColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserShotgunInnerColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserShotgunInResetId, AppearanceMetrics, &ColorRow, Localize("Shotgun Laser Inner Color"), &g_Config.m_ClLaserShotgunInnerColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserShotgunInnerColor)), false);
				}

				// ***** Entities ***** //
				Flow.Space(AppearanceMetrics.m_SectionGap);
				// 上面已有分区行距，标题行不再自己声明，避免叠成两档。
				DoAppearanceHeading(ColorCardContent, "appearance-entities-title", Localize("Entities"), HeadlineFontSize, HeadlineHeight, 0.0f);

				// General entity laser settings
				static CButtonContainer s_LaserDoorOutResetId, s_LaserDoorInResetId, s_LaserFreezeOutResetId, s_LaserFreezeInResetId, s_LaserDraggerOutResetId, s_LaserDraggerInResetId;

				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserDoorOutlineColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserDoorOutResetId, AppearanceMetrics, &ColorRow, Localize("Door Laser Outline Color"), &g_Config.m_ClLaserDoorOutlineColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserDoorOutlineColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserDoorInnerColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserDoorInResetId, AppearanceMetrics, &ColorRow, Localize("Door Laser Inner Color"), &g_Config.m_ClLaserDoorInnerColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserDoorInnerColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserFreezeOutlineColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserFreezeOutResetId, AppearanceMetrics, &ColorRow, Localize("Freeze Laser Outline Color"), &g_Config.m_ClLaserFreezeOutlineColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserFreezeOutlineColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserFreezeInnerColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserFreezeInResetId, AppearanceMetrics, &ColorRow, Localize("Freeze Laser Inner Color"), &g_Config.m_ClLaserFreezeInnerColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserFreezeInnerColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserDraggerOutlineColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserDraggerOutResetId, AppearanceMetrics, &ColorRow, Localize("Dragger Outline Color"), &g_Config.m_ClLaserDraggerOutlineColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserDraggerOutlineColor)), false);
				}
				{
					CUIRect ColorRow;
					if(Flow.Control("appearance-color-ClLaserDraggerInnerColor", AppearanceMetrics.m_ButtonHeight, ColorRow, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
						DoLine_ColorPicker(&s_LaserDraggerInResetId, AppearanceMetrics, &ColorRow, Localize("Dragger Inner Color"), &g_Config.m_ClLaserDraggerInnerColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_ClLaserDraggerInnerColor)), false);
				}

				static CButtonContainer s_AllToRifleResetId, s_AllToDefaultResetId;

				// 卡尾动作区：配色列表之后、整卡级「全部设为…」按钮之前，比分区再重一档。
				// 间距由按钮行自己声明（A 型），不要再额外补一格 Space。
				if(Flow.Control("appearance-laser-set-all-to-rifle", AppearanceMetrics.m_ButtonHeight, Button, ui_token::settings::ActionGap(AppearanceMetrics.m_LineSpacing)))
				{
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
				}

				// 重置直接取各配置项的出厂值（CConfig::ms_*），不再手抄一份十进制颜色。
				// 两个操作按钮是相邻两行，取行间档（行自己声明，不再先 Space 再让行声明一份）。
				if(Flow.Control("appearance-laser-reset-defaults", AppearanceMetrics.m_ButtonHeight, Button, ui_token::settings::RowGap(AppearanceMetrics.m_LineSpacing)))
				{
					if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, APPEARANCE_TAB_LASER, &s_AllToDefaultResetId, "appearance-laser-reset-defaults", Localize("Reset to defaults"), 0, &Button))
					{
						g_Config.m_ClLaserRifleOutlineColor = CConfig::ms_ClLaserRifleOutlineColor;
						g_Config.m_ClLaserRifleInnerColor = CConfig::ms_ClLaserRifleInnerColor;
						g_Config.m_ClLaserShotgunOutlineColor = CConfig::ms_ClLaserShotgunOutlineColor;
						g_Config.m_ClLaserShotgunInnerColor = CConfig::ms_ClLaserShotgunInnerColor;
						g_Config.m_ClLaserDoorOutlineColor = CConfig::ms_ClLaserDoorOutlineColor;
						g_Config.m_ClLaserDoorInnerColor = CConfig::ms_ClLaserDoorInnerColor;
						g_Config.m_ClLaserFreezeOutlineColor = CConfig::ms_ClLaserFreezeOutlineColor;
						g_Config.m_ClLaserFreezeInnerColor = CConfig::ms_ClLaserFreezeInnerColor;
						g_Config.m_ClLaserDraggerOutlineColor = CConfig::ms_ClLaserDraggerOutlineColor;
						g_Config.m_ClLaserDraggerInnerColor = CConfig::ms_ClLaserDraggerInnerColor;
					}
				}
			});
			AddCard(13, LaserPreviewMinCardHeight, [=, this](CUIRect ContentRect) mutable {
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
				PreviewCardContent.HSplitTop(2 * RowSpacing, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * RowSpacing, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * RowSpacing, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * RowSpacing, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * RowSpacing, nullptr, &PreviewCardContent);
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
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmChatRelatedOnly != 0);
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
