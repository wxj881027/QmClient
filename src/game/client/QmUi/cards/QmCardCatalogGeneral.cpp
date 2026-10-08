#include "QmCardCatalog.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/chat.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

namespace qm_card_catalog
{
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
		const auto IsGeneralDynamicCameraEnabled = []() {
			return g_Config.m_ClDyncam != 0 || g_Config.m_ClMouseFollowfactor > 0;
		};
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
		if(str_comp(pStableId, "deck:general-game") == 0)
		{
			Out.m_Render = [pMenus, GeneralMetrics](CUIRect Content) {
				CUIRect Button;
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				const bool IsDyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
				if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClDyncam, "general-dynamic-camera", Localize("Dynamic Camera"), IsDyncam, &Button))
				{
					if(IsDyncam)
					{
						g_Config.m_ClDyncam = 0;
						g_Config.m_ClMouseFollowfactor = 0;
					}
					else
						g_Config.m_ClDyncam = 1;
				}
				if(g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0)
				{
					Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
					Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
					if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClDyncamSmoothness, "general-smooth-dynamic-camera", Localize("Smooth Dynamic Camera"), g_Config.m_ClDyncamSmoothness, &Button))
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
				Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClAutoswitchWeapons, "general-switch-weapon-pickup", Localize("Switch weapon on pickup"), g_Config.m_ClAutoswitchWeapons, &Button))
					g_Config.m_ClAutoswitchWeapons ^= 1;
				Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClAutoswitchWeaponsOutOfAmmo, "general-switch-weapon-out-of-ammo", Localize("Switch weapon when out of ammo"), g_Config.m_ClAutoswitchWeaponsOutOfAmmo, &Button))
					g_Config.m_ClAutoswitchWeaponsOutOfAmmo ^= 1;
			};
			Out.m_Measure = [GeneralMetrics, IsGeneralDynamicCameraEnabled](float) {
				return ResolveSettingsGeneralGameContentHeight(GeneralMetrics, IsGeneralDynamicCameraEnabled());
			};
			Out.m_MeasureRevision = IsGeneralDynamicCameraEnabled() ? 1 : 0;
			Out.m_PreLayoutInput = [pMenus, GeneralMetrics, IsGeneralDynamicCameraEnabled](CUIRect Content) {
				if(pMenus->m_MenuTextPlanCollecting)
					return false;
				CUIRect Button;
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				if(!pMenus->Ui()->DoButtonLogic(&g_Config.m_ClDyncam, 0, &Button, BUTTONFLAG_LEFT))
					return false;
				if(IsGeneralDynamicCameraEnabled())
				{
					g_Config.m_ClDyncam = 0;
					g_Config.m_ClMouseFollowfactor = 0;
				}
				else
					g_Config.m_ClDyncam = 1;
				return true;
			};

			return true;
		}
		if(str_comp(pStableId, "deck:general-language") == 0)
		{
			Out.m_Measure = [GeneralLanguageListHeight](float) { return GeneralLanguageListHeight; };
			Out.m_Render = [pMenus, GeneralMetrics, GeneralLanguageListHeight](CUIRect Content) {
				Content.h = std::min(Content.h, GeneralLanguageListHeight);
				pMenus->PrepareLanguagePageCache(Content.w, false);
				pMenus->RenderLanguageSelection(Content, &GeneralMetrics);
			};

			return true;
		}
		if(str_comp(pStableId, "deck:general-client") == 0)
		{
			const float ClientContentHeight = ResolveSettingsGeneralClientContentHeight(GeneralMetrics, GeneralThemeListHeight);
			Out.m_Measure = [ClientContentHeight](float) { return ClientContentHeight; };
			Out.m_Render = [pMenus, DoNumericField, GeneralMetrics, GeneralThemeListHeight](CUIRect Content) {
				CUIRect Button;
				char aBuf[128 + IO_MAX_PATH_LENGTH];
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &g_Config.m_ClSkipStartMenu, "general-skip-main-menu", Localize("Skip the main menu"), g_Config.m_ClSkipStartMenu, &Button))
					g_Config.m_ClSkipStartMenu ^= 1;
				Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				str_copy(aBuf, " ");
				str_append(aBuf, Localize("Hz", "Hertz"));
				DoNumericField("general-refresh-rate", &g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, Button, Localize("Update Rate"), 10, 10000, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);
				Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				static int s_LowerRefreshRate;
				if(pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, &s_LowerRefreshRate, "general-lower-refresh-rate", Localize("Save power by lowering update rate (higher input latency)"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button))
					g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;
				Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
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
					Content.HSplitTop(GeneralMetrics.m_ButtonHeight, &Row, &Content);
					Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);
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
						Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				}
				Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
				Content.h = std::min(Content.h, GeneralThemeListHeight);
				pMenus->RenderThemeSelection(Content, &GeneralMetrics);
			};

			return true;
		}
		if(str_comp(pStableId, "deck:general-recording") == 0)
		{
			Out.m_MeasureRevision =
				((uint64_t)(g_Config.m_ClAutoDemoRecord != 0) << 0) |
				((uint64_t)(g_Config.m_ClAutoScreenshot != 0) << 1) |
				((uint64_t)(g_Config.m_ClAutoStatboardScreenshot != 0) << 2) |
				((uint64_t)(g_Config.m_ClAutoCSV != 0) << 3);
			Out.m_Measure = [GeneralMetrics](float) {
				const int EnabledRows =
					(g_Config.m_ClAutoDemoRecord != 0) +
					(g_Config.m_ClAutoScreenshot != 0) +
					(g_Config.m_ClAutoStatboardScreenshot != 0) +
					(g_Config.m_ClAutoCSV != 0);
				return 4.0f * GeneralMetrics.m_RowStep + EnabledRows * (GeneralMetrics.m_RowStep + GeneralMetrics.m_LineSpacing);
			};
			Out.m_VisibilityController = true;
			Out.m_PreLayoutInput = [pMenus, GeneralMetrics](CUIRect Content) {
				if(pMenus->m_MenuTextPlanCollecting)
					return false;
				bool Changed = false;
				CUIRect Button;
				const auto ProcessToggle = [pMenus, &Content, &Button, &Changed, GeneralMetrics](int *pEnabled) {
					Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
					if(pMenus->Ui()->DoButtonLogic(pEnabled, 0, &Button, BUTTONFLAG_LEFT))
					{
						*pEnabled ^= 1;
						Changed = true;
					}
					Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
					if(*pEnabled)
					{
						Content.HSplitTop(GeneralMetrics.m_LineHeight, nullptr, &Content);
						Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
					}
				};
				ProcessToggle(&g_Config.m_ClAutoDemoRecord);
				ProcessToggle(&g_Config.m_ClAutoScreenshot);
				ProcessToggle(&g_Config.m_ClAutoStatboardScreenshot);
				ProcessToggle(&g_Config.m_ClAutoCSV);
				return Changed;
			};
			Out.m_Render = [pMenus, DoNumericField, GeneralMetrics](CUIRect Content) {
				CUIRect Button;
				const auto DoAutoRecord = [pMenus, &Content, &Button, DoNumericField, GeneralMetrics](int *pEnabled, int *pMax, const char *pToggleId, const char *pToggleText, const char *pMaxId, const char *pMaxText) {
					Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
					pMenus->DoSettingsButton_CheckBox(CMenus::SETTINGS_GENERAL, -1, -1, pEnabled, pToggleId, pToggleText, *pEnabled, &Button, SLabelProperties{}, false);
					Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
					if(*pEnabled)
					{
						Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
						DoNumericField(pMaxId, pMax, pMax, Button, pMaxText, 1, 1000, CUi::SCROLLBAR_OPTION_INFINITE);
						Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
					}
				};
				DoAutoRecord(&g_Config.m_ClAutoDemoRecord, &g_Config.m_ClAutoDemoMax, "general-auto-demo-record", Localize("Automatically record demos"), "general-auto-demo-max", Localize("Max demos"));
				DoAutoRecord(&g_Config.m_ClAutoScreenshot, &g_Config.m_ClAutoScreenshotMax, "general-auto-screenshot", Localize("Automatically take game over screenshot"), "general-auto-screenshot-max", Localize("Max Screenshots"));
				DoAutoRecord(&g_Config.m_ClAutoStatboardScreenshot, &g_Config.m_ClAutoStatboardScreenshotMax, "general-auto-statboard-screenshot", Localize("Automatically take statboard screenshot"), "general-auto-statboard-screenshot-max", Localize("Max Screenshots"));
				DoAutoRecord(&g_Config.m_ClAutoCSV, &g_Config.m_ClAutoCSVMax, "general-auto-csv", Localize("Automatically create statboard csv"), "general-auto-csv-max", Localize("Max CSVs"));
			};

			return true;
		}
		if(str_comp(pStableId, "deck:tclient-info-files") == 0)
		{
			Out.m_Measure = [GeneralMetrics](float) {
				return ResolveSettingsRowsHeight(2, GeneralMetrics.m_ButtonHeight, GeneralMetrics.m_LineSpacing);
			};
			Out.m_Render = [pMenus, GeneralMetrics](CUIRect Content) {
				static CButtonContainer s_Config, s_Profiles, s_Warlist, s_Chatbinds;
				const auto OpenFile = [pMenus](ConfigDomain Domain) {
					char aBuf[IO_MAX_PATH_LENGTH];
					pMenus->Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[Domain].m_aConfigPath, aBuf, sizeof(aBuf));
					pMenus->Client()->ViewFile(aBuf);
				};
				const auto DoFileButton = [pMenus, GeneralMetrics, &OpenFile](CButtonContainer &Id, const char *pTextId, const char *pText, const CUIRect &ButtonRect, ConfigDomain Domain) {
					if(pMenus->DoSettingsButton_Menu(CMenus::SETTINGS_GENERAL, -1, -1, &Id, pTextId, pText, 0, &ButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, GeneralMetrics.m_BodySize))
						OpenFile(Domain);
				};
				CUIRect Row, LeftButton, RightButton;
				Content.HSplitTop(GeneralMetrics.m_ButtonHeight, &Row, &Content);
				Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);
				DoFileButton(s_Config, "tclient-files-qmclient-settings", Localize("QmClient Settings"), LeftButton, ConfigDomain::QMCLIENT);
				DoFileButton(s_Profiles, "tclient-files-profiles", Localize("Profiles"), RightButton, ConfigDomain::TCLIENTPROFILES);
				Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				Content.HSplitTop(GeneralMetrics.m_ButtonHeight, &Row, &Content);
				Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);
				DoFileButton(s_Warlist, "tclient-files-warlist", Localize("War List"), LeftButton, ConfigDomain::TCLIENTWARLIST);
				DoFileButton(s_Chatbinds, "tclient-files-chatbinds", Localize("Chat Binds"), RightButton, ConfigDomain::TCLIENTCHATBINDS);
			};
			return true;
		}
		return false;
	}
} // namespace qm_card_catalog
