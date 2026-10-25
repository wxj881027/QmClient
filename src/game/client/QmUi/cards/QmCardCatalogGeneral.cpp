#include "QmCardCatalogInternal.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/chat.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/mobile_spectator_controls.h>
#include <game/client/components/qmclient/screenshot_clipboard.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

namespace qm_card_catalog
{
	void QmCardRenderHook::RenderScreenshotWatermarkSettings(const SQmCardBuildContext &Ctx, CUIRect Content)
	{
		CMenus *pMenus = Ctx.m_pMenus;
		const auto &Metrics = Ctx.m_Metrics;
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Row;
		SLabelProperties LabelProps;
		LabelProps.m_DisallowNewline = true;
		LabelProps.m_StopAtEnd = true;
		LabelProps.m_MinimumFontSize = 6.0f;
		const auto DoToggle = [&](int *pOption, const char *pId, const char *pText) {
			const bool HasRow = Flow.Control(pId, Metrics.m_LineHeight, Row);
			Flow.Space(Metrics.m_LineSpacing);
			if(!HasRow)
				return;
			if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, -1, pOption, pId, pText, *pOption, &Row, LabelProps, true, Metrics.m_BodySize))
				*pOption ^= 1;
		};
		DoToggle(&g_Config.m_QmScreenshotWatermark, "screenshot-watermark", Localize("Automatically add a watermark to screenshots"));
		DoToggle(&g_Config.m_QmScreenshotWatermarkTimestamp, "screenshot-watermark-timestamp", Localize("Timestamp"));
		DoToggle(&g_Config.m_QmScreenshotWatermarkMap, "screenshot-watermark-map", Localize("Map name"));
		if(Flow.Control("watermark-text", Metrics.m_LineHeight, Row))
		{
			static CLineInput s_WatermarkTextInput(g_Config.m_QmScreenshotWatermarkText, sizeof(g_Config.m_QmScreenshotWatermarkText));
			ui_widget::SInputFieldOptions InputOptions;
			InputOptions.m_pPlaceholder = Localize("Custom watermark text");
			InputOptions.m_FontSize = Metrics.m_BodySize;
			ui_widget::InputField(Ctx.m_UiContext, &s_WatermarkTextInput, Row, InputOptions);
		}
		Flow.Space(Metrics.m_LineSpacing);
		if(Flow.Control("watermark-position", Metrics.m_LineHeight, Row))
		{
			static CUi::SDropDownState s_PositionState;
			const char *apPositions[] = {Localize("Bottom left"), Localize("Bottom right"), Localize("Top left"), Localize("Top right")};
			const int Position = std::clamp(g_Config.m_QmScreenshotWatermarkPosition, 0, 3);
			const int NewPosition = pMenus->DoSettingsDropDown(&Row, Position, apPositions, std::size(apPositions), s_PositionState, {}, &g_Config.m_QmScreenshotWatermarkPosition);
			if(NewPosition >= 0 && NewPosition < 4)
				g_Config.m_QmScreenshotWatermarkPosition = NewPosition;
		}
	}

	bool QmCardRenderHook::BuildGeneralCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr || pStableId == nullptr)
			return false;
		const auto *pDefault = qm_card_registry::FindByStableId(pStableId);
		if(pDefault == nullptr)
			return false;
		CMenus *pMenus = Ctx.m_pMenus;
		const IUiContext GeneralCardCtx = Ctx.m_UiContext;
		const SSettingsContentMetrics GeneralMetrics = Ctx.m_Metrics;
		const float BodySize = GeneralMetrics.m_BodySize;
		const SSettingsListCardGeometry GeneralLanguageGeometry = ResolveSettingsGeneralLanguageListGeometry((int)g_Localization.Languages().size(), GeneralMetrics);
		const SSettingsListCardGeometry GeneralThemeGeometry = ResolveSettingsGeneralThemeListGeometry((int)pMenus->GameClient()->m_MenuBackground.GetThemes().size(), GeneralMetrics);
		const float GeneralLanguageListHeight = GeneralLanguageGeometry.m_ContentHeight;
		const float GeneralThemeListHeight = GeneralThemeGeometry.m_ContentHeight;
		const auto DoNumericField = [pMenus, GeneralCardCtx, BodySize](const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, unsigned Flags, const char *pSuffix = "") {
			ui_widget::SNumericFieldOptions Options;
			Options.m_pLabel = pLabel;
			Options.m_pSuffix = pSuffix;
			Options.m_pScale = &CUi::ms_LinearScrollbarScale;
			Options.m_Flags = Flags;
			Options.m_FontSize = BodySize;
			Options.m_LabelAlign = TEXTALIGN_ML;
			Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
			if(pMenus->PrepareSettingsNumericFieldLabel(CMenus::SETTINGS_GENERAL, -1, -1, pTextId, Rect, pLabel, Flags, Options))
				return false;
			return ui_widget::NumericField(GeneralCardCtx, pMenus->GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
		};

		Out = {};
		Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
		const auto UseDeclared = [&] {
			const auto Render = Out.m_Render;
			Out.m_RenderMeasured = [Render](CUIRect &Content) { Render(Content); };
			UseDeclaredModuleContent(Ctx, Out);
		};
		if(str_comp(pStableId, "deck:general-game") == 0)
		{
			Out.m_Render = [pMenus, GeneralMetrics](CUIRect Content) {
				CSettingsContentLayoutFlow Flow(Content);
				CUIRect Button;
				if(Flow.Control("general-dynamic-camera", GeneralMetrics.m_LineHeight, Button))
				{
					const bool IsDyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
					if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClDyncam, "general-dynamic-camera", Localize("Dynamic Camera"), IsDyncam, &Button))
					{
						g_Config.m_ClDyncam = IsDyncam ? 0 : 1;
						if(IsDyncam)
							g_Config.m_ClMouseFollowfactor = 0;
					}
				}
				if(g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0)
				{
					Flow.Space(GeneralMetrics.m_LineSpacing);
					if(Flow.Control("general-smooth-dynamic-camera", GeneralMetrics.m_LineHeight, Button) && pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClDyncamSmoothness, "general-smooth-dynamic-camera", Localize("Smooth Dynamic Camera"), g_Config.m_ClDyncamSmoothness, &Button))
					{
						if(g_Config.m_ClDyncamSmoothness)
							g_Config.m_ClDyncamSmoothness = 0;
						else
						{
							g_Config.m_ClDyncamSmoothness = 50;
							g_Config.m_ClDyncamStabilizing = 50;
						}
					}
				}
				Flow.Space(GeneralMetrics.m_LineSpacing);
				if(Flow.Control("general-switch-weapon-pickup", GeneralMetrics.m_LineHeight, Button) && pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClAutoswitchWeapons, "general-switch-weapon-pickup", Localize("Switch weapon on pickup"), g_Config.m_ClAutoswitchWeapons, &Button))
					g_Config.m_ClAutoswitchWeapons ^= 1;
				Flow.Space(GeneralMetrics.m_LineSpacing);
				if(Flow.Control("general-switch-weapon-out-of-ammo", GeneralMetrics.m_LineHeight, Button) && pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClAutoswitchWeaponsOutOfAmmo, "general-switch-weapon-out-of-ammo", Localize("Switch weapon when out of ammo"), g_Config.m_ClAutoswitchWeaponsOutOfAmmo, &Button))
					g_Config.m_ClAutoswitchWeaponsOutOfAmmo ^= 1;
			};
			UseDeclared();
			return true;
		}
		if(str_comp(pStableId, "deck:general-language") == 0)
		{
			Out.m_Render = [pMenus, GeneralMetrics, GeneralLanguageListHeight](CUIRect Content) {
				CSettingsContentLayoutFlow Flow(Content);
				CUIRect List;
				if(Flow.Control("general-language-list", GeneralLanguageListHeight, List))
				{
					pMenus->PrepareLanguagePageCache(List.w, false);
					pMenus->RenderLanguageSelection(List, &GeneralMetrics);
				}
			};

			UseDeclared();
			return true;
		}
		if(str_comp(pStableId, "deck:general-client") == 0)
		{
			Out.m_Render = [pMenus, DoNumericField, GeneralMetrics, GeneralThemeListHeight](CUIRect Content) {
				CSettingsContentLayoutFlow Flow(Content);
				CUIRect Button;
				char aBuf[128 + IO_MAX_PATH_LENGTH];
				if(Flow.Control("general-skip-main-menu", GeneralMetrics.m_LineHeight, Button))
				{
					if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClSkipStartMenu, "general-skip-main-menu", Localize("Skip the main menu"), g_Config.m_ClSkipStartMenu, &Button))
						g_Config.m_ClSkipStartMenu ^= 1;
				}
				Flow.Space(GeneralMetrics.m_SectionGap);
				if(Flow.Control("general-refresh-rate", GeneralMetrics.m_LineHeight, Button))
				{
					str_copy(aBuf, " ");
					str_append(aBuf, Localize("Hz", "Hertz"));
					DoNumericField("general-refresh-rate", &g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, Button, Localize("Update Rate"), 10, 10000, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);
				}
				Flow.Space(GeneralMetrics.m_LineSpacing);
				if(Flow.Control("general-lower-refresh-rate", GeneralMetrics.m_LineHeight, Button))
				{
					static int s_LowerRefreshRate;
					if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &s_LowerRefreshRate, "general-lower-refresh-rate", Localize("Save power by lowering update rate (higher input latency)"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button))
						g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;
				}
				Flow.Space(GeneralMetrics.m_SectionGap);
				static CButtonContainer s_SettingsButtonId, s_SavesButtonId, s_ConfigButtonId, s_ThemesButtonId;
				const auto DoOpenButton = [pMenus, GeneralMetrics](CButtonContainer &Id, const char *pTextId, const char *pText, const char *pPath, bool CreateDirectory, const char *pTooltip, const CUIRect &ButtonRect) {
					if(pMenus->DoSettingsButton_Menu(CMenus::SETTINGS_GENERAL, -1, -1, &Id, pTextId, pText, 0, &ButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, GeneralMetrics.m_BodySize))
					{
						char aPath[IO_MAX_PATH_LENGTH];
						pMenus->Storage()->GetCompletePath(IStorage::TYPE_SAVE, pPath, aPath, sizeof(aPath));
						if(CreateDirectory)
							pMenus->Storage()->CreateFolder(pPath, IStorage::TYPE_SAVE);
						pMenus->Client()->ViewFile(aPath);
					}
					pMenus->GameClient()->m_Tooltips.DoToolTip(&Id, &ButtonRect, pTooltip);
				};
				for(int RowIndex = 0; RowIndex < 2; ++RowIndex)
				{
					CUIRect Row, LeftButton, RightButton;
					if(!Flow.Control(RowIndex == 0 ? "general-config-files" : "general-directories", GeneralMetrics.m_LineHeight, Row))
					{
						if(RowIndex == 0)
							Flow.Space(GeneralMetrics.m_LineSpacing);
						continue;
					}
					static ui_widget::SSettingsActionGroupState s_aDirectoryWidths[2];
					const char *apLabels[] = {RowIndex == 0 ? Localize("Settings file") : Localize("Config directory"), RowIndex == 0 ? Localize("Saves file") : Localize("Themes directory")};
					const auto Actions = ui_widget::PrepareSettingsActionGroup(*pMenus->Ui(), s_aDirectoryWidths[RowIndex], Row, apLabels, 2, GeneralMetrics, ESettingsActionGroupWidth::FILL);
					LeftButton = Actions.m_aButtons[0];
					RightButton = Actions.m_aButtons[1];
					if(RowIndex == 0)
					{
						DoOpenButton(s_SettingsButtonId, "general-settings-file", Localize("Settings file"), s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, false, Localize("Open the settings file"), LeftButton);
						DoOpenButton(s_SavesButtonId, "general-saves-file", Localize("Saves file"), SAVES_FILE, false, Localize("Open the saves file"), RightButton);
					}
					else
					{
						DoOpenButton(s_ConfigButtonId, "general-config-directory", Localize("Config directory"), "", false, Localize("Open the directory that contains the configuration and user files"), LeftButton);
						DoOpenButton(s_ThemesButtonId, "general-themes-directory", Localize("Themes directory"), "themes", true, Localize("Open the directory to add custom themes"), RightButton);
					}
					if(RowIndex == 0)
						Flow.Space(GeneralMetrics.m_LineSpacing);
				}
				Flow.Space(GeneralMetrics.m_SectionGap);
				CUIRect ThemeList;
				if(Flow.Control("general-theme-list", GeneralThemeListHeight, ThemeList))
					pMenus->RenderThemeSelection(ThemeList, &GeneralMetrics);
			};

			UseDeclared();
			return true;
		}
		if(str_comp(pStableId, "deck:general-recording") == 0)
		{
			Out.m_Render = [pMenus, DoNumericField, GeneralMetrics, GeneralCardCtx](CUIRect Content) {
				CSettingsContentLayoutFlow Flow(Content);
				CUIRect Button;
				const auto DoAutoRecord = [pMenus, &Flow, &Button, DoNumericField, GeneralMetrics](int *pEnabled, int *pMax, const char *pToggleId, const char *pToggleText, const char *pMaxId, const char *pMaxText) {
					if(Flow.Control(pToggleId, GeneralMetrics.m_LineHeight, Button) && pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, -1, pEnabled, pToggleId, pToggleText, *pEnabled, &Button))
						*pEnabled ^= 1;
					Flow.Space(GeneralMetrics.m_LineSpacing);
					if(*pEnabled)
					{
						if(Flow.Control(pMaxId, GeneralMetrics.m_LineHeight, Button))
							DoNumericField(pMaxId, pMax, pMax, Button, pMaxText, 1, 1000, CUi::SCROLLBAR_OPTION_INFINITE);
						Flow.Space(GeneralMetrics.m_SectionGap);
					}
				};
				DoAutoRecord(&g_Config.m_ClAutoDemoRecord, &g_Config.m_ClAutoDemoMax, "general-auto-demo-record", Localize("Automatically record demos"), "general-auto-demo-max", Localize("Max demos"));
				DoAutoRecord(&g_Config.m_ClAutoScreenshot, &g_Config.m_ClAutoScreenshotMax, "general-auto-screenshot", Localize("Automatically take game over screenshot"), "general-auto-screenshot-max", Localize("Max Screenshots"));
				DoAutoRecord(&g_Config.m_ClAutoStatboardScreenshot, &g_Config.m_ClAutoStatboardScreenshotMax, "general-auto-statboard-screenshot", Localize("Automatically take statboard screenshot"), "general-auto-statboard-screenshot-max", Localize("Max Screenshots"));
				DoAutoRecord(&g_Config.m_ClAutoCSV, &g_Config.m_ClAutoCSVMax, "general-auto-csv", Localize("Automatically create statboard csv"), "general-auto-csv-max", Localize("Max CSVs"));
				if(qm_screenshot_clipboard::Supported())
				{
					if(Flow.Control("general-screenshot-clipboard", GeneralMetrics.m_LineHeight, Button) && pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, -1, &g_Config.m_QmScreenshotClipboard, "general-screenshot-clipboard", Localize("Automatically copy manual screenshots to clipboard"), g_Config.m_QmScreenshotClipboard, &Button))
						g_Config.m_QmScreenshotClipboard ^= 1;
					Flow.Space(GeneralMetrics.m_LineSpacing);
				}
				Flow.Space(GeneralMetrics.m_SectionGap);
				static CLineInput s_DemoDirectoryInput(g_Config.m_QmDemoDirectory, sizeof(g_Config.m_QmDemoDirectory));
				static CLineInput s_VideoDirectoryInput(g_Config.m_QmVideoDirectory, sizeof(g_Config.m_QmVideoDirectory));
				static CLineInput s_ScreenshotDirectoryInput(g_Config.m_QmScreenshotDirectory, sizeof(g_Config.m_QmScreenshotDirectory));
				const auto DoDirectory = [&](CLineInput &Input, const char *pId, const char *pLabel, const char *pDefault) {
					if(Flow.Control(pId, GeneralMetrics.m_LineHeight, Button))
						pMenus->Ui()->DoLabel(&Button, pLabel, GeneralMetrics.m_BodySize, TEXTALIGN_ML);
					Flow.Space(GeneralMetrics.m_LineSpacing);
					char aInputId[128];
					str_format(aInputId, sizeof(aInputId), "%s-input", pId);
					if(Flow.Control(aInputId, GeneralMetrics.m_LineHeight, Button))
					{
						ui_widget::SInputFieldOptions Options;
						Options.m_pPlaceholder = pDefault;
						Options.m_FontSize = GeneralMetrics.m_BodySize;
						ui_widget::InputField(GeneralCardCtx, &Input, Button, Options);
					}
					Flow.Space(GeneralMetrics.m_LineSpacing);
				};
				DoDirectory(s_DemoDirectoryInput, "general-demo-directory", Localize("Demo directory"), "demos");
				DoDirectory(s_VideoDirectoryInput, "general-video-directory", Localize("Video directory"), "videos");
				DoDirectory(s_ScreenshotDirectoryInput, "general-screenshot-directory", Localize("Screenshot directory"), "screenshots");
				Flow.Text("general-directory-help", Localize("Leave empty to use the default directory. Relative paths use the config directory."), 0.0f, GeneralMetrics.m_BodySize, GeneralMetrics.m_SectionGap, [pMenus](const char *pText, float Width, float Size) { return pMenus->TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H + Size * 0.25f; }, [pMenus, GeneralMetrics](const CUIRect &Row, const char *pText) { SLabelProperties Props; Props.m_MaxWidth = Row.w; pMenus->Ui()->DoLabel(&Row, pText, GeneralMetrics.m_BodySize, TEXTALIGN_ML, Props); }, GeneralMetrics.m_LineHeight);
				if(qm_mobile_spectator::Enabled())
				{
					CUIRect MediaRow;
					if(Flow.Control("general-mobile-media", GeneralMetrics.m_ButtonHeight, MediaRow))
					{
						CUIRect Demos, Videos;
						MediaRow.VSplitMid(&Demos, &Videos, GeneralMetrics.m_LineSpacing);
						static CButtonContainer s_Demos, s_Videos;
						if(pMenus->DoButton_Menu(&s_Demos, Localize("Demos directory"), 0, &Demos))
							pMenus->GameClient()->m_QmMobileSpectator.ShowMediaFiles(false);
						if(pMenus->DoButton_Menu(&s_Videos, Localize("Videos directory"), 0, &Videos))
							pMenus->GameClient()->m_QmMobileSpectator.ShowMediaFiles(true);
					}
					Flow.Space(GeneralMetrics.m_SectionGap);
				}
				SQmCardBuildContext WatermarkCtx;
				WatermarkCtx.m_pMenus = pMenus;
				WatermarkCtx.m_UiContext = GeneralCardCtx;
				WatermarkCtx.m_Metrics = GeneralMetrics;
				RenderScreenshotWatermarkSettings(WatermarkCtx, Content);
			};

			UseDeclared();
			return true;
		}
		if(str_comp(pStableId, "deck:tclient-info-files") == 0)
		{
			return BuildConfigFilesCard(Ctx, Out);
		}
		return false;
	}
} // namespace qm_card_catalog
