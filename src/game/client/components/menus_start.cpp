/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/QmUi/QmLayout.h>
#include <game/client/QmUi/QmLegacy.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

using namespace FontIcons;

namespace
{
	std::chrono::nanoseconds StartMenuPerfNow(bool TrackPerf)
	{
		return TrackPerf ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	}

	double StartMenuPerfElapsedMs(std::chrono::nanoseconds StartTime)
	{
		return std::chrono::duration<double, std::milli>(time_get_nanoseconds() - StartTime).count();
	}

	void LogStartMenuPerfStage(const IClient *pClient, const char *pStage, std::chrono::nanoseconds StartTime, bool TrackPerf, bool UseV2Layout, const char *pExtra = nullptr)
	{
		if(!TrackPerf)
			return;

		char aExtra[192];
		if(pExtra != nullptr && pExtra[0] != '\0')
			str_format(aExtra, sizeof(aExtra), "operation=start_menu page=start layout=%s %s", UseV2Layout ? "v2" : "legacy", pExtra);
		else
			str_format(aExtra, sizeof(aExtra), "operation=start_menu page=start layout=%s", UseV2Layout ? "v2" : "legacy");
		QmPerfLogStage("perf/menu", pStage, StartMenuPerfElapsedMs(StartTime), false, pClient, "start", nullptr, aExtra);
	}

	void ComputeExternalButtons(const CUIRect &MainView, bool UseV2Layout, CUIRect &DiscordButton, CUIRect &LearnButton, CUIRect &TutorialButton, CUIRect &WebsiteButton, CUIRect &StatisticsButton, CUIRect &NewsButton)
	{
		CUIRect ExtMenu;
		MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
		ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);

		if(!UseV2Layout)
		{
			ExtMenu.HSplitBottom(20.0f, &ExtMenu, &DiscordButton);
			ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
			ExtMenu.HSplitBottom(20.0f, &ExtMenu, &LearnButton);
			ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
			ExtMenu.HSplitBottom(20.0f, &ExtMenu, &TutorialButton);
			ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
			ExtMenu.HSplitBottom(20.0f, &ExtMenu, &WebsiteButton);
			ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
			ExtMenu.HSplitBottom(20.0f, &ExtMenu, &NewsButton);
			ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr);
			ExtMenu.HSplitBottom(20.0f, &ExtMenu, &StatisticsButton);
			return;
		}

		CUiV2LayoutEngine LayoutEngine;
		SUiStyle ContainerStyle;
		ContainerStyle.m_Axis = EUiAxis::COLUMN;
		ContainerStyle.m_Gap = 5.0f;
		ContainerStyle.m_AlignItems = EUiAlign::STRETCH;
		ContainerStyle.m_JustifyContent = EUiAlign::END;

		static std::vector<SUiLayoutChild> s_vExternalButtonChildren(6);
		for(SUiLayoutChild &Child : s_vExternalButtonChildren)
		{
			Child.m_Style.m_Height = SUiLength::Px(20.0f);
		}

		LayoutEngine.ComputeChildren(ContainerStyle, CUiV2LegacyAdapter::FromCUIRect(ExtMenu), s_vExternalButtonChildren);
		StatisticsButton = CUiV2LegacyAdapter::ToCUIRect(s_vExternalButtonChildren[0].m_Box);
		NewsButton = CUiV2LegacyAdapter::ToCUIRect(s_vExternalButtonChildren[1].m_Box);
		WebsiteButton = CUiV2LegacyAdapter::ToCUIRect(s_vExternalButtonChildren[2].m_Box);
		TutorialButton = CUiV2LegacyAdapter::ToCUIRect(s_vExternalButtonChildren[3].m_Box);
		LearnButton = CUiV2LegacyAdapter::ToCUIRect(s_vExternalButtonChildren[4].m_Box);
		DiscordButton = CUiV2LegacyAdapter::ToCUIRect(s_vExternalButtonChildren[5].m_Box);
	}

	void ComputeMainButtons(const CUIRect &MenuArea, bool UseV2Layout, CUIRect aMenuButtons[6], CUIRect &NewFeaturesButton)
	{
		if(!UseV2Layout)
		{
			CUIRect Cursor = MenuArea;
			Cursor.HSplitBottom(40.0f, &Cursor, &aMenuButtons[0]);
			Cursor.HSplitBottom(5.0f, &Cursor, nullptr);
			Cursor.HSplitBottom(40.0f, &Cursor, &NewFeaturesButton);
			Cursor.HSplitBottom(55.0f, &Cursor, nullptr);
			Cursor.HSplitBottom(40.0f, &Cursor, &aMenuButtons[1]);
			Cursor.HSplitBottom(5.0f, &Cursor, nullptr);
			Cursor.HSplitBottom(40.0f, &Cursor, &aMenuButtons[2]);
			Cursor.HSplitBottom(5.0f, &Cursor, nullptr);
			Cursor.HSplitBottom(40.0f, &Cursor, &aMenuButtons[3]);
			Cursor.HSplitBottom(5.0f, &Cursor, nullptr);
			Cursor.HSplitBottom(40.0f, &Cursor, &aMenuButtons[4]);
			Cursor.HSplitBottom(5.0f, &Cursor, nullptr);
			Cursor.HSplitBottom(40.0f, &Cursor, &aMenuButtons[5]);
			return;
		}

		CUIRect TopArea = MenuArea;
		TopArea.HSplitBottom(40.0f, &TopArea, &aMenuButtons[0]);
		TopArea.HSplitBottom(5.0f, &TopArea, nullptr);
		TopArea.HSplitBottom(40.0f, &TopArea, &NewFeaturesButton);
		TopArea.HSplitBottom(55.0f, &TopArea, nullptr);

		CUiV2LayoutEngine LayoutEngine;
		SUiStyle ContainerStyle;
		ContainerStyle.m_Axis = EUiAxis::COLUMN;
		ContainerStyle.m_Gap = 5.0f;
		ContainerStyle.m_AlignItems = EUiAlign::STRETCH;
		ContainerStyle.m_JustifyContent = EUiAlign::END;

		static std::vector<SUiLayoutChild> s_vMainButtonChildren(5);
		for(SUiLayoutChild &Child : s_vMainButtonChildren)
		{
			Child.m_Style.m_Height = SUiLength::Px(40.0f);
		}

		LayoutEngine.ComputeChildren(ContainerStyle, CUiV2LegacyAdapter::FromCUIRect(TopArea), s_vMainButtonChildren);
		aMenuButtons[5] = CUiV2LegacyAdapter::ToCUIRect(s_vMainButtonChildren[0].m_Box);
		aMenuButtons[4] = CUiV2LegacyAdapter::ToCUIRect(s_vMainButtonChildren[1].m_Box);
		aMenuButtons[3] = CUiV2LegacyAdapter::ToCUIRect(s_vMainButtonChildren[2].m_Box);
		aMenuButtons[2] = CUiV2LegacyAdapter::ToCUIRect(s_vMainButtonChildren[3].m_Box);
		aMenuButtons[1] = CUiV2LegacyAdapter::ToCUIRect(s_vMainButtonChildren[4].m_Box);
	}

}

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	RenderStartMenuImpl(MainView, false);
}

void CMenusStart::RenderStartMenuV2(CUIRect MainView)
{
	RenderStartMenuImpl(MainView, true);
}

void CMenusStart::RenderStartMenuImpl(CUIRect MainView, bool UseV2Layout)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	const bool TrackPerf = QmPerfEnabled();
	const std::chrono::nanoseconds TotalStart = StartMenuPerfNow(TrackPerf);
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	// render logo
	{
		const std::chrono::nanoseconds StageStart = StartMenuPerfNow(TrackPerf);
		Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1, 1, 1, 1);
		IGraphics::CQuadItem QuadItem(MainView.w / 2 - 170, 60, 360, 103);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		LogStartMenuPerfStage(Client(), "start_menu_logo", StageStart, TrackPerf, UseV2Layout);
	}

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;

	int NewPage = -1;
	CUIRect DiscordButtonRect, LearnButtonRect, TutorialButtonRect, WebsiteButtonRect, StatisticsButtonRect, NewsButtonRect;
	{
		const std::chrono::nanoseconds StageStart = StartMenuPerfNow(TrackPerf);
		ComputeExternalButtons(MainView, UseV2Layout, DiscordButtonRect, LearnButtonRect, TutorialButtonRect, WebsiteButtonRect, StatisticsButtonRect, NewsButtonRect);
		LogStartMenuPerfStage(Client(), "start_menu_external_layout", StageStart, TrackPerf, UseV2Layout);
	}
	{
		const std::chrono::nanoseconds StageStart = StartMenuPerfNow(TrackPerf);
		static CButtonContainer s_DiscordButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, Localize("Discord"), 0, &DiscordButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Client()->ViewLink(Localize("https://ddnet.org/discord"));
		}

		static CButtonContainer s_LearnButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &LearnButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Client()->ViewLink(Localize("https://wiki.ddnet.org/"));
		}

		static CButtonContainer s_TutorialButton;
		static float s_JoinTutorialTime = 0.0f;
		if(GameClient()->m_Menus.DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &TutorialButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) ||
			(s_JoinTutorialTime != 0.0f && Client()->LocalTime() >= s_JoinTutorialTime))
		{
			// Activate internet tab before joining tutorial to make sure the server info
			// for the tutorial servers is available.
			GameClient()->m_Menus.SetMenuPage(CMenus::PAGE_INTERNET);
			GameClient()->m_Menus.RefreshBrowserTab(true);
			const char *pAddr = ServerBrowser()->GetTutorialServer();
			if(pAddr)
			{
				Client()->Connect(pAddr);
				s_JoinTutorialTime = 0.0f;
			}
			else if(s_JoinTutorialTime == 0.0f)
			{
				dbg_msg("menus", "couldn't find tutorial server, retrying in 5 seconds");
				s_JoinTutorialTime = Client()->LocalTime() + 5.0f;
			}
			else
			{
				Client()->AddWarning(SWarning(Localize("Can't find a Tutorial server")));
				s_JoinTutorialTime = 0.0f;
			}
		}

		static CButtonContainer s_WebsiteButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &WebsiteButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Client()->ViewLink("https://ddnet.org/");
		}

		static CButtonContainer s_StatisticsButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_StatisticsButton, Localize("Stats"), 0, &StatisticsButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			NewPage = CMenus::PAGE_STATS;

		static CButtonContainer s_NewsButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_NewsButton, Localize("News"), 0, &NewsButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, g_Config.m_UiUnreadNews ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_N))
			NewPage = CMenus::PAGE_NEWS;
		LogStartMenuPerfStage(Client(), "start_menu_external_buttons", StageStart, TrackPerf, UseV2Layout);
	}

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);
	CUIRect QuitNote;
	Menu.HSplitBottom(22.0f, &Menu, &QuitNote);
	CUIRect Line1, Line2;
	QuitNote.HSplitTop(11.0f, &Line1, &QuitNote);
	QuitNote.HSplitTop(11.0f, &Line2, nullptr);
	Ui()->DoLabel(&Line1, Localize("Before I die"), 6.0f, TEXTALIGN_MC);
	Ui()->DoLabel(&Line2, Localize("In memory of me"), 3.0f, TEXTALIGN_MC);

	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	const bool EditorDirty = GameClient()->Editor()->HasUnsavedData();
	{
		const std::chrono::nanoseconds StageStart = StartMenuPerfNow(TrackPerf);
		constexpr int MenuButtonCount = 6;
		CUIRect aMenuButtons[MenuButtonCount];
		CUIRect NewFeaturesButton;
		ComputeMainButtons(Menu, UseV2Layout, aMenuButtons, NewFeaturesButton);

		if(UseV2Layout)
		{
			IUiContext Ctx;
			Ctx.m_pUi = Ui();
			Ctx.m_pMenus = &GameClient()->m_Menus;
			Ctx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			Ctx.m_pTooltips = &GameClient()->m_Tooltips;
			Ctx.m_pTextRender = TextRender();
			Ctx.m_ScopeHash = MakeUiScopeHash("start_menu");
			Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

			static CButtonContainer s_QuitButton;
			bool UsedEscape = false;
			if(ui_widget::SecondaryButton(Ctx, &s_QuitButton, Localize("Quit"), aMenuButtons[0]) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
			{
				if(UsedEscape || EditorDirty || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
				{
					GameClient()->m_Menus.ShowQuitPopup();
				}
				else
				{
					Client()->Quit();
				}
			}

			static CButtonContainer s_SettingsButton;
			if(ui_widget::SecondaryButton(Ctx, &s_SettingsButton, Localize("Settings"), aMenuButtons[1]) || CheckHotKey(KEY_S))
				NewPage = CMenus::PAGE_SETTINGS;

			static CButtonContainer s_QmNewFeaturesButton;
			if(ui_widget::SecondaryButton(Ctx, &s_QmNewFeaturesButton, Localize("New features"), NewFeaturesButton))
				GameClient()->m_Menus.ShowQmNewFeaturesPopup();

			static CButtonContainer s_LocalServerButton;
			if(ui_widget::SecondaryButton(Ctx, &s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), aMenuButtons[2]) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
			{
				if(LocalServerRunning)
				{
					GameClient()->m_LocalServer.KillServer();
				}
				else
				{
					GameClient()->m_LocalServer.RunServer({});
				}
			}

			static CButtonContainer s_MapEditorButton;
			if(ui_widget::SecondaryButton(Ctx, &s_MapEditorButton, Localize("Editor"), aMenuButtons[3]) || CheckHotKey(KEY_E))
			{
				g_Config.m_ClEditor = 1;
				Input()->MouseModeRelative();
			}

			static CButtonContainer s_DemoButton;
			if(ui_widget::SecondaryButton(Ctx, &s_DemoButton, Localize("Demos"), aMenuButtons[4]) || CheckHotKey(KEY_D))
			{
				NewPage = CMenus::PAGE_DEMOS;
			}

			static CButtonContainer s_PlayButton;
			if(ui_widget::PrimaryButton(Ctx, &s_PlayButton, Localize("Play", "Start menu"), aMenuButtons[5]) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
			{
				NewPage = ((g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5) || g_Config.m_UiPage == CMenus::PAGE_FAVORITE_MAPS) ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
			}
		}
		else
		{
			static float s_aMenuButtonScale[MenuButtonCount] = {};
			static float s_aMenuButtonTargetScale[MenuButtonCount] = {};
			static bool s_MenuButtonScaleInit = false;
			if(!s_MenuButtonScaleInit)
			{
				for(int i = 0; i < MenuButtonCount; ++i)
				{
					s_aMenuButtonScale[i] = 1.0f;
					s_aMenuButtonTargetScale[i] = 1.0f;
				}
				s_MenuButtonScaleInit = true;
			}

			const auto ScaleButtonRect = [](const CUIRect &Base, float Scale) {
				CUIRect Out = Base;
				Out.w *= Scale;
				Out.h *= Scale;
				Out.x = Base.x + (Base.w - Out.w) * 0.5f;
				Out.y = Base.y + (Base.h - Out.h) * 0.5f;
				return Out;
			};

			constexpr int QuitIndex = 0;
			int HoveredIndex = -1;
			for(int i = 0; i < MenuButtonCount; ++i)
			{
				if(i == QuitIndex)
					continue;
				const CUIRect HoverRect = ScaleButtonRect(aMenuButtons[i], s_aMenuButtonScale[i]);
				if(Ui()->MouseHovered(&HoverRect))
				{
					HoveredIndex = i;
					break;
				}
			}
			const CUIRect QuitHoverRect = ScaleButtonRect(aMenuButtons[QuitIndex], s_aMenuButtonScale[QuitIndex]);
			const bool QuitHovered = Ui()->MouseHovered(&QuitHoverRect);

			const bool AnyHovered = HoveredIndex != -1;
			const float HoverScale = 1.08f;
			const float OtherScale = 0.94f;
			const float Speed = 12.0f;
			const float Blend = std::clamp(Client()->RenderFrameTime() * Speed, 0.0f, 1.0f);
			for(int i = 0; i < MenuButtonCount; ++i)
			{
				float Target = 1.0f;
				if(i == QuitIndex)
				{
					Target = QuitHovered ? HoverScale : 1.0f;
				}
				else if(QuitHovered)
				{
					Target = 1.0f;
				}
				else if(AnyHovered)
				{
					Target = (i == HoveredIndex) ? HoverScale : OtherScale;
				}

				s_aMenuButtonScale[i] += (Target - s_aMenuButtonScale[i]) * Blend;
				s_aMenuButtonTargetScale[i] = s_aMenuButtonScale[i];
			}

			CUIRect ScaledButton = ScaleButtonRect(aMenuButtons[0], s_aMenuButtonScale[0]);
			static CButtonContainer s_QuitButton;
			bool UsedEscape = false;
			if(GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &ScaledButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
			{
				if(UsedEscape || EditorDirty || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
				{
					GameClient()->m_Menus.ShowQuitPopup();
				}
				else
				{
					Client()->Quit();
				}
			}

			ScaledButton = ScaleButtonRect(aMenuButtons[1], s_aMenuButtonScale[1]);
			static CButtonContainer s_SettingsButton;
			if(GameClient()->m_Menus.DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_S))
				NewPage = CMenus::PAGE_SETTINGS;

			// 新功能入口：紧贴「退出」上方，打开内置新功能弹窗。
			static CButtonContainer s_QmNewFeaturesButton;
			if(GameClient()->m_Menus.DoButton_Menu(&s_QmNewFeaturesButton, Localize("New features"), 0, &NewFeaturesButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
				GameClient()->m_Menus.ShowQmNewFeaturesPopup();

			ScaledButton = ScaleButtonRect(aMenuButtons[2], s_aMenuButtonScale[2]);
			static CButtonContainer s_LocalServerButton;
			if(GameClient()->m_Menus.DoButton_Menu(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
			{
				if(LocalServerRunning)
				{
					GameClient()->m_LocalServer.KillServer();
				}
				else
				{
					GameClient()->m_LocalServer.RunServer({});
				}
			}

			ScaledButton = ScaleButtonRect(aMenuButtons[3], s_aMenuButtonScale[3]);
			static CButtonContainer s_MapEditorButton;
			if(GameClient()->m_Menus.DoButton_Menu(&s_MapEditorButton, Localize("Editor"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, EditorDirty ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_E))
			{
				g_Config.m_ClEditor = 1;
				Input()->MouseModeRelative();
			}

			ScaledButton = ScaleButtonRect(aMenuButtons[4], s_aMenuButtonScale[4]);
			static CButtonContainer s_DemoButton;
			if(GameClient()->m_Menus.DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_D))
			{
				NewPage = CMenus::PAGE_DEMOS;
			}

			ScaledButton = ScaleButtonRect(aMenuButtons[5], s_aMenuButtonScale[5]);
			static CButtonContainer s_PlayButton;
			if(GameClient()->m_Menus.DoButton_Menu(&s_PlayButton, Localize("Play", "Start menu"), 0, &ScaledButton, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
			{
				NewPage = ((g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5) || g_Config.m_UiPage == CMenus::PAGE_FAVORITE_MAPS) ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
			}
		}
		char aButtonExtra[64];
		str_format(aButtonExtra, sizeof(aButtonExtra), "images=%d", g_Config.m_ClShowStartMenuImages ? 1 : 0);
		LogStartMenuPerfStage(Client(), "start_menu_main_buttons", StageStart, TrackPerf, UseV2Layout, aButtonExtra);
	}

	// render version
	{
		const std::chrono::nanoseconds StageStart = StartMenuPerfNow(TrackPerf);
		CUIRect CurVersion, ConsoleButton;
		MainView.HSplitBottom(45.0f, nullptr, &CurVersion);
		CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
		CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
		CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
		ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);
		Ui()->DoLabel(&CurVersion, GAME_RELEASE_VERSION, 14.0f, TEXTALIGN_MR);

		CUIRect TClientVersion;
		MainView.HSplitTop(15.0f, &TClientVersion, &MainView);
		TClientVersion.VSplitRight(40.0f, &TClientVersion, nullptr);
		char aTBuf[64];
		str_format(aTBuf, sizeof(aTBuf), CLIENT_NAME " %s", CLIENT_RELEASE_VERSION);
		Ui()->DoLabel(&TClientVersion, aTBuf, 14.0f, TEXTALIGN_MR);
#if defined(CONF_AUTOUPDATE) && defined(CONF_FAMILY_WINDOWS)
		if(g_Config.m_QmAutoUpdate)
		{
			CUIRect UpdateToDateText;
			MainView.HSplitTop(15.0f, &UpdateToDateText, nullptr);
			UpdateToDateText.VSplitRight(40.0f, &UpdateToDateText, nullptr);
			if(GameClient()->m_TClient.m_UpdateCheckFailed)
			{
				Ui()->DoLabel(&UpdateToDateText, Localize("Update failed. Please try again"), 14.0f, TEXTALIGN_MR);
			}
			else if(!GameClient()->m_TClient.m_FetchedQmClientUpdateInfo)
			{
				Ui()->DoLabel(&UpdateToDateText, Localize("(Fetching Update Info)"), 14.0f, TEXTALIGN_MR);
			}
			else if(GameClient()->m_TClient.NeedQmClientUpdate())
			{
				Ui()->DoLabel(&UpdateToDateText, Localize("(Update required)"), 14.0f, TEXTALIGN_MR);
			}
			else
			{
				Ui()->DoLabel(&UpdateToDateText, Localize("(On Latest)"), 14.0f, TEXTALIGN_MR);
			}
		}
#endif
		static CButtonContainer s_ConsoleButton;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FONT_ICON_TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
		{
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
		}
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		LogStartMenuPerfStage(Client(), "start_menu_version_console", StageStart, TrackPerf, UseV2Layout);
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
	LogStartMenuPerfStage(Client(), "start_menu_total", TotalStart, TrackPerf, UseV2Layout);
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
