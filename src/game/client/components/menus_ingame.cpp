/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ghost.h"
#include "menus.h"
#include "motd.h"
#include "voting.h"

#include <base/color.h>
#include <base/hash_ctxt.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/console.h>
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
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/touch_controls.h>
#include <game/client/frame_scheduler.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
	constexpr const char *REPORT_SCAN_PATH = "/v1/scan";
	constexpr const char *REPORT_CONTENT_TYPE = "application/json; charset=utf-8";

	void LogIngamePerfStage(IClient *pClient, const char *pStage, const double DurationMs, const bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

	void HmacSha256Hex(const char *pSecret, const char *pMessage, char *pBuffer, int BufferSize)
	{
		const unsigned char *pSecretBytes = reinterpret_cast<const unsigned char *>(pSecret);
		size_t SecretLength = str_length(pSecret);
		unsigned char aKeyBlock[64] = {0};

		if(SecretLength > sizeof(aKeyBlock))
		{
			const SHA256_DIGEST SecretDigest = sha256(pSecretBytes, SecretLength);
			mem_copy(aKeyBlock, SecretDigest.data, sizeof(SecretDigest.data));
		}
		else
		{
			mem_copy(aKeyBlock, pSecretBytes, SecretLength);
		}

		unsigned char aOuterPad[64];
		unsigned char aInnerPad[64];
		for(size_t KeyIndex = 0; KeyIndex < sizeof(aKeyBlock); ++KeyIndex)
		{
			aOuterPad[KeyIndex] = aKeyBlock[KeyIndex] ^ 0x5c;
			aInnerPad[KeyIndex] = aKeyBlock[KeyIndex] ^ 0x36;
		}

		SHA256_CTX InnerContext;
		sha256_init(&InnerContext);
		sha256_update(&InnerContext, aInnerPad, sizeof(aInnerPad));
		sha256_update(&InnerContext, pMessage, str_length(pMessage));
		const SHA256_DIGEST InnerDigest = sha256_finish(&InnerContext);

		SHA256_CTX OuterContext;
		sha256_init(&OuterContext);
		sha256_update(&OuterContext, aOuterPad, sizeof(aOuterPad));
		sha256_update(&OuterContext, InnerDigest.data, sizeof(InnerDigest.data));
		const SHA256_DIGEST Digest = sha256_finish(&OuterContext);
		sha256_str(Digest, pBuffer, BufferSize);
	}

	void BuildReportUrl(const char *pPath, char *pBuffer, int BufferSize)
	{
		str_copy(pBuffer, g_Config.m_QmReportEndpoint, BufferSize);
		while(pBuffer[0] != '\0' && pBuffer[str_length(pBuffer) - 1] == '/')
			pBuffer[str_length(pBuffer) - 1] = '\0';
		str_append(pBuffer, pPath, BufferSize);
	}

	bool AddReportHeaders(CHttpRequest *pRequest, const char *pPath, const char *pBody)
	{
		if(g_Config.m_QmReportAppId[0] == '\0' || g_Config.m_QmReportSecret[0] == '\0')
			return false;

		char aTimestamp[32];
		str_format(aTimestamp, sizeof(aTimestamp), "%" PRId64, time_timestamp());

		char aNonce[33];
		secure_random_password(aNonce, sizeof(aNonce), 32);

		const SHA256_DIGEST BodyDigest = sha256(pBody, str_length(pBody));
		char aBodySha256[SHA256_MAXSTRSIZE];
		sha256_str(BodyDigest, aBodySha256, sizeof(aBodySha256));

		char aMessage[1024];
		str_format(aMessage, sizeof(aMessage), "POST\n%s\n%s\n%s\n%s", pPath, aTimestamp, aNonce, aBodySha256);

		char aSignature[SHA256_MAXSTRSIZE];
		HmacSha256Hex(g_Config.m_QmReportSecret, aMessage, aSignature, sizeof(aSignature));

		pRequest->HeaderString("Content-Type", REPORT_CONTENT_TYPE);
		pRequest->HeaderString("X-Adrastia-App-Id", g_Config.m_QmReportAppId);
		pRequest->HeaderString("X-Adrastia-Timestamp", aTimestamp);
		pRequest->HeaderString("X-Adrastia-Nonce", aNonce);
		pRequest->HeaderString("X-Adrastia-Signature", aSignature);
		return true;
	}

	std::shared_ptr<CHttpRequest> CreateReportRequest(const char *pPath, const char *pBody)
	{
		char aUrl[256];
		BuildReportUrl(pPath, aUrl, sizeof(aUrl));

		auto pRequest = std::make_shared<CHttpRequest>(aUrl);
		pRequest->AllowInsecureProtocol();
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{10000, 30000, 100, 10});
		if(!AddReportHeaders(pRequest.get(), pPath, pBody))
			return nullptr;
		pRequest->Post(reinterpret_cast<const unsigned char *>(pBody), str_length(pBody));
		return pRequest;
	}

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

	int ParseCallvoteMapStars(const char *pDescription)
	{
		if(!pDescription)
			return -1;

		const char *pStars = str_find(pDescription, "/5");
		if(!pStars || pStars <= pDescription)
			return -1;

		const char *pStarNumber = pStars;
		while(pStarNumber > pDescription && pStarNumber[-1] >= '0' && pStarNumber[-1] <= '9')
			--pStarNumber;
		if(pStarNumber == pStars)
			return -1;

		char aStars[8];
		const int StarNumberLength = minimum((int)(pStars - pStarNumber), (int)sizeof(aStars) - 1);
		str_copy(aStars, pStarNumber, StarNumberLength + 1);
		const int Stars = str_toint(aStars);
		if(Stars < 1 || Stars > 5)
			return -1;
		return Stars;
	}
} // namespace

void CMenus::ResetReportScan()
{
	if(m_pReportScanRequest)
		m_pReportScanRequest->Abort();
	m_pReportScanRequest.reset();
	m_ReportScanState = EReportScanState::IDLE;
	m_aReportScanAddress[0] = '\0';
}

void CMenus::StartReportScan()
{
	if(m_ReportScanState != EReportScanState::IDLE)
	{
		GameClient()->Echo(Localize("Report request is already in progress"));
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->Echo(Localize("Connect to a server first"));
		return;
	}
	if(GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity())
	{
		GameClient()->Echo(Localize("Reports are not available on Axiom servers"));
		return;
	}
	if(g_Config.m_QmReportAppId[0] == '\0' || g_Config.m_QmReportSecret[0] == '\0')
	{
		GameClient()->Echo(Localize("Configure qm_report_app_id and qm_report_secret first"));
		return;
	}

	const NETADDR *pServerAddr = Client()->ServerAddress();
	if(pServerAddr)
		net_addr_str(pServerAddr, m_aReportScanAddress, sizeof(m_aReportScanAddress), true);
	if(m_aReportScanAddress[0] == '\0')
	{
		GameClient()->Echo(Localize("Could not get current server address"));
		return;
	}

	char aEscapedAddress[NETADDR_MAXSTRSIZE * 2];
	EscapeJson(aEscapedAddress, sizeof(aEscapedAddress), m_aReportScanAddress);

	char aBody[256];
	str_format(aBody, sizeof(aBody), "{\"address\":\"%s\"}", aEscapedAddress);

	m_pReportScanRequest = CreateReportRequest(REPORT_SCAN_PATH, aBody);
	if(!m_pReportScanRequest)
	{
		ResetReportScan();
		GameClient()->Echo(Localize("Could not create report scan request"));
		return;
	}

	m_ReportScanState = EReportScanState::SCANNING;
	Http()->Run(m_pReportScanRequest);
	GameClient()->Echo(Localize("Scanning current server..."));
}

void CMenus::UpdateReportScan()
{
	if(m_ReportScanState == EReportScanState::IDLE || !m_pReportScanRequest || !m_pReportScanRequest->Done())
		return;

	const EHttpState RequestState = m_pReportScanRequest->State();
	if(RequestState != EHttpState::DONE)
	{
		ResetReportScan();
		GameClient()->Echo(RequestState == EHttpState::ABORTED ? Localize("Report request canceled") : Localize("Report request failed due to network error"));
		return;
	}

	const int StatusCode = m_pReportScanRequest->StatusCode();
	if(StatusCode < 200 || StatusCode >= 300)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("Report request failed with HTTP status: %d"), StatusCode);
		ResetReportScan();
		GameClient()->Echo(aBuf);
		return;
	}

	ResetReportScan();
	GameClient()->Echo(Localize("Report scan request submitted"));
}

void CMenus::RenderGame(CUIRect MainView)
{
	UpdateReportScan();

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
	const bool ReportDisabledOnAxiom = GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity();

	const char *pDisconnectButtonLabel = Localize("Disconnect");
	const char *pDummyButtonLabel = Localize("Connect dummy");
	const char *pDummyButtonTextId = "ingame-game-connect-dummy";
	if(Client()->DummyConnecting())
	{
		pDummyButtonLabel = Localize("Connecting dummy");
		pDummyButtonTextId = "ingame-game-connecting-dummy";
	}
	else if(Client()->DummyConnected())
	{
		pDummyButtonLabel = Localize("Disconnect dummy");
		pDummyButtonTextId = "ingame-game-disconnect-dummy";
	}
	const char *pEditHudButtonLabel = Localize("Edit HUD");
	const char *pDemoButtonLabel = Recording ? Localize("Stop record") : Localize("Record demo");
	const char *pDemoButtonTextId = Recording ? "ingame-game-stop-record" : "ingame-game-record-demo";
	char aSaveReplayButtonLabel[64];
	str_format(aSaveReplayButtonLabel, sizeof(aSaveReplayButtonLabel), Localize("Save last %d min"), g_Config.m_ClEscReplayLengthMinutes);
	const char *pDemoMarkerButtonLabel = Localize("Mark demo");
	const char *pReportButtonLabel = Localize("Report");
	const char *pSpectateButtonLabel = Localize("Spectate");
	const char *pJoinRedButtonLabel = Localize("Join red");
	const char *pJoinBlueButtonLabel = Localize("Join blue");
	const char *pJoinGameButtonLabel = Localize("Join game");
	const char *pKillButtonLabel = Localize("Kill");
	const char *pPauseButtonLabel = (!Paused && !Spec) ? Localize("Pause") : Localize("Join game");
	const char *pPauseButtonTextId = (!Paused && !Spec) ? "ingame-game-pause" : "ingame-game-join-game-pause";
	const char *pFastPracticeLabel = FastPracticeEnabled ? Localize("Stop practice") : Localize("Fast practice");
	const char *pFastPracticeTextId = FastPracticeEnabled ? "ingame-game-stop-practice" : "ingame-game-fast-practice";

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
	const float SaveReplayButtonWidthNormal = CalcMenuButtonWidth(aSaveReplayButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float SaveReplayButtonWidthCompact = CalcMenuButtonWidth(aSaveReplayButtonLabel, MenuButtonPaddingCompact, DynamicButtonMinWidth);
	const float DemoMarkerButtonWidthNormal = CalcMenuButtonWidth(pDemoMarkerButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float DemoMarkerButtonWidthCompact = CalcMenuButtonWidth(pDemoMarkerButtonLabel, MenuButtonPaddingCompact, DynamicButtonMinWidth);
	const float ReportButtonWidthNormal = CalcMenuButtonWidth(pReportButtonLabel, MenuButtonPaddingNormal, DynamicButtonMinWidth);
	const float ReportButtonWidthCompact = CalcMenuButtonWidth(pReportButtonLabel, MenuButtonPaddingCompact, DynamicButtonMinWidth);

	const bool ShowGameplayButtons = HasLocalInfo && HasGameInfo && !Paused && !Spec;
	const bool ShowSpectateButton = ShowGameplayButtons && LocalTeam != TEAM_SPECTATORS && !FastPracticeEnabled;
	const bool ShowJoinRedButton = ShowGameplayButtons && IsTeamPlay && LocalTeam != TEAM_RED;
	const bool ShowJoinBlueButton = ShowGameplayButtons && IsTeamPlay && LocalTeam != TEAM_BLUE;
	const bool ShowJoinGameButton = ShowGameplayButtons && !IsTeamPlay && LocalTeam != TEAM_GAME;
	const bool ShowKillButton = ShowGameplayButtons && LocalTeam != TEAM_SPECTATORS;
	const bool ShowPauseButton = GameClient()->m_ReceivedDDNetPlayer && HasLocalInfo && (LocalTeam != TEAM_SPECTATORS || Paused || Spec);
	const bool ShowPracticeButton = GameClient()->m_ReceivedDDNetPlayer && HasLocalInfo && LocalTeam != TEAM_SPECTATORS && !Paused && !Spec;
	const bool ShowAutoCameraButton = HasLocalInfo && (LocalTeam == TEAM_SPECTATORS || Paused || Spec);
	// 短时回放总开关（cl_replays）关闭时，隐藏 ESC 菜单的"保存回放"按钮
	const bool ShowSaveReplayButton = g_Config.m_ClReplays != 0;

	const float UtilityButtonWidthNormal =
		DisconnectButtonWidthNormal + DummyButtonWidthNormal + EditHudButtonWidthNormal + DemoButtonWidthNormal + (ShowSaveReplayButton ? SaveReplayButtonWidthNormal : 0.0f) + DemoMarkerButtonWidthNormal + ReportButtonWidthNormal + UtilityButtonSpacingNormal * (ShowSaveReplayButton ? 6.0f : 5.0f);
	const float UtilityButtonWidthCompact =
		DisconnectButtonWidthCompact + DummyButtonWidthCompact + EditHudButtonWidthCompact + DemoButtonWidthCompact + (ShowSaveReplayButton ? SaveReplayButtonWidthCompact : 0.0f) + DemoMarkerButtonWidthCompact + ReportButtonWidthCompact + UtilityButtonSpacingCompact * (ShowSaveReplayButton ? 6.0f : 5.0f);
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
	bool ShowDDRaceButtons;
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
	const int ButtonBarsCorners = g_Config.m_QmNewUi != 0 ? IGraphics::CORNER_ALL : IGraphics::CORNER_B;
	ButtonBars.Draw(ms_ColorTabbarActive, ButtonBarsCorners, 10.0f);
	ButtonBars.Margin(10.0f, &ButtonBars);
	ButtonBars.HSplitTop(MenuButtonHeight, &ButtonBar, &ButtonBars);
	if(HasSecondaryButtonBar)
	{
		ButtonBars.HSplitTop(10.0f, nullptr, &ButtonBars);
		ButtonBars.HSplitTop(MenuButtonHeight, &ButtonBar2, &ButtonBars);
	}

	CPerfTimer ButtonColumnTimer;
	char aButtonColumnPerfExtra[160];
	str_format(aButtonColumnPerfExtra, sizeof(aButtonColumnPerfExtra), "operation=ingame_esc_open context=online page=game tab=none frame=%" PRIu64, Client()->PerfFrame());
	bool ButtonColumnPerfLogged = false;
	auto LogButtonColumnPerf = [&]() {
		if(ButtonColumnPerfLogged)
			return;
		LogIngamePerfStage(Client(), "ingame_esc_button_column", ButtonColumnTimer.ElapsedMs(), false, aButtonColumnPerfExtra);
		ButtonColumnPerfLogged = true;
	};

	CUIRect UtilityButtonBar = UseSecondaryUtilityButtonBar ? ButtonBar2 : ButtonBar;
	const float UtilityButtonSpacing = UseCompactUtilityButtons ? UtilityButtonSpacingCompact : UtilityButtonSpacingNormal;
	const float DisconnectButtonWidth = UseCompactUtilityButtons ? DisconnectButtonWidthCompact : DisconnectButtonWidthNormal;
	const float DummyButtonWidth = UseCompactUtilityButtons ? DummyButtonWidthCompact : DummyButtonWidthNormal;
	const float EditHudButtonWidth = UseCompactUtilityButtons ? EditHudButtonWidthCompact : EditHudButtonWidthNormal;
	const float DemoButtonWidth = UseCompactUtilityButtons ? DemoButtonWidthCompact : DemoButtonWidthNormal;
	const float SaveReplayButtonWidth = UseCompactUtilityButtons ? SaveReplayButtonWidthCompact : SaveReplayButtonWidthNormal;
	const float DemoMarkerButtonWidth = UseCompactUtilityButtons ? DemoMarkerButtonWidthCompact : DemoMarkerButtonWidthNormal;
	const float ReportButtonWidth = UseCompactUtilityButtons ? ReportButtonWidthCompact : ReportButtonWidthNormal;

	UtilityButtonBar.VSplitRight(DisconnectButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_DisconnectButton;
	if(DoIngameMenuButton(PAGE_GAME, "ingame-game-disconnect", &s_DisconnectButton, pDisconnectButtonLabel, 0, &Button))
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
				str_append(aBuf, Localize("There are unsaved changes in the touch controls editor. Saving first is recommended."));
			}
			PopupConfirm(Localize("Disconnect"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDisconnect);
		}
		else
		{
			Client()->Disconnect();
			RefreshBrowserTab(true);
			LogButtonColumnPerf();
			return;
		}
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(DummyButtonWidth, &UtilityButtonBar, &Button);

	static CButtonContainer s_DummyButton;
	if(!Client()->DummyAllowed())
	{
		DoIngameMenuButton(PAGE_GAME, pDummyButtonTextId, &s_DummyButton, pDummyButtonLabel, 1, &Button);
		GameClient()->m_Tooltips.DoToolTip(&s_DummyButton, &Button, Localize("Dummies are not allowed on this server"));
	}
	else if(Client()->DummyConnectingDelayed())
	{
		DoIngameMenuButton(PAGE_GAME, pDummyButtonTextId, &s_DummyButton, pDummyButtonLabel, 1, &Button);
		GameClient()->m_Tooltips.DoToolTip(&s_DummyButton, &Button, Localize("Please wait…"));
	}
	else if(Client()->DummyConnecting())
	{
		DoIngameMenuButton(PAGE_GAME, pDummyButtonTextId, &s_DummyButton, pDummyButtonLabel, 1, &Button);
	}
	else if(DoIngameMenuButton(PAGE_GAME, pDummyButtonTextId, &s_DummyButton, pDummyButtonLabel, 0, &Button))
	{
		if(!Client()->DummyConnected())
		{
			GameClient()->m_QmAxiomAutoLogin.EnableDummyReconnectForServer();
			Client()->DummyConnect();
		}
		else
		{
			if(GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
			{
				PopupConfirm(Localize("Disconnect dummy"), Localize("Are you sure that you want to disconnect your dummy?"), Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDisconnectDummy);
			}
			else
			{
				GameClient()->OnDummyManualDisconnect();
				Client()->DummyDisconnect(nullptr);
				SetActive(false);
			}
		}
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(EditHudButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_EditHudButton;
	if(DoIngameMenuButton(PAGE_GAME, "ingame-game-edit-hud", &s_EditHudButton, pEditHudButtonLabel, 0, &Button))
	{
		GameClient()->m_HudEditor.SetActive(true);
		SetActive(false);
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(DemoButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_DemoButton;
	if(DoIngameMenuButton(PAGE_GAME, pDemoButtonTextId, &s_DemoButton, pDemoButtonLabel, 0, &Button))
	{
		if(!Recording)
			Client()->DemoRecorder_Start(Client()->GetCurrentMap(), true, RECORDER_MANUAL);
		else
			Client()->DemoRecorder(RECORDER_MANUAL)->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
	}

	if(ShowSaveReplayButton)
	{
		UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
		UtilityButtonBar.VSplitRight(SaveReplayButtonWidth, &UtilityButtonBar, &Button);
		static CButtonContainer s_SaveReplayButton;
		if(DoIngameMenuButton(PAGE_GAME, "ingame-game-save-replay", &s_SaveReplayButton, aSaveReplayButtonLabel, 0, &Button))
		{
			Client()->SaveReplay(g_Config.m_ClEscReplayLengthMinutes * 60);
		}
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(DemoMarkerButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_DemoMarkerButton;
	if(DoIngameMenuButton(PAGE_GAME, "ingame-game-mark-demo", &s_DemoMarkerButton, pDemoMarkerButtonLabel, 0, &Button))
	{
		const EDemoMarkerResult DemoMarkerResult = Client()->AddDemoMarker();
		if(DemoMarkerResult == EDemoMarkerResult::ADDED)
			GameClient()->Echo(Localize("Demo marker added"));
		else
			GameClient()->Echo(Localize("No demo is being recorded"));
	}

	UtilityButtonBar.VSplitRight(UtilityButtonSpacing, &UtilityButtonBar, nullptr);
	UtilityButtonBar.VSplitRight(ReportButtonWidth, &UtilityButtonBar, &Button);
	static CButtonContainer s_ReportButton;
	if(m_ReportScanState != EReportScanState::IDLE)
	{
		DoIngameMenuButton(PAGE_GAME, "ingame-game-report", &s_ReportButton, pReportButtonLabel, 1, &Button);
		GameClient()->m_Tooltips.DoToolTip(&s_ReportButton, &Button, Localize("Scanning current server"));
	}
	else if(ReportDisabledOnAxiom)
	{
		DoIngameMenuButton(PAGE_GAME, "ingame-game-report", &s_ReportButton, pReportButtonLabel, 1, &Button);
		GameClient()->m_Tooltips.DoToolTip(&s_ReportButton, &Button, Localize("Reports are not available on Axiom servers"));
	}
	else if(DoIngameMenuButton(PAGE_GAME, "ingame-game-report", &s_ReportButton, pReportButtonLabel, 0, &Button))
	{
		StartReportScan();
	}

	if(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pGameInfoObj && !Paused && !Spec)
	{
		if(ShowSpectateButton)
		{
			ButtonBar.VSplitLeft(SpectateButtonWidth, &Button, &ButtonBar);
			ButtonBar.VSplitLeft(5.0f, nullptr, &ButtonBar);
			static CButtonContainer s_SpectateButton;
			if(!Client()->DummyConnecting() && DoIngameMenuButton(PAGE_GAME, "ingame-game-spectate", &s_SpectateButton, pSpectateButtonLabel, 0, &Button))
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
				if(!Client()->DummyConnecting() && DoIngameMenuButton(PAGE_GAME, "ingame-game-join-red", &s_JoinRedButton, pJoinRedButtonLabel, 0, &Button))
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
				if(!Client()->DummyConnecting() && DoIngameMenuButton(PAGE_GAME, "ingame-game-join-blue", &s_JoinBlueButton, pJoinBlueButtonLabel, 0, &Button))
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
				if(!Client()->DummyConnecting() && DoIngameMenuButton(PAGE_GAME, "ingame-game-join-game", &s_JoinGameButton, pJoinGameButtonLabel, 0, &Button))
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
			if(DoIngameMenuButton(PAGE_GAME, "ingame-game-kill", &s_KillButton, pKillButtonLabel, 0, &Button))
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
			if(DoIngameMenuButton(PAGE_GAME, pPauseButtonTextId, &s_PauseButton, pPauseButtonLabel, 0, &Button))
			{
				Console()->ExecuteLine("say /pause", IConsole::CLIENT_ID_UNSPECIFIED);
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
				const char *pFastPracticeButtonTextId = UseCompactLabel ? "ingame-game-fast-practice-compact" : pFastPracticeTextId;
				if(DoIngameMenuButton(PAGE_GAME, pFastPracticeButtonTextId, &s_FastPracticeButton, pFastPracticeButtonLabel, FastPracticeEnabled ? 1 : 0, &Button))
				{
					SetActive(false);
					GameClient()->m_FastPractice.Toggle(true);
					LogButtonColumnPerf();
					return;
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
		if(DoIngameMenuCheckBox(PAGE_GAME, "ingame-game-edit-touch-controls", &s_TouchControlsEditCheckbox, Localize("Edit touch controls"), GameClient()->m_TouchControls.IsEditingActive(), &Button))
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
		if(DoIngameMenuButton(PAGE_GAME, "ingame-game-close", &s_CloseButton, Localize("Close"), 0, &Button))
		{
			SetActive(false);
		}

		ButtonBar2.VSplitRight(5.0f, &ButtonBar2, nullptr);
		ButtonBar2.VSplitRight(160.0f, &ButtonBar2, &Button);
		static CButtonContainer s_RemoveConsoleButton;
		if(DoIngameMenuButton(PAGE_GAME, "ingame-game-remote-console", &s_RemoveConsoleButton, Localize("Remote console"), 0, &Button))
		{
			Console()->ExecuteLine("toggle_remote_console", IConsole::CLIENT_ID_UNSPECIFIED);
		}

		ButtonBar2.VSplitRight(5.0f, &ButtonBar2, nullptr);
		ButtonBar2.VSplitRight(120.0f, &ButtonBar2, &Button);
		static CButtonContainer s_LocalConsoleButton;
		if(DoIngameMenuButton(PAGE_GAME, "ingame-game-console", &s_LocalConsoleButton, Localize("Console"), 0, &Button))
		{
			Console()->ExecuteLine("toggle_local_console", IConsole::CLIENT_ID_UNSPECIFIED);
		}
		// Only when these are all false, the preview page is rendered. Once the page is not rendered, update is needed upon next rendering.
		if(!GameClient()->m_TouchControls.IsEditingActive() || m_MenusIngameTouchControls.m_CurrentMenu != CMenusIngameTouchControls::EMenuType::MENU_BUTTONS || GameClient()->m_TouchControls.IsButtonEditing())
			m_MenusIngameTouchControls.m_NeedUpdatePreview = true;
		// Quit preview all buttons automatically.
		if(!GameClient()->m_TouchControls.IsEditingActive() || m_MenusIngameTouchControls.m_CurrentMenu != CMenusIngameTouchControls::EMenuType::MENU_PREVIEW)
			GameClient()->m_TouchControls.SetPreviewAllButtons(false);
	}
	LogButtonColumnPerf();
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
				DrawUiSwitchTransitionOverlay(MenuContentClip, ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha));
			Ui()->ClipDisable();
		}
	}
}

void CMenus::PopupConfirmDisconnect()
{
	Client()->Disconnect();
	Ui()->SetActiveItem(nullptr);
	RefreshBrowserTab(true);
}

void CMenus::PopupConfirmDisconnectDummy()
{
	GameClient()->OnDummyManualDisconnect();
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

void CMenus::PopupConfirmOpenWiki()
{
	Client()->ViewLink(Localize("https://wiki.ddnet.org/wiki/Touch_controls"));
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
	DoIngameMenuTitleLabel(PAGE_PLAYERS, "ingame-players-player-title", &Player, Localize("Player"), 24.0f, TEXTALIGN_ML);

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
		auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

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

void CMenus::RenderIngameServerInfoValueCached(const char *pTextId, unsigned &TextHash, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)
{
	const char *pSafeText = pText != nullptr ? pText : "";
	if(m_MenuTextPlanCollecting)
	{
		const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(pRect, Size, Align, LabelProps);
		CollectMenuTextPlanItem(MENU_TEXT_SCOPE_INGAME, PAGE_SERVER_INFO, -1, -1, pTextId, pSafeText, pRect, Size, Align, LabelProps, StyleKey);
		return;
	}

	const unsigned NewHash = str_quickhash(pSafeText);
	TextHash = NewHash;
	CUIElement *pReadyElement = nullptr;
	CUIElement *pLastReadyElement = nullptr;
	if(RequestSnapshotTextContainer(pTextId, pRect, pSafeText, Size, Align, LabelProps, &pReadyElement) && pReadyElement != nullptr)
		RenderSnapshotTextContainer(*pReadyElement, pRect);
	else
	{
		pLastReadyElement = m_SnapshotTextLastReadyByScope[pTextId != nullptr ? pTextId : ""];
		if(pLastReadyElement != nullptr)
			RenderSnapshotTextContainer(*pLastReadyElement, pRect);
		else
			RequestSnapshotTextContainer(pTextId, pRect, pSafeText, Size, Align, LabelProps);
	}
	(void)TextHash;
}

void CMenus::RenderSnapshotTextContainer(CUIElement &Element, const CUIRect *pRect)
{
	if(pRect == nullptr)
		return;
	const CUIElement::SUIElementRect *pElementRect = Element.Rect(0);
	if(pElementRect != nullptr && pElementRect->m_UITextContainer.Valid())
		TextRender()->RenderTextContainer(pElementRect->m_UITextContainer, pElementRect->m_TextColor, pElementRect->m_TextOutlineColor, pRect->x, pRect->y);
}

bool CMenus::RequestSnapshotTextContainer(const char *pScope, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, CUIElement **ppReadyElement)
{
	if(ppReadyElement != nullptr)
		*ppReadyElement = nullptr;
	if(pRect == nullptr || pText == nullptr)
		return false;
	SMenuSnapshotTextKey Key;
	Key.m_Scope = pScope != nullptr ? pScope : "";
	Key.m_TextHash = str_quickhash(pText);
	Key.m_Width = round_to_int(pRect->w * 10.0f);
	Key.m_FontSize = round_to_int(Size * 10.0f);
	Key.m_Align = Align;
	Key.m_LocaleHash = str_quickhash(g_Config.m_ClLanguagefile);
	Key.m_UiScaleHash = (uint64_t)round_to_int(Graphics()->ScreenHiDPIScale() * 1000.0f);
	SMenuSnapshotTextEntry &Entry = m_SnapshotTextCache[Key];
	if(Entry.m_Ready)
	{
		Entry.m_pLastReadyElement = &Entry.m_Element;
		m_SnapshotTextLastReadyByScope[Key.m_Scope] = Entry.m_pLastReadyElement;
		if(ppReadyElement != nullptr)
			*ppReadyElement = Entry.m_pLastReadyElement;
		return true;
	}
	Entry.m_Text = pText;
	Entry.m_Rect = *pRect;
	Entry.m_Size = Size;
	Entry.m_Align = Align;
	Entry.m_LabelProps = LabelProps;
	if(std::find(m_SnapshotTextPending.begin(), m_SnapshotTextPending.end(), Key) == m_SnapshotTextPending.end())
		m_SnapshotTextPending.push_back(Key);
	return false;
}

void CMenus::DrainSnapshotTextContainers()
{
	if(m_SnapshotTextPending.empty())
		return;
	int SnapshotCacheMiss = 0;
	int TextContainerNew = 0;
	while(m_IngameTextFrameBudget.m_TextContainerTokens > 0 && !m_SnapshotTextPending.empty())
	{
		const SMenuSnapshotTextKey Key = m_SnapshotTextPending.front();
		m_SnapshotTextPending.erase(m_SnapshotTextPending.begin());
		auto It = m_SnapshotTextCache.find(Key);
		if(It == m_SnapshotTextCache.end())
			continue;
		SMenuSnapshotTextEntry &Entry = It->second;
		if(!Entry.m_Element.IsRegistered())
			Entry.m_Element.Init(Ui(), 1);
		bool TextContainerRecreated = false;
		Ui()->DoLabelStreamed(*Entry.m_Element.Rect(0), &Entry.m_Rect, Entry.m_Text.c_str(), Entry.m_Size, Entry.m_Align, Entry.m_LabelProps, -1, nullptr, false, &TextContainerRecreated);
		if(TextContainerRecreated)
			++TextContainerNew;
		Entry.m_Ready = Entry.m_Element.Rect(0)->m_UITextContainer.Valid();
		if(Entry.m_Ready)
		{
			Entry.m_pLastReadyElement = &Entry.m_Element;
			m_SnapshotTextLastReadyByScope[Key.m_Scope] = Entry.m_pLastReadyElement;
		}
		++SnapshotCacheMiss;
		--m_IngameTextFrameBudget.m_TextContainerTokens;
	}
	if(QmPerfEnabled() && (SnapshotCacheMiss > 0 || TextContainerNew > 0))
	{
		char aPayload[192];
		str_format(aPayload, sizeof(aPayload), "event=text_runtime_budget page=game operation=ingame_server_info frame=%" PRIu64 " snapshot_cache_miss=%d text_container_new=%d text_container_uploads=%d text_container_create_ms=0.000 text_container_upload_ms=0.000",
			(uint64_t)Client()->PerfFrame(), SnapshotCacheMiss, TextContainerNew, TextContainerNew);
		QmPerfLogPayload("perf/text", aPayload, Client(), "game");
	}
}

void CMenus::PrepareIngameServerInfoTextRuntime(const CUIRect *pMainView)
{
	CUIRect MainView;
	if(pMainView != nullptr)
	{
		MainView = *pMainView;
	}
	else
	{
		CUIRect Screen = *Ui()->Screen();
		CUIRect TabBar;
		const float MenubarHeight = g_Config.m_QmNewUi ? 24.0f : 30.0f;
		Screen.HSplitTop(MenubarHeight, &TabBar, &MainView);
		if(g_Config.m_QmNewUi)
			MainView.HSplitTop(6.0f, nullptr, &MainView);
	}

	const float FontSizeTitle = 32.0f;
	const float FontSizeBody = 20.0f;
	const float MotdFontSize = 16.0f;
	const float ServerInfoLabelWidth = 132.0f;

	SSettingsAdaptiveBudgetInput TextBudgetInput;
	TextBudgetInput.m_FrameId = Client()->PerfFrame();
	str_copy(TextBudgetInput.m_aOperation, "ingame_server_info", sizeof(TextBudgetInput.m_aOperation));
	str_copy(TextBudgetInput.m_aPage, "game", sizeof(TextBudgetInput.m_aPage));
	str_copy(TextBudgetInput.m_aTab, "server_info", sizeof(TextBudgetInput.m_aTab));
	str_copy(TextBudgetInput.m_aContext, SettingsPerfContextName(), sizeof(TextBudgetInput.m_aContext));
	TextBudgetInput.m_FrameMsAverage = (float)GameClient()->m_QmMonitoring.Snapshot().m_Performance.m_FrameTimeMs;
	TextBudgetInput.m_FrameMsP95 = TextBudgetInput.m_FrameMsAverage;
	TextBudgetInput.m_TargetFrameMs = 8.333f;
	TextBudgetInput.m_BackgroundBacklog = maximum(1, (int)m_SnapshotTextPending.size() + 1);
	TextBudgetInput.m_VisibleWaiting = 1;
	PrepareSettingsAdaptiveBudgetInput(TextBudgetInput);
	m_IngameTextFrameBudget = GameClient()->FrameScheduler()->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, TextBudgetInput);
	LogSettingsAdaptiveBudget("ingame_server_info_snapshot_text", TextBudgetInput, m_IngameTextFrameBudget);
	m_IngameTextFrameBudget.m_TextContainerTokens = maximum(1, m_IngameTextFrameBudget.m_TextContainerTokens);

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);

	CUIRect ServerInfo, GameInfo, Motd;
	MainView.Margin(10.0f, &MainView);
	MainView.HSplitMid(&ServerInfo, &Motd, 10.0f);
	ServerInfo.VSplitMid(&ServerInfo, &GameInfo, 10.0f);
	ServerInfo.Margin(10.0f, &ServerInfo);
	GameInfo.Margin(10.0f, &GameInfo);
	Motd.Margin(10.0f, &Motd);

	CUIRect Label;
	ServerInfo.HSplitTop(FontSizeTitle, &Label, &ServerInfo);
	ServerInfo.HSplitTop(5.0f, nullptr, &ServerInfo);
	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	ServerInfo.HSplitTop(FontSizeBody, nullptr, &ServerInfo);
	RequestSnapshotTextContainer("ingame-server-info-name-value", &Label, CurrentServerInfo.m_aName, FontSizeBody, TEXTALIGN_ML, {});

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	char aBuf[256];
	auto RequestServerInfoField = [&](const char *pValueTextId, CUIRect *pRow, const char *pValueText) {
		CUIRect LabelRect, ValueRect;
		pRow->VSplitLeft(ServerInfoLabelWidth, &LabelRect, &ValueRect);
		RequestSnapshotTextContainer(pValueTextId, &ValueRect, pValueText, FontSizeBody, TEXTALIGN_ML, {});
	};
	RequestServerInfoField("ingame-server-info-address-value", &Label, CurrentServerInfo.m_aAddress);

	if(GameClient()->m_Snap.m_pLocalInfo)
	{
		ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
		str_format(aBuf, sizeof(aBuf), "%d", GameClient()->m_Snap.m_pLocalInfo->m_Latency);
		RequestServerInfoField("ingame-server-info-ping-value", &Label, aBuf);
	}

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	RequestServerInfoField("ingame-server-info-version-value", &Label, CurrentServerInfo.m_aVersion);
	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	RequestServerInfoField("ingame-server-info-password-value", &Label, CurrentServerInfo.m_Flags & SERVER_FLAG_PASSWORD ? Localize("Yes") : Localize("No"));

	GameInfo.HSplitTop(FontSizeTitle, &Label, &GameInfo);
	GameInfo.HSplitTop(5.0f, nullptr, &GameInfo);
	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	RequestServerInfoField("ingame-game-info-type-value", &Label, CurrentServerInfo.m_aGameType);
	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	RequestServerInfoField("ingame-game-info-map-value", &Label, CurrentServerInfo.m_aMap);

	const auto *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(pGameInfoObj)
	{
		if(pGameInfoObj->m_ScoreLimit)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), "%d", pGameInfoObj->m_ScoreLimit);
			RequestServerInfoField("ingame-game-info-score-limit-value", &Label, aBuf);
		}
		if(pGameInfoObj->m_TimeLimit)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), "%d min", pGameInfoObj->m_TimeLimit);
			RequestServerInfoField("ingame-game-info-time-limit-value", &Label, aBuf);
		}
		if(pGameInfoObj->m_RoundCurrent && pGameInfoObj->m_RoundNum)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), "%d/%d", pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
			RequestServerInfoField("ingame-game-info-round-value", &Label, aBuf);
		}
	}

	if(GameClient()->m_GameInfo.m_DDRaceTeam)
	{
		const char *pTeamMode = nullptr;
		switch(Config()->m_SvTeam)
		{
		case SV_TEAM_FORBIDDEN: pTeamMode = Localize("forbidden", "Team status"); break;
		case SV_TEAM_ALLOWED: pTeamMode = g_Config.m_SvSoloServer ? Localize("solo", "Team status") : Localize("allowed", "Team status"); break;
		case SV_TEAM_MANDATORY: pTeamMode = Localize("required", "Team status"); break;
		case SV_TEAM_FORCED_SOLO: pTeamMode = Localize("solo", "Team status"); break;
		default: pTeamMode = "";
		}
		GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
		RequestServerInfoField("ingame-game-info-teams-value", &Label, pTeamMode);
	}

	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	str_format(aBuf, sizeof(aBuf), "%d/%d", GameClient()->m_Snap.m_NumPlayers, CurrentServerInfo.m_MaxClients);
	RequestServerInfoField("ingame-game-info-players-value", &Label, aBuf);

	CUIRect MotdHeader;
	Motd.HSplitTop(2.0f * MotdFontSize, &MotdHeader, &Motd);
	Motd.HSplitTop(5.0f, nullptr, &Motd);
	if(GameClient()->m_Motd.ServerMotd()[0])
		RequestIngameMotdParagraphCache(Motd, MotdFontSize);

	if(QmPerfEnabled())
	{
		char aPayload[192];
		str_format(aPayload, sizeof(aPayload), "event=server_info_text_prepare frame=%" PRIu64 " pending_snapshot=%d motd_pending=%d", (uint64_t)Client()->PerfFrame(), (int)m_SnapshotTextPending.size(), m_IngameMotdParagraphCache.m_Pending ? 1 : 0);
		QmPerfLogPayload("perf/text", aPayload, Client(), "game");
	}
}

bool CMenus::RequestIngameMotdParagraphCache(CUIRect Motd, float FontSize)
{
	const char *pMotd = GameClient()->m_Motd.ServerMotd();
	const unsigned TextHash = str_quickhash(pMotd);
	const int64_t UpdateTime = GameClient()->m_Motd.ServerMotdUpdateTime();
	if(IngameMotdParagraphCacheMatches(Motd, FontSize))
		return true;

	if(!m_IngameMotdParagraphCache.m_Pending ||
		m_IngameMotdParagraphCache.m_PendingTextHash != TextHash ||
		m_IngameMotdParagraphCache.m_PendingUpdateTime != UpdateTime ||
		absolute(m_IngameMotdParagraphCache.m_PendingWidth - Motd.w) >= 0.01f ||
		absolute(m_IngameMotdParagraphCache.m_PendingFontSize - FontSize) >= 0.01f)
	{
		TextRender()->DeleteTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex);
		m_IngameMotdParagraphCache.m_BuildCursor = CTextCursor();
		m_IngameMotdParagraphCache.m_BuildByteOffset = 0;
		m_IngameMotdParagraphCache.m_BuildHeight = 0.0f;
		m_IngameMotdParagraphCache.m_PendingTextHash = TextHash;
		m_IngameMotdParagraphCache.m_PendingUpdateTime = UpdateTime;
		m_IngameMotdParagraphCache.m_PendingRect = Motd;
		m_IngameMotdParagraphCache.m_PendingWidth = Motd.w;
		m_IngameMotdParagraphCache.m_PendingFontSize = FontSize;
		m_IngameMotdParagraphCache.m_PendingFrame = Client()->PerfFrame();
	}
	m_IngameMotdParagraphCache.m_Pending = true;
	return false;
}

bool CMenus::IngameMotdParagraphCacheMatches(CUIRect Motd, float FontSize) const
{
	const char *pMotd = GameClient()->m_Motd.ServerMotd();
	const unsigned TextHash = str_quickhash(pMotd);
	const int64_t UpdateTime = GameClient()->m_Motd.ServerMotdUpdateTime();
	return m_IngameMotdParagraphCache.m_Valid &&
	       m_IngameMotdParagraphCache.m_TextHash == TextHash &&
	       m_IngameMotdParagraphCache.m_UpdateTime == UpdateTime &&
	       absolute(m_IngameMotdParagraphCache.m_Width - Motd.w) < 0.01f &&
	       absolute(m_IngameMotdParagraphCache.m_FontSize - FontSize) < 0.01f;
}

void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)
{
	const char *pMotd = GameClient()->m_Motd.ServerMotd();
	const unsigned TextHash = str_quickhash(pMotd);
	const int64_t UpdateTime = GameClient()->m_Motd.ServerMotdUpdateTime();
	const bool CacheHit = IngameMotdParagraphCacheMatches(Motd, FontSize);
	double ParagraphLayoutMs = 0.0;
	bool BudgetBlocked = false;
	bool CacheMiss = false;
	if(!CacheHit && m_IngameMotdParagraphCache.m_Pending)
	{
		CacheMiss = true;
		const uint64_t Frame = Client()->PerfFrame();
		const bool PendingMatches =
			m_IngameMotdParagraphCache.m_PendingTextHash == TextHash &&
			m_IngameMotdParagraphCache.m_PendingUpdateTime == UpdateTime &&
			absolute(m_IngameMotdParagraphCache.m_PendingWidth - Motd.w) < 0.01f &&
			absolute(m_IngameMotdParagraphCache.m_PendingFontSize - FontSize) < 0.01f;
		if(!PendingMatches)
		{
			TextRender()->DeleteTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex);
			m_IngameMotdParagraphCache.m_BuildCursor = CTextCursor();
			m_IngameMotdParagraphCache.m_BuildByteOffset = 0;
			m_IngameMotdParagraphCache.m_BuildHeight = 0.0f;
			m_IngameMotdParagraphCache.m_PendingTextHash = TextHash;
			m_IngameMotdParagraphCache.m_PendingUpdateTime = UpdateTime;
			m_IngameMotdParagraphCache.m_PendingRect = Motd;
			m_IngameMotdParagraphCache.m_PendingWidth = Motd.w;
			m_IngameMotdParagraphCache.m_PendingFontSize = FontSize;
			m_IngameMotdParagraphCache.m_PendingFrame = Frame;
			BudgetBlocked = true;
		}
		const int ParagraphLayoutTokens = m_IngameTextFrameBudget.m_ParagraphLayoutTokens;
		if((!AllowCurrentFrame && Frame <= m_IngameMotdParagraphCache.m_PendingFrame) || ParagraphLayoutTokens <= 0)
		{
			BudgetBlocked = true;
		}
		else
		{
			if(m_IngameTextFrameBudget.m_ParagraphLayoutTokens > 0)
				--m_IngameTextFrameBudget.m_ParagraphLayoutTokens;
			const auto LayoutStart = time_get_nanoseconds();
			if(m_IngameMotdParagraphCache.m_BuildByteOffset == 0 && !m_IngameMotdParagraphCache.m_BuildTextContainerIndex.Valid())
			{
				m_IngameMotdParagraphCache.m_BuildCursor = CTextCursor();
				m_IngameMotdParagraphCache.m_BuildCursor.m_FontSize = FontSize;
				m_IngameMotdParagraphCache.m_BuildCursor.m_LineWidth = Motd.w;
			}

			const int TextLength = str_length(pMotd);
			int ChunkLength = minimum(SIngameMotdParagraphCache::INGAME_MOTD_PARAGRAPH_CHUNK_BYTES, TextLength - m_IngameMotdParagraphCache.m_BuildByteOffset);
			if(m_IngameMotdParagraphCache.m_BuildByteOffset + ChunkLength < TextLength)
			{
				while(ChunkLength > 0 && !str_utf8_isstart(pMotd[m_IngameMotdParagraphCache.m_BuildByteOffset + ChunkLength]))
					--ChunkLength;
				if(ChunkLength <= 0)
					ChunkLength = str_utf8_forward(pMotd, m_IngameMotdParagraphCache.m_BuildByteOffset) - m_IngameMotdParagraphCache.m_BuildByteOffset;
			}
			std::string ChunkText(pMotd + m_IngameMotdParagraphCache.m_BuildByteOffset, ChunkLength);
			TextRender()->CreateOrAppendTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex, &m_IngameMotdParagraphCache.m_BuildCursor, ChunkText.c_str(), ChunkLength);
			m_IngameMotdParagraphCache.m_BuildByteOffset += ChunkLength;
			ParagraphLayoutMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - LayoutStart).count();
			m_IngameMotdParagraphCache.m_BuildHeight = m_IngameMotdParagraphCache.m_BuildCursor.Height();

			if(m_IngameMotdParagraphCache.m_BuildByteOffset >= TextLength)
			{
				// 文本容器不支持多 owner；构建期间继续显示旧容器，完成后再原子替换。
				TextRender()->DeleteTextContainer(m_MotdTextContainerIndex);
				std::swap(m_MotdTextContainerIndex, m_IngameMotdParagraphCache.m_BuildTextContainerIndex);
				m_IngameMotdParagraphCache.m_BuildTextContainerIndex.Reset();
				m_IngameMotdParagraphCache.m_BuildCursor = CTextCursor();
				m_IngameMotdParagraphCache.m_BuildByteOffset = 0;
				m_IngameMotdParagraphCache.m_TextHash = TextHash;
				m_IngameMotdParagraphCache.m_UpdateTime = UpdateTime;
				m_IngameMotdParagraphCache.m_Width = Motd.w;
				m_IngameMotdParagraphCache.m_FontSize = FontSize;
				m_IngameMotdParagraphCache.m_Height = m_IngameMotdParagraphCache.m_BuildHeight;
				m_IngameMotdParagraphCache.m_LastStableHeight = m_IngameMotdParagraphCache.m_Height;
				m_IngameMotdParagraphCache.m_Valid = true;
				m_IngameMotdParagraphCache.m_Pending = false;
			}
		}
	}
	if(QmPerfEnabled())
	{
		if(CacheHit || CacheMiss || BudgetBlocked || ParagraphLayoutMs >= QmPerfThresholdMs())
		{
			char aPayload[256];
			str_format(aPayload, sizeof(aPayload),
				"event=text_runtime_budget page=game operation=ingame_server_info frame=%" PRIu64 " paragraph_cache_hit=%d paragraph_cache_miss=%d paragraph_budget_blocked=%d paragraph_layout_ms=%.3f",
				(uint64_t)Client()->PerfFrame(), CacheHit ? 1 : 0, CacheMiss ? 1 : 0, BudgetBlocked ? 1 : 0, ParagraphLayoutMs);
			QmPerfLogPayload("perf/text", aPayload, Client(), "game");
		}
	}
}

bool CMenus::RenderIngameMotdStableParagraphCache(CUIRect Motd, float FontSize, CUIRect MotdTextArea)
{
	if(m_IngameMotdParagraphCache.m_Valid && m_MotdTextContainerIndex.Valid() &&
		absolute(m_IngameMotdParagraphCache.m_Width - Motd.w) < 0.01f &&
		absolute(m_IngameMotdParagraphCache.m_FontSize - FontSize) < 0.01f)
	{
		TextRender()->RenderTextContainer(m_MotdTextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), MotdTextArea.x, MotdTextArea.y);
		return true;
	}
	return false;
}

void CMenus::RenderIngameMotdFallbackText(CUIRect MotdTextArea, float FontSize)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(MotdTextArea.x, MotdTextArea.y));
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = MotdTextArea.w;
	TextRender()->TextEx(&Cursor, GameClient()->m_Motd.ServerMotd(), -1);
}

void CMenus::DrainIngameUiSnapshotTextRuntime()
{
	DrainSnapshotTextContainers();
}

void CMenus::DrainIngameUiTextRuntime(bool AllowCurrentFrame)
{
	DrainIngameUiSnapshotTextRuntime();
	if(m_IngameMotdParagraphCache.m_Pending)
	{
		DrainIngameMotdParagraphCache(m_IngameMotdParagraphCache.m_PendingRect, m_IngameMotdParagraphCache.m_PendingFontSize, AllowCurrentFrame);
	}
}

void CMenus::RenderServerInfo(CUIRect MainView)
{
	const float FontSizeTitle = 32.0f;
	const float FontSizeBody = 20.0f;
	const float ServerInfoLabelWidth = 132.0f;

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);
	SSettingsAdaptiveBudgetInput TextBudgetInput;
	TextBudgetInput.m_FrameId = Client()->PerfFrame();
	str_copy(TextBudgetInput.m_aOperation, "ingame_server_info", sizeof(TextBudgetInput.m_aOperation));
	str_copy(TextBudgetInput.m_aPage, "game", sizeof(TextBudgetInput.m_aPage));
	str_copy(TextBudgetInput.m_aTab, "server_info", sizeof(TextBudgetInput.m_aTab));
	str_copy(TextBudgetInput.m_aContext, SettingsPerfContextName(), sizeof(TextBudgetInput.m_aContext));
	TextBudgetInput.m_FrameMsAverage = (float)GameClient()->m_QmMonitoring.Snapshot().m_Performance.m_FrameTimeMs;
	TextBudgetInput.m_FrameMsP95 = TextBudgetInput.m_FrameMsAverage;
	TextBudgetInput.m_TargetFrameMs = 8.333f;
	TextBudgetInput.m_BackgroundBacklog = maximum(1, (int)m_SnapshotTextPending.size() + (m_IngameMotdParagraphCache.m_Pending ? 1 : 0));
	TextBudgetInput.m_VisibleWaiting = m_IngameMotdParagraphCache.m_Pending ? 2 : 1;
	PrepareSettingsAdaptiveBudgetInput(TextBudgetInput);
	m_IngameTextFrameBudget = GameClient()->FrameScheduler()->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, TextBudgetInput);
	LogSettingsAdaptiveBudget("ingame_server_info_snapshot_text", TextBudgetInput, m_IngameTextFrameBudget);
	m_IngameTextFrameBudget.m_TextContainerTokens = maximum(1, m_IngameTextFrameBudget.m_TextContainerTokens);

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
	DoIngameMenuTitleLabel(PAGE_SERVER_INFO, "ingame-server-info-title", &Label, Localize("Server info"), FontSizeTitle, TEXTALIGN_ML);

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	ServerInfo.HSplitTop(FontSizeBody, nullptr, &ServerInfo);
	RenderIngameServerInfoValueCached("ingame-server-info-name-value", m_IngameServerInfoTextSnapshot.m_ServerNameHash, &Label, CurrentServerInfo.m_aName, FontSizeBody, TEXTALIGN_ML);

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	char aBuf[256];
	auto DoServerInfoField = [&](const char *pLabelTextId, const char *pValueTextId, unsigned &ValueHash, CUIRect *pRow, const char *pLabelText, const char *pValueText) {
		char aLabel[128];
		str_format(aLabel, sizeof(aLabel), "%s:", pLabelText);
		CUIRect LabelRect, ValueRect;
		pRow->VSplitLeft(ServerInfoLabelWidth, &LabelRect, &ValueRect);
		DoIngameMenuLabel(PAGE_SERVER_INFO, pLabelTextId, &LabelRect, aLabel, FontSizeBody, TEXTALIGN_ML);
		RenderIngameServerInfoValueCached(pValueTextId, ValueHash, &ValueRect, pValueText, FontSizeBody, TEXTALIGN_ML);
	};
	DoServerInfoField("ingame-server-info-address-label", "ingame-server-info-address-value", m_IngameServerInfoTextSnapshot.m_AddressHash, &Label, Localize("Address"), CurrentServerInfo.m_aAddress);

	if(GameClient()->m_Snap.m_pLocalInfo)
	{
		ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
		str_format(aBuf, sizeof(aBuf), "%d", GameClient()->m_Snap.m_pLocalInfo->m_Latency);
		DoServerInfoField("ingame-server-info-ping-label", "ingame-server-info-ping-value", m_IngameServerInfoTextSnapshot.m_PingHash, &Label, Localize("Ping"), aBuf);
	}

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	DoServerInfoField("ingame-server-info-version-label", "ingame-server-info-version-value", m_IngameServerInfoTextSnapshot.m_VersionHash, &Label, Localize("Version"), CurrentServerInfo.m_aVersion);

	ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
	DoServerInfoField("ingame-server-info-password-label", "ingame-server-info-password-value", m_IngameServerInfoTextSnapshot.m_PasswordHash, &Label, Localize("Password"), CurrentServerInfo.m_Flags & SERVER_FLAG_PASSWORD ? Localize("Yes") : Localize("No"));

	const CCommunity *pCommunity = ServerBrowser()->Community(CurrentServerInfo.m_aCommunityId);
	if(pCommunity != nullptr)
	{
		ServerInfo.HSplitTop(FontSizeBody, &Label, &ServerInfo);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Community"));
		DoIngameMenuLabel(PAGE_SERVER_INFO, "ingame-server-info-community-label", &Label, aBuf, FontSizeBody, TEXTALIGN_ML);

		const CCommunityIcon *pIcon = m_CommunityIcons.Find(pCommunity->Id());
		if(pIcon != nullptr)
		{
			Label.VSplitLeft(ServerInfoLabelWidth, nullptr, &Label);
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
		if(DoIngameMenuButton(PAGE_SERVER_INFO, "ingame-server-info-copy-button", &s_CopyButton, Localize("Copy info"), 0, &Button))
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
		if(DoIngameMenuCheckBox(PAGE_SERVER_INFO, "ingame-server-info-favorite", &s_AddFavButton, Localize("Favorite"), IsFavorite != TRISTATE::NONE, &Button))
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
		if(DoIngameMenuCheckBox(PAGE_SERVER_INFO, "ingame-server-info-favorite-map", &s_AddFavMapButton, Localize("Favorite map"), IsMapFavorite, &Button))
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
	DoIngameMenuTitleLabel(PAGE_SERVER_INFO, "ingame-game-info-title", &Label, Localize("Game info"), FontSizeTitle, TEXTALIGN_ML);

	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	DoServerInfoField("ingame-game-info-type-label", "ingame-game-info-type-value", m_IngameServerInfoTextSnapshot.m_GameTypeHash, &Label, Localize("Game type"), CurrentServerInfo.m_aGameType);

	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	DoServerInfoField("ingame-game-info-map-label", "ingame-game-info-map-value", m_IngameServerInfoTextSnapshot.m_MapHash, &Label, Localize("Map"), CurrentServerInfo.m_aMap);

	const auto *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(pGameInfoObj)
	{
		if(pGameInfoObj->m_ScoreLimit)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), "%d", pGameInfoObj->m_ScoreLimit);
			DoServerInfoField("ingame-game-info-score-limit-label", "ingame-game-info-score-limit-value", m_IngameServerInfoTextSnapshot.m_ScoreLimitHash, &Label, Localize("Score limit"), aBuf);
		}

		if(pGameInfoObj->m_TimeLimit)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), "%d min", pGameInfoObj->m_TimeLimit);
			DoServerInfoField("ingame-game-info-time-limit-label", "ingame-game-info-time-limit-value", m_IngameServerInfoTextSnapshot.m_TimeLimitHash, &Label, Localize("Time limit"), aBuf);
		}

		if(pGameInfoObj->m_RoundCurrent && pGameInfoObj->m_RoundNum)
		{
			GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
			str_format(aBuf, sizeof(aBuf), "%d/%d", pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
			DoServerInfoField("ingame-game-info-round-label", "ingame-game-info-round-value", m_IngameServerInfoTextSnapshot.m_RoundHash, &Label, Localize("Round"), aBuf);
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
		if((Config()->m_SvTeam == SV_TEAM_ALLOWED || Config()->m_SvTeam == SV_TEAM_MANDATORY) && (Config()->m_SvMinTeamSize != DefaultConfig::SvMinTeamSize || Config()->m_SvMaxTeamSize != DefaultConfig::SvMaxTeamSize))
		{
			if(Config()->m_SvMinTeamSize != DefaultConfig::SvMinTeamSize && Config()->m_SvMaxTeamSize != DefaultConfig::SvMaxTeamSize)
				str_format(aBuf, sizeof(aBuf), "%s (%s %d, %s %d)", pTeamMode, Localize("minimum", "Team size"), Config()->m_SvMinTeamSize, Localize("maximum", "Team size"), Config()->m_SvMaxTeamSize);
			else if(Config()->m_SvMinTeamSize != DefaultConfig::SvMinTeamSize)
				str_format(aBuf, sizeof(aBuf), "%s (%s %d)", pTeamMode, Localize("minimum", "Team size"), Config()->m_SvMinTeamSize);
			else
				str_format(aBuf, sizeof(aBuf), "%s (%s %d)", pTeamMode, Localize("maximum", "Team size"), Config()->m_SvMaxTeamSize);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s", pTeamMode);
		}
		GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
		DoServerInfoField("ingame-game-info-teams-label", "ingame-game-info-teams-value", m_IngameServerInfoTextSnapshot.m_TeamsHash, &Label, Localize("Teams"), aBuf);
	}

	GameInfo.HSplitTop(FontSizeBody, &Label, &GameInfo);
	str_format(aBuf, sizeof(aBuf), "%d/%d", GameClient()->m_Snap.m_NumPlayers, CurrentServerInfo.m_MaxClients);
	DoServerInfoField("ingame-game-info-players-label", "ingame-game-info-players-value", m_IngameServerInfoTextSnapshot.m_PlayersHash, &Label, Localize("Players"), aBuf);

	IUiContext ServerInfoTextInputCtx;
	ServerInfoTextInputCtx.m_pUi = Ui();
	ServerInfoTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	ServerInfoTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	ServerInfoTextInputCtx.m_ScopeHash = MakeUiScopeHash("ingame_server_info_text_inputs");
	ServerInfoTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

	if(CurrentServerInfo.m_aMap[0] != '\0' && GameInfo.h >= 34.0f)
	{
		static CLineInputBuffered<256> s_MapNoteInput;
		static char s_aMapNoteInputMap[MAX_MAP_LENGTH] = "";
		if(str_comp(s_aMapNoteInputMap, CurrentServerInfo.m_aMap) != 0)
		{
			const char *pNote = GameClient()->m_TClient.GetMapNote(CurrentServerInfo.m_aMap);
			s_MapNoteInput.Set(pNote ? pNote : "");
			str_copy(s_aMapNoteInputMap, CurrentServerInfo.m_aMap);
		}

		CUIRect NoteRow, NoteLabel, NoteInput;
		GameInfo.HSplitTop(8.0f, nullptr, &GameInfo);
		GameInfo.HSplitTop(22.0f, &NoteRow, &GameInfo);
		NoteRow.VSplitLeft(72.0f, &NoteLabel, &NoteInput);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Note"));
		DoIngameMenuLabel(PAGE_SERVER_INFO, "ingame-game-info-note", &NoteLabel, aBuf, FontSizeBody, TEXTALIGN_ML);
		ui_widget::SInputFieldOptions MapNoteInputOptions;
		MapNoteInputOptions.m_pPlaceholder = Localize("Note");
		MapNoteInputOptions.m_FontSize = FontSizeBody * 0.85f;
		if(ui_widget::InputField(ServerInfoTextInputCtx, &s_MapNoteInput, NoteInput, MapNoteInputOptions).m_Changed)
			GameClient()->m_TClient.SetMapNote(CurrentServerInfo.m_aMap, s_MapNoteInput.GetString());
	}

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
	DoIngameMenuTitleLabel(PAGE_SERVER_INFO, "ingame-server-info-motd-title", &MotdHeader, Localize("MOTD"), 2.0f * MotdFontSize, TEXTALIGN_ML);

	if(m_MenuTextPlanCollecting)
		return;

	if(!GameClient()->m_Motd.ServerMotd()[0])
		return;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM);
	ScrollParams.m_ScrollUnit = 5 * MotdFontSize;
	s_ScrollRegion.Begin(&Motd, &ScrollOffset, &ScrollParams);
	Motd.y += ScrollOffset.y;

	RequestIngameMotdParagraphCache(Motd, MotdFontSize);
	const bool CacheReady = IngameMotdParagraphCacheMatches(Motd, MotdFontSize);

	CUIRect MotdTextArea;
	const float MotdTextHeight = CacheReady ? m_IngameMotdParagraphCache.m_Height : maximum(m_IngameMotdParagraphCache.m_LastStableHeight, maximum(3.0f * MotdFontSize, Motd.h));
	Motd.HSplitTop(MotdTextHeight, &MotdTextArea, &Motd);
	s_ScrollRegion.AddRect(MotdTextArea);

	if(CacheReady && m_MotdTextContainerIndex.Valid())
	{
		TextRender()->RenderTextContainer(m_MotdTextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), MotdTextArea.x, MotdTextArea.y);
	}
	else
	{
		const bool RenderedMotdParagraph = RenderIngameMotdStableParagraphCache(Motd, MotdFontSize, MotdTextArea);
		if(!RenderedMotdParagraph)
			RenderIngameMotdFallbackText(MotdTextArea, MotdFontSize);
		if(!RenderedMotdParagraph && QmPerfEnabled())
		{
			char aPayload[160];
			str_format(aPayload, sizeof(aPayload), "event=text_runtime_budget page=game operation=ingame_server_info frame=%" PRIu64 " server_info_not_ready=1", (uint64_t)Client()->PerfFrame());
			QmPerfLogPayload("perf/text", aPayload, Client(), "game");
		}
	}

	s_ScrollRegion.End();
}

bool CMenus::RenderServerControlServer(CUIRect MainView, bool UpdateScroll)
{
	CUIRect List = MainView;
	int NumVoteOptions = 0;
	int Selected = -1;
	struct SCallvoteOptionRenderEntry
	{
		const CVoteOptionClient *m_pOption;
		int m_OptionIndex;
		int m_Stars;
	};
	SCallvoteOptionRenderEntry aOptions[MAX_VOTE_OPTIONS];

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

	auto IsVisibleBySortMode = [this](int Stars) {
		switch(m_CallvoteMapSort)
		{
		case ECallvoteMapSort::ALL:
			return true;
		case ECallvoteMapSort::STAR_1:
			return Stars == 1;
		case ECallvoteMapSort::STAR_2:
			return Stars == 2;
		case ECallvoteMapSort::STAR_3:
			return Stars == 3;
		case ECallvoteMapSort::STAR_4:
			return Stars == 4;
		case ECallvoteMapSort::STAR_5:
			return Stars == 5;
		case ECallvoteMapSort::LOW_TO_HIGH:
		case ECallvoteMapSort::HIGH_TO_LOW:
			return Stars > 0;
		case ECallvoteMapSort::NUM_MODES:
			break;
		}
		return true;
	};

	int i = 0;
	for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, i++)
	{
		if(!QmTextMatchesIncludeExcludeFilter(pOption->m_aDescription, m_FilterInput.GetString(), m_ExcludeInput.GetString()))
			continue;
		const int Stars = ParseCallvoteMapStars(pOption->m_aDescription);
		if(!IsVisibleBySortMode(Stars))
			continue;

		aOptions[NumVoteOptions] = {pOption, i, Stars};
		NumVoteOptions++;
	}

	if(m_CallvoteMapSort == ECallvoteMapSort::LOW_TO_HIGH || m_CallvoteMapSort == ECallvoteMapSort::HIGH_TO_LOW)
	{
		std::stable_sort(aOptions, aOptions + NumVoteOptions, [this](const SCallvoteOptionRenderEntry &Left, const SCallvoteOptionRenderEntry &Right) {
			if(Left.m_Stars != Right.m_Stars)
			{
				if(m_CallvoteMapSort == ECallvoteMapSort::LOW_TO_HIGH)
					return Left.m_Stars < Right.m_Stars;
				return Left.m_Stars > Right.m_Stars;
			}
			return Left.m_OptionIndex < Right.m_OptionIndex;
		});
	}

	for(int OptionIndex = 0; OptionIndex < NumVoteOptions; ++OptionIndex)
	{
		if(aOptions[OptionIndex].m_OptionIndex == m_CallvoteSelectedOption)
		{
			Selected = OptionIndex;
			break;
		}
	}

	static CListBox s_ListBox;
	s_ListBox.DoStart(19.0f, NumVoteOptions, 1, 3, Selected, &List);

	CServerInfo CurrentServerInfo;
	Client()->GetServerInfo(&CurrentServerInfo);
	const CCommunity *pCurrentCommunity = ServerBrowser()->Community(CurrentServerInfo.m_aCommunityId);

	for(int OptionIndex = 0; OptionIndex < NumVoteOptions; ++OptionIndex)
	{
		const CVoteOptionClient *pOption = aOptions[OptionIndex].m_pOption;

		const CListboxItem Item = s_ListBox.DoNextItem(pOption);
		if(!Item.m_Visible)
			continue;

		CUIRect Label;
		Item.m_Rect.VMargin(2.0f, &Label);

		// 检查是否是收藏地图，用金色高亮
		char aMapName[128];
		bool IsFavorite = false;
		bool IsFinished = false;
		if(ExtractMapName(pOption->m_aDescription, aMapName, sizeof(aMapName)))
		{
			IsFavorite = GameClient()->m_TClient.IsFavoriteMap(aMapName);
			IsFinished = g_Config.m_BrIndicateFinished && pCurrentCommunity != nullptr && pCurrentCommunity->HasRank(aMapName) == CServerInfo::RANK_RANKED;
		}

		if(IsFinished)
		{
			CUIRect Icon;
			Label.VSplitLeft(Label.h, &Icon, &Label);
			Icon.Margin(2.0f, &Icon);
			RenderFontIcon(Icon, FONT_ICON_FLAG_CHECKERED, 13.0f, TEXTALIGN_MC);
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
	m_CallvoteSelectedOption = Selected != -1 ? aOptions[Selected].m_OptionIndex : -1;
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

		if(!QmTextMatchesIncludeExcludeFilter(GameClient()->m_aClients[Index].m_aName, m_FilterInput.GetString(), m_ExcludeInput.GetString()))
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
	DrawRoundedSurface(Ui(), MainView, ms_ColorTabbarActive, ms_ColorTabbarActive, 10.0f, 0.0f, IGraphics::CORNER_B);
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
	CUIRect QuickSearch, QuickExclude;
	Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
	const bool HasMapSort = s_ControlPage == EServerControlTab::SETTINGS;
	const float MapSortWidth = HasMapSort ? 140.0f : 0.0f;
	const float MapSortGap = HasMapSort ? 10.0f : 0.0f;
	const float FilterWidth = std::min(220.0f, std::max(1.0f, (Bottom.w - 5.0f - MapSortWidth - MapSortGap) * 0.5f));
	Bottom.VSplitLeft(FilterWidth, &QuickSearch, &Bottom);
	Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
	Bottom.VSplitLeft(FilterWidth, &QuickExclude, &Bottom);
	if(m_ControlPageOpening)
	{
		m_ControlPageOpening = false;
		Ui()->SetActiveItem(&m_FilterInput);
		m_FilterInput.SelectAll();
	}
	IUiContext CallvoteSearchCtx;
	CallvoteSearchCtx.m_pUi = Ui();
	CallvoteSearchCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	CallvoteSearchCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	CallvoteSearchCtx.m_ScopeHash = MakeUiScopeHash("ingame_callvote_search");
	CallvoteSearchCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SInputFieldOptions CallvoteSearchOptions;
	CallvoteSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	CallvoteSearchOptions.m_Clearable = true;
	CallvoteSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	CallvoteSearchOptions.m_FontSize = 14.0f;
	bool Searching = ui_widget::InputField(CallvoteSearchCtx, &m_FilterInput, QuickSearch, CallvoteSearchOptions).m_Changed;
	IUiContext CallvoteExcludeCtx = CallvoteSearchCtx;
	CallvoteExcludeCtx.m_ScopeHash = MakeUiScopeHash("ingame_callvote_exclude");
	ui_widget::SInputFieldOptions CallvoteExcludeOptions;
	CallvoteExcludeOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	CallvoteExcludeOptions.m_Clearable = true;
	CallvoteExcludeOptions.m_pPlaceholder = Localize("Exclude");
	CallvoteExcludeOptions.m_FontSize = 14.0f;
	Searching = ui_widget::InputField(CallvoteExcludeCtx, &m_ExcludeInput, QuickExclude, CallvoteExcludeOptions).m_Changed || Searching;
	IUiContext CallvoteTextInputCtx;
	CallvoteTextInputCtx.m_pUi = Ui();
	CallvoteTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	CallvoteTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	CallvoteTextInputCtx.m_ScopeHash = MakeUiScopeHash("ingame_callvote_text_inputs");
	CallvoteTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

	if(HasMapSort)
	{
		static CScrollRegion s_CallvoteMapSortScrollRegion;
		m_CallvoteMapSortDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_CallvoteMapSortScrollRegion;
		const char *apSortLabels[] = {
			Localize("All stars"),
			Localize("1 star"),
			Localize("2 stars"),
			Localize("3 stars"),
			Localize("4 stars"),
			Localize("5 stars"),
			Localize("Low to high"),
			Localize("High to low"),
		};
		static_assert(std::size(apSortLabels) == (int)ECallvoteMapSort::NUM_MODES);
		const int OldSort = (int)m_CallvoteMapSort;
		CUIRect SortDropDown;
		Bottom.VSplitLeft(MapSortGap, nullptr, &Bottom);
		Bottom.VSplitLeft(130.0f, &SortDropDown, &Bottom);
		const int NewSort = Ui()->DoDropDown(&SortDropDown, OldSort, apSortLabels, std::size(apSortLabels), m_CallvoteMapSortDropDownState);
		if(NewSort != OldSort && NewSort >= 0 && NewSort < (int)ECallvoteMapSort::NUM_MODES)
		{
			m_CallvoteMapSort = (ECallvoteMapSort)NewSort;
			Searching = true;
		}
	}

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
			DrawUiSwitchTransitionOverlay(VoteContentClip, ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha));
		Ui()->ClipDisable();
	}

	// call vote
	Bottom.VSplitRight(10.0f, &Bottom, nullptr);
	Bottom.VSplitRight(120.0f, &Bottom, &Button);

	static CButtonContainer s_CallVoteButton;
	if(DoIngameMenuButton(PAGE_CALLVOTE, "ingame-call-vote-call", &s_CallVoteButton, Localize("Call vote"), 0, &Button) || Call)
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
	DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-call-vote-reason-label", &Reason, pLabel, 14.0f, TEXTALIGN_ML);
	float w = TextRender()->TextWidth(14.0f, pLabel, -1, -1.0f);
	Reason.VSplitLeft(w + 10.0f, nullptr, &Reason);
	if(Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed())
	{
		Ui()->SetActiveItem(&m_CallvoteReasonInput);
		m_CallvoteReasonInput.SelectAll();
	}
	ui_widget::SInputFieldOptions ReasonInputOptions;
	ReasonInputOptions.m_pPlaceholder = Localize("Reason");
	ReasonInputOptions.m_FontSize = 14.0f;
	ui_widget::InputField(CallvoteTextInputCtx, &m_CallvoteReasonInput, Reason, ReasonInputOptions);

	// vote option loading indicator
	if(s_ControlPage == EServerControlTab::SETTINGS && GameClient()->m_Voting.IsReceivingOptions())
	{
		CUIRect Spinner, LoadingLabel;
		Bottom.VSplitLeft(20.0f, nullptr, &Bottom);
		Bottom.VSplitLeft(16.0f, &Spinner, &Bottom);
		Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
		Bottom.VSplitRight(10.0f, &LoadingLabel, nullptr);
		Ui()->RenderProgressSpinner(Spinner.Center(), 8.0f);
		DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-call-vote-loading", &LoadingLabel, Localize("Loading…"), 14.0f, TEXTALIGN_ML);
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
		if(DoIngameMenuButton(PAGE_CALLVOTE, "ingame-call-vote-force", &s_ForceVoteButton, Localize("Force vote"), 0, &Button))
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
			if(DoIngameMenuButton(PAGE_CALLVOTE, "ingame-call-vote-remove", &s_RemoveVoteButton, Localize("Remove"), 0, &Button))
				GameClient()->m_Voting.RemovevoteOption(m_CallvoteSelectedOption);

			// add vote
			RconExtension.HSplitTop(20.0f, &Bottom, &RconExtension);
			Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
			Bottom.VSplitLeft(250.0f, &Button, &Bottom);
			DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-call-vote-description-label", &Button, Localize("Vote description:"), 14.0f, TEXTALIGN_ML);

			Bottom.VSplitLeft(20.0f, nullptr, &Button);
			DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-call-vote-command-label", &Button, Localize("Vote command:"), 14.0f, TEXTALIGN_ML);

			static CLineInputBuffered<VOTE_DESC_LENGTH> s_VoteDescriptionInput;
			static CLineInputBuffered<VOTE_CMD_LENGTH> s_VoteCommandInput;
			RconExtension.HSplitTop(20.0f, &Bottom, &RconExtension);
			Bottom.VSplitRight(10.0f, &Bottom, nullptr);
			Bottom.VSplitRight(120.0f, &Bottom, &Button);
			static CButtonContainer s_AddVoteButton;
			if(DoIngameMenuButton(PAGE_CALLVOTE, "ingame-call-vote-add", &s_AddVoteButton, Localize("Add"), 0, &Button))
				if(!s_VoteDescriptionInput.IsEmpty() && !s_VoteCommandInput.IsEmpty())
					GameClient()->m_Voting.AddvoteOption(s_VoteDescriptionInput.GetString(), s_VoteCommandInput.GetString());

			Bottom.VSplitLeft(5.0f, nullptr, &Bottom);
			Bottom.VSplitLeft(250.0f, &Button, &Bottom);
			ui_widget::SInputFieldOptions VoteDescriptionInputOptions;
			VoteDescriptionInputOptions.m_pPlaceholder = Localize("Vote description");
			VoteDescriptionInputOptions.m_FontSize = 14.0f;
			ui_widget::InputField(CallvoteTextInputCtx, &s_VoteDescriptionInput, Button, VoteDescriptionInputOptions);

			Bottom.VMargin(20.0f, &Button);
			ui_widget::SInputFieldOptions VoteCommandInputOptions;
			VoteCommandInputOptions.m_pPlaceholder = Localize("Vote command");
			VoteCommandInputOptions.m_FontSize = 14.0f;
			ui_widget::InputField(CallvoteTextInputCtx, &s_VoteCommandInput, Button, VoteCommandInputOptions);
		}
	}
}

void CMenus::RenderUnfinishedMaps(CUIRect MainView)
{
	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(10.0f, &MainView);

	CUIRect Row, Label, Button;
	MainView.HSplitTop(24.0f, &Row, &MainView);
	DoIngameMenuTitleLabel(PAGE_CALLVOTE, "ingame-unfinished-maps-title", &Row, Localize("Unfinished maps"), 18.0f, TEXTALIGN_ML);

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(18.0f, &Row, &MainView);
	DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-unfinished-description-player-mode", &Row, Localize("Calculate unfinished maps for player in certain mode"), 14.0f, TEXTALIGN_ML);
	MainView.HSplitTop(18.0f, &Row, &MainView);
	DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-unfinished-description-random-pick", &Row, Localize("And pick one randomly"), 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Row.VSplitLeft(90.0f, &Label, &Row);
	DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-unfinished-player-name-label", &Label, Localize("Player name:"), 14.0f, TEXTALIGN_ML);

	static char s_aPlayerName[16] = "";
	static CLineInput s_PlayerNameInput(s_aPlayerName, sizeof(s_aPlayerName));
	s_PlayerNameInput.SetEmptyText(Client()->PlayerName());
	static bool s_NameDirty = false;
	IUiContext UnfinishedMapsTextInputCtx;
	UnfinishedMapsTextInputCtx.m_pUi = Ui();
	UnfinishedMapsTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	UnfinishedMapsTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	UnfinishedMapsTextInputCtx.m_ScopeHash = MakeUiScopeHash("ingame_unfinished_maps_text_inputs");
	UnfinishedMapsTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SInputFieldOptions PlayerNameInputOptions;
	PlayerNameInputOptions.m_pPlaceholder = Client()->PlayerName();
	PlayerNameInputOptions.m_FontSize = 12.0f;
	if(ui_widget::InputField(UnfinishedMapsTextInputCtx, &s_PlayerNameInput, Row, PlayerNameInputOptions).m_Changed)
		s_NameDirty = true;

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(24.0f, &Row, &MainView);
	Row.VSplitLeft(90.0f, &Label, &Row);
	DoIngameMenuLabel(PAGE_CALLVOTE, "ingame-unfinished-map-type-label", &Label, Localize("Map type:"), 14.0f, TEXTALIGN_ML);

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
	if(DoIngameMenuCheckBox(PAGE_CALLVOTE, "ingame-unfinished-auto-start-vote", &s_UnfinishedMapAutoVote, Localize("Auto start vote"), s_UnfinishedMapAutoVote, &Row))
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
	if(DoIngameMenuButton(PAGE_CALLVOTE, "ingame-unfinished-random-pick", &s_PickButton, Localize("Random pick"), 0, &Button))
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
		if(DoIngameMenuCheckBox(PAGE_CALLVOTE, "ingame-unfinished-favorite-map", &s_PickedFavButton, Localize("Favorite map"), IsFavorite, &RowFav))
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
	CUIRect TabBar, Button;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);

	int NewPage = g_Config.m_UiPage;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_InternetButton;
	if(DoMenuTabV2(&s_InternetButton, FONT_ICON_EARTH_AMERICAS, g_Config.m_UiPage == PAGE_INTERNET, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_INTERNET;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_InternetButton, &Button, Localize("Internet"));

	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_LanButton;
	if(DoMenuTabV2(&s_LanButton, FONT_ICON_NETWORK_WIRED, g_Config.m_UiPage == PAGE_LAN, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_LAN;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_LanButton, &Button, Localize("LAN"));

	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_FavoritesButton;
	if(DoMenuTabV2(&s_FavoritesButton, FONT_ICON_STAR, g_Config.m_UiPage == PAGE_FAVORITES, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_FAVORITES;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FavoritesButton, &Button, Localize("Favorites"));

	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TabBar.VSplitLeft(75.0f, &Button, &TabBar);
	static CButtonContainer s_FavoriteMapsButton;
	if(DoMenuTabV2(&s_FavoriteMapsButton, "", g_Config.m_UiPage == PAGE_FAVORITE_MAPS, &Button, IGraphics::CORNER_NONE))
	{
		NewPage = PAGE_FAVORITE_MAPS;
	}
	const float FavoriteMapsIconSide = minimum(Button.w, Button.h) * 0.56f;
	const CUIRect FavoriteMapsIconRect{Button.x + (Button.w - FavoriteMapsIconSide) * 0.5f, Button.y + (Button.h - FavoriteMapsIconSide) * 0.5f, FavoriteMapsIconSide, FavoriteMapsIconSide};
	const ColorRGBA FavoriteMapsIconColor = ConfiguredQmUiIconColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	if(!GameClient()->QmIconManager()->RenderIcon(EQmIcon::BOOKMARK, FavoriteMapsIconRect, FavoriteMapsIconColor))
	{
		const unsigned OldFlags = TextRender()->GetRenderFlags();
		const EFontPreset OldPreset = TextRender()->GetFontPreset();
		const ColorRGBA OldTextColor = TextRender()->GetTextColor();
		TextRender()->TextColor(FavoriteMapsIconColor);
		TextRender()->SetFontPreset(QmIconWeightUsesBoldFontFallback(g_Config.m_QmUiIconWeight) ? EFontPreset::ICON_FONT_BOLD : EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ui()->DoLabel(&FavoriteMapsIconRect, FONT_ICON_BOOKMARK, FavoriteMapsIconSide, TEXTALIGN_MC);
		TextRender()->SetRenderFlags(OldFlags);
		TextRender()->SetFontPreset(OldPreset);
		TextRender()->TextColor(OldTextColor);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FavoriteMapsButton, &Button, Localize("Favorite map"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	int MaxPage = PAGE_FAVORITES + ServerBrowser()->FavoriteCommunities().size();
	if(
		!Ui()->IsPopupOpen() &&
		CLineInput::GetActiveInput() == nullptr &&
		((g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= MaxPage) || g_Config.m_UiPage == PAGE_FAVORITE_MAPS) &&
		((m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5) || m_MenuPage == PAGE_FAVORITE_MAPS))
	{
		if(Input()->KeyPress(KEY_RIGHT))
		{
			if(g_Config.m_UiPage == PAGE_FAVORITES)
			{
				NewPage = PAGE_FAVORITE_MAPS;
			}
			else if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
			{
				NewPage = ServerBrowser()->FavoriteCommunities().empty() ? PAGE_INTERNET : PAGE_FAVORITE_COMMUNITY_1;
			}
			else
			{
				NewPage = g_Config.m_UiPage + 1;
			}
			if(NewPage > MaxPage && NewPage != PAGE_FAVORITE_MAPS)
				NewPage = PAGE_INTERNET;
		}
		if(Input()->KeyPress(KEY_LEFT))
		{
			if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
			{
				NewPage = PAGE_FAVORITES;
			}
			else if(!ServerBrowser()->FavoriteCommunities().empty() && g_Config.m_UiPage == PAGE_FAVORITE_COMMUNITY_1)
			{
				NewPage = PAGE_FAVORITE_MAPS;
			}
			else
			{
				NewPage = g_Config.m_UiPage - 1;
			}
			if(NewPage < PAGE_INTERNET)
				NewPage = ServerBrowser()->FavoriteCommunities().empty() ? PAGE_FAVORITE_MAPS : MaxPage;
		}
	}

	size_t FavoriteCommunityIndex = 0;
	static CButtonContainer s_aFavoriteCommunityButtons[5];
	static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)PAGE_FAVORITE_COMMUNITY_5 - PAGE_FAVORITE_COMMUNITY_1 + 1);
	for(const CCommunity *pCommunity : ServerBrowser()->FavoriteCommunities())
	{
		TabBar.VSplitLeft(75.0f, &Button, &TabBar);
		const int Page = PAGE_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex;
		if(DoMenuTabV2(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], FONT_ICON_ELLIPSIS, g_Config.m_UiPage == Page, &Button, IGraphics::CORNER_NONE, nullptr, nullptr, nullptr, m_CommunityIcons.Find(pCommunity->Id())))
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

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	RenderServerbrowser(MainView, false);
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
	if(!pSelf->GameClient()->m_Ghost.GhostLoader()->GetGhostInfo(aFilename, &Info, pMap, pSelf->GameClient()->Map()->Sha256(), pSelf->GameClient()->Map()->Crc()))
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
	if(DoIngameMenuButton(PAGE_GHOST, "ingame-ghost-directory", &s_DirectoryButton, Localize("Ghosts directory"), 0, &Button))
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
		if(DoIngameMenuButton(PAGE_GHOST, ActivateAll ? "ingame-ghost-activate-all" : "ingame-ghost-deactivate-all", &s_ActivateAll, pActionText, 0, &Button))
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
		if(DoIngameMenuButton(PAGE_GHOST, pGhost->Active() ? "ingame-ghost-deactivate" : "ingame-ghost-activate", &s_GhostButton, pText, 0, &Button) || s_ListBox.WasItemActivated())
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
	if(DoIngameMenuButton(PAGE_GHOST, "ingame-ghost-delete", &s_DeleteButton, Localize("Delete"), 0, &Button))
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
		if(DoIngameMenuButton(PAGE_GHOST, "ingame-ghost-save", &s_SaveButton, Localize("Save"), 0, &Button))
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
