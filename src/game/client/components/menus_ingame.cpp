/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ghost.h"
#include "menus.h"
#include "motd.h"
#include "voting.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/demo.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/ghost.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>

#include <generated/protocol.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/touch_controls.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <chrono>
#include <string>
#include <unordered_map>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
struct SUnfinishedMapsQuery
{
	enum class EState
	{
		IDLE,
		RUNNING,
		READY,
		FAILED,
	};

	std::shared_ptr<CHttpRequest> m_pRequest;
	std::unordered_map<std::string, std::vector<std::string>> m_UnfinishedByType;
	EState m_State = EState::IDLE;

	void Reset()
	{
		if(m_pRequest)
			m_pRequest->Abort();
		m_pRequest.reset();
		m_UnfinishedByType.clear();
		m_State = EState::IDLE;
	}

	void Start(IHttp *pHttp, const char *pPlayerName)
	{
		if(!pHttp || !pPlayerName || pPlayerName[0] == '\0')
		{
			Reset();
			m_State = EState::FAILED;
			return;
		}

		Reset();
		m_State = EState::RUNNING;

		char aEncodedName[256];
		EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName);

		char aUrl[512];
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);

		auto pRequest = std::make_shared<CHttpRequest>(aUrl);
		pRequest->Timeout(CTimeout{10000, 30000, 100, 10});
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		m_pRequest = pRequest;
		pHttp->Run(pRequest);
	}

	bool IsReady() const { return m_State == EState::READY; }
	bool IsLoading() const { return m_State == EState::RUNNING; }
	bool HasData() const { return m_State == EState::READY; }

	const std::vector<std::string> *FindType(const char *pTypeKey) const
	{
		if(!pTypeKey || pTypeKey[0] == '\0')
			return nullptr;

		auto It = m_UnfinishedByType.find(pTypeKey);
		if(It != m_UnfinishedByType.end())
			return &It->second;

		if(const char *pRest = str_startswith(pTypeKey, "DDmaX "))
		{
			char aKey[32];
			str_format(aKey, sizeof(aKey), "DDmaX.%s", pRest);
			It = m_UnfinishedByType.find(aKey);
			if(It != m_UnfinishedByType.end())
				return &It->second;
		}
		if(const char *pRest = str_startswith(pTypeKey, "DDmaX."))
		{
			char aKey[32];
			str_copy(aKey, "DDmaX ");
			str_append(aKey, pRest, sizeof(aKey));
			It = m_UnfinishedByType.find(aKey);
			if(It != m_UnfinishedByType.end())
				return &It->second;
		}
		return nullptr;
	}

	void Update()
	{
		if(!m_pRequest || !m_pRequest->Done())
			return;

		const EHttpState State = m_pRequest->State();
		if(State != EHttpState::DONE || m_pRequest->StatusCode() != 200)
		{
			Reset();
			m_State = EState::FAILED;
			return;
		}

		json_value *pRoot = m_pRequest->ResultJson();
		if(!pRoot || pRoot->type != json_object)
		{
			if(pRoot)
				json_value_free(pRoot);
			Reset();
			m_State = EState::FAILED;
			return;
		}

		const json_value *pTypes = json_object_get(pRoot, "types");
		if(pTypes == &json_value_none || pTypes->type != json_object)
		{
			json_value_free(pRoot);
			Reset();
			m_State = EState::FAILED;
			return;
		}

		for(unsigned i = 0; i < pTypes->u.object.length; ++i)
		{
			const char *pTypeName = pTypes->u.object.values[i].name;
			const json_value *pTypeObj = pTypes->u.object.values[i].value;
			if(!pTypeName || !pTypeObj || pTypeObj->type != json_object)
				continue;

			const json_value *pMaps = json_object_get(pTypeObj, "maps");
			if(pMaps == &json_value_none || pMaps->type != json_object)
				continue;

			std::vector<std::string> Unfinished;
			Unfinished.reserve(pMaps->u.object.length);
			for(unsigned j = 0; j < pMaps->u.object.length; ++j)
			{
				const char *pMapName = pMaps->u.object.values[j].name;
				const json_value *pMapObj = pMaps->u.object.values[j].value;
				if(!pMapName || !pMapObj || pMapObj->type != json_object)
					continue;

				const json_value *pFinishes = json_object_get(pMapObj, "finishes");
				int Finishes = 0;
				if(pFinishes != &json_value_none && pFinishes->type == json_integer)
					Finishes = (int)pFinishes->u.integer;
				if(Finishes == 0)
					Unfinished.emplace_back(pMapName);
			}

			m_UnfinishedByType.emplace(pTypeName, std::move(Unfinished));
		}

		json_value_free(pRoot);
		m_pRequest.reset();
		m_State = EState::READY;
	}
};
} // namespace

void CMenus::RenderGame(CUIRect MainView)
{
	CUIRect Button, ButtonBars, ButtonBar, ButtonBar2;
	constexpr float MenuButtonHeight = 25.0f;
	constexpr float PrimaryButtonSpacing = 5.0f;
	constexpr float DynamicButtonMinWidth = 52.0f;
	constexpr float PracticeButtonMinWidth = 32.0f;
	constexpr float AutoCameraButtonWidth = 32.0f;
	constexpr float UtilityButtonSpacingNormal = 5.0f;
	constexpr float UtilityButtonSpacingCompact = 4.0f;
	constexpr float MenuButtonPaddingNormal = MenuButtonHeight + 8.0f;
	constexpr float MenuButtonPaddingCompact = MenuButtonHeight + 2.0f;

	const float MenuButtonTextHeight = (MenuButtonHeight - 4.0f) * 0.9f;
	const float MenuButtonFontSize = MenuButtonTextHeight * CUi::ms_FontmodHeight;
	const auto CalcMenuButtonWidth = [&](const char *pText, float HorizontalPadding, float MinWidth) {
		const float TextWidth = pText != nullptr && pText[0] != '\0' ? TextRender()->TextWidth(MenuButtonFontSize, pText, -1, -1.0f) : 0.0f;
		return maximum(MinWidth, TextWidth + HorizontalPadding);
	};

	bool Paused = false;
	bool Spec = false;
	if(GameClient()->m_Snap.m_LocalClientId >= 0)
	{
		Paused = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Paused;
		Spec = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Spec;
	}

	const bool HasLocalInfo = GameClient()->m_Snap.m_pLocalInfo != nullptr;
	const bool HasGameInfo = GameClient()->m_Snap.m_pGameInfoObj != nullptr;
	const bool IsTeamPlay = GameClient()->IsTeamPlay();
	const int LocalTeam = HasLocalInfo ? GameClient()->m_Snap.m_pLocalInfo->m_Team : TEAM_SPECTATORS;
	const bool Recording = DemoRecorder(RECORDER_MANUAL)->IsRecording();
	const bool FastPracticeEnabled = GameClient()->m_FastPractice.Enabled();

	const char *pDisconnectButtonLabel = Localize("Disconnect");
	const char *pDummyButtonLabel = Localize("Connect Dummy");
	if(Client()->DummyConnecting())
		pDummyButtonLabel = Localize("Connecting dummy");
	else if(Client()->DummyConnected())
		pDummyButtonLabel = Localize("Disconnect Dummy");
	const char *pEditHudButtonLabel = Localize("Edit HUD");
	const char *pDemoButtonLabel = Recording ? Localize("Stop record") : Localize("Record demo");
	const char *pSpectateButtonLabel = Localize("Spectate");
	const char *pJoinRedButtonLabel = Localize("Join red");
	const char *pJoinBlueButtonLabel = Localize("Join blue");
	const char *pJoinGameButtonLabel = Localize("Join game");
	const char *pKillButtonLabel = Localize("Kill");
	const char *pPauseButtonLabel = (!Paused && !Spec) ? Localize("Pause") : Localize("Join game");
	const char *pFastPracticeLabel = FastPracticeEnabled ? Localize("Stop practice") : Localize("Fast practice");

	const float SpectateButtonWidth = CalcMenuButtonWidth(pSpectateButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float JoinRedButtonWidth = CalcMenuButtonWidth(pJoinRedButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float JoinBlueButtonWidth = CalcMenuButtonWidth(pJoinBlueButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float JoinGameButtonWidth = CalcMenuButtonWidth(pJoinGameButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float KillButtonWidth = CalcMenuButtonWidth(pKillButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float CurrentPauseButtonWidth = CalcMenuButtonWidth(pPauseButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float NormalPracticeButtonWidth = CalcMenuButtonWidth(pFastPracticeLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float CompactPracticeButtonWidth = CalcMenuButtonWidth("fp", MenuButtonPaddingCompact, PracticeButtonMinWidth);
	const float DisconnectButtonWidthNormal = CalcMenuButtonWidth(pDisconnectButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float DisconnectButtonWidthCompact = CalcMenuButtonWidth(pDisconnectButtonLabel, MenuButtonPaddingCompact, DynamicButtonMinWidth);
	const float DummyButtonWidthNormal = CalcMenuButtonWidth(pDummyButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float DummyButtonWidthCompact = CalcMenuButtonWidth(pDummyButtonLabel, MenuButtonPaddingCompact, DynamicButtonMinWidth);
	const float EditHudButtonWidthNormal = CalcMenuButtonWidth(pEditHudButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float EditHudButtonWidthCompact = CalcMenuButtonWidth(pEditHudButtonLabel, MenuButtonPaddingCompact, DynamicButtonMinWidth);
	const float DemoButtonWidthNormal = CalcMenuButtonWidth(pDemoButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float DemoButtonWidthCompact = CalcMenuButtonWidth(pDemoButtonLabel, MenuButtonPaddingCompact, DynamicButtonMinWidth);

	const bool ShowGameplayButtons = HasLocalInfo && HasGameInfo && !Paused && !Spec;
	const bool ShowSpectateButton = ShowGameplayButtons && LocalTeam != TEAM_SPECTATORS;
	const bool ShowJoinRedButton = ShowGameplayButtons && IsTeamPlay && LocalTeam != TEAM_RED;
	const bool ShowJoinBlueButton = ShowGameplayButtons && IsTeamPlay && LocalTeam != TEAM_BLUE;
	const bool ShowJoinGameButton = ShowGameplayButtons && !IsTeamPlay && LocalTeam != TEAM_GAME;
	const bool ShowKillButton = ShowGameplayButtons && LocalTeam != TEAM_SPECTATORS;
	const bool ShowPauseButton = GameClient()->m_ReceivedDDNetPlayer && HasLocalInfo && (LocalTeam != TEAM_SPECTATORS || Paused || Spec);
	const bool ShowPracticeButton = GameClient()->m_ReceivedDDNetPlayer && HasLocalInfo && LocalTeam != TEAM_SPECTATORS && !Paused && !Spec;
	const bool ShowAutoCameraButton = HasLocalInfo && (LocalTeam == TEAM_SPECTATORS || Paused || Spec);

	const float UtilityButtonWidthNormal =
		DisconnectButtonWidthNormal + DummyButtonWidthNormal + EditHudButtonWidthNormal + DemoButtonWidthNormal + UtilityButtonSpacingNormal * 3.0f;
	const float UtilityButtonWidthCompact =
		DisconnectButtonWidthCompact + DummyButtonWidthCompact + EditHudButtonWidthCompact + DemoButtonWidthCompact + UtilityButtonSpacingCompact * 3.0f;
	const float PrimaryButtonBarWidth = maximum(0.0f, MainView.w - 20.0f);

	auto CalcPrimaryButtonsWidth = [&](bool IncludeTeamplayDDRaceButtons) {
		float Width = 0.0f;
		auto AddButtonWidth = [&](bool Show, float ButtonWidth) {
			if(!Show)
				return;
			if(Width > 0.0f)
				Width += PrimaryButtonSpacing;
			Width += ButtonWidth;
		};

		AddButtonWidth(ShowSpectateButton, SpectateButtonWidth);
		AddButtonWidth(ShowJoinRedButton, JoinRedButtonWidth);
		AddButtonWidth(ShowJoinBlueButton, JoinBlueButtonWidth);
		AddButtonWidth(ShowJoinGameButton, JoinGameButtonWidth);

		const bool ShowTeamplayDDRaceButtons = !IsTeamPlay || IncludeTeamplayDDRaceButtons;
		AddButtonWidth(ShowKillButton && ShowTeamplayDDRaceButtons, KillButtonWidth);
		AddButtonWidth(ShowPauseButton && ShowTeamplayDDRaceButtons, CurrentPauseButtonWidth);
		AddButtonWidth(ShowPracticeButton && ShowTeamplayDDRaceButtons, NormalPracticeButtonWidth);
		AddButtonWidth(ShowAutoCameraButton, AutoCameraButtonWidth);
		return Width;
	};

	bool UseCompactUtilityButtons = false;
	bool UseSecondaryUtilityButtonBar = false;
	bool ShowDDRaceButtons = !IsTeamPlay;
	if(g_Config.m_ClTouchControls)
	{
		ShowDDRaceButtons = MainView.w > 855.0f;
	}
	else
	{
		auto TryUtilityLayout = [&](float RequiredPrimaryWidth) {
			if(PrimaryButtonBarWidth >= RequiredPrimaryWidth + UtilityButtonWidthNormal)
			{
				UseCompactUtilityButtons = false;
				UseSecondaryUtilityButtonBar = false;
				return true;
			}
			if(PrimaryButtonBarWidth >= RequiredPrimaryWidth + UtilityButtonWidthCompact)
			{
				UseCompactUtilityButtons = true;
				UseSecondaryUtilityButtonBar = false;
				return true;
			}
			if(PrimaryButtonBarWidth >= RequiredPrimaryWidth && PrimaryButtonBarWidth >= UtilityButtonWidthNormal)
			{
				UseCompactUtilityButtons = false;
				UseSecondaryUtilityButtonBar = true;
				return true;
			}
			if(PrimaryButtonBarWidth >= RequiredPrimaryWidth && PrimaryButtonBarWidth >= UtilityButtonWidthCompact)
			{
				UseCompactUtilityButtons = true;
				UseSecondaryUtilityButtonBar = true;
				return true;
			}
			return false;
		};

		if(IsTeamPlay && TryUtilityLayout(CalcPrimaryButtonsWidth(true)))
		{
			ShowDDRaceButtons = true;
		}
		else
		{
			ShowDDRaceButtons = !IsTeamPlay;
			if(!TryUtilityLayout(CalcPrimaryButtonsWidth(false)))
			{
				UseCompactUtilityButtons = true;
				UseSecondaryUtilityButtonBar = true;
			}
		}
	}

	const bool HasSecondaryButtonBar = g_Config.m_ClTouchControls || UseSecondaryUtilityButtonBar;
	MainView.HSplitTop(45.0f + (HasSecondaryButtonBar ? 35.0f : 0.0f), &ButtonBars, &MainView);
	ButtonBars.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	ButtonBars.Margin(10.0f, &ButtonBars);
	ButtonBars.HSplitTop(MenuButtonHeight, &ButtonBar, &ButtonBars);
	if(HasSecondaryButtonBar)
	{
		ButtonBars.HSplitTop(10.0f, nullptr, &ButtonBars);
		ButtonBars.HSplitTop(MenuButtonHeight, &ButtonBar2, &ButtonBars);
	}

	CUIRect UtilityButtonBar = UseSecondaryUtilityButtonBar ? ButtonBar2 : ButtonBar;
	const float UtilityButtonSpacing = UseCompactUtilityButtons ? UtilityButtonSpacingCompact : UtilityButtonSpacingNormal;
	const float DisconnectButtonWidth = UseCompactUtilityButtons ? DisconnectButtonWidthCompact : DisconnectButtonWidthNormal;
	const float DummyButtonWidth = UseCompactUtilityButtons ? DummyButtonWidthCompact : DummyButtonWidthNormal;
	const float EditHudButtonWidth = UseCompactUtilityButtons ? EditHudButtonWidthCompact : EditHudButtonWidthNormal;
	const float DemoButtonWidth = UseCompactUtilityButtons ? DemoButtonWidthCompact : DemoButtonWidthNormal;

	UtilityButtonBar.VSplitRight(DisconnectButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_DisconnectButton;
	if(DoButton_Menu(&s_DisconnectButton, Localize("Disconnect"), 0, &Button))
	{
		if((GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0) ||
			GameClient()->m_TouchControls.HasEditingChanges() ||
			GameClient()->m_Menus.m_MenusIngameTouchControls.UnsavedChanges())
		{
			char aBuf[256] = {'\0'};
			if(GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
			{
				str_copy(aBuf, Localize("Are you sure that you want to disconnect?"));
			}
			if(GameClient()->m_TouchControls.HasEditingChanges() ||
				GameClient()->m_Menus.m_MenusIngameTouchControls.UnsavedChanges())
			{
				if(aBuf[0] != '\0')
				{
					str_append(aBuf, "\n\n");
				}
				str_append(aBuf, Localize("There's an unsaved change in the touch controls editor, you might want to save it."));
			}
			PopupConfirm(Localize("Disconnect"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDisconnect);
		}
		else
		{
			Client()->Disconnect();
			RefreshBrowserTab(true);
		}
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(DummyButtonWidth, &UtilityButtonBar, &Button);

	static CButtonContainer s_DummyButton;
	if(!Client()->DummyAllowed())
	{
		DoButton_Menu(&s_DummyButton, pDummyButtonLabel, 1, &Button);
		GameClient()->m_Tooltips.DoToolTip(&s_DummyButton, &Button, Localize("Dummy is not allowed on this server"));
	}
	else if(Client()->DummyConnectingDelayed())
	{
		DoButton_Menu(&s_DummyButton, pDummyButtonLabel, 1, &Button);
		GameClient()->m_Tooltips.DoToolTip(&s_DummyButton, &Button, Localize("Please wait…"));
	}
	else if(Client()->DummyConnecting())
	{
		DoButton_Menu(&s_DummyButton, pDummyButtonLabel, 1, &Button);
	}
	else if(DoButton_Menu(&s_DummyButton, pDummyButtonLabel, 0, &Button))
	{
		if(!Client()->DummyConnected())
		{
			Client()->DummyConnect();
		}
		else
		{
			if(GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
			{
				PopupConfirm(Localize("Disconnect Dummy"), Localize("Are you sure that you want to disconnect your dummy?"), Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDisconnectDummy);
			}
			else
			{
				Client()->DummyDisconnect(nullptr);
				SetActive(false);
			}
		}
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(EditHudButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_EditHudButton;
	if(DoButton_Menu(&s_EditHudButton, pEditHudButtonLabel, 0, &Button))
	{
		GameClient()->m_HudEditor.SetActive(true);
		SetActive(false);
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(DemoButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_DemoButton;
	if(DoButton_Menu(&s_DemoButton, pDemoButtonLabel, 0, &Button))
	{
		if(!Recording)
			Client()->DemoRecorder_Start(Client()->GetCurrentMap(), true, RECORDER_MANUAL);
		else
			Client()->DemoRecorder(RECORDER_MANUAL)->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
	}

	if(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pGameInfoObj && !Paused && !Spec)
	{
		if(GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS)
		{
			ButtonBar.VSplitLeft(SpectateButtonWidth, &Button, &ButtonBar);
			ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);
			static CButtonContainer s_SpectateButton;
			if(!Client()->DummyConnecting() && DoButton_Menu(&s_SpectateButton, pSpectateButtonLabel, 0, &Button))
			{
				if(g_Config.m_ClDummy == 0 || Client()->DummyConnected())
				{
					GameClient()->SendSwitchTeam(TEAM_SPECTATORS);
					SetActive(false);
				}
			}
		}

		if(GameClient()->IsTeamPlay())
		{
			if(GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_RED)
			{
				ButtonBar.VSplitLeft(JoinRedButtonWidth, &Button, &ButtonBar);
				ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);
				static CButtonContainer s_JoinRedButton;
				if(!Client()->DummyConnecting() && DoButton_Menu(&s_JoinRedButton, pJoinRedButtonLabel, 0, &Button))
				{
					GameClient()->SendSwitchTeam(TEAM_RED);
					SetActive(false);
				}
			}

			if(GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_BLUE)
			{
				ButtonBar.VSplitLeft(JoinBlueButtonWidth, &Button, &ButtonBar);
				ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);
				static CButtonContainer s_JoinBlueButton;
				if(!Client()->DummyConnecting() && DoButton_Menu(&s_JoinBlueButton, pJoinBlueButtonLabel, 0, &Button))
				{
					GameClient()->SendSwitchTeam(TEAM_BLUE);
					SetActive(false);
				}
			}
		}
		else
		{
			if(GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_GAME)
			{
				ButtonBar.VSplitLeft(JoinGameButtonWidth, &Button, &ButtonBar);
				ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);
				static CButtonContainer s_JoinGameButton;
				if(!Client()->DummyConnecting() && DoButton_Menu(&s_JoinGameButton, pJoinGameButtonLabel, 0, &Button))
				{
					GameClient()->SendSwitchTeam(TEAM_GAME);
					SetActive(false);
				}
			}
		}

		if(GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS && (ShowDDRaceButtons || !GameClient()->IsTeamPlay()))
		{
			ButtonBar.VSplitLeft(KillButtonWidth, &Button, &ButtonBar);
			ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);

			static CButtonContainer s_KillButton;
			if(DoButton_Menu(&s_KillButton, pKillButtonLabel, 0, &Button))
			{
				GameClient()->SendKill();
				SetActive(false);
			}
		}
	}

	if(GameClient()->m_ReceivedDDNetPlayer && GameClient()->m_Snap.m_pLocalInfo && (ShowDDRaceButtons || !GameClient()->IsTeamPlay()))
	{
		if(GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS || Paused || Spec)
		{
			ButtonBar.VSplitLeft(CurrentPauseButtonWidth, &Button, &ButtonBar);
			ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);

			static CButtonContainer s_PauseButton;
			if(DoButton_Menu(&s_PauseButton, pPauseButtonLabel, 0, &Button))
			{
				Console()->ExecuteLine("say /pause");
				SetActive(false);
			}
		}

		if(GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS && !Paused && !Spec)
		{
			const float ReservedWidth = ShowAutoCameraButton ? (AutoCameraButtonWidth + PrimaryButtonSpacing) : 0.0f;

			const bool CompactPractice = ButtonBar.w < NormalPracticeButtonWidth + PrimaryButtonSpacing + ReservedWidth;
			float PracticeButtonWidth = CompactPractice ? CompactPracticeButtonWidth : NormalPracticeButtonWidth;
			const float MaxPracticeButtonWidth = maximum(0.0f, ButtonBar.w - ReservedWidth);
			if(MaxPracticeButtonWidth < PracticeButtonMinWidth)
				PracticeButtonWidth = MaxPracticeButtonWidth;
			else
				PracticeButtonWidth = std::clamp(PracticeButtonWidth, PracticeButtonMinWidth, MaxPracticeButtonWidth);

			if(PracticeButtonWidth > 0.0f)
			{
				ButtonBar.VSplitLeft(PracticeButtonWidth, &Button, &ButtonBar);
				if(ButtonBar.w >= PrimaryButtonSpacing)
					ButtonBar.VSplitLeft(PrimaryButtonSpacing, nullptr, &ButtonBar);

				static CButtonContainer s_FastPracticeButton;
				const bool UseCompactLabel = CompactPractice || PracticeButtonWidth <= CompactPracticeButtonWidth;
				const char *pFastPracticeButtonLabel = UseCompactLabel ? "fp" : pFastPracticeLabel;
				if(DoButton_Menu(&s_FastPracticeButton, pFastPracticeButtonLabel, FastPracticeEnabled ? 1 : 0, &Button))
				{
					Console()->ExecuteLine("fast_practice_toggle", IConsole::CLIENT_ID_UNSPECIFIED);
				}
			}
		}
	}

	if(GameClient()->m_Snap.m_pLocalInfo && (GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS || Paused || Spec))
	{
		ButtonBar.VSplitLeft(32.0f, &Button, &ButtonBar);
		ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);

		static CButtonContainer s_AutoCameraButton;

		bool Active = GameClient()->m_Camera.m_AutoSpecCamera && GameClient()->m_Camera.SpectatingPlayer() && GameClient()->m_Camera.CanUseAutoSpecCamera();
		bool Enabled = g_Config.m_ClSpecAutoSync;
		if(Ui()->DoButton_FontIcon(&s_AutoCameraButton, FONT_ICON_CAMERA, !Active, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Enabled))
		{
			GameClient()->m_Camera.ToggleAutoSpecCamera();
		}
		GameClient()->m_Camera.UpdateAutoSpecCameraTooltip();
		GameClient()->m_Tooltips.DoToolTip(&s_AutoCameraButton, &Button, GameClient()->m_Camera.AutoSpecCameraTooltip());
	}

	if(g_Config.m_ClTouchControls)
	{
		ButtonBar2.VSplitLeft(200.0f, &Button, &ButtonBar2);
		static char s_TouchControlsEditCheckbox;
		if(DoButton_CheckBox(&s_TouchControlsEditCheckbox, Localize("Edit touch controls"), GameClient()->m_TouchControls.IsEditingActive(), &Button))
		{
			if(GameClient()->m_TouchControls.IsEditingActive() && m_MenusIngameTouchControls.UnsavedChanges())
			{
				m_MenusIngameTouchControls.m_pOldSelectedButton = GameClient()->m_TouchControls.SelectedButton();
				m_MenusIngameTouchControls.m_pNewSelectedButton = nullptr;
				PopupConfirm(Localize("Unsaved changes"), Localize("Save all changes before turning off the editor?"), Localize("Save"), Localize("Cancel"), &CMenus::PopupConfirmTurnOffEditor);
			}
			else
			{
				GameClient()->m_TouchControls.SetEditingActive(!GameClient()->m_TouchControls.IsEditingActive());
				if(GameClient()->m_TouchControls.IsEditingActive())
				{
					GameClient()->m_TouchControls.ResetVirtualVisibilities();
					m_MenusIngameTouchControls.m_EditElement = CMenusIngameTouchControls::EElementType::LAYOUT;
				}
				else
				{
					m_MenusIngameTouchControls.ResetButtonPointers();
				}
			}
		}

		ButtonBar2.VSplitRight(80.0f, &ButtonBar2, &Button);
		static CButtonContainer s_CloseButton;
		if(DoButton_Menu(&s_CloseButton, Localize("Close"), 0, &Button))
		{
			SetActive(false);
		}

		ButtonBar2.VSplitRight(5.0f, &ButtonBar2, nullptr);
		ButtonBar2.VSplitRight(160.0f, &ButtonBar2, &Button);
		static CButtonContainer s_RemoveConsoleButton;
		if(DoButton_Menu(&s_RemoveConsoleButton, Localize("Remote console"), 0, &Button))
		{
			Console()->ExecuteLine("toggle_remote_console");
		}

		ButtonBar2.VSplitRight(5.0f, &ButtonBar2, nullptr);
		ButtonBar2.VSplitRight(120.0f, &ButtonBar2, &Button);
		static CButtonContainer s_LocalConsoleButton;
		if(DoButton_Menu(&s_LocalConsoleButton, Localize("Console"), 0, &Button))
		{
			Console()->ExecuteLine("toggle_local_console");
		}
		// Only when these are all false, the preview page is rendered. Once the page is not rendered, update is needed upon next rendering.
		if(!GameClient()->m_TouchControls.IsEditingActive() || m_MenusIngameTouchControls.m_CurrentMenu != CMenusIngameTouchControls::EMenuType::MENU_BUTTONS || GameClient()->m_TouchControls.IsButtonEditing())
			m_MenusIngameTouchControls.m_NeedUpdatePreview = true;
		// Quit preview all buttons automatically.
		if(!GameClient()->m_TouchControls.IsEditingActive() || m_MenusIngameTouchControls.m_CurrentMenu != CMenusIngameTouchControls::EMenuType::MENU_PREVIEW)
			GameClient()->m_TouchControls.SetPreviewAllButtons(false);
		if(GameClient()->m_TouchControls.IsEditingActive())
		{
			// Resolve issues if needed before rendering, so the elements could have a correct value on this frame.
			// Issues need to be resolved before popup. So CheckCachedSettings could not be bad.
			m_MenusIngameTouchControls.ResolveIssues();
			// Do Popups if needed.
			CTouchControls::CPopupParam PopupParam = GameClient()->m_TouchControls.RequiredPopup();
			if(PopupParam.m_PopupType != CTouchControls::EPopupType::NUM_POPUPS)
			{
				m_MenusIngameTouchControls.DoPopupType(PopupParam);
				return;
			}
			if(m_MenusIngameTouchControls.m_FirstEnter)
			{
				m_MenusIngameTouchControls.m_aCachedVisibilities[(int)CTouchControls::EButtonVisibility::DEMO_PLAYER] = CMenusIngameTouchControls::EVisibilityType::EXCLUDE;
				m_MenusIngameTouchControls.m_ColorActive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorActive()).Pack(true);
				m_MenusIngameTouchControls.m_ColorInactive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorInactive()).Pack(true);
				m_MenusIngameTouchControls.m_FirstEnter = false;
			}
			// Their width is all 505.0f, height is adjustable, you can directly change its h value, so no need for changing where tab is.
			CUIRect SelectingTab;
			MainView.HSplitTop(40.0f, nullptr, &MainView);
			MainView.VMargin((MainView.w - CMenusIngameTouchControls::BUTTON_EDITOR_WIDTH) / 2.0f, &MainView);
			MainView.HSplitTop(25.0f, &SelectingTab, &MainView);

			static bool s_TouchMenuTransitionInitialized = false;
			static CMenusIngameTouchControls::EMenuType s_PrevTouchMenu = CMenusIngameTouchControls::EMenuType::MENU_FILE;
			static float s_TouchMenuTransitionDirection = 0.0f;
			const uint64_t TouchMenuSwitchNode = UiAnimNodeKey("ingame_touch_menu_switch");

			m_MenusIngameTouchControls.RenderSelectingTab(SelectingTab);
			if(!s_TouchMenuTransitionInitialized)
			{
				s_PrevTouchMenu = m_MenusIngameTouchControls.m_CurrentMenu;
				s_TouchMenuTransitionInitialized = true;
			}
			else if(m_MenusIngameTouchControls.m_CurrentMenu != s_PrevTouchMenu)
			{
				s_TouchMenuTransitionDirection = static_cast<int>(m_MenusIngameTouchControls.m_CurrentMenu) > static_cast<int>(s_PrevTouchMenu) ? 1.0f : -1.0f;
				TriggerUiSwitchAnimation(TouchMenuSwitchNode, 0.18f);
				s_PrevTouchMenu = m_MenusIngameTouchControls.m_CurrentMenu;
			}

			const float TransitionStrength = ReadUiSwitchAnimation(TouchMenuSwitchNode);
			const bool TransitionActive = TransitionStrength > 0.0f && s_TouchMenuTransitionDirection != 0.0f;
			const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
			CUIRect MenuContent = MainView;
			const CUIRect MenuContentClip = MainView;
			if(TransitionActive)
			{
				Ui()->ClipEnable(&MenuContentClip);
				ApplyUiSwitchOffset(MenuContent, TransitionStrength, s_TouchMenuTransitionDirection, false, 0.08f, 24.0f, 120.0f);
			}

			switch(m_MenusIngameTouchControls.m_CurrentMenu)
			{
			case CMenusIngameTouchControls::EMenuType::MENU_FILE: m_MenusIngameTouchControls.RenderTouchControlsEditor(MenuContent); break;
			case CMenusIngameTouchControls::EMenuType::MENU_BUTTONS: m_MenusIngameTouchControls.RenderTouchButtonEditor(MenuContent); break;
			case CMenusIngameTouchControls::EMenuType::MENU_SETTINGS: m_MenusIngameTouchControls.RenderConfigSettings(MenuContent); break;
			case CMenusIngameTouchControls::EMenuType::MENU_PREVIEW: m_MenusIngameTouchControls.RenderPreviewSettings(MenuContent); break;
			default: dbg_assert_failed("Unknown selected tab value = %d.", (int)m_MenusIngameTouchControls.m_CurrentMenu);
			}
			if(TransitionActive)
			{
				if(TransitionAlpha > 0.0f)
					MenuContentClip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha), IGraphics::CORNER_NONE, 0.0f);
				Ui()->ClipDisable();
			}
		}
	}
}

void CMenus::PopupConfirmDisconnect()
{
	Client()->Disconnect();
}

void CMenus::PopupConfirmDisconnectDummy()
{
	Client()->DummyDisconnect(nullptr);
	SetActive(false);
}

void CMenus::PopupConfirmDiscardTouchControlsChanges()
{
	if(GameClient()->m_TouchControls.LoadConfigurationFromFile(IStorage::TYPE_ALL))
	{
		m_MenusIngameTouchControls.m_ColorActive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorActive()).Pack(true);
		m_MenusIngameTouchControls.m_ColorInactive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorInactive()).Pack(true);
		GameClient()->m_TouchControls.SetEditingChanges(false);
	}
	else
	{
		SWarning Warning(Localize("Error loading touch controls"), Localize("Could not load touch controls from file. See local console for details."));
		Warning.m_AutoHide = false;
		Client()->AddWarning(Warning);
	}
}

void CMenus::PopupConfirmResetTouchControls()
{
	bool Success = false;
	for(int StorageType = IStorage::TYPE_SAVE + 1; StorageType < Storage()->NumPaths(); ++StorageType)
	{
		if(GameClient()->m_TouchControls.LoadConfigurationFromFile(StorageType))
		{
			Success = true;
			break;
		}
	}
	if(Success)
	{
		m_MenusIngameTouchControls.m_ColorActive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorActive()).Pack(true);
		m_MenusIngameTouchControls.m_ColorInactive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorInactive()).Pack(true);
		GameClient()->m_TouchControls.SetEditingChanges(true);
	}
	else
	{
		SWarning Warning(Localize("Error loading touch controls"), Localize("Could not load default touch controls from file. See local console for details."));
		Warning.m_AutoHide = false;
		Client()->AddWarning(Warning);
	}
}

void CMenus::PopupConfirmImportTouchControlsClipboard()
{
	if(GameClient()->m_TouchControls.LoadConfigurationFromClipboard())
	{
		m_MenusIngameTouchControls.m_ColorActive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorActive()).Pack(true);
		m_MenusIngameTouchControls.m_ColorInactive = color_cast<ColorHSLA>(GameClient()->m_TouchControls.BackgroundColorInactive()).Pack(true);
		GameClient()->m_TouchControls.SetEditingChanges(true);
	}
	else
	{
		SWarning Warning(Localize("Error loading touch controls"), Localize("Could not load touch controls from clipboard. See local console for details."));
		Warning.m_AutoHide = false;
		Client()->AddWarning(Warning);
	}
}

void CMenus::PopupConfirmDeleteButton()
{
	GameClient()->m_TouchControls.DeleteSelectedButton();
	m_MenusIngameTouchControls.ResetCachedSettings();
	GameClient()->m_TouchControls.SetEditingChanges(true);
}

void CMenus::PopupCancelDeselectButton()
{
	m_MenusIngameTouchControls.ResetButtonPointers();
	m_MenusIngameTouchControls.SetUnsavedChanges(false);
	m_MenusIngameTouchControls.ResetCachedSettings();
}

void CMenus::PopupConfirmSelectedNotVisible()
{
	if(m_MenusIngameTouchControls.UnsavedChanges())
	{
		// The m_pSelectedButton can't nullptr, because this function is triggered when selected button not visible.
		m_MenusIngameTouchControls.m_pOldSelectedButton = GameClient()->m_TouchControls.SelectedButton();
		m_MenusIngameTouchControls.m_pNewSelectedButton = nullptr;
		m_MenusIngameTouchControls.m_CloseMenu = true;
		m_MenusIngameTouchControls.ChangeSelectedButtonWhileHavingUnsavedChanges();
	}
	else
	{
		m_MenusIngameTouchControls.ResetButtonPointers();
		GameClient()->m_Menus.SetActive(false);
	}
}

void CMenus::PopupConfirmChangeSelectedButton()
{
	if(m_MenusIngameTouchControls.CheckCachedSettings())
	{
		GameClient()->m_TouchControls.SetSelectedButton(m_MenusIngameTouchControls.m_pNewSelectedButton);
		m_MenusIngameTouchControls.SaveCachedSettingsToTarget(m_MenusIngameTouchControls.m_pOldSelectedButton);
		// Update wild pointer.
		if(m_MenusIngameTouchControls.m_pNewSelectedButton != nullptr)
			m_MenusIngameTouchControls.m_pNewSelectedButton = GameClient()->m_TouchControls.SelectedButton();
		GameClient()->m_TouchControls.SetEditingChanges(true);
		m_MenusIngameTouchControls.SetUnsavedChanges(false);
		PopupCancelChangeSelectedButton();
	}
}

void CMenus::PopupCancelChangeSelectedButton()
{
	GameClient()->m_TouchControls.SetSelectedButton(m_MenusIngameTouchControls.m_pNewSelectedButton);
	m_MenusIngameTouchControls.CacheAllSettingsFromTarget(m_MenusIngameTouchControls.m_pNewSelectedButton);
	m_MenusIngameTouchControls.SetUnsavedChanges(false);
	if(m_MenusIngameTouchControls.m_pNewSelectedButton != nullptr)
	{
		m_MenusIngameTouchControls.UpdateSampleButton();
	}
	else
	{
		m_MenusIngameTouchControls.ResetButtonPointers();
	}
	if(m_MenusIngameTouchControls.m_CloseMenu)
		GameClient()->m_Menus.SetActive(false);
}

void CMenus::PopupConfirmTurnOffEditor()
{
	if(m_MenusIngameTouchControls.CheckCachedSettings())
	{
		m_MenusIngameTouchControls.SaveCachedSettingsToTarget(m_MenusIngameTouchControls.m_pOldSelectedButton);
		GameClient()->m_TouchControls.SetEditingActive(!GameClient()->m_TouchControls.IsEditingActive());
		m_MenusIngameTouchControls.ResetButtonPointers();
	}
}

void CMenus::RenderPlayers(CUIRect MainView)
{
	CUIRect Button, Button2, ButtonBar, PlayerList, Player;
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);

	// list background color
	MainView.Margin(10.0f, &PlayerList);
	PlayerList.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	PlayerList.Margin(10.0f, &PlayerList);

	// headline
	PlayerList.HSplitTop(34.0f, &ButtonBar, &PlayerList);
	ButtonBar.VSplitRight(231.0f, &Player, &ButtonBar);
	Ui()->DoLabel(&Player, Localize("Player"), 24.0f, TEXTALIGN_ML);

	ButtonBar.HMargin(1.0f, &ButtonBar);
	float Width = ButtonBar.h * 2.0f;
	ButtonBar.VSplitLeft(Width, &Button, &ButtonBar);
	RenderTools()->RenderIcon(IMAGE_GUIICONS, SPRITE_GUIICON_MUTE, &Button);

	ButtonBar.VSplitLeft(20.0f, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(Width, &Button, &ButtonBar);
	RenderTools()->RenderIcon(IMAGE_GUIICONS, SPRITE_GUIICON_EMOTICON_MUTE, &Button);

	ButtonBar.VSplitLeft(20.0f, nullptr, &ButtonBar);
	ButtonBar.VSplitLeft(Width, &Button, &ButtonBar);
	RenderTools()->RenderIcon(IMAGE_GUIICONS, SPRITE_GUIICON_FRIEND, &Button);

	int TotalPlayers = 0;
	for(const auto &pInfoByName : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfoByName)
			continue;

		int Index = pInfoByName->m_ClientId;

		if(Index == GameClient()->m_Snap.m_LocalClientId)
			continue;

		TotalPlayers++;
	}

	static CListBox s_ListBox;
	s_ListBox.DoStart(24.0f, TotalPlayers, 1, 3, -1, &PlayerList);

	// options
	static char s_aPlayerIds[MAX_CLIENTS][4] = {{0}};

	for(int i = 0, Count = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameClient()->m_Snap.m_apInfoByName[i])
			continue;

		int Index = GameClient()->m_Snap.m_apInfoByName[i]->m_ClientId;
		if(Index == GameClient()->m_Snap.m_LocalClientId)
			continue;

		CGameClient::CClientData &CurrentClient = GameClient()->m_aClients[Index];
		const bool HideSkin = GameClient()->ShouldHideStreamerSkin(Index);
		char aNameBuf[MAX_NAME_LENGTH];
		char aClanBuf[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(Index, aNameBuf, sizeof(aNameBuf));
		GameClient()->FormatStreamerClan(Index, aClanBuf, sizeof(aClanBuf));
		const CListboxItem Item = s_ListBox.DoNextItem(&CurrentClient);

		Count++;

		if(!Item.m_Visible)
			continue;

		CUIRect Row = Item.m_Rect;
		if(Count % 2 == 1)
			Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
		Row.VSplitRight(s_ListBox.ScrollbarWidthMax() - s_ListBox.ScrollbarWidth(), &Row, nullptr);
		Row.VSplitRight(300.0f, &Player, &Row);

		// player info
		Player.VSplitLeft(28.0f, &Button, &Player);

		CTeeRenderInfo TeeInfo = CurrentClient.m_RenderInfo;
		TeeInfo.m_Size = Button.h;

		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		vec2 TeeRenderPos(Button.x + Button.h / 2, Button.y + Button.h / 2 + OffsetToMid.y);
		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
		Ui()->DoButtonLogic(&s_aPlayerIds[Index][3], 0, &Button, BUTTONFLAG_NONE);
		GameClient()->m_Tooltips.DoToolTip(&s_aPlayerIds[Index][3], &Button, HideSkin ? "default" : CurrentClient.m_aSkinName);

		Player.HSplitTop(1.5f, nullptr, &Player);
		Player.VSplitMid(&Player, &Button);
		Row.VSplitRight(210.0f, &Button2, &Row);

		Ui()->DoLabel(&Player, aNameBuf, 14.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Button, aClanBuf, 14.0f, TEXTALIGN_ML);

		GameClient()->m_CountryFlags.Render(CurrentClient.m_Country, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f),
			Button2.x, Button2.y + Button2.h / 2.0f - 0.75f * Button2.h / 2.0f, 1.5f * Button2.h, 0.75f * Button2.h);

		// ignore chat button
		Row.HMargin(2.0f, &Row);
		Row.VSplitLeft(Width, &Button, &Row);
		Button.VSplitLeft((Width - Button.h) / 4.0f, nullptr, &Button);
		Button.VSplitLeft(Button.h, &Button, nullptr);
		if(g_Config.m_ClShowChatFriends && !CurrentClient.m_Friend)
			DoButton_Toggle(&s_aPlayerIds[Index][0], 1, &Button, false);
		else if(DoButton_Toggle(&s_aPlayerIds[Index][0], CurrentClient.m_ChatIgnore, &Button, true))
			CurrentClient.m_ChatIgnore ^= 1;

		// ignore emoticon button
		Row.VSplitLeft(30.0f, nullptr, &Row);
		Row.VSplitLeft(Width, &Button, &Row);
		Button.VSplitLeft((Width - Button.h) / 4.0f, nullptr, &Button);
		Button.VSplitLeft(Button.h, &Button, nullptr);
		if(g_Config.m_ClShowChatFriends && !CurrentClient.m_Friend)
			DoButton_Toggle(&s_aPlayerIds[Index][1], 1, &Button, false);
		else if(DoButton_Toggle(&s_aPlayerIds[Index][1], CurrentClient.m_EmoticonIgnore, &Button, true))
			CurrentClient.m_EmoticonIgnore ^= 1;

		// friend button
		Row.VSplitLeft(10.0f, nullptr, &Row);
		Row.VSplitLeft(Width, &Button, &Row);
		Button.VSplitLeft((Width - Button.h) / 4.0f, nullptr, &Button);
		Button.VSplitLeft(Button.h, &Button, nullptr);
		if(DoButton_Toggle(&s_aPlayerIds[Index][2], CurrentClient.m_Friend, &Button, true))
		{
			if(CurrentClient.m_Friend)
				GameClient()->Friends()->RemoveFriend(CurrentClient.m_aName, CurrentClient.m_aClan);
			else
				GameClient()->Friends()->AddFriend(CurrentClient.m_aName, CurrentClient.m_aClan);

			GameClient()->Client()->ServerBrowserUpdate();
		}
	}

	s_ListBox.DoEnd();
}

void CMenus::RenderServerInfo(CUIRect MainView)
{
	const float FontSizeTitle = 32.0f;
	const float FontSizeBody = 20.0f;

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	CUIRect ServerInfo, GameInfo, Motd;
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(10.0f, &MainView);
	MainView.HSplitMid(&ServerInfo, &Motd, 10.0f);
	ServerInfo.VSplitMid(&ServerInfo, &GameInfo, 10.0f);

	ServerInfo.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	ServerInfo.Margin(10.0f, &ServerInfo);

	CUIRect Label;
	ServerInfo.HSplitTop(FontSizeTitle, &Label, &ServerInfo);
	ServerInfo.HSplitTop(5.0f, nullptr, &ServerInfo);
	Ui()->DoLabel(&Label, Localize("Server info"), FontSizeTitle, TEXTALIGN_ML);

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	ServerInfo.HSplitTop(FontSizeBody, nullptr, &ServerInfo);
	Ui()->DoLabel(&Label, CurrentServerInfo.m_aName, FontSizeBody, TEXTALIGN_ML);

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Address"), CurrentServerInfo.m_aAddress);
	Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);

	if(GameClient()->m_Snap.m_pLocalInfo)
	{
		ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Ping"), GameClient()->m_Snap.m_pLocalInfo->m_Latency);
		Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);
	}

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Version"), CurrentServerInfo.m_aVersion);
	Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Password"), CurrentServerInfo.m_Flags & SERVER_FLAG_PASSWORD ? Localize("Yes") : Localize("No"));
	Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);

	const CCommunity *pCommunity = ServerBrowser()->Community(CurrentServerInfo.m_aCommunityId);
	if(pCommunity != nullptr)
	{
		ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Community"));
		Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);

		const CCommunityIcon *pIcon = m_CommunityIcons.Find(pCommunity->Id());
		if(pIcon != nullptr)
		{
			Label.VSplitLeft(TextRender()->TextWidth(FontSizeBody, aBuf) + 8.0f, nullptr, &Label);
			Label.VSplitLeft(2.0f * Label.h, &Label, nullptr);
			m_CommunityIcons.Render(pIcon, Label, true);
			static char s_CommunityTooltipButtonId;
			Ui()->DoButtonLogic(&s_CommunityTooltipButtonId, 0, &Label, BUTTONFLAG_NONE);
			GameClient()->m_Tooltips.DoToolTip(&s_CommunityTooltipButtonId, &Label, pCommunity->Name());
		}
	}

	// copy info button
	{
		CUIRect Button;
		ServerInfo.HSplitBottom(20.0f, &ServerInfo, &Button);
		Button.VSplitRight(200.0f, &ServerInfo, &Button);
		static CButtonContainer s_CopyButton;
		if(DoButton_Menu(&s_CopyButton, Localize("Copy info"), 0, &Button))
		{
			char aInfo[512];
			str_format(
				aInfo,
				sizeof(aInfo),
				"%s\n"
				"Address: ddnet://%s\n"
				"Map: %s\n"
				"My IGN: %s\n",
				CurrentServerInfo.m_aName,
				CurrentServerInfo.m_aAddress,
				CurrentServerInfo.m_aMap,
				Client()->PlayerName());
			Input()->SetClipboardText(aInfo);
		}
	}

	// favorite checkbox
	{
		CUIRect Button;
		TRISTATE IsFavorite = Favorites()->IsFavorite(CurrentServerInfo.m_aAddresses, CurrentServerInfo.m_NumAddresses);
		ServerInfo.HSplitBottom(20.0f, &ServerInfo, &Button);
		static int s_AddFavButton = 0;
		if(DoButton_CheckBox(&s_AddFavButton, Localize("Favorite"), IsFavorite != TRISTATE::NONE, &Button))
		{
			if(IsFavorite != TRISTATE::NONE)
				Favorites()->Remove(CurrentServerInfo.m_aAddresses, CurrentServerInfo.m_NumAddresses);
			else
				Favorites()->Add(CurrentServerInfo.m_aAddresses, CurrentServerInfo.m_NumAddresses);
		}
	}

	// favorite map checkbox
	{
		CUIRect Button;
		bool IsMapFavorite = GameClient()->m_TClient.IsFavoriteMap(CurrentServerInfo.m_aMap);
		ServerInfo.HSplitBottom(20.0f, &ServerInfo, &Button);
		static int s_AddFavMapButton = 0;
		if(DoButton_CheckBox(&s_AddFavMapButton, Localize("Favorite map"), IsMapFavorite, &Button))
		{
			if(IsMapFavorite)
				GameClient()->m_TClient.RemoveFavoriteMap(CurrentServerInfo.m_aMap);
			else
				GameClient()->m_TClient.AddFavoriteMap(CurrentServerInfo.m_aMap);
		}
	}

	GameInfo.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	GameInfo.Margin(10.0f, &GameInfo);

	GameInfo.HSplitTop(FontSizeTitle, &Label, &GameInfo);
	GameInfo.HSplitTop(5.0f, nullptr, &GameInfo);
	Ui()->DoLabel(&Label, Localize("Game info"), FontSizeTitle, TEXTALIGN_ML);

	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Game type"), CurrentServerInfo.m_aGameType);
	Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);

	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Map"), CurrentServerInfo.m_aMap);
	Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);

	const auto *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(pGameInfoObj)
	{
		if(pGameInfoObj->m_ScoreLimit)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Score limit"), pGameInfoObj->m_ScoreLimit);
			Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);
		}

		if(pGameInfoObj->m_TimeLimit)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), Localize("Time limit: %d min"), pGameInfoObj->m_TimeLimit);
			Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);
		}

		if(pGameInfoObj->m_RoundCurrent && pGameInfoObj->m_RoundNum)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), Localize("Round %d/%d"), pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
			Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);
		}
	}

	if(GameClient()->m_GameInfo.m_DDRaceTeam)
	{
		const char *pTeamMode = nullptr;
		switch(Config()->m_SvTeam)
		{
		case SV_TEAM_FORBIDDEN:
			pTeamMode = Localize("forbidden", "Team status");
			break;
		case SV_TEAM_ALLOWED:
			if(g_Config.m_SvSoloServer)
				pTeamMode = Localize("solo", "Team status");
			else
				pTeamMode = Localize("allowed", "Team status");
			break;
		case SV_TEAM_MANDATORY:
			pTeamMode = Localize("required", "Team status");
			break;
		case SV_TEAM_FORCED_SOLO:
			pTeamMode = Localize("solo", "Team status");
			break;
		default:
			dbg_assert_failed("unknown team mode");
		}
		if((Config()->m_SvTeam == SV_TEAM_ALLOWED || Config()->m_SvTeam == SV_TEAM_MANDATORY) && (Config()->m_SvMinTeamSize != CConfig::ms_SvMinTeamSize || Config()->m_SvMaxTeamSize != CConfig::ms_SvMaxTeamSize))
		{
			if(Config()->m_SvMinTeamSize != CConfig::ms_SvMinTeamSize && Config()->m_SvMaxTeamSize != CConfig::ms_SvMaxTeamSize)
				str_format(aBuf, sizeof(aBuf), "%s: %s (%s %d, %s %d)", Localize("Teams"), pTeamMode, Localize("minimum", "Team size"), Config()->m_SvMinTeamSize, Localize("maximum", "Team size"), Config()->m_SvMaxTeamSize);
			else if(Config()->m_SvMinTeamSize != CConfig::ms_SvMinTeamSize)
				str_format(aBuf, sizeof(aBuf), "%s: %s (%s %d)", Localize("Teams"), pTeamMode, Localize("minimum", "Team size"), Config()->m_SvMinTeamSize);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s (%s %d)", Localize("Teams"), pTeamMode, Localize("maximum", "Team size"), Config()->m_SvMaxTeamSize);
		}
		else
			str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Teams"), pTeamMode);
		GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
		Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);
	}

	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	str_format(aBuf, sizeof(aBuf), "%s: %d/%d", Localize("Players"), GameClient()->m_Snap.m_NumPlayers, CurrentServerInfo.m_MaxClients);
	Ui()->DoLabel(&Label, aBuf, FontSizeBody, TEXTALIGN_ML);

	RenderServerInfoMotd(Motd);
}

void CMenus::RenderServerInfoMotd(CUIRect Motd)
{
	const float MotdFontSize = 16.0f;
	Motd.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	Motd.Margin(10.0f, &Motd);

	CUIRect MotdHeader;
	Motd.HSplitTop(2.0f * MotdFontSize, &MotdHeader, &Motd);
	Motd.HSplitTop(5.0f, nullptr, &Motd);
	Ui()->DoLabel(&MotdHeader, Localize("MOTD"), 2.0f * MotdFontSize, TEXTALIGN_ML);

	if(!GameClient()->m_Motd.ServerMotd()[0])
		return;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 5 * MotdFontSize;
	s_ScrollRegion.Begin(&Motd, &ScrollOffset, &ScrollParams);
	Motd.y += ScrollOffset.y;

	static float s_MotdHeight = 0.0f;
	static int64_t s_MotdLastUpdateTime = -1;
	if(!m_MotdTextContainerIndex.Valid() || s_MotdLastUpdateTime == -1 || s_MotdLastUpdateTime != GameClient()->m_Motd.ServerMotdUpdateTime())
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = MotdFontSize;
		Cursor.m_LineWidth = Motd.w;
		TextRender()->RecreateTextContainer(m_MotdTextContainerIndex, &Cursor, GameClient()->m_Motd.ServerMotd());
		s_MotdHeight = Cursor.Height();
		s_MotdLastUpdateTime = GameClient()->m_Motd.ServerMotdUpdateTime();
	}

	CUIRect MotdTextArea;
	Motd.HSplitTop(s_MotdHeight, &MotdTextArea, &Motd);
	s_ScrollRegion.AddRect(MotdTextArea);

	if(m_MotdTextContainerIndex.Valid())
		TextRender()->RenderTextContainer(m_MotdTextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), MotdTextArea.x, MotdTextArea.y);

	s_ScrollRegion.End();
}

bool CMenus::RenderServerControlServer(CUIRect MainView, bool UpdateScroll)
{
	CUIRect List = MainView;
	int NumVoteOptions = 0;
	int aIndices[MAX_VOTE_OPTIONS];
	int Selected = -1;
	int TotalShown = 0;

	// 检查是否为地图投票并提取地图名的辅助函数
	auto ExtractMapName = [](const char *pDescription, char *pMapName, int MaxLen) -> bool {
		// 地图投票格式通常为:
		// "MapName by Author | x/5 ★" 或 "Map: MapName"
		if(!pDescription)
			return false;

		// 尝试匹配 "Map: " 前缀
		const char *pMapPrefix = str_find_nocase(pDescription, "Map:");
		if(pMapPrefix)
		{
			pMapPrefix += 4; // 跳过 "Map:"
			while(*pMapPrefix == ' ')
				pMapPrefix++;
			str_copy(pMapName, pMapPrefix, MaxLen);
			return true;
		}

		// 尝试匹配 "Name by Author" 格式
		const char *pBy = str_find_nocase(pDescription, " by ");
		if(pBy && (str_find(pDescription, "★") || str_find(pDescription, "✰")))
		{
			int Len = minimum((int)(pBy - pDescription), MaxLen - 1);
			str_copy(pMapName, pDescription, Len + 1);
			return true;
		}

		return false;
	};

	int i = 0;
	for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, i++)
	{
		if(!m_FilterInput.IsEmpty() && !str_utf8_find_nocase(pOption->m_aDescription, m_FilterInput.GetString()))
			continue;
		if(i == m_CallvoteSelectedOption)
			Selected = TotalShown;
		TotalShown++;
	}

	static CListBox s_ListBox;
	s_ListBox.DoStart(19.0f, TotalShown, 1, 3, Selected, &List);

	i = 0;
	for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, i++)
	{
		if(!m_FilterInput.IsEmpty() && !str_utf8_find_nocase(pOption->m_aDescription, m_FilterInput.GetString()))
			continue;
		aIndices[NumVoteOptions] = i;
		NumVoteOptions++;

		const CListboxItem Item = s_ListBox.DoNextItem(pOption);
		if(!Item.m_Visible)
			continue;

		CUIRect Label;
		Item.m_Rect.VMargin(2.0f, &Label);

		// 检查是否是收藏地图，用金色高亮
		char aMapName[128];
		bool IsFavorite = false;
		if(ExtractMapName(pOption->m_aDescription, aMapName, sizeof(aMapName)))
		{
			IsFavorite = GameClient()->m_TClient.IsFavoriteMap(aMapName);
		}

		if(IsFavorite)
		{
			// 金色高亮
			ColorRGBA GoldColor(1.0f, 0.85f, 0.0f, 1.0f);
			TextRender()->TextColor(GoldColor);
			Ui()->DoLabel(&Label, pOption->m_aDescription, 13.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else
		{
			Ui()->DoLabel(&Label, pOption->m_aDescription, 13.0f, TEXTALIGN_ML);
		}
	}

	Selected = s_ListBox.DoEnd();
	if(UpdateScroll)
		s_ListBox.ScrollToSelected();
	m_CallvoteSelectedOption = Selected != -1 ? aIndices[Selected] : -1;
	return s_ListBox.WasItemActivated();
}

bool CMenus::RenderServerControlKick(CUIRect MainView, bool FilterSpectators, bool UpdateScroll)
{
	int NumOptions = 0;
	int Selected = -1;
	int aPlayerIds[MAX_CLIENTS];
	for(const auto &pInfoByName : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfoByName)
			continue;

		int Index = pInfoByName->m_ClientId;
		if(Index == GameClient()->m_Snap.m_LocalClientId || (FilterSpectators && pInfoByName->m_Team == TEAM_SPECTATORS))
			continue;

		if(!str_utf8_find_nocase(GameClient()->m_aClients[Index].m_aName, m_FilterInput.GetString()))
			continue;

		if(m_CallvoteSelectedPlayer == Index)
			Selected = NumOptions;
		aPlayerIds[NumOptions] = Index;
		NumOptions++;
	}

	static CListBox s_ListBox;
	s_ListBox.DoStart(24.0f, NumOptions, 1, 3, Selected, &MainView);

	for(int i = 0; i < NumOptions; i++)
	{
		const CListboxItem Item = s_ListBox.DoNextItem(&aPlayerIds[i]);
		if(!Item.m_Visible)
			continue;

		CUIRect TeeRect, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h, &TeeRect, &Label);

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[aPlayerIds[i]].m_RenderInfo;
		TeeInfo.m_Size = TeeRect.h;

		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		vec2 TeeRenderPos(TeeRect.x + TeeInfo.m_Size / 2, TeeRect.y + TeeInfo.m_Size / 2 + OffsetToMid.y);

		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);

		char aNameBuf[MAX_NAME_LENGTH];
		GameClient()->FormatStreamerName(aPlayerIds[i], aNameBuf, sizeof(aNameBuf));
		Ui()->DoLabel(&Label, aNameBuf, 16.0f, TEXTALIGN_ML);
	}

	Selected = s_ListBox.DoEnd();
	if(UpdateScroll)
		s_ListBox.ScrollToSelected();
	m_CallvoteSelectedPlayer = Selected != -1 ? aPlayerIds[Selected] : -1;
	return s_ListBox.WasItemActivated();
}

void CMenus::RenderServerControl(CUIRect MainView)
{
	enum class EServerControlTab
	{
		SETTINGS,
		KICKVOTE,
		SPECVOTE,
	};
	static EServerControlTab s_ControlPage = EServerControlTab::SETTINGS;
	static bool s_ControlPageTransitionInitialized = false;
	static EServerControlTab s_PrevControlPage = EServerControlTab::SETTINGS;
	static float s_ControlPageTransitionDirection = 0.0f;
	const uint64_t ControlPageSwitchNode = UiAnimNodeKey("ingame_callvote_tab_switch");

	// render background
	CUIRect Bottom, RconExtension, TabBar, Button;
	MainView.HSplitTop(20.0f, &Bottom, &MainView);
	Bottom.Draw(ms_ColorTabbarActive, IGraphics::CORNER_NONE, 0.0f);
	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(10.0f, &MainView);

	if(Client()->RconAuthed())
		MainView.HSplitBottom(90.0f, &MainView, &RconExtension);

	// tab bar
	TabBar.VSplitLeft(TabBar.w / 3, &Button, &TabBar);
	static CButtonContainer s_Button0;
	if(DoButton_MenuTab(&s_Button0, Localize("Change settings"), s_ControlPage == EServerControlTab::SETTINGS, &Button, IGraphics::CORNER_NONE))
		s_ControlPage = EServerControlTab::SETTINGS;

	TabBar.VSplitMid(&Button, &TabBar);
	static CButtonContainer s_Button1;
	if(DoButton_MenuTab(&s_Button1, Localize("Kick player"), s_ControlPage == EServerControlTab::KICKVOTE, &Button, IGraphics::CORNER_NONE))
		s_ControlPage = EServerControlTab::KICKVOTE;

	static CButtonContainer s_Button2;
	if(DoButton_MenuTab(&s_Button2, Localize("Move player to spectators"), s_ControlPage == EServerControlTab::SPECVOTE, &TabBar, IGraphics::CORNER_NONE))
		s_ControlPage = EServerControlTab::SPECVOTE;

	if(!s_ControlPageTransitionInitialized)
	{
		s_PrevControlPage = s_ControlPage;
		s_ControlPageTransitionInitialized = true;
	}
	else if(s_ControlPage != s_PrevControlPage)
	{
		s_ControlPageTransitionDirection = static_cast<int>(s_ControlPage) > static_cast<int>(s_PrevControlPage) ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(ControlPageSwitchNode, 0.18f);
		s_PrevControlPage = s_ControlPage;
	}
	const float TransitionStrength = ReadUiSwitchAnimation(ControlPageSwitchNode);
	const bool TransitionActive = TransitionStrength > 0.0f && s_ControlPageTransitionDirection != 0.0f;
	const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);

	// render page
	MainView.HSplitBottom(ms_ButtonHeight + 5 * 2, &MainView, &Bottom);
	Bottom.HMargin(5.0f, &Bottom);
	Bottom.HSplitTop(5.0f, nullptr, &Bottom);

	// render quick search
	CUIRect QuickSearch;
	Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
	Bottom.VSplitLeft(250.0f, &QuickSearch, &Bottom);
	if(m_ControlPageOpening)
	{
		m_ControlPageOpening = false;
		Ui()->SetActiveItem(&m_FilterInput);
		m_FilterInput.SelectAll();
	}
	bool Searching = Ui()->DoEditBox_Search(&m_FilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	// vote menu
	CUIRect VoteContent = MainView;
	const CUIRect VoteContentClip = MainView;
	if(TransitionActive)
	{
		Ui()->ClipEnable(&VoteContentClip);
		ApplyUiSwitchOffset(VoteContent, TransitionStrength, s_ControlPageTransitionDirection, false, 0.08f, 24.0f, 120.0f);
	}
	bool Call = false;
	if(s_ControlPage == EServerControlTab::SETTINGS)
		Call = RenderServerControlServer(VoteContent, Searching);
	else if(s_ControlPage == EServerControlTab::KICKVOTE)
		Call = RenderServerControlKick(VoteContent, false, Searching);
	else if(s_ControlPage == EServerControlTab::SPECVOTE)
		Call = RenderServerControlKick(VoteContent, true, Searching);
	if(TransitionActive)
	{
		if(TransitionAlpha > 0.0f)
			VoteContentClip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha), IGraphics::CORNER_NONE, 0.0f);
		Ui()->ClipDisable();
	}

	// call vote
	Bottom.VSplitRight(10.0f, &Bottom, nullptr);
	Bottom.VSplitRight(120.0f, &Bottom, &Button);

	static CButtonContainer s_CallVoteButton;
	if(DoButton_Menu(&s_CallVoteButton, Localize("Call vote"), 0, &Button) || Call)
	{
		if(s_ControlPage == EServerControlTab::SETTINGS)
		{
			if(0 <= m_CallvoteSelectedOption && m_CallvoteSelectedOption < GameClient()->m_Voting.NumOptions())
			{
				GameClient()->m_Voting.CallvoteOption(m_CallvoteSelectedOption, m_CallvoteReasonInput.GetString());
				if(g_Config.m_UiCloseWindowAfterChangingSetting)
					SetActive(false);
			}
		}
		else if(s_ControlPage == EServerControlTab::KICKVOTE)
		{
			if(m_CallvoteSelectedPlayer >= 0 && m_CallvoteSelectedPlayer < MAX_CLIENTS &&
				GameClient()->m_Snap.m_apPlayerInfos[m_CallvoteSelectedPlayer])
			{
				GameClient()->m_Voting.CallvoteKick(m_CallvoteSelectedPlayer, m_CallvoteReasonInput.GetString());
				SetActive(false);
			}
		}
		else if(s_ControlPage == EServerControlTab::SPECVOTE)
		{
			if(m_CallvoteSelectedPlayer >= 0 && m_CallvoteSelectedPlayer < MAX_CLIENTS &&
				GameClient()->m_Snap.m_apPlayerInfos[m_CallvoteSelectedPlayer])
			{
				GameClient()->m_Voting.CallvoteSpectate(m_CallvoteSelectedPlayer, m_CallvoteReasonInput.GetString());
				SetActive(false);
			}
		}
		m_CallvoteReasonInput.Clear();
	}

	// render kick reason
	CUIRect Reason;
	Bottom.VSplitRight(20.0f, &Bottom, nullptr);
	Bottom.VSplitRight(200.0f, &Bottom, &Reason);
	const char *pLabel = Localize("Reason:");
	Ui()->DoLabel(&Reason, pLabel, 14.0f, TEXTALIGN_ML);
	float w = TextRender()->TextWidth(14.0f, pLabel, -1, -1.0f);
	Reason.VSplitLeft(w + 10.0f, nullptr, &Reason);
	if(Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed())
	{
		Ui()->SetActiveItem(&m_CallvoteReasonInput);
		m_CallvoteReasonInput.SelectAll();
	}
	Ui()->DoEditBox(&m_CallvoteReasonInput, &Reason, 14.0f);

	// vote option loading indicator
	if(s_ControlPage == EServerControlTab::SETTINGS && GameClient()->m_Voting.IsReceivingOptions())
	{
		CUIRect Spinner, LoadingLabel;
		Bottom.VSplitLeft(20.0f, nullptr, &Bottom);
		Bottom.VSplitLeft(16.0f, &Spinner, &Bottom);
		Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
		Bottom.VSplitRight(10.0f, &LoadingLabel, nullptr);
		Ui()->RenderProgressSpinner(Spinner.Center(), 8.0f);
		Ui()->DoLabel(&LoadingLabel, Localize("Loading…"), 14.0f, TEXTALIGN_ML);
	}

	// extended features (only available when authed in rcon)
	if(Client()->RconAuthed())
	{
		// background
		RconExtension.HSplitTop(10.0f, nullptr, &RconExtension);
		RconExtension.HSplitTop(20.0f, &Bottom, &RconExtension);
		RconExtension.HSplitTop(5.0f, nullptr, &RconExtension);

		// force vote
		Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
		Bottom.VSplitLeft(120.0f, &Button, &Bottom);

		static CButtonContainer s_ForceVoteButton;
		if(DoButton_Menu(&s_ForceVoteButton, Localize("Force vote"), 0, &Button))
		{
			if(s_ControlPage == EServerControlTab::SETTINGS)
			{
				GameClient()->m_Voting.CallvoteOption(m_CallvoteSelectedOption, m_CallvoteReasonInput.GetString(), true);
			}
			else if(s_ControlPage == EServerControlTab::KICKVOTE)
			{
				if(m_CallvoteSelectedPlayer >= 0 && m_CallvoteSelectedPlayer < MAX_CLIENTS &&
					GameClient()->m_Snap.m_apPlayerInfos[m_CallvoteSelectedPlayer])
				{
					GameClient()->m_Voting.CallvoteKick(m_CallvoteSelectedPlayer, m_CallvoteReasonInput.GetString(), true);
					SetActive(false);
				}
			}
			else if(s_ControlPage == EServerControlTab::SPECVOTE)
			{
				if(m_CallvoteSelectedPlayer >= 0 && m_CallvoteSelectedPlayer < MAX_CLIENTS &&
					GameClient()->m_Snap.m_apPlayerInfos[m_CallvoteSelectedPlayer])
				{
					GameClient()->m_Voting.CallvoteSpectate(m_CallvoteSelectedPlayer, m_CallvoteReasonInput.GetString(), true);
					SetActive(false);
				}
			}
			m_CallvoteReasonInput.Clear();
		}

		if(s_ControlPage == EServerControlTab::SETTINGS)
		{
			// remove vote
			Bottom.VSplitRight(10.0f, &Bottom, nullptr);
			Bottom.VSplitRight(120.0f, nullptr, &Button);
			static CButtonContainer s_RemoveVoteButton;
			if(DoButton_Menu(&s_RemoveVoteButton, Localize("Remove"), 0, &Button))
				GameClient()->m_Voting.RemovevoteOption(m_CallvoteSelectedOption);

			// add vote
			RconExtension.HSplitTop(20.0f, &Bottom, &RconExtension);
			Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
			Bottom.VSplitLeft(250.0f, &Button, &Bottom);
			Ui()->DoLabel(&Button, Localize("Vote description:"), 14.0f, TEXTALIGN_ML);

			Bottom.VSplitLeft(20.0f, nullptr, &Button);
			Ui()->DoLabel(&Button, Localize("Vote command:"), 14.0f, TEXTALIGN_ML);

			static CLineInputBuffered<VOTE_DESC_LENGTH> s_VoteDescriptionInput;
			static CLineInputBuffered<VOTE_CMD_LENGTH> s_VoteCommandInput;
			RconExtension.HSplitTop(20.0f, &Bottom, &RconExtension);
			Bottom.VSplitRight(10.0f, &Bottom, nullptr);
			Bottom.VSplitRight(120.0f, &Bottom, &Button);
			static CButtonContainer s_AddVoteButton;
			if(DoButton_Menu(&s_AddVoteButton, Localize("Add"), 0, &Button))
				if(!s_VoteDescriptionInput.IsEmpty() && !s_VoteCommandInput.IsEmpty())
					GameClient()->m_Voting.AddvoteOption(s_VoteDescriptionInput.GetString(), s_VoteCommandInput.GetString());

			Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
			Bottom.VSplitLeft(250.0f, &Button, &Bottom);
			Ui()->DoEditBox(&s_VoteDescriptionInput, &Button, 14.0f);

			Bottom.VMargin(20.0f, &Button);
			Ui()->DoEditBox(&s_VoteCommandInput, &Button, 14.0f);
		}
	}
}

void CMenus::RenderUnfinishedMaps(CUIRect MainView)
{
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(10.0f, &MainView);

	CUIRect Row, Label, Button;
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Ui()->DoLabel(&Row, Localize("Unfinished maps"), 18.0f, TEXTALIGN_ML);

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(18.0f, &Row, &MainView);
	Ui()->DoLabel(&Row, Localize("Calculate unfinished maps for player in certain mode"), 14.0f, TEXTALIGN_ML);
	MainView.HSplitTop(18.0f, &Row, &MainView);
	Ui()->DoLabel(&Row, Localize("And pick one randomly"), 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Row.VSplitLeft(90.0f, &Label, &Row);
	Ui()->DoLabel(&Label, Localize("Player name:"), 14.0f, TEXTALIGN_ML);

	static char s_aPlayerName[16] = "";
	static CLineInput s_PlayerNameInput(s_aPlayerName, sizeof(s_aPlayerName));
	s_PlayerNameInput.SetEmptyText(Client()->PlayerName());
	static bool s_NameDirty = false;
	if(Ui()->DoEditBox(&s_PlayerNameInput, &Row, 12.0f))
		s_NameDirty = true;

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Row.VSplitLeft(90.0f, &Label, &Row);
	Ui()->DoLabel(&Label, Localize("Map type:"), 14.0f, TEXTALIGN_ML);

	const char *apTypeLabels[] = {
		Localize("Simple"),
		Localize("Intermediate"),
		Localize("Advanced"),
		Localize("Classic Easy"),
		Localize("Classic Next"),
		Localize("Classic Pro"),
		Localize("Classic Nut"),
		Localize("Traditional"),
		Localize("Solo"),
		Localize("Race"),
		Localize("Fun"),
		Localize("Event"),
		Localize("Insane"),
		Localize("Dummy"),
	};
	const char *apTypeKeys[] = {
		"Novice",
		"Moderate",
		"Brutal",
		"DDmaX Easy",
		"DDmaX Next",
		"DDmaX Pro",
		"DDmaX Nut",
		"Oldschool",
		"Solo",
		"Race",
		"Fun",
		"Event",
		"Insane",
		"Dummy",
	};
	static_assert(std::size(apTypeLabels) == std::size(apTypeKeys));
	const int NumTypes = (int)std::size(apTypeLabels);
	static int s_UnfinishedMapType = 0;
	if(s_UnfinishedMapType < 0 || s_UnfinishedMapType >= NumTypes)
		s_UnfinishedMapType = 0;

	static CUi::SDropDownState s_TypeDropDownState;
	static CScrollRegion s_TypeDropDownScrollRegion;
	s_TypeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TypeDropDownScrollRegion;
	const int NewType = Ui()->DoDropDown(&Row, s_UnfinishedMapType, apTypeLabels, NumTypes, s_TypeDropDownState);
	if(NewType != s_UnfinishedMapType)
	{
		s_UnfinishedMapType = NewType;
		GameClient()->m_Voting.ClearUnfinishedMapVoteChain();
	}
	const char *pSelectedTypeKey = apTypeKeys[s_UnfinishedMapType];
	const char *pSelectedTypeLabel = apTypeLabels[s_UnfinishedMapType];

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(20.0f, &Row, &MainView);
	static int s_UnfinishedMapAutoVote = 0;
	if(DoButton_CheckBox(&s_UnfinishedMapAutoVote, Localize("Auto start vote"), s_UnfinishedMapAutoVote, &Row))
	{
		s_UnfinishedMapAutoVote ^= 1;
		if(!s_UnfinishedMapAutoVote)
			GameClient()->m_Voting.ClearUnfinishedMapVoteChain();
	}

	static SUnfinishedMapsQuery s_UnfinishedQuery;
	s_UnfinishedQuery.Update();

	std::vector<const char *> vUnfinishedMaps;
	if(s_UnfinishedQuery.HasData())
	{
		const std::vector<std::string> *pUnfinished = s_UnfinishedQuery.FindType(pSelectedTypeKey);
		if(pUnfinished)
		{
			vUnfinishedMaps.reserve(pUnfinished->size());
			for(const std::string &MapName : *pUnfinished)
				vUnfinishedMaps.push_back(MapName.c_str());
		}
	}

	MainView.HSplitTop(18.0f, &Row, &MainView);
	char aCountBuf[128];
	if(s_UnfinishedQuery.IsLoading())
	{
		str_copy(aCountBuf, Localize("Unfinished map data refreshing"));
	}
	else if(!s_UnfinishedQuery.HasData())
	{
		str_copy(aCountBuf, Localize("Unfinished map data not fetched"));
	}
	else
	{
		str_format(aCountBuf, sizeof(aCountBuf), Localize("Unfinished map count: %d"), (int)vUnfinishedMaps.size());
	}
	Ui()->DoLabel(&Row, aCountBuf, 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Row.VSplitLeft(120.0f, &Button, &Row);
	static CButtonContainer s_PickButton;
	static char s_aPickedMap[MAX_MAP_LENGTH] = "";
	static char s_aStatusText[128] = "";
	static int s_PickedCopyId = 0;
	static float s_PickedCopyTime = 0.0f;
	static CButtonContainer s_PickedFavButton;
	if(DoButton_Menu(&s_PickButton, Localize("Random pick"), 0, &Button))
	{
		s_aStatusText[0] = '\0';
		const char *pQueryName = s_aPlayerName[0] != '\0' ? s_aPlayerName : Client()->PlayerName();
		if(!g_Config.m_BrIndicateFinished)
		{
			str_copy(s_aStatusText, Localize("Please enable finished indicator first"));
		}
		else if(s_NameDirty || !s_UnfinishedQuery.HasData())
		{
			if(!s_UnfinishedQuery.IsLoading())
				s_UnfinishedQuery.Start(Http(), pQueryName);
			s_NameDirty = false;
			str_copy(s_aStatusText, Localize("Unfinished map data is refreshing, please try again later"));
		}
		else if(vUnfinishedMaps.empty())
		{
			str_copy(s_aStatusText, Localize("No unfinished maps available"));
			s_aPickedMap[0] = '\0';
		}
		else
		{
			int PickIndex = (int)(random_float() * vUnfinishedMaps.size());
			if(PickIndex >= (int)vUnfinishedMaps.size())
				PickIndex = (int)vUnfinishedMaps.size() - 1;
			str_copy(s_aPickedMap, vUnfinishedMaps[PickIndex]);
			s_PickedCopyTime = 0.0f;

			if(s_UnfinishedMapAutoVote)
			{
				const auto Action = GameClient()->m_Voting.StartUnfinishedMapVoteChain(s_aPickedMap, pSelectedTypeKey, pSelectedTypeLabel);
				if(Action == CVoting::EUnfinishedMapVoteAction::MAP_VOTE_SENT)
					str_copy(s_aStatusText, Localize("Vote started automatically"));
				else if(Action == CVoting::EUnfinishedMapVoteAction::TYPE_VOTE_SENT)
					str_copy(s_aStatusText, Localize("Type switch vote started automatically"));
				else
					str_copy(s_aStatusText, Localize("No corresponding vote option found"));
			}
		}
	}
	if(s_aPickedMap[0] != '\0')
	{
		MainView.HSplitTop(20.0f, &Row, &MainView);
		CUIRect RowLabel, RowFav;
		Row.VSplitRight(120.0f, &RowLabel, &RowFav);

		if(Ui()->MouseInside(&RowLabel))
		{
			Ui()->SetHotItem(&s_PickedCopyId);
			if(Ui()->MouseButtonClicked(0))
			{
				Input()->SetClipboardText(s_aPickedMap);
				s_PickedCopyTime = Client()->LocalTime();
			}
		}

		if(s_PickedCopyTime > 0.0f && Client()->LocalTime() - s_PickedCopyTime < 1.5f)
		{
			TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f);
			Ui()->DoLabel(&RowLabel, Localize("Copied"), 14.0f, TEXTALIGN_ML);
		}
		else
		{
			char aResultBuf[128];
			str_format(aResultBuf, sizeof(aResultBuf), Localize("Pick result: %s"), s_aPickedMap);
			Ui()->DoLabel(&RowLabel, aResultBuf, 14.0f, TEXTALIGN_ML);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(Ui()->HotItem() == &s_PickedCopyId)
			GameClient()->m_Tooltips.DoToolTip(&s_PickedCopyId, &RowLabel, Localize("Click to copy map name"));

		const bool IsFavorite = GameClient()->m_TClient.IsFavoriteMap(s_aPickedMap);
		if(DoButton_CheckBox(&s_PickedFavButton, Localize("Favorite map"), IsFavorite, &RowFav))
		{
			if(IsFavorite)
				GameClient()->m_TClient.RemoveFavoriteMap(s_aPickedMap);
			else
				GameClient()->m_TClient.AddFavoriteMap(s_aPickedMap);
		}
	}

	if(s_aStatusText[0] != '\0')
	{
		MainView.HSplitTop(18.0f, &Row, &MainView);
		Ui()->DoLabel(&Row, s_aStatusText, 14.0f, TEXTALIGN_ML);
	}
}

void CMenus::RenderInGameNetwork(CUIRect MainView)
{
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);

	CUIRect TabBar, Button;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);

	int NewPage = g_Config.m_UiPage;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_InternetButton;
	if(DoButton_MenuTab(&s_InternetButton, FONT_ICON_EARTH_AMERICAS, g_Config.m_UiPage == PAGE_INTERNET, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_INTERNET;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_InternetButton, &Button, Localize("Internet"));

	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_LanButton;
	if(DoButton_MenuTab(&s_LanButton, FONT_ICON_NETWORK_WIRED, g_Config.m_UiPage == PAGE_LAN, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_LAN;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_LanButton, &Button, Localize("LAN"));

	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_FavoritesButton;
	if(DoButton_MenuTab(&s_FavoritesButton, FONT_ICON_STAR, g_Config.m_UiPage == PAGE_FAVORITES, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_FAVORITES;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FavoritesButton, &Button, Localize("Favorites"));

	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_FavoriteMapsButton;
	if(DoButton_MenuTab(&s_FavoriteMapsButton, "🔖", g_Config.m_UiPage == PAGE_FAVORITE_MAPS, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_FAVORITE_MAPS;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FavoriteMapsButton, &Button, Localize("收藏地图"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	size_t FavoriteCommunityIndex = 0;
	static CButtonContainer s_aFavoriteCommunityButtons[5];
	static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)PAGE_FAVORITE_COMMUNITY_5 - PAGE_FAVORITE_COMMUNITY_1 + 1);
	for(const CCommunity *pCommunity : ServerBrowser()->FavoriteCommunities())
	{
		TabBar.VSplitLeft(75.0f, &Button, &TabBar);
		const int Page = PAGE_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex;
		if(DoButton_MenuTab(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], FONT_ICON_ELLIPSIS, g_Config.m_UiPage == Page, &Button, IGraphics::CORNER_NONE, nullptr, nullptr, nullptr, nullptr, 10.0f, m_CommunityIcons.Find(pCommunity->Id())))
		{
			NewPage = Page;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], &Button, pCommunity->Name());

		++FavoriteCommunityIndex;
		if(FavoriteCommunityIndex >= std::size(s_aFavoriteCommunityButtons))
			break;
	}

	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	if(NewPage != g_Config.m_UiPage)
	{
		SetMenuPage(NewPage);
	}

	RenderServerbrowser(MainView);
}

// ghost stuff
int CMenus::GhostlistFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	CMenus *pSelf = (CMenus *)pUser;
	const char *pMap = pSelf->Client()->GetCurrentMap();
	if(IsDir || !str_endswith(pInfo->m_pName, ".gho") || !str_startswith(pInfo->m_pName, pMap))
		return 0;

	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "%s/%s", pSelf->GameClient()->m_Ghost.GetGhostDir(), pInfo->m_pName);

	CGhostInfo Info;
	if(!pSelf->GameClient()->m_Ghost.GhostLoader()->GetGhostInfo(aFilename, &Info, pMap, pSelf->Client()->GetCurrentMapSha256(), pSelf->Client()->GetCurrentMapCrc()))
		return 0;

	CGhostItem Item;
	str_copy(Item.m_aFilename, aFilename);
	str_copy(Item.m_aPlayer, Info.m_aOwner);
	Item.m_Date = pInfo->m_TimeModified;
	Item.m_Time = Info.m_Time;
	if(Item.m_Time > 0)
		pSelf->m_vGhosts.push_back(Item);

	if(time_get_nanoseconds() - pSelf->m_GhostPopulateStartTime > 500ms)
	{
		pSelf->RenderLoading(Localize("Loading ghost files"), "", 0);
	}

	return 0;
}

void CMenus::GhostlistPopulate()
{
	m_vGhosts.clear();
	m_GhostPopulateStartTime = time_get_nanoseconds();
	Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, GameClient()->m_Ghost.GetGhostDir(), GhostlistFetchCallback, this);
	SortGhostlist();

	CGhostItem *pOwnGhost = nullptr;
	for(auto &Ghost : m_vGhosts)
	{
		Ghost.m_Failed = false;
		if(str_comp(Ghost.m_aPlayer, Client()->PlayerName()) == 0 && (!pOwnGhost || Ghost < *pOwnGhost))
			pOwnGhost = &Ghost;
	}

	if(pOwnGhost)
	{
		pOwnGhost->m_Own = true;
		pOwnGhost->m_Slot = GameClient()->m_Ghost.Load(pOwnGhost->m_aFilename);
	}
}

CMenus::CGhostItem *CMenus::GetOwnGhost()
{
	for(auto &Ghost : m_vGhosts)
		if(Ghost.m_Own)
			return &Ghost;
	return nullptr;
}

void CMenus::UpdateOwnGhost(CGhostItem Item)
{
	int Own = -1;
	for(size_t i = 0; i < m_vGhosts.size(); i++)
		if(m_vGhosts[i].m_Own)
			Own = i;

	if(Own == -1)
	{
		Item.m_Own = true;
	}
	else if(g_Config.m_ClRaceGhostSaveBest && (Item.HasFile() || !m_vGhosts[Own].HasFile()))
	{
		Item.m_Own = true;
		DeleteGhostItem(Own);
	}
	else if(m_vGhosts[Own].m_Time > Item.m_Time)
	{
		Item.m_Own = true;
		m_vGhosts[Own].m_Own = false;
		m_vGhosts[Own].m_Slot = -1;
	}
	else
	{
		Item.m_Own = false;
		Item.m_Slot = -1;
	}

	Item.m_Date = std::time(nullptr);
	Item.m_Failed = false;
	m_vGhosts.insert(std::lower_bound(m_vGhosts.begin(), m_vGhosts.end(), Item), Item);
	SortGhostlist();
}

void CMenus::DeleteGhostItem(int Index)
{
	if(m_vGhosts[Index].HasFile())
		Storage()->RemoveFile(m_vGhosts[Index].m_aFilename, IStorage::TYPE_SAVE);
	m_vGhosts.erase(m_vGhosts.begin() + Index);
}

void CMenus::SortGhostlist()
{
	if(g_Config.m_GhSort == GHOST_SORT_NAME)
		std::stable_sort(m_vGhosts.begin(), m_vGhosts.end(), [](const CGhostItem &Left, const CGhostItem &Right) {
			return g_Config.m_GhSortOrder ? (str_comp(Left.m_aPlayer, Right.m_aPlayer) > 0) : (str_comp(Left.m_aPlayer, Right.m_aPlayer) < 0);
		});
	else if(g_Config.m_GhSort == GHOST_SORT_TIME)
		std::stable_sort(m_vGhosts.begin(), m_vGhosts.end(), [](const CGhostItem &Left, const CGhostItem &Right) {
			return g_Config.m_GhSortOrder ? (Left.m_Time > Right.m_Time) : (Left.m_Time < Right.m_Time);
		});
	else if(g_Config.m_GhSort == GHOST_SORT_DATE)
		std::stable_sort(m_vGhosts.begin(), m_vGhosts.end(), [](const CGhostItem &Left, const CGhostItem &Right) {
			return g_Config.m_GhSortOrder ? (Left.m_Date > Right.m_Date) : (Left.m_Date < Right.m_Date);
		});
}

void CMenus::RenderGhost(CUIRect MainView)
{
	// render background
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	MainView.VSplitRight(5.0f, &MainView, nullptr);

	CUIRect Headers, Status;
	CUIRect View = MainView;

	View.HSplitTop(17.0f, &Headers, &View);
	View.HSplitBottom(28.0f, &View, &Status);

	// split of the scrollbar
	Headers.Draw(ColorRGBA(1, 1, 1, 0.25f), IGraphics::CORNER_T, 5.0f);
	Headers.VSplitRight(20.0f, &Headers, nullptr);

	class CColumn
	{
	public:
		const char *m_pCaption;
		int m_Id;
		int m_Sort;
		float m_Width;
		CUIRect m_Rect;
	};

	enum
	{
		COL_ACTIVE = 0,
		COL_NAME,
		COL_TIME,
		COL_DATE,
	};

	static CColumn s_aCols[] = {
		{"", -1, GHOST_SORT_NONE, 2.0f, {0}},
		{"", COL_ACTIVE, GHOST_SORT_NONE, 30.0f, {0}},
		{Localizable("Name"), COL_NAME, GHOST_SORT_NAME, 200.0f, {0}},
		{Localizable("Time"), COL_TIME, GHOST_SORT_TIME, 90.0f, {0}},
		{Localizable("Date"), COL_DATE, GHOST_SORT_DATE, 150.0f, {0}},
	};

	int NumCols = std::size(s_aCols);

	// do layout
	for(int i = 0; i < NumCols; i++)
	{
		Headers.VSplitLeft(s_aCols[i].m_Width, &s_aCols[i].m_Rect, &Headers);

		if(i + 1 < NumCols)
			Headers.VSplitLeft(2, nullptr, &Headers);
	}

	// do headers
	for(const auto &Col : s_aCols)
	{
		if(DoButton_GridHeader(&Col.m_Id, Localize(Col.m_pCaption), g_Config.m_GhSort == Col.m_Sort, &Col.m_Rect))
		{
			if(Col.m_Sort != GHOST_SORT_NONE)
			{
				if(g_Config.m_GhSort == Col.m_Sort)
					g_Config.m_GhSortOrder ^= 1;
				else
					g_Config.m_GhSortOrder = 0;
				g_Config.m_GhSort = Col.m_Sort;

				SortGhostlist();
			}
		}
	}

	View.Draw(ColorRGBA(0, 0, 0, 0.15f), 0, 0);

	const int NumGhosts = m_vGhosts.size();
	int NumFailed = 0;
	int NumActivated = 0;
	static int s_SelectedIndex = 0;
	static CListBox s_ListBox;
	s_ListBox.DoStart(17.0f, NumGhosts, 1, 3, s_SelectedIndex, &View, false);

	for(int i = 0; i < NumGhosts; i++)
	{
		const CGhostItem *pGhost = &m_vGhosts[i];
		const CListboxItem Item = s_ListBox.DoNextItem(pGhost);

		if(pGhost->m_Failed)
			NumFailed++;
		if(pGhost->Active())
			NumActivated++;

		if(!Item.m_Visible)
			continue;

		ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f);
		if(pGhost->m_Own)
			Color = color_cast<ColorRGBA>(ColorHSLA(0.33f, 1.0f, 0.75f));

		if(pGhost->m_Failed)
			Color = ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f);

		TextRender()->TextColor(Color.WithAlpha(pGhost->HasFile() ? 1.0f : 0.5f));

		for(int c = 0; c < NumCols; c++)
		{
			CUIRect Button;
			Button.x = s_aCols[c].m_Rect.x;
			Button.y = Item.m_Rect.y;
			Button.h = Item.m_Rect.h;
			Button.w = s_aCols[c].m_Rect.w;

			int Id = s_aCols[c].m_Id;

			if(Id == COL_ACTIVE)
			{
				if(pGhost->Active())
				{
					Graphics()->WrapClamp();
					Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[(SPRITE_OOP + 7) - SPRITE_OOP]);
					Graphics()->QuadsBegin();
					IGraphics::CQuadItem QuadItem(Button.x + Button.w / 2, Button.y + Button.h / 2, 20.0f, 20.0f);
					Graphics()->QuadsDraw(&QuadItem, 1);

					Graphics()->QuadsEnd();
					Graphics()->WrapNormal();
				}
			}
			else if(Id == COL_NAME)
			{
				Ui()->DoLabel(&Button, pGhost->m_aPlayer, 12.0f, TEXTALIGN_ML);
			}
			else if(Id == COL_TIME)
			{
				char aBuf[64];
				str_time(pGhost->m_Time / 10, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
				Ui()->DoLabel(&Button, aBuf, 12.0f, TEXTALIGN_ML);
			}
			else if(Id == COL_DATE)
			{
				char aBuf[64];
				str_timestamp_ex(pGhost->m_Date, aBuf, sizeof(aBuf), FORMAT_SPACE);
				Ui()->DoLabel(&Button, aBuf, 12.0f, TEXTALIGN_ML);
			}
		}

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	s_SelectedIndex = s_ListBox.DoEnd();

	Status.Draw(ColorRGBA(1, 1, 1, 0.25f), IGraphics::CORNER_B, 5.0f);
	Status.Margin(5.0f, &Status);

	CUIRect Button;
	Status.VSplitLeft(25.0f, &Button, &Status);

	static CButtonContainer s_ReloadButton;
	static CButtonContainer s_DirectoryButton;
	static CButtonContainer s_ActivateAll;

	if(Ui()->DoButton_FontIcon(&s_ReloadButton, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &Button, BUTTONFLAG_LEFT) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		GameClient()->m_Ghost.UnloadAll();
		GhostlistPopulate();
	}

	Status.VSplitLeft(5.0f, &Button, &Status);
	Status.VSplitLeft(175.0f, &Button, &Status);
	if(DoButton_Menu(&s_DirectoryButton, Localize("Ghosts directory"), 0, &Button))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "ghosts", aBuf, sizeof(aBuf));
		Storage()->CreateFolder("ghosts", IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}

	Status.VSplitLeft(5.0f, &Button, &Status);
	if(NumGhosts - NumFailed > 0)
	{
		Status.VSplitLeft(175.0f, &Button, &Status);
		bool ActivateAll = ((NumGhosts - NumFailed) != NumActivated) && GameClient()->m_Ghost.FreeSlots();

		const char *pActionText = ActivateAll ? Localize("Activate all") : Localize("Deactivate all");
		if(DoButton_Menu(&s_ActivateAll, pActionText, 0, &Button))
		{
			for(int i = 0; i < NumGhosts; i++)
			{
				CGhostItem *pGhost = &m_vGhosts[i];
				if(pGhost->m_Failed || (ActivateAll && pGhost->m_Slot != -1))
					continue;

				if(ActivateAll)
				{
					if(!GameClient()->m_Ghost.FreeSlots())
						break;

					pGhost->m_Slot = GameClient()->m_Ghost.Load(pGhost->m_aFilename);
					if(pGhost->m_Slot == -1)
						pGhost->m_Failed = true;
				}
				else
				{
					GameClient()->m_Ghost.UnloadAll();
					pGhost->m_Slot = -1;
				}
			}
		}
	}

	if(s_SelectedIndex == -1 || s_SelectedIndex >= (int)m_vGhosts.size())
		return;

	CGhostItem *pGhost = &m_vGhosts[s_SelectedIndex];

	CGhostItem *pOwnGhost = GetOwnGhost();
	int ReservedSlots = !pGhost->m_Own && !(pOwnGhost && pOwnGhost->Active());
	if(!pGhost->m_Failed && pGhost->HasFile() && (pGhost->Active() || GameClient()->m_Ghost.FreeSlots() > ReservedSlots))
	{
		Status.VSplitRight(120.0f, &Status, &Button);

		static CButtonContainer s_GhostButton;
		const char *pText = pGhost->Active() ? Localize("Deactivate") : Localize("Activate");
		if(DoButton_Menu(&s_GhostButton, pText, 0, &Button) || s_ListBox.WasItemActivated())
		{
			if(pGhost->Active())
			{
				GameClient()->m_Ghost.Unload(pGhost->m_Slot);
				pGhost->m_Slot = -1;
			}
			else
			{
				pGhost->m_Slot = GameClient()->m_Ghost.Load(pGhost->m_aFilename);
				if(pGhost->m_Slot == -1)
					pGhost->m_Failed = true;
			}
		}
		Status.VSplitRight(5.0f, &Status, nullptr);
	}

	Status.VSplitRight(120.0f, &Status, &Button);

	static CButtonContainer s_DeleteButton;
	if(DoButton_Menu(&s_DeleteButton, Localize("Delete"), 0, &Button))
	{
		if(pGhost->Active())
			GameClient()->m_Ghost.Unload(pGhost->m_Slot);
		DeleteGhostItem(s_SelectedIndex);
	}

	Status.VSplitRight(5.0f, &Status, nullptr);

	bool Recording = GameClient()->m_Ghost.GhostRecorder()->IsRecording();
	if(!pGhost->HasFile() && !Recording && pGhost->Active())
	{
		static CButtonContainer s_SaveButton;
		Status.VSplitRight(120.0f, &Status, &Button);
		if(DoButton_Menu(&s_SaveButton, Localize("Save"), 0, &Button))
			GameClient()->m_Ghost.SaveGhost(pGhost);
	}
}

void CMenus::RenderIngameHint()
{
	// With touch controls enabled there is a Close button in the menu and usually no Escape key available.
	if(g_Config.m_ClTouchControls)
		return;

	float Width = 300 * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0, 0, Width, 300);
	TextRender()->TextColor(1, 1, 1, 1);
	TextRender()->Text(5, 280, 5, Localize("Menu opened. Press Esc key again to close menu."), -1.0f);
	Ui()->MapScreen();
}
