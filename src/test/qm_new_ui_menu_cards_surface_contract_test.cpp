// QmNewUi 菜单源码合同：设置卡片视觉表面与共享样式助手。
// 卡片甲板状态/生命周期合同见 qm_new_ui_menu_cards_deck_contract_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

namespace
{

	[[maybe_unused]] size_t MatchingBrace(const std::string &Source, size_t BodyStart)
	{
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Index;
			}
		}
		return std::string::npos;
	}

	[[maybe_unused]] size_t CountRoundedRectDirectCalls(const std::string &Source)
	{
		const std::regex CallRegex("Graphics\\(\\)->DrawRect(Ext|Ext4|4)?\\([^;]{0,260}IGraphics::CORNER_(ALL|TL|TR|BL|BR|L|R|T|B)");
		size_t Count = 0;
		for(std::sregex_iterator It(Source.begin(), Source.end(), CallRegex), End; It != End; ++It)
			++Count;
		return Count;
	}

} // namespace

TEST(QmNewUiMenuCardsSurfaceContract, QmSettingsCardsUseSharedStyleHelpers)
{
	const std::string HeaderSource = ReadTextFile("src/game/client/components/menus.h");
	EXPECT_NE(HeaderSource.find("struct SQmSettingsCardStyle"), std::string::npos);
	EXPECT_NE(HeaderSource.find("SQmSettingsCardStyle QmSettingsCardStyle(float UiScale) const;"), std::string::npos);
	EXPECT_NE(HeaderSource.find("CScrollRegionParams QmSettingsScrollRegionParams(float UiScale) const;"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("RenderQmSettingsGlassCard"), std::string::npos);

	const std::string MenuSource = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(MenuSource.find("CMenus::SQmSettingsCardStyle CMenus::QmSettingsCardStyle(float UiScale) const"), std::string::npos);
	EXPECT_NE(MenuSource.find("const SQmScrollContainerStyle ScrollStyle = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);"), std::string::npos);
	EXPECT_NE(MenuSource.find("Style.m_ScrollbarWidth = ScrollStyle.m_ScrollbarWidth;"), std::string::npos);
	EXPECT_NE(MenuSource.find("Request.m_Profile = EQmScrollProfile::SETTINGS_OUTER;"), std::string::npos);
	EXPECT_NE(MenuSource.find("return QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(Request, UiScale, 0.0f));"), std::string::npos);
	EXPECT_EQ(MenuSource.find("QmScrollRegionParamsForSize(EQmScrollSize::LARGE, UiScale)"), std::string::npos);
	EXPECT_EQ(MenuSource.find("Params.m_ScrollUnit = 60.0f * UiScale;"), std::string::npos);
	EXPECT_EQ(MenuSource.find("Params.m_ScrollbarThickness = Style.m_ScrollbarWidth;"), std::string::npos);
	EXPECT_EQ(MenuSource.find("Params.m_ScrollbarMargin = Style.m_ScrollbarMargin;"), std::string::npos);
	EXPECT_EQ(MenuSource.find("RenderQmSettingsGlassCard"), std::string::npos);
}

TEST(QmNewUiMenuCardsSurfaceContract, SettingsCardUsesOneCanonicalSurfaceWithoutLegacyGlass)
{
	const std::string MenuSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Body = ReadTextFile("src/game/client/QmUi/SettingsCard.cpp");
	ASSERT_FALSE(Body.empty());
	EXPECT_EQ(Body.find("Shadow.Draw"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ctx, ChromeRect, Surface, Border, CardRadius"), std::string::npos);
	EXPECT_EQ(Body.find("ChromeRect.Draw(Surface, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("ChromeRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("ResolveSettingsCardBorderRingClipRects"), std::string::npos);
	EXPECT_EQ(Body.find("InnerSurface.Margin(BorderWidth, &InnerSurface);"), std::string::npos);
	EXPECT_EQ(Body.find("BorderRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("DrawOutline(Border)"), std::string::npos);
	EXPECT_EQ(MenuSource.find("RenderQmSettingsGlassCard"), std::string::npos);
	EXPECT_EQ(MenuSource.find("m_QmCardBackdropBlur"), std::string::npos);
}

TEST(QmNewUiMenuCardsSurfaceContract, SettingsCardDeckSharedComponentMigratesSoundBindWheelStatusBar)
{
	const std::string HeaderSource = ReadTextFile("src/game/client/components/menus.h");
	EXPECT_EQ(HeaderSource.find("struct SSettingsCardDeckLayout"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("struct SSettingsCardDeckCard"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("RenderSettingsCardDragHandle("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("RenderSettingsCardDeckDragOverlay("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("m_SettingsCardDeckOrders"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("m_SettingsCardDeckColumnPrefs"), std::string::npos);
	EXPECT_NE(HeaderSource.find("qm_card_order::CModel m_SettingsCardOrderModel;"), std::string::npos);
	EXPECT_NE(HeaderSource.find("CSettingsCardDeck m_SettingsCardDeck;"), std::string::npos);

	const std::string MenuSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	EXPECT_EQ(MenuSource.find("RenderQmSettingsGlassCard"), std::string::npos);
	EXPECT_EQ(MenuSource.find("SettingsCardDeckStableId"), std::string::npos);
	EXPECT_EQ(MenuSource.find("LoadSettingsCardDeckOrdersFromGlobalConfig"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("CommitSettingsCardDeckDrop(Model, pTab, pStableId"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCard(Ctx, Card.m_Frame"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string FormatBackendDisplayName = FunctionBody(SettingsSource, "void FormatQmGraphicsBackendDisplayName(");
	ASSERT_FALSE(FormatBackendDisplayName.empty());
	EXPECT_NE(FormatBackendDisplayName.find("\"OpenGL %d.%d\""), std::string::npos);
	EXPECT_EQ(FormatBackendDisplayName.find("\"OpenGL_QmClient_%d_%d\""), std::string::npos);
	const std::string RenderSettingsSound = FunctionBody(SettingsSource, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsSound.empty());
	EXPECT_NE(RenderSettingsSound.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("SettingsCardDeckForRenderPass().RenderCached(SoundCardCtx, SoundPage, \"sound\""), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("AddCard(ToggleSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("AddCard(VolumeSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("AddCard(AudioPackSpec"), std::string::npos);
	EXPECT_LT(RenderSettingsSound.find("AddCard(VolumeSpec"), RenderSettingsSound.find("AddCard(AudioPackSpec"));
	EXPECT_NE(RenderSettingsSound.find("DoSoundNumericField(\"sound-volume\""), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("DoSoundNumericField(\"sound-background-music-volume\""), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("DoSliderWithValueInput("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("Ui()->DoButton_QmIcon(&s_AudioPackRefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("DoButton_Menu(&s_AudioPackRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("str_format(aBadge, sizeof(aBadge), \"%d\", Entry.m_FileCount);"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("str_copy(aBadge, Localize(\"Built-in\"), sizeof(aBadge));"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("Localize(\"Selected pack\")"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("\"audio_packs_title\""), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("Definition.m_IsVisible = std::move(IsVisible);"), std::string::npos);
	const size_t ToggleCard = RenderSettingsSound.find("AddCard(ToggleSpec");
	const size_t VolumeCard = RenderSettingsSound.find("AddCard(VolumeSpec");
	const size_t AudioPackCard = RenderSettingsSound.find("AddCard(AudioPackSpec");
	ASSERT_NE(ToggleCard, std::string::npos);
	ASSERT_NE(VolumeCard, std::string::npos);
	ASSERT_NE(AudioPackCard, std::string::npos);
	EXPECT_NE(RenderSettingsSound.substr(ToggleCard, VolumeCard - ToggleCard).find("}, {}, true, ProcessSoundToggleInput, g_Config.m_SndEnable);"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.substr(VolumeCard, AudioPackCard - VolumeCard).find("[]() { return g_Config.m_SndEnable != 0; }"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.substr(AudioPackCard).find("[]() { return g_Config.m_SndEnable != 0; }"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("Definition.m_PreLayoutInput = std::move(PreLayoutInput);"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("ProcessSoundToggleInput"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("vCards.back().m_Measure = [LineHeight, LineSpacing]"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("SLabelProperties{}, false"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("AudioPackView.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f)"), std::string::npos);

	const std::string RenderSettingsTClientSettings = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientBindWheel = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientChatBinds = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientStatusBar = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)");
	const std::string StatusBarHeader = ReadTextFile("src/game/client/components/tclient/statusbar.h");
	ASSERT_FALSE(RenderSettingsTClientSettings.empty());
	ASSERT_FALSE(RenderSettingsTClientBindWheel.empty());
	ASSERT_FALSE(RenderSettingsTClientChatBinds.empty());
	ASSERT_FALSE(RenderSettingsTClientStatusBar.empty());
	EXPECT_EQ(TClientSource.find("void CMenus::HandleSettingsCardDeckDrag("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientSettings.find("RenderSettingsCardDragHandle(CardBoxRect, &HandleRect, QmSettingsCardStyle(1.0f));"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientSettings.find("SettingsCardDeckItemFromSection(SectionMeta, ColumnId, (int)i, CardRect, HandleRect);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientSettings.find("m_SettingsCardDeck.RenderCached(SettingsUiContext(\"settings_tclient_main\""), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("SettingsPageLayout(MainView, UiScale)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("deck:tclient-bind-wheel-editor"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("deck:tclient-bind-wheel-preview"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_BindWheelPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("SettingsCardOrderModel()"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("MainView.VSplitLeft(MainView.w / 2.1f"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("BeginSettingsCardDeck(MainView, s_BindWheelSettingsScrollRegion, s_BindWheelSettingsScrollY, 1.0f, \"tclient-bind-wheel\", SETTINGS_TCLIENT, nullptr)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("deck:tclient-status-bar-settings"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("deck:tclient-status-bar-items"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("deck:tclient-status-bar-preview"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_StatusBarPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const int Rows = ResolveSettingsStatusCodeRows(StatusBarCodeCount, ContentWidth);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const float StatusBarPreviewHeight = LineSize + MarginSmall * 2.0f;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const auto MeasurePreview = [&MeasureItems, StatusBarCodeCount, StatusBarItemCount, StatusBarPreviewHeight]"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_TypeSelectedOld < StatusBarCodeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(s_SelectedItem >= 0 && s_TypeSelectedOld >= 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("RenderStatusBarCodes(Content);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("Definition.m_MeasureRevision = StatusLayoutRevision;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("PreviewContentHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("static char s_aCodeLanguage[sizeof(g_Config.m_ClLanguagefile)]"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("static char s_aDropDownLanguage[sizeof(g_Config.m_ClLanguagefile)]"), std::string::npos);
	EXPECT_NE(StatusBarHeader.find("\"g\", \"Snapshot Age\""), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(View.w > 360.0f)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("CTClientSettingsRowAllocator Rows(View);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("MarginSmall * 10.0f"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("View.HSplitTop(LineSize, &Label, &View);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(!ReadOnly && DoSettingsButton_Menu"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(!ReadOnly && DoSettingsButton_CheckBox"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(!ReadOnly && DoButtonLineSize_Menu"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("MainView.HSplitBottom(100.0f, &MainView, &StatusBar);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("s_StatusBarSettingsCardHeight"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("s_StatusBarSettingsScrollY"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("const float EditorContentHeight = LineSize * 7.0f + SmallSize + MarginSmall * 4.0f;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("320.0f - CardChromeHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("const auto RenderPreview = [this, ReadOnly](CUIRect RightView) {\n\t\tif(ReadOnly)\n\t\t\treturn;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("s_BindWheelEditorCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("deck:tclient-chat-binds-kaomoji"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("deck:tclient-chat-binds-warlist"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("deck:tclient-chat-binds-other"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ChatBindsPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("if(!ReadOnly && ui_widget::InputField"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("return CBindChat::BIND_DEFAULTS[Index].second.size() * (MarginSmall + LineSize);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("Content.HSplitTop(HeadlineHeight, &Label, &Content);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("BeginSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("FinishSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("TClientCacheSectionBoxRect("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("s_PrevChatBindsScrollY"), std::string::npos);
	const std::string CardRegistry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	EXPECT_NE(CardRegistry.find("{\"deck:tclient-chat-binds-kaomoji\", \"tclient-chat-binds\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(CardRegistry.find("{\"deck:tclient-chat-binds-warlist\", \"tclient-chat-binds\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(CardRegistry.find("{\"deck:tclient-chat-binds-other\", \"tclient-chat-binds\", ECardColumn::Left, 1"), std::string::npos);
}

TEST(QmNewUiMenuCardsSurfaceContract, SettingsTransitionsDoNotChangePageBrightness)
{
	const std::vector<const char *> vFiles = {
		"src/game/client/components/menus_settings.cpp",
		"src/game/client/components/menus_settings7.cpp",
		"src/game/client/components/tclient/menus_tclient.cpp",
		"src/game/client/components/qmclient/menus_qmclient.cpp",
	};
	for(const char *pFile : vFiles)
	{
		const std::string Source = ReadTextFile(pFile);
		EXPECT_EQ(Source.find("ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)"), std::string::npos) << pFile;
		EXPECT_EQ(Source.find("ColorRGBA(0.0f, 0.0f, 0.0f, TabTransitionAlpha)"), std::string::npos) << pFile;
	}
}
