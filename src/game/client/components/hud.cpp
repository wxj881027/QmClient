/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hud.h"

#include "binds.h"
#include "camera.h"
#include "controls.h"
#include "jump_hint_utils.h"
#include "voting.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmLayout.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/qm_icon_manager.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
	constexpr float HUD_CURRENT_WEAPON_SCALE = 1.2f;
	constexpr float MEDIA_ISLAND_SATELLITE_RING_RADIUS_SCALE = 0.75f;
	constexpr float MEDIA_ISLAND_SATELLITE_RING_THICKNESS_SCALE = 0.15f;
	constexpr float MEDIA_ISLAND_OUTER_SHADOW_PIXELS = 5.0f;
	constexpr float MEDIA_ISLAND_OUTER_SHADOW_OPACITY = 0.35f;

	bool IsVulkanBackend(IGraphics *pGraphics)
	{
		if(pGraphics != nullptr)
		{
			int Major = 0;
			int Minor = 0;
			int Patch = 0;
			const char *pBackendName = "";
			if(pGraphics->GetDetectedContextVersion(Major, Minor, Patch, pBackendName))
				return str_comp_nocase(pBackendName, "Vulkan") == 0;
		}
		return str_comp_nocase(g_Config.m_GfxBackend, "Vulkan") == 0;
	}

	bool IsOpenGlBackend()
	{
		return str_comp_nocase(g_Config.m_GfxBackend, "OpenGL") == 0;
	}

	int MediaIslandBlurTargetDimension(int ScreenDimension)
	{
		return ScreenDimension > 0 ? (ScreenDimension + 3) / 4 : 0;
	}

	struct SHudTextInfoLayout
	{
		float m_FpsX = 0.0f;
		float m_FpsY = 5.0f;
		float m_PredX = 0.0f;
		float m_PredY = 5.0f;
		float m_LossX = 0.0f;
		float m_LossY = 5.0f;
	};

	ColorRGBA GetPredictionNetworkColor(float PacketLoss, bool ConnectionProblems)
	{
		if(ConnectionProblems || PacketLoss >= 5.0f)
			return ColorRGBA(1.0f, 0.25f, 0.18f, 1.0f);
		if(PacketLoss >= 1.0f)
			return ColorRGBA(1.0f, 0.74f, 0.18f, 1.0f);
		return ColorRGBA(0.35f, 1.0f, 0.38f, 1.0f);
	}

	ColorRGBA GetPredictionMarginColor(IClient::EPredictionMarginState PredictionMarginState)
	{
		switch(PredictionMarginState)
		{
		case IClient::EPredictionMarginState::UNSTABLE:
			return ColorRGBA(1.0f, 0.25f, 0.18f, 1.0f);
		case IClient::EPredictionMarginState::IGNORED_SPIKE:
			return ColorRGBA(1.0f, 0.90f, 0.18f, 1.0f);
		case IClient::EPredictionMarginState::STABLE:
			return ColorRGBA(0.35f, 1.0f, 0.38f, 1.0f);
		}
		return ColorRGBA(0.35f, 1.0f, 0.38f, 1.0f);
	}

	SHudTextInfoLayout ComputeHudTextInfoLayoutV2(bool ShowFps, bool ShowPred, bool ShowLoss, bool UseMiniLayout, float HudWidth, float MiniX, float MiniY, float MiniW, float MiniH, float FpsWidth, float PredWidth, float LossWidth, std::vector<SUiLayoutChild> &vChildrenScratch)
	{
		SHudTextInfoLayout Result;
		if(!ShowFps && !ShowPred && !ShowLoss)
			return Result;

		CUiV2LayoutEngine LayoutEngine;
		std::vector<SUiLayoutChild> &vChildren = vChildrenScratch;
		vChildren.clear();
		vChildren.reserve(3);

		if(ShowFps)
		{
			SUiLayoutChild Child;
			Child.m_Style.m_Width = SUiLength::Px(FpsWidth);
			Child.m_Style.m_Height = SUiLength::Px(10.0f);
			vChildren.push_back(Child);
		}
		if(ShowPred)
		{
			SUiLayoutChild Child;
			Child.m_Style.m_Width = SUiLength::Px(PredWidth);
			Child.m_Style.m_Height = SUiLength::Px(10.0f);
			vChildren.push_back(Child);
		}
		if(ShowLoss)
		{
			SUiLayoutChild Child;
			Child.m_Style.m_Width = SUiLength::Px(LossWidth);
			Child.m_Style.m_Height = SUiLength::Px(10.0f);
			vChildren.push_back(Child);
		}

		SUiStyle ContainerStyle;
		SUiLayoutBox ContainerBox;
		const bool HasMultipleLines = vChildren.size() > 1;
		if(UseMiniLayout)
		{
			ContainerStyle.m_Axis = EUiAxis::ROW;
			ContainerStyle.m_Gap = HasMultipleLines ? 6.0f : 0.0f;
			ContainerStyle.m_AlignItems = EUiAlign::START;
			ContainerStyle.m_JustifyContent = EUiAlign::START;

			const float TotalWidth = FpsWidth + PredWidth + LossWidth + ContainerStyle.m_Gap * maximum(0, (int)vChildren.size() - 1);
			ContainerBox.m_X = MiniX + MiniW - TotalWidth;
			ContainerBox.m_Y = MiniY + MiniH + 4.0f;
			ContainerBox.m_W = TotalWidth;
			ContainerBox.m_H = 10.0f;
		}
		else
		{
			ContainerStyle.m_Axis = EUiAxis::COLUMN;
			ContainerStyle.m_Gap = HasMultipleLines ? 3.0f : 0.0f;
			ContainerStyle.m_AlignItems = EUiAlign::END;
			ContainerStyle.m_JustifyContent = EUiAlign::START;

			const float MaxWidth = maximum(maximum(FpsWidth, PredWidth), LossWidth);
			ContainerBox.m_X = HudWidth - 10.0f - MaxWidth;
			ContainerBox.m_Y = 5.0f;
			ContainerBox.m_W = MaxWidth;
			ContainerBox.m_H = (float)vChildren.size() * 10.0f + (float)maximum(0, (int)vChildren.size() - 1) * ContainerStyle.m_Gap;
		}

		LayoutEngine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

		size_t ChildIndex = 0;
		if(ShowFps && ChildIndex < vChildren.size())
		{
			Result.m_FpsX = vChildren[ChildIndex].m_Box.m_X;
			Result.m_FpsY = vChildren[ChildIndex].m_Box.m_Y;
			++ChildIndex;
		}
		if(ShowPred && ChildIndex < vChildren.size())
		{
			Result.m_PredX = vChildren[ChildIndex].m_Box.m_X;
			Result.m_PredY = vChildren[ChildIndex].m_Box.m_Y;
			++ChildIndex;
		}
		if(ShowLoss && ChildIndex < vChildren.size())
		{
			Result.m_LossX = vChildren[ChildIndex].m_Box.m_X;
			Result.m_LossY = vChildren[ChildIndex].m_Box.m_Y;
		}

		return Result;
	}

	uint64_t HudTextInfoNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_text_info_v2"));
		return (s_BaseKey << 32) | static_cast<uint64_t>(str_quickhash(pScope));
	}

	uint64_t HudLocalTimeNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_local_time_v2"));
		return (s_BaseKey << 32) | static_cast<uint64_t>(str_quickhash(pScope));
	}

	uint64_t HudMediaIslandNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_media_island"));
		return (s_BaseKey << 32) | static_cast<uint64_t>(str_quickhash(pScope));
	}

	uint64_t HudMediaIslandSatelliteNodeKey(EHudMediaIslandCountdownType Type, int Id)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_media_island_satellite_item"));
		return (s_BaseKey << 32) | (static_cast<uint64_t>(static_cast<int>(Type) & 0xff) << 24) | static_cast<uint64_t>(Id & 0x00ffffff);
	}

	uint64_t HudWeaponPresentationNodeKey(int ClientId, int Weapon)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_weapon_presentation"));
		return (s_BaseKey << 32) | static_cast<uint64_t>((ClientId & 0xff) << 8) | static_cast<uint64_t>(Weapon & 0xff);
	}

	uint64_t HudRecordingStatusNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_recording_status"));
		return (s_BaseKey << 32) | static_cast<uint64_t>(str_quickhash(pScope));
	}

	uint64_t HudSwitchCountdownNodeKey(int Index)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("hud_switch_countdown"));
		return (s_BaseKey << 32) | static_cast<uint64_t>(Index);
	}

	int GetDisplayedCheckpoint(CGameClient &GameClient)
	{
		if(GameClient.m_Snap.m_pGameInfoObj == nullptr)
			return 0;

		int ClientId = -1;
		if(GameClient.m_Snap.m_pLocalCharacter != nullptr && !GameClient.m_Snap.m_SpecInfo.m_Active &&
			!(GameClient.m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			ClientId = GameClient.m_Snap.m_LocalClientId;
		}
		else if(GameClient.m_Snap.m_SpecInfo.m_Active && GameClient.m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		{
			ClientId = GameClient.m_Snap.m_SpecInfo.m_SpectatorId;
		}

		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return 0;

		CCharacter *pCharacter = nullptr;
		if(ClientId == GameClient.m_Snap.m_LocalClientId && !GameClient.m_Snap.m_SpecInfo.m_Active)
			pCharacter = GameClient.m_PredictedWorld.GetCharacterById(ClientId);
		if(pCharacter == nullptr)
			pCharacter = GameClient.m_GameWorld.GetCharacterById(ClientId);
		if(pCharacter == nullptr)
			pCharacter = GameClient.m_PredictedWorld.GetCharacterById(ClientId);

		if(pCharacter != nullptr && pCharacter->m_TeleCheckpoint > 0)
			return pCharacter->m_TeleCheckpoint;

		const auto &Character = GameClient.m_Snap.m_aCharacters[ClientId];
		if(Character.m_HasExtendedData && Character.m_ExtendedData.m_TeleCheckpoint > 0)
			return Character.m_ExtendedData.m_TeleCheckpoint;

		return 0;
	}

	ColorRGBA LerpColor(const ColorRGBA &From, const ColorRGBA &To, float Amount)
	{
		Amount = std::clamp(Amount, 0.0f, 1.0f);
		return ColorRGBA(
			mix(From.r, To.r, Amount),
			mix(From.g, To.g, Amount),
			mix(From.b, To.b, Amount),
			mix(From.a, To.a, Amount));
	}

	struct SSwapCountdownInfo
	{
		ColorRGBA m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		char m_aText[128] = {0};
		char m_aRequestText[128] = {0};
		int m_Dummy = 0;
		int m_InstanceId = 0;
		int m_StartTick = 0;
		bool m_Outgoing = false;
		SHudMediaIslandSwapLifecycle m_Lifecycle{};
	};

	struct SSwapCountdownList
	{
		std::array<SSwapCountdownInfo, NUM_DUMMIES *(MAX_CLIENTS + 1)> m_aInfos{};
		int m_Count = 0;
	};

	struct SHudFrozenTeamInfo
	{
		bool m_Available = false;
		int m_NumInTeam = 0;
		int m_NumFrozen = 0;
		int m_LocalTeamId = 0;
	};

	struct SHudFrozenHudRect
	{
		bool m_Visible = false;
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	struct SHudGameTimerInfo
	{
		bool m_Visible = false;
		bool m_IsCritical = false;
		float m_Alpha = 1.0f;
		float m_FontSize = 10.0f;
		float m_X = 0.0f;
		float m_Y = 2.0f;
		float m_W = 0.0f;
		float m_Left = 0.0f;
		char m_aText[32] = {};
	};

	struct SHudTopTimerCapsuleInfo
	{
		bool m_Visible = false;
		bool m_IsCritical = false;
		float m_Alpha = 1.0f;
		float m_FontSize = 10.0f;
		float m_BoxX = 0.0f;
		float m_BoxY = 1.0f;
		float m_BoxW = 0.0f;
		float m_BoxH = 16.0f;
		float m_TextX = 0.0f;
		float m_TextY = 0.0f;
		char m_aText[32] = {};
	};

	SHudFrozenTeamInfo BuildHudFrozenTeamInfo(const CGameClient &GameClient)
	{
		SHudFrozenTeamInfo Result;
		if(!GameClient.m_GameInfo.m_EntitiesDDRace)
			return Result;

		Result.m_Available = true;
		if(GameClient.m_Snap.m_LocalClientId >= 0 && GameClient.m_Snap.m_SpecInfo.m_SpectatorId >= 0)
		{
			if(GameClient.m_Snap.m_SpecInfo.m_Active == 1 && GameClient.m_Snap.m_SpecInfo.m_SpectatorId != -1)
				Result.m_LocalTeamId = GameClient.m_Teams.Team(GameClient.m_Snap.m_SpecInfo.m_SpectatorId);
			else
				Result.m_LocalTeamId = GameClient.m_Teams.Team(GameClient.m_Snap.m_LocalClientId);
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameClient.m_Snap.m_apPlayerInfos[i])
				continue;

			if(GameClient.m_Teams.Team(i) == Result.m_LocalTeamId)
			{
				Result.m_NumInTeam++;
				if(QmHudTeeIsFrozen(
					   GameClient.m_aClients[i].m_HudFrozenTeeState,
					   GameClient.m_aClients[i].m_FreezeEnd,
					   GameClient.m_aClients[i].m_DeepFrozen))
					Result.m_NumFrozen++;
			}
		}

		return Result;
	}

	bool BuildHudFrozenSummaryText(const SHudFrozenTeamInfo &FrozenInfo, char *pBuf, size_t BufSize)
	{
		pBuf[0] = '\0';
		if(!FrozenInfo.m_Available || g_Config.m_TcShowFrozenText <= 0)
			return false;

		if(g_Config.m_TcShowFrozenText == 1)
			str_format(pBuf, BufSize, "%d/%d", FrozenInfo.m_NumInTeam - FrozenInfo.m_NumFrozen, FrozenInfo.m_NumInTeam);
		else if(g_Config.m_TcShowFrozenText == 2)
			str_format(pBuf, BufSize, "%d/%d", FrozenInfo.m_NumFrozen, FrozenInfo.m_NumInTeam);
		else
			return false;

		return true;
	}

	bool BuildHudTeamText(const CGameClient &GameClient, char *pBuf, size_t BufSize)
	{
		pBuf[0] = '\0';
		int ClientId = GameClient.m_Snap.m_LocalClientId;
		if(GameClient.m_Snap.m_SpecInfo.m_Active)
		{
			if(GameClient.m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
				return false;
			ClientId = GameClient.m_Snap.m_SpecInfo.m_SpectatorId;
		}
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || GameClient.m_Snap.m_apPlayerInfos[ClientId] == nullptr)
			return false;

		const int Team = GameClient.m_Teams.Team(ClientId);
		if(!QmHudMediaIslandShouldShowTeam(g_Config.m_QmHudIslandShowTeam, GameClient.m_GameInfo.m_EntitiesDDRace, Team))
			return false;

		str_format(pBuf, BufSize, Localize("Team %d"), Team);
		return pBuf[0] != '\0';
	}

	SHudFrozenHudRect BuildFrozenHudRect(const CGameClient &GameClient, float HudWidth, float HudHeight, float TopIslandAvoidanceRight)
	{
		SHudFrozenHudRect Result;
		const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(GameClient);
		if(!g_Config.m_TcShowFrozenHud || !FrozenInfo.m_Available || FrozenInfo.m_NumInTeam <= 0 || GameClient.m_Scoreboard.IsActive() || (FrozenInfo.m_LocalTeamId == 0 && g_Config.m_TcFrozenHudTeamOnly))
			return Result;

		const float TeeSize = g_Config.m_TcFrozenHudTeeSize;
		int MaxTees = (int)(8.3f * (HudWidth / HudHeight) * 13.0f / TeeSize);
		if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred && !g_Config.m_ClShowPacketLoss)
			MaxTees = (int)(9.5f * (HudWidth / HudHeight) * 13.0f / TeeSize);
		const int MaxRows = g_Config.m_TcFrozenMaxRows;
		float StartPos = HudWidth / 2.0f + 38.0f * (HudWidth / HudHeight) / 1.78f;
		if(TopIslandAvoidanceRight > 0.0f)
			StartPos = std::max(StartPos, TopIslandAvoidanceRight + TeeSize * 0.5f + 4.0f);

		const float RowLeft = StartPos - TeeSize * 0.5f;
		const float AvailableRowWidth = std::max(TeeSize, HudWidth - RowLeft);
		MaxTees = std::max(1, std::min(MaxTees, (int)std::floor(AvailableRowWidth / TeeSize)));

		const int TotalRows = std::min(MaxRows, (FrozenInfo.m_NumInTeam + MaxTees - 1) / MaxTees);
		Result.m_Visible = TotalRows > 0;
		Result.m_X = RowLeft;
		Result.m_Y = 0.0f;
		Result.m_W = TeeSize * std::min(FrozenInfo.m_NumInTeam, MaxTees);
		Result.m_H = TeeSize + 3.0f + (TotalRows - 1) * TeeSize;
		return Result;
	}

	SHudGameTimerInfo BuildHudGameTimerInfo(const CGameClient &GameClient, const IClient &Client, ITextRender *pTextRender, float HudWidth)
	{
		SHudGameTimerInfo Result;
		const CNetObj_GameInfo *pGameInfoObj = GameClient.m_Snap.m_pGameInfoObj;
		if(pGameInfoObj == nullptr || (pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH) != 0)
			return Result;

		int Time = 0;
		if(pGameInfoObj->m_TimeLimit && pGameInfoObj->m_WarmupTimer <= 0)
		{
			Time = pGameInfoObj->m_TimeLimit * 60 - ((Client.GameTick(g_Config.m_ClDummy) - pGameInfoObj->m_RoundStartTick) / Client.GameTickSpeed());

			if(pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
				Time = 0;
		}
		else if(pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
		{
			// The Warmup timer is negative in this case to make sure that incompatible clients will not see a warmup timer
			Time = (Client.GameTick(g_Config.m_ClDummy) + pGameInfoObj->m_WarmupTimer) / Client.GameTickSpeed();
		}
		else
		{
			Time = (Client.GameTick(g_Config.m_ClDummy) - pGameInfoObj->m_RoundStartTick) / Client.GameTickSpeed();
		}

		str_time((int64_t)Time * 100, TIME_DAYS, Result.m_aText, sizeof(Result.m_aText));

		static float s_TextWidthM = pTextRender->TextWidth(Result.m_FontSize, "00:00", -1, -1.0f);
		static float s_TextWidthH = pTextRender->TextWidth(Result.m_FontSize, "00:00:00", -1, -1.0f);
		static float s_TextWidth0D = pTextRender->TextWidth(Result.m_FontSize, "0d 00:00:00", -1, -1.0f);
		static float s_TextWidth00D = pTextRender->TextWidth(Result.m_FontSize, "00d 00:00:00", -1, -1.0f);
		static float s_TextWidth000D = pTextRender->TextWidth(Result.m_FontSize, "000d 00:00:00", -1, -1.0f);

		Result.m_W = Time >= 3600 * 24 * 100 ? s_TextWidth000D :
						       (Time >= 3600 * 24 * 10 ? s_TextWidth00D :
										 (Time >= 3600 * 24 ? s_TextWidth0D :
												      (Time >= 3600 ? s_TextWidthH : s_TextWidthM)));

		Result.m_X = HudWidth * 0.5f - Result.m_W * 0.5f;
		Result.m_Left = Result.m_X;
		Result.m_IsCritical = pGameInfoObj->m_TimeLimit && Time <= 60 && pGameInfoObj->m_WarmupTimer <= 0;
		Result.m_Alpha = Time <= 10 && (2 * time_get() / time_freq()) % 2 ? 0.5f : 1.0f;
		Result.m_Visible = true;
		return Result;
	}

	SHudTopTimerCapsuleInfo BuildHudTopTimerCapsuleInfo(const SHudGameTimerInfo &TimerInfo)
	{
		SHudTopTimerCapsuleInfo Result;
		if(!TimerInfo.m_Visible)
			return Result;

		constexpr float BoxY = 1.0f;
		constexpr float BoxH = QmHudMediaIslandScaled(16.0f);
		constexpr float PaddingX = QmHudMediaIslandScaled(4.0f);
		const float TimerTextWidth = TimerInfo.m_W * QmHudMediaIslandDesignScale;

		Result.m_Visible = true;
		Result.m_IsCritical = TimerInfo.m_IsCritical;
		Result.m_Alpha = TimerInfo.m_Alpha;
		Result.m_FontSize = QmHudMediaIslandScaled(TimerInfo.m_FontSize);
		Result.m_BoxY = BoxY;
		Result.m_BoxH = BoxH;
		Result.m_BoxW = std::round(TimerTextWidth + PaddingX * 2.0f);
		Result.m_BoxX = std::round(TimerInfo.m_X + TimerInfo.m_W * 0.5f - Result.m_BoxW * 0.5f);
		Result.m_TextX = std::round(Result.m_BoxX + (Result.m_BoxW - TimerTextWidth) * 0.5f);
		Result.m_TextY = std::round(BoxY + (BoxH - Result.m_FontSize) * 0.5f - QmHudMediaIslandScaled(0.5f));
		str_copy(Result.m_aText, TimerInfo.m_aText, sizeof(Result.m_aText));
		return Result;
	}

	bool BuildHudRecordingStatusText(const CGameClient &GameClient, char *pBuf, size_t BufSize)
	{
		pBuf[0] = '\0';

		const auto &&AppendRecorderInfo = [&](int Recorder, const char *pName) {
			if(GameClient.DemoRecorder(Recorder)->IsRecording())
			{
				char aTime[32];
				str_time((int64_t)GameClient.DemoRecorder(Recorder)->Length() * 100, TIME_HOURS, aTime, sizeof(aTime));
				if(pBuf[0] != '\0')
					str_append(pBuf, "  ", BufSize);
				str_append(pBuf, pName, BufSize);
				str_append(pBuf, " ", BufSize);
				str_append(pBuf, aTime, BufSize);
			}
		};

		AppendRecorderInfo(RECORDER_MANUAL, Localize("Manual"));
		AppendRecorderInfo(RECORDER_RACE, Localize("Race"));
		AppendRecorderInfo(RECORDER_AUTO, Localize("Auto"));
		AppendRecorderInfo(RECORDER_REPLAYS, Localize("Replay"));
		return pBuf[0] != '\0';
	}

	bool ShouldRenderHudLocalTime(const CGameClient &GameClient)
	{
		return g_Config.m_ClShowLocalTimeAlways || GameClient.m_Scoreboard.IsActive() || GameClient.m_HudEditor.IsActive();
	}

	void DrawSmoothRoundedRect(IGraphics *pGraphics, float x, float y, float w, float h, float r, ColorRGBA Color, int Corners = IGraphics::CORNER_ALL)
	{
		if(pGraphics == nullptr || w <= 0.0f || h <= 0.0f)
			return;

		r = std::clamp(r, 0.0f, std::min(w, h) * 0.5f);

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);

		if(Corners == 0 || r <= 0.0f)
		{
			IGraphics::CQuadItem QuadItem(x, y, w, h);
			pGraphics->QuadsDrawTL(&QuadItem, 1);
			pGraphics->QuadsEnd();
			return;
		}

		constexpr int NumSegments = 20;
		constexpr float Pi = 3.14159265359f;
		const float SegmentAngle = Pi / 2.0f / NumSegments;
		std::array<IGraphics::CFreeformItem, NumSegments * 2> aFreeform;
		int NumFreeformItems = 0;

		for(int i = 0; i < NumSegments; i += 2)
		{
			const float A1 = i * SegmentAngle;
			const float A2 = (i + 1) * SegmentAngle;
			const float A3 = (i + 2) * SegmentAngle;
			const float Ca1 = std::cos(A1);
			const float Ca2 = std::cos(A2);
			const float Ca3 = std::cos(A3);
			const float Sa1 = std::sin(A1);
			const float Sa2 = std::sin(A2);
			const float Sa3 = std::sin(A3);

			if(Corners & IGraphics::CORNER_TL)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + r, y + r,
					x + (1.0f - Ca1) * r, y + (1.0f - Sa1) * r,
					x + (1.0f - Ca3) * r, y + (1.0f - Sa3) * r,
					x + (1.0f - Ca2) * r, y + (1.0f - Sa2) * r);

			if(Corners & IGraphics::CORNER_TR)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + w - r, y + r,
					x + w - r + Ca1 * r, y + (1.0f - Sa1) * r,
					x + w - r + Ca3 * r, y + (1.0f - Sa3) * r,
					x + w - r + Ca2 * r, y + (1.0f - Sa2) * r);

			if(Corners & IGraphics::CORNER_BL)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + r, y + h - r,
					x + (1.0f - Ca1) * r, y + h - r + Sa1 * r,
					x + (1.0f - Ca3) * r, y + h - r + Sa3 * r,
					x + (1.0f - Ca2) * r, y + h - r + Sa2 * r);

			if(Corners & IGraphics::CORNER_BR)
				aFreeform[NumFreeformItems++] = IGraphics::CFreeformItem(
					x + w - r, y + h - r,
					x + w - r + Ca1 * r, y + h - r + Sa1 * r,
					x + w - r + Ca3 * r, y + h - r + Sa3 * r,
					x + w - r + Ca2 * r, y + h - r + Sa2 * r);
		}

		if(NumFreeformItems > 0)
			pGraphics->QuadsDrawFreeform(aFreeform.data(), NumFreeformItems);

		std::array<IGraphics::CQuadItem, 9> aQuads;
		int NumQuadItems = 0;
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + r, y + r, w - r * 2.0f, h - r * 2.0f);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + r, y, w - r * 2.0f, r);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + r, y + h - r, w - r * 2.0f, r);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x, y + r, r, h - r * 2.0f);
		aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + w - r, y + r, r, h - r * 2.0f);

		if(!(Corners & IGraphics::CORNER_TL))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x, y, r, r);
		if(!(Corners & IGraphics::CORNER_TR))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + w, y, -r, r);
		if(!(Corners & IGraphics::CORNER_BL))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x, y + h, r, -r);
		if(!(Corners & IGraphics::CORNER_BR))
			aQuads[NumQuadItems++] = IGraphics::CQuadItem(x + w, y + h, -r, -r);

		pGraphics->QuadsDrawTL(aQuads.data(), NumQuadItems);
		pGraphics->QuadsEnd();
	}

	vec2 RotatePoint(vec2 Point, float Angle)
	{
		const float Sin = std::sin(Angle);
		const float Cos = std::cos(Angle);
		return vec2(Point.x * Cos - Point.y * Sin, Point.x * Sin + Point.y * Cos);
	}

	void DrawSmoothCircle(IGraphics *pGraphics, vec2 Center, float Radius, ColorRGBA Color)
	{
		if(pGraphics == nullptr || Radius <= 0.0f)
			return;

		constexpr int NumSegments = 48;
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);
		pGraphics->DrawCircle(Center.x, Center.y, Radius, NumSegments);
		pGraphics->QuadsEnd();
	}

	void DrawMediaIslandArcGeometry(IGraphics *pGraphics, vec2 Center, float Radius, float Thickness, float Progress, ColorRGBA Color)
	{
		Progress = std::clamp(Progress, 0.0f, 1.0f);
		if(pGraphics == nullptr || Radius <= 0.0f || Thickness <= 0.0f || Progress <= 0.0f)
			return;

		constexpr int MaxSegments = 64;
		constexpr float Pi = 3.14159265359f;
		const int NumSegments = std::max(1, (int)std::ceil(MaxSegments * Progress));
		const float OuterRadius = Radius + Thickness * 0.5f;
		const float InnerRadius = std::max(0.0f, Radius - Thickness * 0.5f);
		const float Sweep = 2.0f * Pi * Progress;
		std::array<IGraphics::CFreeformItem, MaxSegments> aSegments;
		for(int i = 0; i < NumSegments; ++i)
		{
			const float Angle0 = Sweep * i / NumSegments;
			const float Angle1 = Sweep * (i + 1) / NumSegments;
			const vec2 Direction0(std::sin(Angle0), -std::cos(Angle0));
			const vec2 Direction1(std::sin(Angle1), -std::cos(Angle1));
			aSegments[i] = IGraphics::CFreeformItem(
				Center + Direction0 * OuterRadius,
				Center + Direction1 * OuterRadius,
				Center + Direction1 * InnerRadius,
				Center + Direction0 * InnerRadius);
		}

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);
		pGraphics->QuadsDrawFreeform(aSegments.data(), NumSegments);
		pGraphics->QuadsEnd();
	}

	ColorRGBA MediaIslandCountdownColor(EHudMediaIslandCountdownType Type)
	{
		switch(Type)
		{
		case EHudMediaIslandCountdownType::SWAP: return ColorRGBA(0.10f, 0.90f, 1.0f, 1.0f);
		case EHudMediaIslandCountdownType::SWITCH: return ColorRGBA(1.0f, 0.55f, 0.10f, 1.0f);
		case EHudMediaIslandCountdownType::MUTE: return ColorRGBA(1.0f, 0.20f, 0.24f, 1.0f);
		}
		return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}

	void DrawMediaIslandOuterShadowFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)
	{
		if(pGraphics == nullptr || State.m_OuterShadowSize <= 0.0f || State.m_OuterShadowOpacity <= 0.0f)
			return;

		constexpr int LayerCount = 4;
		const ColorRGBA LayerColor(0.0f, 0.0f, 0.0f, std::clamp(State.m_OuterShadowOpacity, 0.0f, 1.0f) / LayerCount);
		for(int Layer = LayerCount; Layer >= 1; --Layer)
		{
			const float Expansion = State.m_OuterShadowSize * Layer / LayerCount;
			DrawSmoothRoundedRect(
				pGraphics,
				State.m_MainRect.x - Expansion,
				State.m_MainRect.y - Expansion,
				State.m_MainRect.w + Expansion * 2.0f,
				State.m_MainRect.h + Expansion * 2.0f,
				State.m_MainRadius + Expansion,
				LayerColor,
				State.m_MainCorners);
			for(int i = 0; i < State.m_ItemCount; ++i)
			{
				const SHudMediaIslandSdfItem &Item = State.m_Items[i];
				const float Radius = std::max(0.0f, std::min(Item.m_Radii.x, Item.m_Radii.y));
				DrawSmoothRoundedRect(
					pGraphics,
					Item.m_Center.x - Item.m_Radii.x - Expansion,
					Item.m_Center.y - Item.m_Radii.y - Expansion,
					Item.m_Radii.x * 2.0f + Expansion * 2.0f,
					Item.m_Radii.y * 2.0f + Expansion * 2.0f,
					Radius + Expansion,
					LayerColor);
			}
			if(State.m_HasRightCapsule && State.m_RightCapsule.m_Rect.w > 0.0f && State.m_RightCapsule.m_Rect.h > 0.0f)
			{
				const CUIRect &Rect = State.m_RightCapsule.m_Rect;
				DrawSmoothRoundedRect(
					pGraphics,
					Rect.x - Expansion,
					Rect.y - Expansion,
					Rect.w + Expansion * 2.0f,
					Rect.h + Expansion * 2.0f,
					State.m_RightCapsule.m_Radius + Expansion,
					LayerColor,
					IGraphics::CORNER_ALL);
			}
		}
	}

	void DrawMediaIslandGeometryFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)
	{
		if(pGraphics == nullptr)
			return;
		DrawMediaIslandOuterShadowFallback(pGraphics, State);
		DrawSmoothRoundedRect(pGraphics, State.m_MainRect.x, State.m_MainRect.y, State.m_MainRect.w, State.m_MainRect.h, State.m_MainRadius, State.m_BackgroundColor, State.m_MainCorners);
		for(int i = 0; i < State.m_ItemCount; ++i)
		{
			const SHudMediaIslandSdfItem &Item = State.m_Items[i];
			const float Radius = std::max(0.0f, std::min(Item.m_Radii.x, Item.m_Radii.y));
			DrawSmoothRoundedRect(
				pGraphics,
				Item.m_Center.x - Item.m_Radii.x,
				Item.m_Center.y - Item.m_Radii.y,
				Item.m_Radii.x * 2.0f,
				Item.m_Radii.y * 2.0f,
				Radius,
				State.m_BackgroundColor);
			if(Item.m_ContentAlpha > 0.001f)
			{
				const float RingRadius = State.m_RingRadius * Item.m_ContentScale;
				const float RingThickness = State.m_RingThickness * Item.m_ContentScale;
				DrawMediaIslandArcGeometry(pGraphics, Item.m_Center, RingRadius, RingThickness, 1.0f, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * 0.18f * Item.m_ContentAlpha));
				DrawMediaIslandArcGeometry(pGraphics, Item.m_Center, RingRadius, RingThickness, Item.m_CountdownProgress, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * Item.m_ContentAlpha));
			}
		}
		if(State.m_HasRightCapsule && State.m_RightCapsule.m_Rect.w > 0.0f && State.m_RightCapsule.m_Rect.h > 0.0f)
		{
			const CUIRect &Rect = State.m_RightCapsule.m_Rect;
			DrawSmoothRoundedRect(pGraphics, Rect.x, Rect.y, Rect.w, Rect.h, State.m_RightCapsule.m_Radius, State.m_BackgroundColor, IGraphics::CORNER_ALL);
		}
	}

	void DrawMediaIslandCountdownSatellite(
		IGraphics *pGraphics,
		vec2 Center,
		float SatelliteRadius,
		float RingRadius,
		float RingThickness,
		float Progress,
		float Alpha,
		float ScreenPixelSize,
		ColorRGBA BackgroundColor,
		ColorRGBA RingColor)
	{
		Alpha = std::clamp(Alpha, 0.0f, 1.0f);
		if(pGraphics == nullptr || SatelliteRadius <= 0.0f || RingRadius <= 0.0f || RingThickness <= 0.0f || Alpha <= 0.0f)
			return;

		BackgroundColor.a *= Alpha;
		SHudMediaIslandSdfRenderState State;
		State.m_MainRect = {Center.x - SatelliteRadius, Center.y - SatelliteRadius, SatelliteRadius * 2.0f, SatelliteRadius * 2.0f};
		State.m_MainRadius = SatelliteRadius;
		State.m_MainCorners = IGraphics::CORNER_ALL;
		State.m_ItemCount = 1;
		State.m_Items[0].m_Center = Center;
		State.m_Items[0].m_Radii = vec2();
		State.m_Items[0].m_ContentAlpha = Alpha;
		State.m_Items[0].m_CountdownProgress = std::clamp(Progress, 0.0f, 1.0f);
		State.m_Items[0].m_RingColor = RingColor;
		State.m_RingRadius = RingRadius;
		State.m_RingThickness = RingThickness;
		State.m_BackgroundColor = BackgroundColor;
		State.m_ScreenPixelSize = std::max(ScreenPixelSize, 0.0001f);
		State.m_OuterShadowSize = State.m_ScreenPixelSize * MEDIA_ISLAND_OUTER_SHADOW_PIXELS;
		State.m_OuterShadowOpacity = MEDIA_ISLAND_OUTER_SHADOW_OPACITY * BackgroundColor.a;
		State.m_Rect = QmHudMediaIslandSdfOuterRect(State);

		IGraphics::SMediaIslandSdfParams GpuSdfParams;
		if(!QmHudMediaIslandBuildGpuSdfParams(State, GpuSdfParams))
			return;
		if(pGraphics->HasMediaIslandSdf())
			pGraphics->RenderMediaIslandSdf(GpuSdfParams);
		else
			DrawMediaIslandGeometryFallback(pGraphics, State);
	}

	EQmIcon MediaIslandCountdownIcon(EHudMediaIslandCountdownType Type, bool Completed = false, bool SwapOutgoing = false)
	{
		if(Completed)
			return EQmIcon::SATELLITE_CHECK;
		switch(Type)
		{
		case EHudMediaIslandCountdownType::SWAP: return SwapOutgoing ? EQmIcon::SATELLITE_SWAP_OUTGOING : EQmIcon::SATELLITE_SWAP_INCOMING;
		case EHudMediaIslandCountdownType::SWITCH: return EQmIcon::SATELLITE_SWITCH;
		case EHudMediaIslandCountdownType::MUTE: return EQmIcon::SATELLITE_MUTE;
		}
		return EQmIcon::COUNT;
	}

	void DrawTexturedQuad(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, vec2 Center, float Radius, float Alpha = 1.0f)
	{
		if(pGraphics == nullptr || !Texture.IsValid() || Radius <= 0.0f)
			return;

		pGraphics->WrapClamp();
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		const IGraphics::CQuadItem CoverQuad(Center.x - Radius, Center.y - Radius, Radius * 2.0f, Radius * 2.0f);
		pGraphics->QuadsDrawTL(&CoverQuad, 1);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
		pGraphics->TextureClear();
	}

	bool BuildSwapCountdownInfo(const CGameClient &GameClient, IClient &Client, int Dummy, const SSwapCountdownState &State, SSwapCountdownInfo &Out)
	{
		if(Dummy < 0 || Dummy >= NUM_DUMMIES)
			return false;

		const int StartTick = State.m_StartTick;
		if(StartTick <= 0)
			return false;

		const int TickSpeed = Client.GameTickSpeed();
		const int CurTick = Client.GameTick(Dummy);
		const SHudMediaIslandSwapLifecycle Lifecycle = QmHudMediaIslandSwapLifecycle(StartTick, CurTick, TickSpeed);
		if(!Lifecycle.m_Visible)
			return false;

		const char *pCounterpart = State.m_Counterpart.c_str();
		if(pCounterpart[0] == '\0')
			pCounterpart = "?";
		const int TargetClientId = GameClient.m_aLocalIds[Dummy];
		const char *pLocal = TargetClientId >= 0 && TargetClientId < MAX_CLIENTS && GameClient.m_aClients[TargetClientId].m_aName[0] != '\0' ?
					     GameClient.m_aClients[TargetClientId].m_aName :
					     (Dummy == 0 ? Client.PlayerName() : Client.DummyName());
		const int SecondsLeft = Lifecycle.m_SecondsLeft;
		Out.m_Dummy = Dummy;
		Out.m_InstanceId = State.m_InstanceId * NUM_DUMMIES + Dummy;
		Out.m_StartTick = StartTick;
		Out.m_Outgoing = State.m_Outgoing;
		Out.m_Lifecycle = Lifecycle;
		const char *pFrom = Out.m_Outgoing ? pLocal : pCounterpart;
		const char *pTo = Out.m_Outgoing ? pCounterpart : pLocal;
		if(!Out.m_Outgoing)
			str_format(Out.m_aRequestText, sizeof(Out.m_aRequestText), Localize("%s has requested to swap with %s"), pFrom, pTo);
		if(!Lifecycle.m_Completed)
		{
			str_format(Out.m_aText, sizeof(Out.m_aText), "%s->%s Swap:%d秒", pFrom, pTo, SecondsLeft);
			Out.m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			str_format(Out.m_aText, sizeof(Out.m_aText), "%s->%s 可交换!", pFrom, pTo);
			Out.m_TextColor = ColorRGBA(0.5f, 1.0f, 0.5f, 1.0f);
		}

		return true;
	}

	bool BuildSwapCountdownList(const CGameClient &GameClient, IClient &Client, SSwapCountdownList &Out)
	{
		Out.m_Count = 0;
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			if(!QmHudMediaIslandSwapVisibleForConnection(Dummy, g_Config.m_ClDummy))
				continue;
			for(const SSwapCountdownState &State : GameClient.m_TClient.GetSwapCountdowns(Dummy))
			{
				if(Out.m_Count < (int)Out.m_aInfos.size() && BuildSwapCountdownInfo(GameClient, Client, Dummy, State, Out.m_aInfos[Out.m_Count]))
					++Out.m_Count;
			}
		}
		return Out.m_Count > 0;
	}

	float ResolveAnimatedLayoutValueEx(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, float &LastTarget, float DurationSec, float DelaySec, EEasing Easing)
	{
		LastTarget = Target;
		if(DelaySec > 0.0f)
			return ResolveUiAnimValue(AnimRuntime, NodeKey, Property, Target, DurationSec + DelaySec, Easing);
		return ResolveUiAnimValue(AnimRuntime, NodeKey, Property, Target, DurationSec, Easing);
	}

	float ResolveAnimatedLayoutValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, float &LastTarget)
	{
		return ResolveAnimatedLayoutValueEx(AnimRuntime, NodeKey, Property, Target, LastTarget, 0.10f, 0.0f, EEasing::EASE_OUT);
	}

	int GetMediaIslandSpectatorCount(const CGameClient &GameClient, const IClient &Client)
	{
		int Count = 0;
		if(Client.IsSixup())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(i == GameClient.m_aLocalIds[0] || (Client.DummyConnected() && i == GameClient.m_aLocalIds[1]))
					continue;

				if(Client.m_TranslationContext.m_aClients[i].m_PlayerFlags7 & protocol7::PLAYERFLAG_WATCHING)
					++Count;
			}
		}
		else if(const CNetObj_SpectatorCount *pSpectatorCount = GameClient.m_Snap.m_pSpectatorCount)
		{
			Count = maximum(pSpectatorCount->m_NumSpectators, 0);
		}

		return Count;
	}

	void FormatSpeedrunTime(int64_t RemainingMilliseconds, char *pBuf, size_t BufSize)
	{
		const int RemainingHours = (int)(RemainingMilliseconds / (60 * 60 * 1000));
		const int RemainingMinutes = (int)((RemainingMilliseconds / (60 * 1000)) % 60);
		const int RemainingSeconds = (int)((RemainingMilliseconds / 1000) % 60);
		const int Milliseconds = (int)(RemainingMilliseconds % 1000);
		if(RemainingHours > 0)
			str_format(pBuf, BufSize, "%02d:%02d:%02d.%03d", RemainingHours, RemainingMinutes, RemainingSeconds, Milliseconds);
		else
			str_format(pBuf, BufSize, "%02d:%02d.%03d", RemainingMinutes, RemainingSeconds, Milliseconds);
	}
}

CHud::CHud()
{
	m_FPSTextContainerIndex.Reset();
	m_DDRaceEffectsTextContainerIndex.Reset();
	m_PlayerAngleTextContainerIndex.Reset();
	m_PlayerPrevAngle = -INFINITY;
	m_TextInfoV2AnimState.Reset();
	m_LocalTimeV2AnimState.Reset();
	m_MediaIslandAnimState.Reset();
	m_MediaIslandFrameCache.Reset();
	m_MediaIslandBlurReady = false;
	m_MediaIslandBlurLastAttemptFrame = 0;
	m_MediaIslandBlurAttemptInitialized = false;
	m_WeaponPresentationState.Reset();
	m_RecordingStatusAnimState.Reset();
	m_SwitchCountdownAnimState.Reset();
	ResetSwitchCountdownRings();
	m_SwitchCountdownTracker.Reset();
	m_MediaIslandMuteState.Reset();
	m_vTextInfoLayoutChildrenScratch.reserve(2);
	m_vLocalTimeLayoutChildrenScratch.resize(1);

	for(int i = 0; i < 2; i++)
	{
		m_aPlayerSpeedTextContainers[i].Reset();
		m_aPlayerPrevSpeed[i] = -INFINITY;
		m_aPlayerPositionContainers[i].Reset();
		m_aPlayerPrevPosition[i] = -INFINITY;
	}
}

void CHud::HandleSpamProtectionMessage(const char *pMessage)
{
	if(pMessage == nullptr || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	const char *pMainName = Client()->PlayerName();
	const int MainId = GameClient()->m_aLocalIds[0];
	if(MainId >= 0 && MainId < MAX_CLIENTS && GameClient()->m_aClients[MainId].m_aName[0] != '\0')
		pMainName = GameClient()->m_aClients[MainId].m_aName;

	const char *pDummyName = "";
	if(Client()->DummyConnected())
	{
		pDummyName = Client()->DummyName();
		const int DummyId = GameClient()->m_aLocalIds[1];
		if(DummyId >= 0 && DummyId < MAX_CLIENTS && GameClient()->m_aClients[DummyId].m_aName[0] != '\0')
			pDummyName = GameClient()->m_aClients[DummyId].m_aName;
	}

	int Seconds = 0;
	const EHudMediaIslandMuteMessage Message = QmHudParseSpamProtectionMute(pMessage, pMainName, pDummyName, Seconds);
	if(Message == EHudMediaIslandMuteMessage::NONE || Seconds <= 0)
		return;

	auto &MuteState = m_MediaIslandMuteState;
	const int64_t Now = time_get();
	const int64_t RemainingTicks = static_cast<int64_t>(Seconds) * time_freq();
	if(Message == EHudMediaIslandMuteMessage::SPAM_BROADCAST)
	{
		const bool Duplicate = MuteState.m_Confirmed && Now < MuteState.m_EndTick && std::abs(MuteState.m_DurationTicks - RemainingTicks) < time_freq();
		if(Duplicate)
			return;
		MuteState.m_Confirmed = true;
		MuteState.m_TriggerTick = Now;
		MuteState.m_DurationTicks = RemainingTicks;
		MuteState.m_EndTick = Now + RemainingTicks;
	}
	else if(MuteState.m_Confirmed && Now < MuteState.m_EndTick)
	{
		MuteState.m_EndTick = Now + RemainingTicks;
	}
}

void CHud::ResetHudContainers()
{
	for(auto &ScoreInfo : m_aScoreInfo)
	{
		TextRender()->DeleteTextContainer(ScoreInfo.m_OptionalNameTextContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextRankContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextScoreContainerIndex);
		Graphics()->DeleteQuadContainer(ScoreInfo.m_RoundRectQuadContainerIndex);

		ScoreInfo.Reset();
	}

	TextRender()->DeleteTextContainer(m_FPSTextContainerIndex);
	TextRender()->DeleteTextContainer(m_DDRaceEffectsTextContainerIndex);
	TextRender()->DeleteTextContainer(m_PlayerAngleTextContainerIndex);
	m_PlayerPrevAngle = -INFINITY;
	for(int i = 0; i < 2; i++)
	{
		TextRender()->DeleteTextContainer(m_aPlayerSpeedTextContainers[i]);
		m_aPlayerPrevSpeed[i] = -INFINITY;
		TextRender()->DeleteTextContainer(m_aPlayerPositionContainers[i]);
		m_aPlayerPrevPosition[i] = -INFINITY;
	}

	m_TextInfoV2AnimState.Reset();
	m_LocalTimeV2AnimState.Reset();
	m_MediaIslandAnimState.Reset();
	m_MediaIslandFrameCache.Reset();
	m_MediaIslandBlurReady = false;
	m_MediaIslandBlurLastAttemptFrame = 0;
	m_MediaIslandBlurAttemptInitialized = false;
	m_WeaponPresentationState.Reset();
	m_RecordingStatusAnimState.Reset();
	m_SwitchCountdownAnimState.Reset();
	ResetSwitchCountdownRings();
	m_SwitchCountdownTracker.Reset();
}

void CHud::OnWindowResize()
{
	ResetHudContainers();
}

void CHud::OnReset()
{
	m_TimeCpDiff = 0.0f;
	m_DDRaceTime = 0;
	m_FinishTimeLastReceivedTick = 0;
	m_TimeCpLastReceivedTick = 0;
	m_ShowFinishTime = false;
	m_aPlayerRecord[0] = -1.0f;
	m_aPlayerRecord[1] = -1.0f;
	m_aPlayerSpeed[0] = 0;
	m_aPlayerSpeed[1] = 0;
	m_aLastPlayerSpeedChange[0] = ESpeedChange::NONE;
	m_aLastPlayerSpeedChange[1] = ESpeedChange::NONE;
	m_LastSpectatorCountTick = 0;
	m_aMapProgressDisplayed[0] = 0.0f;
	m_aMapProgressDisplayed[1] = 0.0f;
	m_aMapProgressInitialized[0] = false;
	m_aMapProgressInitialized[1] = false;
	m_MediaIslandAnimState.Reset();
	m_MediaIslandFrameCache.Reset();
	m_MediaIslandMuteState.Reset();
	m_WeaponPresentationState.Reset();
	m_RecordingStatusAnimState.Reset();

	ResetHudContainers();
}

void CHud::OnInit()
{
	OnReset();

	Graphics()->SetColor(1.0, 1.0, 1.0, 1.0);

	m_HudQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	PrepareAmmoHealthAndArmorQuads();

	// all cursors for the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		float ScaleX, ScaleY;
		Graphics()->GetSpriteScale(g_pData->m_Weapons.m_aId[i].m_pSpriteCursor, ScaleX, ScaleY);
		m_aCursorOffset[i] = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 64.f * ScaleX, 64.f * ScaleY);
	}

	// the flags
	m_FlagOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 8.f, 16.f);

	PreparePlayerStateQuads();

	Graphics()->QuadContainerUpload(m_HudQuadContainerIndex);
}

void CHud::DestroyMediaIslandBlurTargets()
{
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurSource);
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurTemporary);
	Graphics()->DestroyRenderTarget(&m_MediaIslandBlurTarget);
	m_MediaIslandBlurWidth = 0;
	m_MediaIslandBlurHeight = 0;
	m_MediaIslandBlurReady = false;
	m_MediaIslandBlurLastAttemptFrame = 0;
	m_MediaIslandBlurAttemptInitialized = false;
}

void CHud::DestroyDummyMiniViewRenderTarget()
{
	Graphics()->DestroyRenderTarget(&m_DummyMiniViewRenderTarget);
	m_DummyMiniViewRenderTargetWidth = 0;
	m_DummyMiniViewRenderTargetHeight = 0;
}

bool CHud::PrepareMediaIslandBlur()
{
	if(!QmHudMediaIslandShouldPrepareBackdropBlur(g_Config.m_QmHudIslandBgOpacity, g_Config.m_QmGaussianBlur != 0) || !Graphics()->HasMediaIslandSdf())
		return false;
	if(!Graphics()->IsBackbufferCaptureSupported() || !Graphics()->IsRenderTargetGaussianBlurSupported())
	{
		if(m_MediaIslandBlurSource.IsValid() || m_MediaIslandBlurTemporary.IsValid() || m_MediaIslandBlurTarget.IsValid())
			DestroyMediaIslandBlurTargets();
		return false;
	}

	const int BlurWidth = MediaIslandBlurTargetDimension(Graphics()->ScreenWidth());
	const int BlurHeight = MediaIslandBlurTargetDimension(Graphics()->ScreenHeight());
	if(BlurWidth <= 0 || BlurHeight <= 0)
	{
		m_MediaIslandBlurReady = false;
		m_MediaIslandBlurLastAttemptFrame = 0;
		m_MediaIslandBlurAttemptInitialized = false;
		return false;
	}

	const bool SizeChanged = BlurWidth != m_MediaIslandBlurWidth || BlurHeight != m_MediaIslandBlurHeight;
	if(SizeChanged || !m_MediaIslandBlurSource.IsValid() || !m_MediaIslandBlurTemporary.IsValid() || !m_MediaIslandBlurTarget.IsValid())
	{
		DestroyMediaIslandBlurTargets();
		m_MediaIslandBlurSource = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		m_MediaIslandBlurTemporary = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		m_MediaIslandBlurTarget = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		if(!m_MediaIslandBlurSource.IsValid() || !m_MediaIslandBlurTemporary.IsValid() || !m_MediaIslandBlurTarget.IsValid())
		{
			DestroyMediaIslandBlurTargets();
			return false;
		}
		m_MediaIslandBlurWidth = BlurWidth;
		m_MediaIslandBlurHeight = BlurHeight;
	}

	const uint64_t CurrentFrame = Client()->PerfFrame();
	if(!QmHudMediaIslandShouldRefreshBackdropBlur(CurrentFrame, m_MediaIslandBlurLastAttemptFrame, m_MediaIslandBlurAttemptInitialized))
		return m_MediaIslandBlurReady;
	m_MediaIslandBlurLastAttemptFrame = CurrentFrame;
	m_MediaIslandBlurAttemptInitialized = true;

	if(!Graphics()->CaptureBackbufferToRenderTarget(m_MediaIslandBlurSource))
	{
		m_MediaIslandBlurReady = false;
		return false;
	}

	IGraphics::SGaussianBlurParams BlurParams;
	BlurParams.m_Radius = 4;
	BlurParams.m_Sigma = 2.0f;
	m_MediaIslandBlurReady = Graphics()->GaussianBlurRenderTarget(
		m_MediaIslandBlurSource,
		m_MediaIslandBlurTemporary,
		m_MediaIslandBlurTarget,
		BlurParams);
	return m_MediaIslandBlurReady;
}

void CHud::OnRelease()
{
	DestroyMediaIslandBlurTargets();
	DestroyDummyMiniViewRenderTarget();
	m_MediaIslandFrameCache.Reset();
}

void CHud::RenderSpeedrunTimer()
{
	if(!g_Config.m_QmSpeedrunTimer && m_SpeedrunTimerExpiredTick <= 0)
		return;

	if(!GameClient()->m_Snap.m_pLocalCharacter)
		return;

	constexpr float SpeedrunTimerY = 20.0f;
	constexpr float SpeedrunTimerExpiredY = 25.0f;

	const int TotalConfiguredMilliseconds =
		g_Config.m_QmSpeedrunTimerHours * 60 * 60 * 1000 +
		g_Config.m_QmSpeedrunTimerMinutes * 60 * 1000 +
		g_Config.m_QmSpeedrunTimerSeconds * 1000 +
		g_Config.m_QmSpeedrunTimerMilliseconds;

	int TotalSpeedrunTimerMilliseconds = TotalConfiguredMilliseconds;
	if(TotalSpeedrunTimerMilliseconds <= 0 && g_Config.m_QmSpeedrunTimerTime > 0)
	{
		const int LegacyMinutes = g_Config.m_QmSpeedrunTimerTime / 100;
		const int LegacySeconds = g_Config.m_QmSpeedrunTimerTime % 100;
		if(LegacySeconds < 60)
			TotalSpeedrunTimerMilliseconds = (LegacyMinutes * 60 + LegacySeconds) * 1000;
	}

	if(TotalSpeedrunTimerMilliseconds <= 0)
		return;

	const bool RaceStarted = (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) &&
				 GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer < 0;

	if(RaceStarted && m_SpeedrunTimerExpiredTick > 0)
		m_SpeedrunTimerExpiredTick = 0;

	if(m_SpeedrunTimerExpiredTick > 0)
	{
		const int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
		if(CurrentTick < m_SpeedrunTimerExpiredTick + Client()->GameTickSpeed() * 5)
		{
			char aBuf[64];
			str_copy(aBuf, Localize("TIME EXPIRED!"), sizeof(aBuf));
			const float Half = m_Width / 2.0f;
			const float FontSize = 12.0f;
			const float w = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, 1.0f);
			TextRender()->Text(Half - w / 2, SpeedrunTimerExpiredY, FontSize, aBuf, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else
		{
			m_SpeedrunTimerExpiredTick = 0;
		}
		return;
	}

	if(!RaceStarted)
		return;

	const int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
	const int StartTick = -GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer;
	const int ElapsedTicks = CurrentTick - StartTick;

	const int64_t DeadlineTicks = (int64_t)TotalSpeedrunTimerMilliseconds * Client()->GameTickSpeed() / 1000;
	const int64_t RemainingTicks = DeadlineTicks - ElapsedTicks;

	if(RemainingTicks <= 0)
	{
		m_SpeedrunTimerExpiredTick = CurrentTick;
		GameClient()->SendKill();
		if(g_Config.m_QmSpeedrunTimerAutoDisable)
			g_Config.m_QmSpeedrunTimer = 0;
		return;
	}

	const int64_t RemainingMilliseconds = RemainingTicks * 1000 / Client()->GameTickSpeed();
	char aBuf[32];
	FormatSpeedrunTime(RemainingMilliseconds, aBuf, sizeof(aBuf));

	const float Half = m_Width / 2.0f;
	const float FontSize = 8.0f;
	const float w = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);

	if(RemainingMilliseconds <= 60 * 1000)
		TextRender()->TextColor(1.0f, 0.25f, 0.25f, 1.0f);

	TextRender()->Text(Half - w / 2, SpeedrunTimerY, FontSize, aBuf, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CHud::RenderGameTimer()
{
	SHudGameTimerInfo TimerInfo = BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width);
	const bool Preview = GameClient()->m_HudEditor.IsActive() && g_Config.m_ClShowhudTimer && !TimerInfo.m_Visible;
	if(!TimerInfo.m_Visible && !Preview)
		return;
	if(Preview)
	{
		str_copy(TimerInfo.m_aText, "11:56.13", sizeof(TimerInfo.m_aText));
		TimerInfo.m_FontSize = 10.0f;
		TimerInfo.m_W = TextRender()->TextWidth(TimerInfo.m_FontSize, TimerInfo.m_aText, -1, -1.0f);
		TimerInfo.m_X = m_Width * 0.5f - TimerInfo.m_W * 0.5f;
		TimerInfo.m_Left = TimerInfo.m_X;
		TimerInfo.m_Visible = true;
		TimerInfo.m_Alpha = 1.0f;
		TimerInfo.m_IsCritical = false;
	}

	if(g_Config.m_QmHudIslandUseOriginalStyle)
	{
		m_RecordingStatusAnimState.Reset();
		const float TimerWidth = TextRender()->TextWidth(TimerInfo.m_FontSize, TimerInfo.m_aText, -1, -1.0f);
		const CUIRect TimerRect = {TimerInfo.m_X - 4.0f, TimerInfo.m_Y - 2.0f, TimerWidth + 8.0f, TimerInfo.m_FontSize + 6.0f};
		const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::GameTimer, TimerRect);

		const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
		if(TimerInfo.m_IsCritical)
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, TimerInfo.m_Alpha);
		else
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, TimerInfo.m_Alpha);
		TextRender()->Text(TimerInfo.m_X, TimerInfo.m_Y, TimerInfo.m_FontSize, TimerInfo.m_aText, -1.0f);
		TextRender()->TextColor(PrevTextColor);
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
		return;
	}

	const SHudTopTimerCapsuleInfo TimerCapsule = BuildHudTopTimerCapsuleInfo(TimerInfo);
	if(!TimerCapsule.m_Visible)
		return;

	constexpr float TimerRadius = QmHudMediaIslandScaled(8.0f);
	constexpr float StatusSectionGap = QmHudMediaIslandScaled(3.0f);
	constexpr float StatusPaddingLeft = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusPaddingRight = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotSize = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotGap = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusFontSize = QmHudMediaIslandScaled(5.3f);

	char aRecordingBuf[512];
	const bool ShowRecordingStatus = BuildHudRecordingStatusText(*GameClient(), aRecordingBuf, sizeof(aRecordingBuf));
	const float StatusTextWidth = ShowRecordingStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aRecordingBuf).m_W) : 0.0f;
	const float RawCollapsedWidth = StatusPaddingLeft + StatusDotSize + StatusPaddingRight;
	const float RawExpandedWidth = StatusPaddingLeft + StatusDotSize + StatusDotGap + StatusTextWidth + StatusPaddingRight;
	const bool ScoreboardExpanded = GameClient()->m_Scoreboard.IsActive();
	const float TimerBoxX = TimerCapsule.m_BoxX;
	const float TimerTextX = TimerCapsule.m_TextX;
	const float StatusSectionX = TimerBoxX + TimerCapsule.m_BoxW + StatusSectionGap;
	const float CollapsedWidth = RawCollapsedWidth;
	const float ExpandedWidth = RawExpandedWidth;
	const float TargetStatusWidth = ShowRecordingStatus ? (ScoreboardExpanded ? ExpandedWidth : CollapsedWidth) : 0.0f;
	const float TargetStatusAlpha = ShowRecordingStatus ? 1.0f : 0.0f;
	const float TargetTextAlpha = ShowRecordingStatus && ScoreboardExpanded ? 1.0f : 0.0f;

	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	const uint64_t StatusBoxNode = HudRecordingStatusNodeKey("box");
	const uint64_t StatusTextNode = HudRecordingStatusNodeKey("text");
	if(!m_RecordingStatusAnimState.m_Initialized)
	{
		m_RecordingStatusAnimState.m_TargetWidth = TargetStatusWidth;
		m_RecordingStatusAnimState.m_TargetAlpha = TargetStatusAlpha;
		m_RecordingStatusAnimState.m_TargetTextAlpha = TargetTextAlpha;
		SetUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::WIDTH, TargetStatusWidth);
		SetUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::ALPHA, TargetStatusAlpha);
		SetUiPresentationStateValue(AnimRuntime, StatusTextNode, EUiAnimProperty::ALPHA, TargetTextAlpha);
		m_RecordingStatusAnimState.m_Initialized = true;
	}

	const float StatusWidth = ResolveAnimatedLayoutValueEx(AnimRuntime, StatusBoxNode, EUiAnimProperty::WIDTH, TargetStatusWidth, m_RecordingStatusAnimState.m_TargetWidth, 0.16f, 0.0f, EEasing::EASE_OUT);
	const float StatusAlpha = std::clamp(ResolveAnimatedLayoutValueEx(AnimRuntime, StatusBoxNode, EUiAnimProperty::ALPHA, TargetStatusAlpha, m_RecordingStatusAnimState.m_TargetAlpha, 0.10f, 0.0f, EEasing::EASE_OUT), 0.0f, 1.0f);
	const float StatusTextAlpha = std::clamp(ResolveAnimatedLayoutValueEx(AnimRuntime, StatusTextNode, EUiAnimProperty::ALPHA, TargetTextAlpha, m_RecordingStatusAnimState.m_TargetTextAlpha, 0.08f, 0.0f, EEasing::EASE_OUT), 0.0f, 1.0f);
	const bool RenderStatusSection = StatusWidth > 1.0f && StatusAlpha > 0.01f;
	const float CombinedWidth = TimerCapsule.m_BoxW + (RenderStatusSection ? (StatusSectionGap + StatusWidth) : 0.0f);
	const CUIRect TimerRect = {TimerBoxX, TimerCapsule.m_BoxY, CombinedWidth, TimerCapsule.m_BoxH};
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::GameTimer, TimerRect);

	const unsigned int PrevFlags = TextRender()->GetRenderFlags();
	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.42f);

	DrawSmoothRoundedRect(Graphics(), TimerBoxX, TimerCapsule.m_BoxY, CombinedWidth, TimerCapsule.m_BoxH, TimerRadius, ColorRGBA(0.04f, 0.05f, 0.07f, 0.80f), HudEditorScope.m_Corners);
	if(TimerCapsule.m_IsCritical)
		TextRender()->TextColor(1.0f, 0.25f, 0.25f, TimerCapsule.m_Alpha);
	else
		TextRender()->TextColor(0.98f, 0.99f, 1.0f, 0.98f);
	TextRender()->Text(TimerTextX, TimerCapsule.m_TextY, TimerCapsule.m_FontSize, TimerCapsule.m_aText, -1.0f);

	if(RenderStatusSection)
	{
		const float DividerX = TimerBoxX + TimerCapsule.m_BoxW + StatusSectionGap * 0.5f;
		Graphics()->DrawRect(DividerX, TimerCapsule.m_BoxY + QmHudMediaIslandScaled(4.0f), QmHudMediaIslandScaled(0.75f), TimerCapsule.m_BoxH - QmHudMediaIslandScaled(8.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * StatusAlpha), IGraphics::CORNER_ALL, QmHudMediaIslandScaled(0.375f));

		const vec2 DotCenter(StatusSectionX + StatusPaddingLeft + StatusDotSize * 0.5f, TimerCapsule.m_BoxY + TimerCapsule.m_BoxH * 0.5f);
		DrawSmoothCircle(Graphics(), DotCenter, StatusDotSize * 0.5f, ColorRGBA(1.0f, 0.15f, 0.15f, 0.95f * StatusAlpha));

		if(StatusTextAlpha > 0.001f && StatusWidth > RawCollapsedWidth + QmHudMediaIslandScaled(2.0f))
		{
			const float TextX = StatusSectionX + StatusPaddingLeft + StatusDotSize + StatusDotGap;
			const float TextY = TimerCapsule.m_BoxY + (TimerCapsule.m_BoxH - StatusFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);
			TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.90f * StatusTextAlpha);
			TextRender()->Text(TextX, TextY, StatusFontSize, aRecordingBuf, -1.0f);
		}
	}

	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevOutlineColor);
	TextRender()->SetRenderFlags(PrevFlags);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderPauseNotification()
{
	const bool Paused = (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED) != 0;
	const bool GameOver = (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) != 0;
	if(!Paused || GameOver)
		return;

	const char *pText = Localize("Game paused");
	float FontSize = 20.0f;
	float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
	const float X = 150.0f * Graphics()->ScreenAspect() - w / 2.0f;
	const float Y = 50.0f;
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::PauseNotification, {X, Y, w, FontSize + 4.0f});
	TextRender()->Text(X, Y, FontSize, pText, -1.0f);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderSuddenDeath()
{
	const bool SuddenDeath = (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH) != 0;
	if(!SuddenDeath)
		return;

	float Half = m_Width / 2.0f;
	const char *pText = Localize("Sudden Death");
	float FontSize = 12.0f;
	float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
	const float X = Half - w / 2.0f;
	const float Y = 2.0f;
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::SuddenDeath, {X, Y, w, FontSize + 4.0f});
	TextRender()->Text(X, Y, FontSize, pText, -1.0f);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderScoreHud()
{
	// render small score hud
	if(!(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::ScoreHud, {m_Width - 150.0f, 229.0f, 150.0f, 56.0f});
		const int ScoreHudCorners = HudEditorScope.m_Corners;
		float StartY = 229.0f; // the height of this display is 56, so EndY is 285

		const float ScoreSingleBoxHeight = 18.0f;

		bool ForceScoreInfoInit = !m_aScoreInfo[0].m_Initialized || !m_aScoreInfo[1].m_Initialized;
		m_aScoreInfo[0].m_Initialized = m_aScoreInfo[1].m_Initialized = true;

		if(GameClient()->IsTeamPlay() && GameClient()->m_Snap.m_pGameDataObj)
		{
			char aScoreTeam[2][16];
			str_format(aScoreTeam[TEAM_RED], sizeof(aScoreTeam[TEAM_RED]), "%d", GameClient()->m_Snap.m_pGameDataObj->m_TeamscoreRed);
			str_format(aScoreTeam[TEAM_BLUE], sizeof(aScoreTeam[TEAM_BLUE]), "%d", GameClient()->m_Snap.m_pGameDataObj->m_TeamscoreBlue);

			bool aRecreateTeamScore[2] = {str_comp(aScoreTeam[0], m_aScoreInfo[0].m_aScoreText) != 0, str_comp(aScoreTeam[1], m_aScoreInfo[1].m_aScoreText) != 0};

			const int aFlagCarrier[2] = {
				GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierRed,
				GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue};

			bool RecreateRect = ForceScoreInfoInit;
			for(int t = 0; t < 2; t++)
			{
				if(m_aScoreInfo[t].m_RoundRectCorners != ScoreHudCorners)
					RecreateRect = true;
				if(aRecreateTeamScore[t])
				{
					m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(14.0f, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE], -1, -1.0f);
					str_copy(m_aScoreInfo[t].m_aScoreText, aScoreTeam[t == 0 ? TEAM_RED : TEAM_BLUE]);
					RecreateRect = true;
				}
			}

			static float s_TextWidth100 = TextRender()->TextWidth(14.0f, "100", -1, -1.0f);
			float ScoreWidthMax = maximum(maximum(m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth), s_TextWidth100);
			float Split = 3.0f;
			float ImageSize = (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) ? 16.0f : Split;
			for(int t = 0; t < 2; t++)
			{
				// draw box
				if(RecreateRect)
				{
					Graphics()->DeleteQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex);

					if(t == 0)
						Graphics()->SetColor(0.975f, 0.17f, 0.17f, 0.3f);
					else
						Graphics()->SetColor(0.17f, 0.46f, 0.975f, 0.3f);
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(m_Width - ScoreWidthMax - ImageSize - 2 * Split, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split, ScoreSingleBoxHeight, 5.0f, ScoreHudCorners);
					m_aScoreInfo[t].m_RoundRectCorners = ScoreHudCorners;
				}
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
				{
					Ui()->RenderGaussianBlur({m_Width - ScoreWidthMax - ImageSize - 2 * Split, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split, ScoreSingleBoxHeight}, 1.0f, ScoreHudCorners, 5.0f);
					Graphics()->TextureClear();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);
				}

				// draw score
				if(aRecreateTeamScore[t])
				{
					CTextCursor Cursor;
					Cursor.SetPosition(vec2(m_Width - ScoreWidthMax + (ScoreWidthMax - m_aScoreInfo[t].m_ScoreTextWidth) / 2 - Split, StartY + t * 20 + (18.f - 14.f) / 2.f));
					Cursor.m_FontSize = 14.0f;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScoreTeam[t]);
				}
				if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
				}

				if(GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS)
				{
					int BlinkTimer = (GameClient()->m_aFlagDropTick[t] != 0 &&
								 (Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_aFlagDropTick[t]) / Client()->GameTickSpeed() >= 25) ?
								 10 :
								 20;
					if(aFlagCarrier[t] == FLAG_ATSTAND || (aFlagCarrier[t] == FLAG_TAKEN && ((Client()->GameTick(g_Config.m_ClDummy) / BlinkTimer) & 1)))
					{
						// draw flag
						Graphics()->TextureSet(t == 0 ? GameClient()->m_GameSkin.m_SpriteFlagRed : GameClient()->m_GameSkin.m_SpriteFlagBlue);
						Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
						Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_FlagOffset, m_Width - ScoreWidthMax - ImageSize, StartY + 1.0f + t * 20);
					}
					else if(aFlagCarrier[t] >= 0)
					{
						// draw name of the flag holder
						int Id = aFlagCarrier[t] % MAX_CLIENTS;
						char aNameBuf[MAX_NAME_LENGTH];
						GameClient()->FormatStreamerName(Id, aNameBuf, sizeof(aNameBuf));
						const char *pName = aNameBuf;
						if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0 || RecreateRect)
						{
							str_copy(m_aScoreInfo[t].m_aPlayerNameText, pName);

							float w = TextRender()->TextWidth(8.0f, pName, -1, -1.0f);

							CTextCursor Cursor;
							Cursor.SetPosition(vec2(minimum(m_Width - w - 1.0f, m_Width - ScoreWidthMax - ImageSize - 2 * Split), StartY + (t + 1) * 20.0f - 2.0f));
							Cursor.m_FontSize = 8.0f;
							TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
						}

						if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
						{
							ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
							ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
							TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
						}

						// draw tee of the flag holder
						CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
						TeeInfo.m_Size = ScoreSingleBoxHeight;

						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(m_Width - ScoreWidthMax - TeeInfo.m_Size / 2 - Split, StartY + (t * 20) + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);

						RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
					}
				}
				StartY += 8.0f;
			}
		}
		else
		{
			int Local = -1;
			int aPos[2] = {1, 2};
			const CNetObj_PlayerInfo *apPlayerInfo[2] = {nullptr, nullptr};
			int i = 0;
			for(int t = 0; t < 2 && i < MAX_CLIENTS && GameClient()->m_Snap.m_apInfoByScore[i]; ++i)
			{
				if(GameClient()->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
				{
					apPlayerInfo[t] = GameClient()->m_Snap.m_apInfoByScore[i];
					if(apPlayerInfo[t]->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
						Local = t;
					++t;
				}
			}
			// search local player info if not a spectator, nor within top2 scores
			if(Local == -1 && GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS)
			{
				for(; i < MAX_CLIENTS && GameClient()->m_Snap.m_apInfoByScore[i]; ++i)
				{
					if(GameClient()->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
						++aPos[1];
					if(GameClient()->m_Snap.m_apInfoByScore[i]->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
					{
						apPlayerInfo[1] = GameClient()->m_Snap.m_apInfoByScore[i];
						Local = 1;
						break;
					}
				}
			}
			char aScore[2][16];
			for(int t = 0; t < 2; ++t)
			{
				if(apPlayerInfo[t])
				{
					if(Client()->IsSixup() && GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE)
						str_time((int64_t)absolute(apPlayerInfo[t]->m_Score) / 10, TIME_MINS_CENTISECS, aScore[t], sizeof(aScore[t]));
					else if(GameClient()->m_GameInfo.m_TimeScore)
					{
						CGameClient::CClientData &ClientData = GameClient()->m_aClients[apPlayerInfo[t]->m_ClientId];
						if(GameClient()->m_ReceivedDDNetPlayerFinishTimes && ClientData.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
						{
							int64_t TimeSeconds = static_cast<int64_t>(absolute(ClientData.m_FinishTimeSeconds));
							int64_t TimeMillis = TimeSeconds * 1000 + (absolute(ClientData.m_FinishTimeMillis) % 1000);

							str_time(TimeMillis / 10, TIME_HOURS, aScore[t], sizeof(aScore[t]));
						}
						else if(apPlayerInfo[t]->m_Score != FinishTime::NOT_FINISHED_TIMESCORE)
						{
							str_time((int64_t)absolute(apPlayerInfo[t]->m_Score) * 100, TIME_HOURS, aScore[t], sizeof(aScore[t]));
						}
						else
							aScore[t][0] = 0;
					}
					else
						str_format(aScore[t], sizeof(aScore[t]), "%d", apPlayerInfo[t]->m_Score);
				}
				else
					aScore[t][0] = 0;
			}

			bool RecreateScores = str_comp(aScore[0], m_aScoreInfo[0].m_aScoreText) != 0 || str_comp(aScore[1], m_aScoreInfo[1].m_aScoreText) != 0 || m_LastLocalClientId != GameClient()->m_Snap.m_LocalClientId;
			m_LastLocalClientId = GameClient()->m_Snap.m_LocalClientId;

			bool RecreateRect = ForceScoreInfoInit;
			for(int t = 0; t < 2; t++)
			{
				if(m_aScoreInfo[t].m_RoundRectCorners != ScoreHudCorners)
					RecreateRect = true;
				if(RecreateScores)
				{
					m_aScoreInfo[t].m_ScoreTextWidth = TextRender()->TextWidth(14.0f, aScore[t], -1, -1.0f);
					str_copy(m_aScoreInfo[t].m_aScoreText, aScore[t]);
					RecreateRect = true;
				}

				if(apPlayerInfo[t])
				{
					int Id = apPlayerInfo[t]->m_ClientId;
					if(Id >= 0 && Id < MAX_CLIENTS)
					{
						char aNameBuf[MAX_NAME_LENGTH];
						GameClient()->FormatStreamerName(Id, aNameBuf, sizeof(aNameBuf));
						const char *pName = aNameBuf;
						if(str_comp(pName, m_aScoreInfo[t].m_aPlayerNameText) != 0)
							RecreateRect = true;
					}
				}
				else
				{
					if(m_aScoreInfo[t].m_aPlayerNameText[0] != 0)
						RecreateRect = true;
				}

				char aBuf[16];
				str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
				if(str_comp(aBuf, m_aScoreInfo[t].m_aRankText) != 0)
					RecreateRect = true;
			}

			static float s_TextWidth10 = TextRender()->TextWidth(14.0f, "10", -1, -1.0f);
			float ScoreWidthMax = maximum(maximum(m_aScoreInfo[0].m_ScoreTextWidth, m_aScoreInfo[1].m_ScoreTextWidth), s_TextWidth10);
			float Split = 3.0f, ImageSize = 16.0f, PosSize = 16.0f;

			for(int t = 0; t < 2; t++)
			{
				// draw box
				if(RecreateRect)
				{
					Graphics()->DeleteQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex);

					if(t == Local)
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.25f);
					else
						Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.25f);
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(m_Width - ScoreWidthMax - ImageSize - 2 * Split - PosSize, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split + PosSize, ScoreSingleBoxHeight, 5.0f, ScoreHudCorners);
					m_aScoreInfo[t].m_RoundRectCorners = ScoreHudCorners;
				}
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
				{
					Ui()->RenderGaussianBlur({m_Width - ScoreWidthMax - ImageSize - 2 * Split - PosSize, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split + PosSize, ScoreSingleBoxHeight}, 1.0f, ScoreHudCorners, 5.0f);
					Graphics()->TextureClear();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);
				}

				if(RecreateScores)
				{
					CTextCursor Cursor;
					Cursor.SetPosition(vec2(m_Width - ScoreWidthMax + (ScoreWidthMax - m_aScoreInfo[t].m_ScoreTextWidth) - Split, StartY + t * 20 + (18.f - 14.f) / 2.f));
					Cursor.m_FontSize = 14.0f;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, &Cursor, aScore[t]);
				}
				// draw score
				if(m_aScoreInfo[t].m_TextScoreContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextScoreContainerIndex, TColor, TOutlineColor);
				}

				if(apPlayerInfo[t])
				{
					// draw name
					int Id = apPlayerInfo[t]->m_ClientId;
					if(Id >= 0 && Id < MAX_CLIENTS)
					{
						char aNameBuf[MAX_NAME_LENGTH];
						GameClient()->FormatStreamerName(Id, aNameBuf, sizeof(aNameBuf));
						const char *pName = aNameBuf;
						if(RecreateRect)
						{
							str_copy(m_aScoreInfo[t].m_aPlayerNameText, pName);

							CTextCursor Cursor;
							Cursor.SetPosition(vec2(minimum(m_Width - TextRender()->TextWidth(8.0f, pName) - 1.0f, m_Width - ScoreWidthMax - ImageSize - 2 * Split - PosSize), StartY + (t + 1) * 20.0f - 2.0f));
							Cursor.m_FontSize = 8.0f;
							TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, &Cursor, pName);
						}

						if(m_aScoreInfo[t].m_OptionalNameTextContainerIndex.Valid())
						{
							ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
							ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
							TextRender()->RenderTextContainer(m_aScoreInfo[t].m_OptionalNameTextContainerIndex, TColor, TOutlineColor);
						}

						// draw tee
						CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
						TeeInfo.m_Size = ScoreSingleBoxHeight;

						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(m_Width - ScoreWidthMax - TeeInfo.m_Size / 2 - Split, StartY + (t * 20) + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);

						RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
					}
				}
				else
				{
					m_aScoreInfo[t].m_aPlayerNameText[0] = 0;
				}

				// draw position
				char aBuf[16];
				str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
				if(RecreateRect)
				{
					str_copy(m_aScoreInfo[t].m_aRankText, aBuf);

					CTextCursor Cursor;
					Cursor.SetPosition(vec2(m_Width - ScoreWidthMax - ImageSize - Split - PosSize, StartY + t * 20 + (18.f - 10.f) / 2.f));
					Cursor.m_FontSize = 10.0f;
					TextRender()->RecreateTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, &Cursor, aBuf);
				}
				if(m_aScoreInfo[t].m_TextRankContainerIndex.Valid())
				{
					ColorRGBA TColor(1.f, 1.f, 1.f, 1.f);
					ColorRGBA TOutlineColor(0.f, 0.f, 0.f, 0.3f);
					TextRender()->RenderTextContainer(m_aScoreInfo[t].m_TextRankContainerIndex, TColor, TOutlineColor);
				}

				StartY += 8.0f;
			}
		}
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
	}
}

void CHud::RenderWarmupTimer()
{
	// render warmup timer
	const bool RaceTime = (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) != 0;
	if(GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0 || RaceTime)
		return;

	char aBuf[256];
	float FontSize = 20.0f;
	float w = TextRender()->TextWidth(FontSize, Localize("Warmup"), -1, -1.0f);
	const int Seconds = GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer / Client()->GameTickSpeed();
	if(Seconds < 5)
		str_format(aBuf, sizeof(aBuf), "%d.%d", Seconds, (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer * 10 / Client()->GameTickSpeed()) % 10);
	else
		str_format(aBuf, sizeof(aBuf), "%d", Seconds);
	const float LabelWidth = w;
	w = TextRender()->TextWidth(FontSize, Seconds < 5 ? "0.0" : aBuf, -1, -1.0f);
	const float MaxWidth = maximum(LabelWidth, w);
	const float BaseX = 150.0f * Graphics()->ScreenAspect() - MaxWidth / 2.0f;
	const float BaseY = 50.0f;
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::WarmupTimer, {BaseX, BaseY, MaxWidth, 45.0f});

	TextRender()->Text(150 * Graphics()->ScreenAspect() + -LabelWidth / 2, BaseY, FontSize, Localize("Warmup"), -1.0f);
	TextRender()->Text(150 * Graphics()->ScreenAspect() + -w / 2, BaseY + 25.0f, FontSize, aBuf, -1.0f);

	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

namespace
{
	struct SHudDummyMiniViewState
	{
		bool m_HasSignal = false;
		char m_aPlaceholderTitle[48] = {};
		char m_aPlaceholderSubtitle[96] = {};
		ColorRGBA m_TargetAccent = ColorRGBA(0.35f, 0.78f, 1.0f, 1.0f);
	};

	bool IsDummyMiniViewPredictedLocalTarget(const CGameClient &GameClient, int MiniViewClientId)
	{
		return GameClient.PredictDummy() &&
		       MiniViewClientId >= 0 &&
		       MiniViewClientId == GameClient.m_aLocalIds[!g_Config.m_ClDummy];
	}

	bool TryGetDummyMiniViewTargetPos(const CGameClient &GameClient, const IClient &Client, int MiniViewClientId, vec2 &OutPos, bool *pFromSnapshot = nullptr)
	{
		if(pFromSnapshot != nullptr)
			*pFromSnapshot = false;

		if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
			return false;

		const CGameClient::CClientData &MiniClient = GameClient.m_aClients[MiniViewClientId];
		if(!MiniClient.m_Active)
			return false;

		if(GameClient.m_Snap.m_aCharacters[MiniViewClientId].m_Active)
		{
			OutPos = MiniClient.m_RenderPos;
			if(MiniClient.m_RenderCur.m_Tick < 0 && MiniClient.m_RenderPrev.m_Tick < 0)
				OutPos = vec2(MiniClient.m_Snapped.m_X, MiniClient.m_Snapped.m_Y);
			if(pFromSnapshot != nullptr)
				*pFromSnapshot = true;
			return true;
		}

		if(MiniViewClientId == GameClient.m_Snap.m_LocalClientId)
		{
			OutPos = GameClient.m_LocalCharacterPos;
			return true;
		}

		if(IsDummyMiniViewPredictedLocalTarget(GameClient, MiniViewClientId))
		{
			OutPos = mix(MiniClient.m_PrevPredicted.m_Pos, MiniClient.m_Predicted.m_Pos, Client.PredIntraGameTick(g_Config.m_ClDummy));
			return true;
		}

		return false;
	}

	void RenderHudEllipsizedText(ITextRender *pTextRender, float X, float Y, float FontSize, float MaxWidth, const char *pText)
	{
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
			return;

		const float TextWidth = std::round(pTextRender->TextBoundingBox(FontSize, pText).m_W);
		if(TextWidth <= MaxWidth + 0.01f)
		{
			pTextRender->Text(X, Y, FontSize, pText, -1.0f);
			return;
		}

		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = MaxWidth;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.SetPosition(vec2(X, Y));
		pTextRender->TextEx(&Cursor, pText);
	}

	SHudDummyMiniViewState BuildHudDummyMiniViewState(const CGameClient &GameClient, const IClient &Client, bool Preview, int DummyClientId, int MiniViewClientId)
	{
		SHudDummyMiniViewState State;
		const bool TargetIsDummy = MiniViewClientId >= 0 ? (MiniViewClientId == DummyClientId) : !g_Config.m_ClDummy;
		State.m_TargetAccent = TargetIsDummy ? ColorRGBA(0.35f, 0.78f, 1.0f, 1.0f) : ColorRGBA(1.0f, 0.74f, 0.34f, 1.0f);
		str_copy(State.m_aPlaceholderTitle, Localize("Dummy mini view"), sizeof(State.m_aPlaceholderTitle));

		if(MiniViewClientId >= 0 && MiniViewClientId < MAX_CLIENTS)
		{
			vec2 TargetPos;
			State.m_HasSignal = TryGetDummyMiniViewTargetPos(GameClient, Client, MiniViewClientId, TargetPos);
		}

		if(Preview)
		{
			str_copy(State.m_aPlaceholderSubtitle, Localize("HUD editor preview"), sizeof(State.m_aPlaceholderSubtitle));
		}
		else if(!Client.DummyConnected())
		{
			str_copy(State.m_aPlaceholderSubtitle, Localize("Connect dummy to enable"), sizeof(State.m_aPlaceholderSubtitle));
		}
		else
		{
			if(!State.m_HasSignal)
				str_copy(State.m_aPlaceholderSubtitle, Localize("Waiting for snapshot"), sizeof(State.m_aPlaceholderSubtitle));
		}

		return State;
	}

	bool IsDummyMiniViewTargetOutsideCurrentView(const CGameClient &GameClient, IGraphics *pGraphics, int MiniViewClientId)
	{
		if(!g_Config.m_QmDummyMiniViewAuto || pGraphics == nullptr)
			return true;
		if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
			return true;

		vec2 TargetPos;
		if(!TryGetDummyMiniViewTargetPos(GameClient, *GameClient.Client(), MiniViewClientId, TargetPos))
			return true;

		float ViewWidth = 0.0f;
		float ViewHeight = 0.0f;
		pGraphics->CalcScreenParams(pGraphics->GameScreenAspect(), GameClient.m_Camera.m_Zoom, &ViewWidth, &ViewHeight);

		const vec2 Center = GameClient.m_Camera.m_Center;
		constexpr float TileMargin = 32.0f;
		const float Left = Center.x - ViewWidth * 0.5f - TileMargin;
		const float Right = Center.x + ViewWidth * 0.5f + TileMargin;
		const float Top = Center.y - ViewHeight * 0.5f - TileMargin;
		const float Bottom = Center.y + ViewHeight * 0.5f + TileMargin;

		return TargetPos.x < Left || TargetPos.x > Right || TargetPos.y < Top || TargetPos.y > Bottom;
	}
}

bool CHud::GetDummyMiniMapRect(float &X, float &Y, float &W, float &H) const
{
	if(!g_Config.m_QmDummyMiniView)
		return false;
	if(GameClient()->m_HudEditor.IsActive())
	{
		const float SizeScale = g_Config.m_QmDummyMiniViewSize / 100.0f;
		const float MaxHeight = 80.0f * SizeScale;
		const float Margin = 5.0f;
		const float Aspect = m_Width / m_Height;
		H = MaxHeight;
		W = MaxHeight * Aspect;
		if(W > m_Width - Margin * 2.0f)
		{
			W = m_Width - Margin * 2.0f;
			H = W / Aspect;
		}
		X = m_Width - Margin - W;
		Y = Margin;
		return true;
	}
	if(!Client()->DummyConnected())
		return false;

	const int DummyClientId = GameClient()->m_aLocalIds[1];
	const int MainClientId = GameClient()->m_aLocalIds[0];
	if(DummyClientId < 0 || DummyClientId >= MAX_CLIENTS)
		return false;
	const int MiniViewClientId = g_Config.m_ClDummy ? MainClientId : DummyClientId;
	if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
		return false;
	const CGameClient::CClientData &MiniClient = GameClient()->m_aClients[MiniViewClientId];
	if(!MiniClient.m_Active)
		return false;
	vec2 TargetPos;
	if(!TryGetDummyMiniViewTargetPos(*GameClient(), *Client(), MiniViewClientId, TargetPos))
		return false;
	if(!IsDummyMiniViewTargetOutsideCurrentView(*GameClient(), Graphics(), MiniViewClientId))
		return false;

	const int MapW = GameClient()->Collision()->GetWidth();
	const int MapH = GameClient()->Collision()->GetHeight();
	if(MapW <= 0 || MapH <= 0)
		return false;

	const float SizeScale = g_Config.m_QmDummyMiniViewSize / 100.0f;
	const float MaxHeight = 80.0f * SizeScale;
	const float Margin = 5.0f;
	const float Aspect = m_Width / m_Height;

	H = MaxHeight;
	W = MaxHeight * Aspect;
	if(W > m_Width - Margin * 2.0f)
	{
		W = m_Width - Margin * 2.0f;
		H = W / Aspect;
	}

	X = m_Width - Margin - W;
	Y = Margin;

	const SHudFrozenHudRect FrozenHudRect = BuildFrozenHudRect(*GameClient(), m_Width, m_Height, GetTopIslandAvoidanceRight());
	if(FrozenHudRect.m_Visible)
	{
		const bool OverlapsFrozenHudX = X < FrozenHudRect.m_X + FrozenHudRect.m_W && X + W > FrozenHudRect.m_X;
		const bool OverlapsFrozenHudY = Y < FrozenHudRect.m_Y + FrozenHudRect.m_H;
		if(OverlapsFrozenHudX && OverlapsFrozenHudY)
			Y = minimum(m_Height - H - Margin, FrozenHudRect.m_Y + FrozenHudRect.m_H + 4.0f);
	}
	return true;
}

void CHud::RenderDummyMiniMap()
{
	float MiniX = 0.0f;
	float MiniY = 0.0f;
	float MiniW = 0.0f;
	float MiniH = 0.0f;
	if(!GetDummyMiniMapRect(MiniX, MiniY, MiniW, MiniH))
	{
		if(m_DummyMiniViewRenderTarget.IsValid() && (!g_Config.m_QmDummyMiniView || !IsVulkanBackend(Graphics())))
			DestroyDummyMiniViewRenderTarget();
		return;
	}
	const bool UseOffscreenTarget = IsVulkanBackend(Graphics());
	if(!UseOffscreenTarget && m_DummyMiniViewRenderTarget.IsValid())
		DestroyDummyMiniViewRenderTarget();
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::DummyMiniMap, {MiniX, MiniY, MiniW, MiniH}, true, false);
	if(HudEditorScope.m_TargetRect.w > 0.0f && HudEditorScope.m_TargetRect.h > 0.0f)
	{
		MiniX = HudEditorScope.m_TargetRect.x;
		MiniY = HudEditorScope.m_TargetRect.y;
		MiniW = HudEditorScope.m_TargetRect.w;
		MiniH = HudEditorScope.m_TargetRect.h;
	}

	const int DummyClientId = GameClient()->m_aLocalIds[1];
	const int MainClientId = GameClient()->m_aLocalIds[0];
	int MiniViewClientId = g_Config.m_ClDummy ? MainClientId : DummyClientId;
	if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
		MiniViewClientId = -1;

	const SHudDummyMiniViewState ViewState = BuildHudDummyMiniViewState(*GameClient(), *Client(), GameClient()->m_HudEditor.IsActive(), DummyClientId, MiniViewClientId);

	const float Radius = std::clamp(MiniH * 0.11f, 5.0f, 7.5f);
	DrawSmoothRoundedRect(Graphics(), MiniX + 0.8f, MiniY + 1.2f, MiniW, MiniH, Radius, ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), HudEditorScope.m_Corners);
	DrawSmoothRoundedRect(Graphics(), MiniX, MiniY, MiniW, MiniH, Radius, ColorRGBA(0.02f, 0.03f, 0.05f, 0.92f), HudEditorScope.m_Corners);
	DrawSmoothRoundedRect(Graphics(), MiniX + 0.75f, MiniY + 0.75f, maximum(0.0f, MiniW - 1.5f), maximum(0.0f, MiniH - 1.5f), maximum(0.0f, Radius - 0.55f), ViewState.m_TargetAccent.WithAlpha(0.16f), HudEditorScope.m_Corners);

	const float FrameInset = 1.45f;
	const float FrameX = MiniX + FrameInset;
	const float FrameY = MiniY + FrameInset;
	const float FrameW = MiniW - FrameInset * 2.0f;
	const float FrameH = MiniH - FrameInset * 2.0f;
	const float FrameRadius = maximum(0.0f, Radius - 1.0f);
	DrawSmoothRoundedRect(Graphics(), FrameX, FrameY, FrameW, FrameH, FrameRadius, ColorRGBA(0.07f, 0.09f, 0.13f, 0.96f));
	DrawSmoothRoundedRect(Graphics(), FrameX + 0.65f, FrameY + 0.65f, maximum(0.0f, FrameW - 1.3f), maximum(0.0f, minimum(FrameH - 1.3f, FrameH * 0.48f)), maximum(0.0f, FrameRadius - 0.45f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f), IGraphics::CORNER_T);

	const float ContentInset = 1.9f;
	const float InnerX = FrameX + ContentInset;
	const float InnerY = FrameY + ContentInset;
	const float InnerW = FrameW - ContentInset * 2.0f;
	const float InnerH = FrameH - ContentInset * 2.0f;
	const float InnerRadius = maximum(0.0f, FrameRadius - 1.1f);
	if(InnerW <= 0.0f || InnerH <= 0.0f)
	{
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
		return;
	}

	DrawSmoothRoundedRect(Graphics(), InnerX, InnerY, InnerW, InnerH, InnerRadius, ColorRGBA(0.03f, 0.04f, 0.06f, 0.92f));

	if(ViewState.m_HasSignal && MiniViewClientId >= 0)
	{
		Graphics()->TextureClear();

		float SavedX0 = 0.0f;
		float SavedY0 = 0.0f;
		float SavedX1 = 0.0f;
		float SavedY1 = 0.0f;
		Graphics()->GetScreen(&SavedX0, &SavedY0, &SavedX1, &SavedY1);

		const int ScreenW = Graphics()->ScreenWidth();
		const int ScreenH = Graphics()->ScreenHeight();
		const float XScale = ScreenW / m_Width;
		const float YScale = ScreenH / m_Height;

		const int ViewX = (int)std::round(InnerX * XScale);
		const int ViewY = (int)std::round((m_Height - (InnerY + InnerH)) * YScale);
		const int ViewW = maximum(1, (int)std::round(InnerW * XScale));
		const int ViewH = maximum(1, (int)std::round(InnerH * YScale));

		int ClampedX = maximum(0, minimum(ViewX, ScreenW - 1));
		int ClampedY = maximum(0, minimum(ViewY, ScreenH - 1));
		int ClampedW = minimum(ViewW, ScreenW - ClampedX);
		int ClampedH = minimum(ViewH, ScreenH - ClampedY);
		if(ClampedW > 0 && ClampedH > 0)
		{
			const ColorRGBA MiniClearColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClOverlayEntities ? g_Config.m_ClBackgroundEntitiesColor : g_Config.m_ClBackgroundColor));
			bool RenderingMiniView = false;
			if(UseOffscreenTarget)
			{
				const bool TargetSizeChanged = ClampedW != m_DummyMiniViewRenderTargetWidth || ClampedH != m_DummyMiniViewRenderTargetHeight;
				if(Graphics()->IsRenderTargetSupported() && (TargetSizeChanged || !m_DummyMiniViewRenderTarget.IsValid()))
				{
					DestroyDummyMiniViewRenderTarget();
					m_DummyMiniViewRenderTarget = Graphics()->CreateRenderTarget(ClampedW, ClampedH);
					if(m_DummyMiniViewRenderTarget.IsValid())
					{
						m_DummyMiniViewRenderTargetWidth = ClampedW;
						m_DummyMiniViewRenderTargetHeight = ClampedH;
					}
				}
				RenderingMiniView = m_DummyMiniViewRenderTarget.IsValid() && Graphics()->BeginRenderTarget(m_DummyMiniViewRenderTarget, MiniClearColor);
				if(!RenderingMiniView)
				{
					static bool s_LoggedRenderTargetFailure = false;
					if(!s_LoggedRenderTargetFailure)
					{
						dbg_msg("hud", "Vulkan dummy mini view render target unavailable: %s", Graphics()->RenderTargetSupportReason());
						s_LoggedRenderTargetFailure = true;
					}
				}
			}
			else
			{
				Graphics()->FlushVertices();
				Graphics()->ClipDisable();
				Graphics()->UpdateViewport(ClampedX, ClampedY, ClampedW, ClampedH, false);
				RenderingMiniView = true;
			}

			if(RenderingMiniView)
			{
				Graphics()->MapScreen(0.0f, 0.0f, 100.0f, 100.0f);
				Graphics()->DrawRect(0.0f, 0.0f, 100.0f, 100.0f, MiniClearColor, IGraphics::CORNER_NONE, 0.0f);

				const CGameClient::CClientData &MiniClient = GameClient()->m_aClients[MiniViewClientId];
				vec2 MiniPos(0.0f, 0.0f);
				bool HasSnapshotSignal = false;
				if(!TryGetDummyMiniViewTargetPos(*GameClient(), *Client(), MiniViewClientId, MiniPos, &HasSnapshotSignal))
				{
					Graphics()->FlushVertices();
					Graphics()->ClipDisable();
					if(UseOffscreenTarget)
						Graphics()->EndRenderTarget();
					else
						Graphics()->UpdateViewport(0, 0, ScreenW, ScreenH, false);
					Graphics()->MapScreen(SavedX0, SavedY0, SavedX1, SavedY1);
					GameClient()->m_HudEditor.EndTransform(HudEditorScope);
					return;
				}

				const float ZoomScale = maximum(0.1f, g_Config.m_QmDummyMiniViewZoom / 100.0f);
				const float MiniZoom = GameClient()->m_Camera.m_Zoom * ZoomScale;

				bool RenderedBackground = false;
				if(g_Config.m_ClOverlayEntities == 100)
					RenderedBackground = GameClient()->m_Background.RenderCustom(MiniPos, MiniZoom);
				if(!RenderedBackground)
					GameClient()->m_MapLayersBackground.RenderCustom(MiniPos, MiniZoom);

				float aPoints[4];
				Graphics()->MapScreenToWorld(MiniPos.x, MiniPos.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->GameScreenAspect(), MiniZoom, aPoints);
				Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

				// Render the monitor view without spawning new effects or sounds.
				const bool PrevMiniRender = GameClient()->IsRenderingDummyMiniMap();
				GameClient()->SetRenderingDummyMiniMap(true);

				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_PROJECTILE_TRAIL);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_TRAIL_EXTRA);
				GameClient()->m_Items.OnRender();
				GameClient()->m_Players.OnRender();
				GameClient()->m_MapLayersForeground.RenderCustom(MiniPos, MiniZoom);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_EXPLOSIONS);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_EXTRA);
				GameClient()->m_Particles.RenderGroup(CParticles::GROUP_GENERAL);

				if(!HasSnapshotSignal)
				{
					CTeeRenderInfo TeeInfo = MiniClient.m_RenderInfo;
					const CAnimState *pIdleState = CAnimState::GetIdle();
					vec2 OffsetToMid;
					CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
					RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(MiniPos.x, MiniPos.y + OffsetToMid.y));
				}

				GameClient()->SetRenderingDummyMiniMap(PrevMiniRender);

				Graphics()->FlushVertices();
				Graphics()->ClipDisable();
				if(UseOffscreenTarget)
					Graphics()->EndRenderTarget();
				else
					Graphics()->UpdateViewport(0, 0, ScreenW, ScreenH, false);
				Graphics()->MapScreen(SavedX0, SavedY0, SavedX1, SavedY1);

				if(UseOffscreenTarget)
				{
					IGraphics::SRenderTargetDrawParams DrawParams;
					DrawParams.m_X = InnerX;
					DrawParams.m_Y = InnerY;
					DrawParams.m_W = InnerW;
					DrawParams.m_H = InnerH;
					DrawParams.m_Corners = HudEditorScope.m_Corners;
					DrawParams.m_Rounding = InnerRadius;
					DrawParams.m_V0 = 0.0f;
					DrawParams.m_V1 = 1.0f;
					Graphics()->DrawRenderTarget(m_DummyMiniViewRenderTarget, DrawParams);
				}
			}
		}
	}

	if(!ViewState.m_HasSignal)
	{
		const unsigned int PrevFlags = TextRender()->GetRenderFlags();
		const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
		const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.36f);

		const float PlaceholderIconSize = std::clamp(minimum(InnerW, InnerH) * 0.14f, 7.0f, 11.0f);
		const float PlaceholderTitleSize = std::clamp(MiniH * 0.078f, 5.3f, 6.6f);
		const float PlaceholderBodySize = std::clamp(MiniH * 0.062f, 4.6f, 5.4f);
		const vec2 PlaceholderCenter(InnerX + InnerW * 0.5f, InnerY + InnerH * 0.52f);

		DrawSmoothCircle(Graphics(), vec2(PlaceholderCenter.x, PlaceholderCenter.y - 7.5f), PlaceholderIconSize * 0.9f, ViewState.m_TargetAccent.WithAlpha(0.16f));
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.68f);
		const float CameraWidth = TextRender()->TextWidth(PlaceholderIconSize, FontIcons::FONT_ICON_CAMERA);
		TextRender()->Text(PlaceholderCenter.x - CameraWidth * 0.5f, PlaceholderCenter.y - PlaceholderIconSize - 8.0f, PlaceholderIconSize, FontIcons::FONT_ICON_CAMERA, -1.0f);

		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.90f);
		const float TitleWidth = TextRender()->TextWidth(PlaceholderTitleSize, ViewState.m_aPlaceholderTitle);
		TextRender()->Text(PlaceholderCenter.x - TitleWidth * 0.5f, PlaceholderCenter.y - 3.0f, PlaceholderTitleSize, ViewState.m_aPlaceholderTitle, -1.0f);

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.64f);
		const float SubtitleMaxWidth = maximum(0.0f, InnerW - 16.0f);
		RenderHudEllipsizedText(TextRender(), PlaceholderCenter.x - SubtitleMaxWidth * 0.5f, PlaceholderCenter.y + 4.2f, PlaceholderBodySize, SubtitleMaxWidth, ViewState.m_aPlaceholderSubtitle);

		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->SetRenderFlags(PrevFlags);
		TextRender()->TextColor(PrevTextColor);
		TextRender()->TextOutlineColor(PrevOutlineColor);
	}

	GameClient()->m_HudEditor.UpdateVisibleRect(EHudEditorElement::DummyMiniMap, {MiniX, MiniY, MiniW, MiniH});
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderTextInfo()
{
	int Showfps = g_Config.m_ClShowfps;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		Showfps = 0;
#endif
	const bool Showpred = g_Config.m_ClShowpred && Client()->State() != IClient::STATE_DEMOPLAYBACK;
	const bool UseV2TextInfoLayout = true;
	CUiV2AnimationRuntime *pAnimRuntime = nullptr;
	if(UseV2TextInfoLayout)
		pAnimRuntime = &GameClient()->UiRuntimeV2()->AnimRuntime();

	float MiniX = 0.0f;
	float MiniY = 0.0f;
	float MiniW = 0.0f;
	float MiniH = 0.0f;
	const bool HasMiniMap = GetDummyMiniMapRect(MiniX, MiniY, MiniW, MiniH);
	SHudTextInfoV2AnimState &AnimState = m_TextInfoV2AnimState;
	if(!UseV2TextInfoLayout)
	{
		AnimState.m_FpsPositionInitialized = false;
		AnimState.m_PredPositionInitialized = false;
		AnimState.m_LossPositionInitialized = false;
		AnimState.m_AlphaInitialized = false;
	}

	const uint64_t FpsNode = HudTextInfoNodeKey("fps");
	const uint64_t PredNode = HudTextInfoNodeKey("pred");
	const uint64_t LossNode = HudTextInfoNodeKey("loss");
	const bool ShowLoss = g_Config.m_ClShowPacketLoss && Client()->State() != IClient::STATE_DEMOPLAYBACK;
	if(UseV2TextInfoLayout && pAnimRuntime != nullptr && !AnimState.m_AlphaInitialized)
	{
		AnimState.m_FpsTargetAlpha = Showfps ? 1.0f : 0.0f;
		AnimState.m_PredTargetAlpha = Showpred ? 1.0f : 0.0f;
		AnimState.m_LossTargetAlpha = ShowLoss ? 1.0f : 0.0f;
		SetUiPresentationStateValue(*pAnimRuntime, FpsNode, EUiAnimProperty::ALPHA, AnimState.m_FpsTargetAlpha);
		SetUiPresentationStateValue(*pAnimRuntime, PredNode, EUiAnimProperty::ALPHA, AnimState.m_PredTargetAlpha);
		SetUiPresentationStateValue(*pAnimRuntime, LossNode, EUiAnimProperty::ALPHA, AnimState.m_LossTargetAlpha);
		AnimState.m_AlphaInitialized = true;
	}

	char aFpsBuf[16] = {0};
	char aPredBuf[64] = {0};
	char aLossBuf[16] = {0};
	constexpr float TextInfoFontSize = 10.0f;
	float FpsWidth = 0.0f;
	float PredWidth = 0.0f;
	float LossWidth = 0.0f;
	int DigitIndex = 0;
	if(Showfps)
	{
		const int FramesPerSecond = round_to_int(1.0f / Client()->FrameTimeAverage());
		str_format(aFpsBuf, sizeof(aFpsBuf), "%d", FramesPerSecond);

		static float s_TextWidth0 = TextRender()->TextWidth(TextInfoFontSize, "0", -1, -1.0f);
		static float s_TextWidth00 = TextRender()->TextWidth(TextInfoFontSize, "00", -1, -1.0f);
		static float s_TextWidth000 = TextRender()->TextWidth(TextInfoFontSize, "000", -1, -1.0f);
		static float s_TextWidth0000 = TextRender()->TextWidth(TextInfoFontSize, "0000", -1, -1.0f);
		static float s_TextWidth00000 = TextRender()->TextWidth(TextInfoFontSize, "00000", -1, -1.0f);
		static const float s_aTextWidth[5] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000};

		DigitIndex = GetDigitsIndex(FramesPerSecond, 4);
		FpsWidth = s_aTextWidth[DigitIndex];
		str_copy(AnimState.m_aLastFpsText, aFpsBuf);
		AnimState.m_LastFpsWidth = FpsWidth;
	}
	if(Showpred)
	{
		str_format(aPredBuf, sizeof(aPredBuf), "%d", Client()->GetPredictionTime());
		PredWidth = TextRender()->TextWidth(TextInfoFontSize, aPredBuf, -1, -1.0f);
		str_copy(AnimState.m_aLastPredText, aPredBuf);
		AnimState.m_LastPredWidth = PredWidth;
	}
	const float PacketLoss = Client()->PacketLoss();
	const ColorRGBA PredictionMarginColor = GetPredictionMarginColor(Client()->PredictionMarginState());
	if(ShowLoss)
	{
		str_format(aLossBuf, sizeof(aLossBuf), "%.1f%%", PacketLoss);
		LossWidth = TextRender()->TextWidth(TextInfoFontSize, aLossBuf, -1, -1.0f);
		str_copy(AnimState.m_aLastLossText, aLossBuf);
		AnimState.m_LastLossWidth = LossWidth;
	}

	float FpsAlpha = Showfps ? 1.0f : 0.0f;
	float PredAlpha = Showpred ? 1.0f : 0.0f;
	float LossAlpha = ShowLoss ? 1.0f : 0.0f;
	if(UseV2TextInfoLayout && pAnimRuntime != nullptr)
	{
		FpsAlpha = ResolveAnimatedLayoutValue(*pAnimRuntime, FpsNode, EUiAnimProperty::ALPHA, Showfps ? 1.0f : 0.0f, AnimState.m_FpsTargetAlpha);
		PredAlpha = ResolveAnimatedLayoutValue(*pAnimRuntime, PredNode, EUiAnimProperty::ALPHA, Showpred ? 1.0f : 0.0f, AnimState.m_PredTargetAlpha);
		LossAlpha = ResolveAnimatedLayoutValue(*pAnimRuntime, LossNode, EUiAnimProperty::ALPHA, ShowLoss ? 1.0f : 0.0f, AnimState.m_LossTargetAlpha);
	}

	const bool RenderFps = Showfps || (UseV2TextInfoLayout && FpsAlpha > 0.01f && AnimState.m_aLastFpsText[0] != '\0');
	const bool RenderPred = Showpred || (UseV2TextInfoLayout && PredAlpha > 0.01f && AnimState.m_aLastPredText[0] != '\0');
	const bool RenderLoss = ShowLoss || (UseV2TextInfoLayout && LossAlpha > 0.01f && AnimState.m_aLastLossText[0] != '\0');
	const float DisplayFpsWidth = Showfps ? FpsWidth : (RenderFps ? AnimState.m_LastFpsWidth : 0.0f);
	const float DisplayPredWidth = Showpred ? PredWidth : (RenderPred ? AnimState.m_LastPredWidth : 0.0f);
	const float DisplayLossWidth = ShowLoss ? LossWidth : (RenderLoss ? AnimState.m_LastLossWidth : 0.0f);
	const char *pFpsText = Showfps ? aFpsBuf : AnimState.m_aLastFpsText;
	const char *pPredText = Showpred ? aPredBuf : AnimState.m_aLastPredText;
	const char *pLossText = ShowLoss ? aLossBuf : AnimState.m_aLastLossText;
	const bool UseMiniLayout = HasMiniMap && (RenderFps || RenderPred || RenderLoss);

	SHudTextInfoLayout V2Layout;
	if(UseV2TextInfoLayout)
	{
		V2Layout = ComputeHudTextInfoLayoutV2(RenderFps, RenderPred, RenderLoss, UseMiniLayout, m_Width, MiniX, MiniY, MiniW, MiniH, DisplayFpsWidth, DisplayPredWidth, DisplayLossWidth, m_vTextInfoLayoutChildrenScratch);
	}

	if(UseV2TextInfoLayout && pAnimRuntime != nullptr)
	{
		if(RenderFps && !AnimState.m_FpsPositionInitialized)
		{
			AnimState.m_FpsTargetX = V2Layout.m_FpsX;
			AnimState.m_FpsTargetY = V2Layout.m_FpsY;
			SetUiPresentationStateValue(*pAnimRuntime, FpsNode, EUiAnimProperty::POS_X, AnimState.m_FpsTargetX);
			SetUiPresentationStateValue(*pAnimRuntime, FpsNode, EUiAnimProperty::POS_Y, AnimState.m_FpsTargetY);
			AnimState.m_FpsPositionInitialized = true;
		}
		if(RenderPred && !AnimState.m_PredPositionInitialized)
		{
			AnimState.m_PredTargetX = V2Layout.m_PredX;
			AnimState.m_PredTargetY = V2Layout.m_PredY;
			SetUiPresentationStateValue(*pAnimRuntime, PredNode, EUiAnimProperty::POS_X, AnimState.m_PredTargetX);
			SetUiPresentationStateValue(*pAnimRuntime, PredNode, EUiAnimProperty::POS_Y, AnimState.m_PredTargetY);
			AnimState.m_PredPositionInitialized = true;
		}
		if(RenderLoss && !AnimState.m_LossPositionInitialized)
		{
			AnimState.m_LossTargetX = V2Layout.m_LossX;
			AnimState.m_LossTargetY = V2Layout.m_LossY;
			SetUiPresentationStateValue(*pAnimRuntime, LossNode, EUiAnimProperty::POS_X, AnimState.m_LossTargetX);
			SetUiPresentationStateValue(*pAnimRuntime, LossNode, EUiAnimProperty::POS_Y, AnimState.m_LossTargetY);
			AnimState.m_LossPositionInitialized = true;
		}
	}

	float StartX = 0.0f;
	float TextY = 5.0f;
	float Gap = 0.0f;
	if(UseMiniLayout && !UseV2TextInfoLayout)
	{
		const int TextInfoCount = (RenderFps ? 1 : 0) + (RenderPred ? 1 : 0) + (RenderLoss ? 1 : 0);
		Gap = TextInfoCount > 1 ? 6.0f : 0.0f;
		const float TotalWidth = DisplayFpsWidth + DisplayPredWidth + DisplayLossWidth + Gap * maximum(0, TextInfoCount - 1);
		StartX = MiniX + MiniW - TotalWidth;
		TextY = MiniY + MiniH + 4.0f;
	}
	CHudEditor::STransformScope TextInfoScope;
	if(RenderFps || RenderPred || RenderLoss)
	{
		bool BoundsInitialized = false;
		float BoundsX = 0.0f;
		float BoundsY = 0.0f;
		float BoundsW = 0.0f;
		float BoundsH = 0.0f;
		const auto ExtendBounds = [&](float X, float Y, float W, float H) {
			if(W <= 0.0f || H <= 0.0f)
				return;
			if(!BoundsInitialized)
			{
				BoundsX = X;
				BoundsY = Y;
				BoundsW = W;
				BoundsH = H;
				BoundsInitialized = true;
				return;
			}
			const float Right = maximum(BoundsX + BoundsW, X + W);
			const float Bottom = maximum(BoundsY + BoundsH, Y + H);
			BoundsX = minimum(BoundsX, X);
			BoundsY = minimum(BoundsY, Y);
			BoundsW = Right - BoundsX;
			BoundsH = Bottom - BoundsY;
		};

		float FpsRectX = m_Width - 10.0f - DisplayFpsWidth;
		float FpsRectY = 5.0f;
		if(UseV2TextInfoLayout)
		{
			FpsRectX = V2Layout.m_FpsX;
			FpsRectY = V2Layout.m_FpsY;
		}
		else if(UseMiniLayout)
		{
			FpsRectX = StartX;
			FpsRectY = TextY;
		}

		float PredRectX = m_Width - 10.0f - DisplayPredWidth;
		float PredRectY = RenderFps ? 18.0f : 5.0f;
		if(UseV2TextInfoLayout)
		{
			PredRectX = V2Layout.m_PredX;
			PredRectY = V2Layout.m_PredY;
		}
		else if(UseMiniLayout)
		{
			PredRectX = StartX + (RenderFps ? (DisplayFpsWidth + Gap) : 0.0f);
			PredRectY = TextY;
		}

		float LossRectX = m_Width - 10.0f - DisplayLossWidth;
		float LossRectY = RenderPred ? PredRectY + 13.0f : (RenderFps ? 18.0f : 5.0f);
		if(UseV2TextInfoLayout)
		{
			LossRectX = V2Layout.m_LossX;
			LossRectY = V2Layout.m_LossY;
		}
		else if(UseMiniLayout)
		{
			LossRectX = StartX + (RenderFps ? (DisplayFpsWidth + Gap) : 0.0f) + (RenderPred ? (DisplayPredWidth + Gap) : 0.0f);
			LossRectY = TextY;
		}

		if(RenderFps)
			ExtendBounds(FpsRectX, FpsRectY, DisplayFpsWidth, TextInfoFontSize);
		if(RenderPred)
			ExtendBounds(PredRectX, PredRectY, DisplayPredWidth, TextInfoFontSize);
		if(RenderLoss)
			ExtendBounds(LossRectX, LossRectY, DisplayLossWidth, TextInfoFontSize);
		if(BoundsInitialized)
			TextInfoScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::TextInfo, {BoundsX - 2.0f, BoundsY - 2.0f, BoundsW + 4.0f, BoundsH + 4.0f});
	}

	if(RenderFps)
	{
		CTextCursor Cursor;
		float FpsX = m_Width - 10 - DisplayFpsWidth;
		float FpsY = 5.0f;
		if(UseV2TextInfoLayout)
		{
			if(pAnimRuntime != nullptr)
			{
				FpsX = ResolveAnimatedLayoutValue(*pAnimRuntime, FpsNode, EUiAnimProperty::POS_X, V2Layout.m_FpsX, AnimState.m_FpsTargetX);
				FpsY = ResolveAnimatedLayoutValue(*pAnimRuntime, FpsNode, EUiAnimProperty::POS_Y, V2Layout.m_FpsY, AnimState.m_FpsTargetY);
			}
			else
			{
				FpsX = V2Layout.m_FpsX;
				FpsY = V2Layout.m_FpsY;
			}
		}
		else if(UseMiniLayout)
		{
			FpsX = StartX;
			FpsY = TextY;
		}
		Cursor.SetPosition(vec2(FpsX, FpsY));
		Cursor.m_FontSize = TextInfoFontSize;
		auto OldFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		if(m_FPSTextContainerIndex.Valid())
			TextRender()->RecreateTextContainerSoft(m_FPSTextContainerIndex, &Cursor, pFpsText);
		else
			TextRender()->CreateTextContainer(m_FPSTextContainerIndex, &Cursor, pFpsText);
		TextRender()->SetRenderFlags(OldFlags);
		if(m_FPSTextContainerIndex.Valid())
		{
			ColorRGBA TextColor = TextRender()->DefaultTextColor();
			ColorRGBA TextOutlineColor = TextRender()->DefaultTextOutlineColor();
			if(UseV2TextInfoLayout)
			{
				TextColor.a *= FpsAlpha;
				TextOutlineColor.a *= FpsAlpha;
			}
			TextRender()->RenderTextContainer(m_FPSTextContainerIndex, TextColor, TextOutlineColor);
		}
	}
	if(RenderPred)
	{
		float PredX = m_Width - 10 - DisplayPredWidth;
		float PredY = RenderFps ? 18.0f : 5.0f;
		if(UseV2TextInfoLayout)
		{
			if(pAnimRuntime != nullptr)
			{
				PredX = ResolveAnimatedLayoutValue(*pAnimRuntime, PredNode, EUiAnimProperty::POS_X, V2Layout.m_PredX, AnimState.m_PredTargetX);
				PredY = ResolveAnimatedLayoutValue(*pAnimRuntime, PredNode, EUiAnimProperty::POS_Y, V2Layout.m_PredY, AnimState.m_PredTargetY);
			}
			else
			{
				PredX = V2Layout.m_PredX;
				PredY = V2Layout.m_PredY;
			}
		}
		else if(UseMiniLayout)
		{
			PredX = StartX + (RenderFps ? (DisplayFpsWidth + Gap) : 0.0f);
			PredY = TextY;
		}
		if(UseV2TextInfoLayout)
		{
			ColorRGBA OldColor = TextRender()->GetTextColor();
			ColorRGBA OldOutlineColor = TextRender()->GetTextOutlineColor();
			ColorRGBA PredTextColor = PredictionMarginColor;
			ColorRGBA PredOutlineColor = TextRender()->DefaultTextOutlineColor();
			PredTextColor.a *= PredAlpha;
			PredOutlineColor.a *= PredAlpha;
			TextRender()->TextColor(PredTextColor);
			TextRender()->TextOutlineColor(PredOutlineColor);
			TextRender()->Text(PredX, PredY, TextInfoFontSize, pPredText, -1.0f);
			TextRender()->TextColor(OldColor);
			TextRender()->TextOutlineColor(OldOutlineColor);
		}
		else
		{
			const ColorRGBA OldColor = TextRender()->GetTextColor();
			TextRender()->TextColor(PredictionMarginColor);
			TextRender()->Text(PredX, PredY, TextInfoFontSize, pPredText, -1.0f);
			TextRender()->TextColor(OldColor);
		}
	}
	if(RenderLoss)
	{
		float LossX = m_Width - 10.0f - DisplayLossWidth;
		float LossY = RenderPred ? 31.0f : (RenderFps ? 18.0f : 5.0f);
		if(UseV2TextInfoLayout)
		{
			if(pAnimRuntime != nullptr)
			{
				LossX = ResolveAnimatedLayoutValue(*pAnimRuntime, LossNode, EUiAnimProperty::POS_X, V2Layout.m_LossX, AnimState.m_LossTargetX);
				LossY = ResolveAnimatedLayoutValue(*pAnimRuntime, LossNode, EUiAnimProperty::POS_Y, V2Layout.m_LossY, AnimState.m_LossTargetY);
			}
			else
			{
				LossX = V2Layout.m_LossX;
				LossY = V2Layout.m_LossY;
			}
		}
		else if(UseMiniLayout)
		{
			LossX = StartX + (RenderFps ? (DisplayFpsWidth + Gap) : 0.0f) + (RenderPred ? (DisplayPredWidth + Gap) : 0.0f);
			LossY = TextY;
		}

		ColorRGBA OldColor = TextRender()->GetTextColor();
		ColorRGBA OldOutlineColor = TextRender()->GetTextOutlineColor();
		ColorRGBA LossTextColor = GetPredictionNetworkColor(PacketLoss, Client()->ConnectionProblems());
		ColorRGBA LossOutlineColor = TextRender()->DefaultTextOutlineColor();
		LossTextColor.a *= LossAlpha;
		LossOutlineColor.a *= LossAlpha;
		TextRender()->TextColor(LossTextColor);
		TextRender()->TextOutlineColor(LossOutlineColor);
		TextRender()->Text(LossX, LossY, TextInfoFontSize, pLossText, -1.0f);
		TextRender()->TextColor(OldColor);
		TextRender()->TextOutlineColor(OldOutlineColor);
	}

	GameClient()->m_HudEditor.EndTransform(TextInfoScope);

	if(GameClient()->m_FastPractice.Enabled())
	{
		constexpr const char *pLine1 = "practice mode";
		constexpr const char *pLine2 = "(you can use practice commands /tc /invincible)";
		const float Line1Size = 10.0f;
		const float Line2Size = 8.0f;
		const float Line1X = m_Width / 2.0f - TextRender()->TextWidth(Line1Size, Localize(pLine1), -1, -1.0f) / 2.0f;
		const float Line2X = m_Width / 2.0f - TextRender()->TextWidth(Line2Size, Localize(pLine2), -1, -1.0f) / 2.0f;
		TextRender()->Text(Line1X, 34.0f, Line1Size, Localize(pLine1), -1.0f);
		TextRender()->Text(Line2X, 45.0f, Line2Size, Localize(pLine2), -1.0f);
	}

	if(g_Config.m_TcMiniDebug)
	{
		float FontSize = 8.0f;
		float TextHeight = 11.0f;
		char aBuf[64];
		float OffsetY = 3.0f;

		int PlayerId = GameClient()->m_Snap.m_LocalClientId;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			PlayerId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

		if(g_Config.m_ClShowhudDDRace && GameClient()->m_Snap.m_aCharacters[PlayerId].m_HasExtendedData && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			OffsetY += 50.0f;
		else if(g_Config.m_ClShowhudHealthAmmo && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			OffsetY += 27.0f;

		vec2 Pos;
		if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
			Pos = vec2(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x, GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y);
		else
			Pos = GameClient()->m_aClients[PlayerId].m_RenderPos;

		str_format(aBuf, sizeof(aBuf), "X: %.2f", Pos.x / 32.0f);
		TextRender()->Text(4, OffsetY, FontSize, aBuf, -1.0f);

		OffsetY += TextHeight;
		str_format(aBuf, sizeof(aBuf), "Y: %.2f", Pos.y / 32.0f);
		TextRender()->Text(4, OffsetY, FontSize, aBuf, -1.0f);
		if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		{
			OffsetY += TextHeight;
			str_format(aBuf, sizeof(aBuf), "Angle: %d", GameClient()->m_aClients[PlayerId].m_RenderCur.m_Angle);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);

			OffsetY += TextHeight;
			str_format(aBuf, sizeof(aBuf), "VelY: %.2f", GameClient()->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelY / 256.0f * 50.0f / 32.0f);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);

			OffsetY += TextHeight;

			str_format(aBuf, sizeof(aBuf), "VelX: %.2f", GameClient()->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelX / 256.0f * 50.0f / 32.0f);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);
		}
	}
	if(g_Config.m_TcRenderCursorSpec && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
	{
		int CurWeapon = 1;
		Graphics()->SetColor(1.f, 1.f, 1.f, g_Config.m_TcRenderCursorSpecAlpha / 100.0f);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], m_Width / 2.0f, m_Height / 2.0f, 0.36f, 0.36f);
	}
	// render team in freeze text and last notify
	if((g_Config.m_TcShowFrozenText > 0 || g_Config.m_TcShowFrozenHud > 0 || g_Config.m_TcNotifyWhenLast) && GameClient()->m_GameInfo.m_EntitiesDDRace)
	{
		const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
		const int NumInTeam = FrozenInfo.m_NumInTeam;
		const int NumFrozen = FrozenInfo.m_NumFrozen;
		const int LocalTeamID = FrozenInfo.m_LocalTeamId;

		// Notify when last
		if(g_Config.m_TcNotifyWhenLast)
		{
			if(NumInTeam > 1 && NumInTeam - NumFrozen == 1)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcNotifyWhenLastColor)));
				float FontSize = g_Config.m_TcNotifyWhenLastSize;
				float XPos = std::clamp((g_Config.m_TcNotifyWhenLastX / 100.0f) * m_Width, 1.0f, m_Width - FontSize);
				float YPos = std::clamp((g_Config.m_TcNotifyWhenLastY / 100.0f) * m_Height, 1.0f, m_Height - FontSize);

				TextRender()->Text(XPos, YPos, FontSize, g_Config.m_TcNotifyWhenLastText, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
		// Show freeze text
		char aBuf[64];
		const bool ShowFrozenSummary = BuildHudFrozenSummaryText(FrozenInfo, aBuf, sizeof(aBuf));
		if(ShowFrozenSummary && !HasVisibleMediaIsland())
			TextRender()->Text(m_Width / 2.0f - TextRender()->TextWidth(10.0f, aBuf) / 2.0f, 12.0f, 10.0f, aBuf);

		// str_format(aBuf, sizeof(aBuf), "%d", GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_PrevPredicted.m_FreezeEnd);
		// str_format(aBuf, sizeof(aBuf), "%d", g_Config.m_ClWhatsMyPing);
		// TextRender()->Text(0, m_Width / 2 - TextRender()->TextWidth(0, 10, aBuf, -1, -1.0f) / 2, 20, 10, aBuf, -1.0f);

		if(g_Config.m_TcShowFrozenHud > 0 && !GameClient()->m_Scoreboard.IsActive() && !(LocalTeamID == 0 && g_Config.m_TcFrozenHudTeamOnly))
		{
			CTeeRenderInfo FreezeInfo;
			const CSkin *pSkin = GameClient()->m_Skins.Find("x_ninja");
			FreezeInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
			FreezeInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
			FreezeInfo.m_BloodColor = pSkin->m_BloodColor;
			FreezeInfo.m_SkinMetrics = pSkin->m_Metrics;
			FreezeInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
			FreezeInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
			FreezeInfo.m_CustomColoredSkin = false;

			float progressiveOffset = 0.0f;
			float TeeSize = g_Config.m_TcFrozenHudTeeSize;
			int MaxTees = (int)(8.3f * (m_Width / m_Height) * 13.0f / TeeSize);
			if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred && !g_Config.m_ClShowPacketLoss)
				MaxTees = (int)(9.5f * (m_Width / m_Height) * 13.0f / TeeSize);
			int MaxRows = g_Config.m_TcFrozenMaxRows;
			float StartPos = m_Width / 2.0f + 38.0f * (m_Width / m_Height) / 1.78f;
			const float AvoidanceRight = GetTopIslandAvoidanceRight();
			if(AvoidanceRight > 0.0f)
				StartPos = std::max(StartPos, AvoidanceRight + TeeSize * 0.5f + 4.0f);

			const float RowLeft = StartPos - TeeSize * 0.5f;
			const float AvailableRowWidth = std::max(TeeSize, m_Width - RowLeft);
			MaxTees = std::max(1, std::min(MaxTees, (int)std::floor(AvailableRowWidth / TeeSize)));

			int TotalRows = std::min(MaxRows, (NumInTeam + MaxTees - 1) / MaxTees);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.4f);
			Graphics()->DrawRectExt(StartPos - TeeSize / 2.0f, 0.0f, TeeSize * std::min(NumInTeam, MaxTees), TeeSize + 3.0f + (TotalRows - 1) * TeeSize, 5.0f, IGraphics::CORNER_B);
			Graphics()->QuadsEnd();

			bool Overflow = NumInTeam > MaxTees * MaxRows;

			int NumDisplayed = 0;
			int NumInRow = 0;
			int CurrentRow = 0;

			for(int OverflowIndex = 0; OverflowIndex < 1 + Overflow; OverflowIndex++)
			{
				for(int i = 0; i < MAX_CLIENTS && NumDisplayed < MaxTees * MaxRows; i++)
				{
					if(!GameClient()->m_Snap.m_apPlayerInfos[i])
						continue;
					if(GameClient()->m_Teams.Team(i) == LocalTeamID)
					{
						bool Frozen = false;
						CTeeRenderInfo TeeInfo = GameClient()->m_aClients[i].m_RenderInfo;
						if(QmHudTeeIsFrozen(
							   GameClient()->m_aClients[i].m_HudFrozenTeeState,
							   GameClient()->m_aClients[i].m_FreezeEnd,
							   GameClient()->m_aClients[i].m_DeepFrozen))
						{
							if(!g_Config.m_TcShowFrozenHudSkins)
								TeeInfo = FreezeInfo;
							Frozen = true;
						}

						if(Overflow && Frozen && OverflowIndex == 0)
							continue;
						if(Overflow && !Frozen && OverflowIndex == 1)
							continue;

						NumDisplayed++;
						NumInRow++;
						if(NumInRow > MaxTees)
						{
							NumInRow = 1;
							progressiveOffset = 0.0f;
							CurrentRow++;
						}

						TeeInfo.m_Size = TeeSize;
						const CAnimState *pIdleState = CAnimState::GetIdle();
						vec2 OffsetToMid;
						CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
						vec2 TeeRenderPos(StartPos + progressiveOffset, TeeSize * (0.7f) + CurrentRow * TeeSize);
						float Alpha = 1.0f;
						CNetObj_Character CurChar = GameClient()->m_aClients[i].m_RenderCur;
						if(g_Config.m_TcShowFrozenHudSkins && Frozen)
						{
							Alpha = 0.6f;
							TeeInfo.m_ColorBody.r *= 0.4f;
							TeeInfo.m_ColorBody.g *= 0.4f;
							TeeInfo.m_ColorBody.b *= 0.4f;
							TeeInfo.m_ColorFeet.r *= 0.4f;
							TeeInfo.m_ColorFeet.g *= 0.4f;
							TeeInfo.m_ColorFeet.b *= 0.4f;
						}
						if(Frozen)
							RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_PAIN, vec2(1.0f, 0.0f), TeeRenderPos, Alpha);
						else
							RenderTools()->RenderTee(pIdleState, &TeeInfo, CurChar.m_Emote, vec2(1.0f, 0.0f), TeeRenderPos);
						progressiveOffset += TeeSize;
					}
				}
			}
		}
	}
}

void CHud::RenderSwapCountdown()
{
	SSwapCountdownList SwapList;
	if(!BuildSwapCountdownList(*GameClient(), *Client(), SwapList))
		return;

	const float FontSize = 8.0f;
	const float X = 5.0f;
	const float Y = m_Height - 12.0f;
	const float LineHeight = 9.0f;

	for(int i = 0; i < SwapList.m_Count; ++i)
	{
		TextRender()->TextColor(SwapList.m_aInfos[i].m_TextColor);
		TextRender()->Text(X, Y - LineHeight * i, FontSize, SwapList.m_aInfos[i].m_aText, -1.0f);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CHud::UpdateSwitchCountdownTracker()
{
	if(!g_Config.m_QmSwitchCountdown)
	{
		m_SwitchCountdownTracker.Reset();
		ResetSwitchCountdownRings();
		return;
	}
	if(!QmHudSwitchCountdownShowsFollowTee(g_Config.m_QmSwitchCountdownMode))
		ResetSwitchCountdownRings();

	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0 || Collision() == nullptr)
		return;

	const auto ClearTrackedSwitch = [&](int Team, int SwitchNumber) {
		m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] = 0;
		m_SwitchCountdownTracker.m_aaTouchTick[Team][SwitchNumber] = 0;
		m_SwitchCountdownTracker.m_aaClientId[Team][SwitchNumber] = -1;
		m_SwitchCountdownTracker.m_aaConnection[Team][SwitchNumber] = -1;
	};
	const auto UpdateSwitchCountdownFromClient = [&](int ClientId, int Connection, bool UsePredictedPos) {
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || Connection < 0 || Connection >= NUM_DUMMIES)
			return;
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			return;

		const vec2 Pos = UsePredictedPos ?
					 GameClient()->m_aClients[ClientId].m_Predicted.m_Pos :
					 vec2(GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);

		const int Team = GameClient()->m_Teams.Team(ClientId);
		if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
			return;

		const int MapIndex = Collision()->GetPureMapIndex(Pos);
		if(MapIndex < 0)
			return;

		const int SwitchType = Collision()->GetSwitchType(MapIndex);
		if(SwitchType != TILE_SWITCHTIMEDOPEN && SwitchType != TILE_SWITCHTIMEDCLOSE)
			return;

		const int SwitchNumber = Collision()->GetSwitchNumber(MapIndex);
		if(SwitchNumber <= 0 || SwitchNumber >= 256)
			return;

		const int CurTick = Client()->GameTick(Connection);
		const int Delay = Collision()->GetSwitchDelay(MapIndex);
		m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] = CurTick + 1 + Delay * TickSpeed;
		m_SwitchCountdownTracker.m_aaTouchTick[Team][SwitchNumber] = CurTick;
		m_SwitchCountdownTracker.m_aaClientId[Team][SwitchNumber] = ClientId;
		m_SwitchCountdownTracker.m_aaConnection[Team][SwitchNumber] = Connection;
	};

	UpdateSwitchCountdownFromClient(GameClient()->m_aLocalIds[0], 0, GameClient()->Predict());
	if(Client()->DummyConnected())
		UpdateSwitchCountdownFromClient(GameClient()->m_aLocalIds[1], 1, GameClient()->PredictDummy());

	for(int Team = 0; Team < NUM_DDRACE_TEAMS; ++Team)
	{
		for(int SwitchNumber = 1; SwitchNumber < 256; ++SwitchNumber)
		{
			if(m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] <= 0)
				continue;
			const int Connection = m_SwitchCountdownTracker.m_aaConnection[Team][SwitchNumber];
			const int ClientId = m_SwitchCountdownTracker.m_aaClientId[Team][SwitchNumber];
			const bool ConnectionActive = Connection == 0 || (Connection == 1 && Client()->DummyConnected());
			if(!ConnectionActive || ClientId < 0 || ClientId != GameClient()->m_aLocalIds[Connection] || m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] <= Client()->GameTick(Connection))
				ClearTrackedSwitch(Team, SwitchNumber);
		}
	}
}

bool CHud::HasActiveSwitchCountdown() const
{
	if(!g_Config.m_QmSwitchCountdown || !QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode))
		return false;
	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0)
		return false;

	const int Team = GameClient()->SwitchStateTeam();
	if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
		return false;

	const int CurTick = Client()->GameTick(g_Config.m_ClDummy);
	for(int i = 1; i < 256; ++i)
	{
		if(m_SwitchCountdownTracker.m_aaEndTick[Team][i] > CurTick && m_SwitchCountdownTracker.m_aaTouchTick[Team][i] > 0)
			return true;
	}

	return false;
}

bool CHud::BuildSwitchCountdownSummary(char *pBuf, const size_t BufSize) const
{
	pBuf[0] = '\0';

	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0)
		return false;

	const int Team = GameClient()->SwitchStateTeam();
	if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
		return false;

	struct SSwitchCountdownEntry
	{
		int m_Number = 0;
		int m_EndTick = 0;
		int m_LastTouchTick = 0;
	};

	std::array<SSwitchCountdownEntry, 256> aEntries;
	int EntryCount = 0;
	const int CurTick = Client()->GameTick(g_Config.m_ClDummy);

	for(int i = 1; i < 256; ++i)
	{
		const int EndTick = m_SwitchCountdownTracker.m_aaEndTick[Team][i];
		const int TouchTick = m_SwitchCountdownTracker.m_aaTouchTick[Team][i];
		if(EndTick <= CurTick || TouchTick <= 0)
			continue;

		if(EntryCount < (int)aEntries.size())
		{
			aEntries[EntryCount].m_Number = i;
			aEntries[EntryCount].m_EndTick = EndTick;
			aEntries[EntryCount].m_LastTouchTick = TouchTick;
			++EntryCount;
		}
	}

	if(EntryCount <= 0)
		return false;

	std::sort(aEntries.begin(), aEntries.begin() + EntryCount, [](const SSwitchCountdownEntry &A, const SSwitchCountdownEntry &B) {
		return A.m_LastTouchTick > B.m_LastTouchTick;
	});

	const int RenderCount = minimum(EntryCount, SWITCH_COUNTDOWN_MAX_LINES);
	for(int i = 0; i < RenderCount; ++i)
	{
		const int RemainingTicks = aEntries[i].m_EndTick - CurTick;
		if(RemainingTicks <= 0)
			continue;

		const int SecondsLeft = (RemainingTicks + TickSpeed - 1) / TickSpeed;
		char aItemBuf[64];
		str_format(aItemBuf, sizeof(aItemBuf), "开关#%d:%d秒", aEntries[i].m_Number, SecondsLeft);
		if(pBuf[0] != '\0')
			str_append(pBuf, "  ", BufSize);
		str_append(pBuf, aItemBuf, BufSize);
	}

	return pBuf[0] != '\0';
}

void CHud::RenderSwitchCountdowns()
{
	UpdateSwitchCountdownTracker();

	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0)
		return;

	const int CurTick = Client()->GameTick(g_Config.m_ClDummy);

	struct SSwitchCountdownEntry
	{
		int m_Number;
		int m_EndTick;
		int m_LastTouchTick;
	};

	std::array<SSwitchCountdownEntry, 256> aEntries;
	int EntryCount = 0;

	const int Team = GameClient()->SwitchStateTeam();
	if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
		return;

	for(int i = 1; i < 256; ++i)
	{
		const int EndTick = m_SwitchCountdownTracker.m_aaEndTick[Team][i];
		if(EndTick <= CurTick)
		{
			m_SwitchCountdownTracker.m_aaEndTick[Team][i] = 0;
			m_SwitchCountdownTracker.m_aaTouchTick[Team][i] = 0;
			continue;
		}

		const int TouchTick = m_SwitchCountdownTracker.m_aaTouchTick[Team][i];
		if(TouchTick <= 0)
			continue;

		if(EntryCount < (int)aEntries.size())
		{
			aEntries[EntryCount] = {i, EndTick, TouchTick};
			EntryCount++;
		}
	}

	if(EntryCount > 0)
	{
		std::sort(aEntries.begin(), aEntries.begin() + EntryCount, [](const SSwitchCountdownEntry &A, const SSwitchCountdownEntry &B) {
			return A.m_LastTouchTick > B.m_LastTouchTick;
		});
	}

	const int RenderCount = minimum(EntryCount, SWITCH_COUNTDOWN_MAX_LINES);

	const float FontSize = 8.0f;
	const float BaseX = 5.0f;
	const float BaseY = m_Height - 12.0f;
	const float SwapGap = 6.0f;
	const float ItemGap = 8.0f;
	const float SlideOffsetY = 10.0f;
	const float SlideOffsetX = 12.0f;

	float SwitchBaseX = BaseX;
	SSwapCountdownList SwapList;
	if(BuildSwapCountdownList(*GameClient(), *Client(), SwapList))
	{
		float SwapWidth = 0.0f;
		for(int i = 0; i < SwapList.m_Count; ++i)
			SwapWidth = std::max(SwapWidth, TextRender()->TextWidth(FontSize, SwapList.m_aInfos[i].m_aText, -1, -1.0f));
		SwitchBaseX += SwapWidth + SwapGap;
	}

	CUiV2AnimationRuntime *pAnimRuntime = &GameClient()->UiRuntimeV2()->AnimRuntime();
	SHudSwitchCountdownAnimState &AnimState = m_SwitchCountdownAnimState;
	std::array<bool, SWITCH_COUNTDOWN_MAX_LINES> aShouldShow = {false, false, false};
	std::array<char[64], SWITCH_COUNTDOWN_MAX_LINES> aCurrentText;
	std::array<float, SWITCH_COUNTDOWN_MAX_LINES> aCurrentWidth = {0.0f, 0.0f, 0.0f};
	std::array<int, SWITCH_COUNTDOWN_MAX_LINES> aCurrentNumber = {-1, -1, -1};
	std::array<float, SWITCH_COUNTDOWN_MAX_LINES> aCurrentTargetX = {0.0f, 0.0f, 0.0f};

	for(int i = 0; i < RenderCount; ++i)
	{
		const int EndTick = aEntries[i].m_EndTick;
		const int RemainingTicks = EndTick - CurTick;
		if(RemainingTicks <= 0)
			continue;

		const int SecondsLeft = (RemainingTicks + TickSpeed - 1) / TickSpeed;
		str_format(aCurrentText[i], sizeof(aCurrentText[i]), "开关#%d:%d秒", aEntries[i].m_Number, SecondsLeft);
		aCurrentWidth[i] = TextRender()->TextWidth(FontSize, aCurrentText[i], -1, -1.0f);
		aCurrentNumber[i] = aEntries[i].m_Number;
		aShouldShow[i] = true;
	}

	float CursorX = SwitchBaseX;
	for(int i = 0; i < SWITCH_COUNTDOWN_MAX_LINES; ++i)
	{
		if(!aShouldShow[i])
			continue;
		aCurrentTargetX[i] = CursorX;
		CursorX += aCurrentWidth[i] + ItemGap;
	}

	for(int i = 0; i < SWITCH_COUNTDOWN_MAX_LINES; ++i)
	{
		const bool ShouldShow = aShouldShow[i];
		const bool WasVisible = AnimState.m_aWasVisible[i];
		const bool IsNewEntry = ShouldShow && (!WasVisible || AnimState.m_aLastSwitchNumber[i] != aCurrentNumber[i]);

		if(ShouldShow)
		{
			str_copy(AnimState.m_aaLastText[i], aCurrentText[i], sizeof(AnimState.m_aaLastText[i]));
			AnimState.m_aLastWidth[i] = aCurrentWidth[i];
			AnimState.m_aLastSwitchNumber[i] = aCurrentNumber[i];
		}

		const float TargetX = ShouldShow ? aCurrentTargetX[i] : AnimState.m_aTargetX[i];
		const float TargetY = (!ShouldShow && WasVisible) ? (BaseY + SlideOffsetY) : BaseY;
		const float TargetAlpha = ShouldShow ? 1.0f : 0.0f;
		float X = TargetX;
		float Y = TargetY;
		float Alpha = TargetAlpha;

		if(pAnimRuntime != nullptr)
		{
			const uint64_t NodeKey = HudSwitchCountdownNodeKey(i);
			if(!AnimState.m_aPositionInitialized[i])
			{
				SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, TargetX);
				SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, TargetY);
				AnimState.m_aTargetX[i] = TargetX;
				AnimState.m_aTargetY[i] = TargetY;
				AnimState.m_aPositionInitialized[i] = true;
			}
			if(!AnimState.m_aAlphaInitialized[i])
			{
				SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, TargetAlpha);
				AnimState.m_aTargetAlpha[i] = TargetAlpha;
				AnimState.m_aAlphaInitialized[i] = true;
			}

			if(IsNewEntry)
			{
				SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, TargetX);
				SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, BaseY - SlideOffsetY);
				SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, 0.0f);
				AnimState.m_aTargetX[i] = TargetX;
				AnimState.m_aTargetY[i] = BaseY - SlideOffsetY;
				AnimState.m_aTargetAlpha[i] = 0.0f;
			}
			else
			{
				const bool ExistingEntry = ShouldShow && WasVisible;
				const bool ExistingReflow = ExistingEntry && std::abs(TargetX - AnimState.m_aTargetX[i]) > 0.5f;
				if(ExistingReflow)
				{
					// Existing items also replay declarative animation: slide in from left and fade in.
					SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, TargetX - SlideOffsetX);
					SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, 0.0f);
					AnimState.m_aTargetX[i] = TargetX - SlideOffsetX;
					AnimState.m_aTargetAlpha[i] = 0.0f;
				}
			}

			X = ResolveAnimatedLayoutValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, TargetX, AnimState.m_aTargetX[i]);
			Y = ResolveAnimatedLayoutValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, TargetY, AnimState.m_aTargetY[i]);
			Alpha = std::clamp(ResolveAnimatedLayoutValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, TargetAlpha, AnimState.m_aTargetAlpha[i]), 0.0f, 1.0f);
		}

		const char *pRenderText = AnimState.m_aaLastText[i];
		if(pRenderText[0] != '\0' && Alpha > 0.01f)
		{
			ColorRGBA TextColor = TextRender()->DefaultTextColor();
			ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
			TextColor.a *= Alpha;
			OutlineColor.a *= Alpha;
			TextRender()->TextColor(TextColor);
			TextRender()->TextOutlineColor(OutlineColor);
			TextRender()->Text(X, Y, FontSize, pRenderText, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		}

		if(!ShouldShow)
		{
			if(pAnimRuntime == nullptr)
			{
				AnimState.m_aaLastText[i][0] = '\0';
				AnimState.m_aLastWidth[i] = 0.0f;
				AnimState.m_aLastSwitchNumber[i] = -1;
			}
			else
			{
				const uint64_t NodeKey = HudSwitchCountdownNodeKey(i);
				if(Alpha <= 0.01f && !pAnimRuntime->HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA))
				{
					AnimState.m_aaLastText[i][0] = '\0';
					AnimState.m_aLastWidth[i] = 0.0f;
					AnimState.m_aLastSwitchNumber[i] = -1;
				}
			}
		}

		AnimState.m_aWasVisible[i] = ShouldShow;
	}
}

void CHud::ResetSwitchCountdownRings()
{
	for(auto &Ring : m_aSwitchCountdownRings)
		Ring.Reset();
}

void CHud::RenderFollowSwitchCountdowns()
{
	if(!g_Config.m_QmSwitchCountdown || !QmHudSwitchCountdownShowsFollowTee(g_Config.m_QmSwitchCountdownMode))
	{
		ResetSwitchCountdownRings();
		return;
	}
	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0)
	{
		ResetSwitchCountdownRings();
		return;
	}

	std::array<SHudSwitchCountdownEntry, NUM_DUMMIES * 255> aCandidates{};
	int CandidateCount = 0;
	for(int Connection = 0; Connection < NUM_DUMMIES; ++Connection)
	{
		if(Connection == 1 && !Client()->DummyConnected())
			continue;
		const int ClientId = GameClient()->m_aLocalIds[Connection];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			continue;
		const int Team = GameClient()->m_Teams.Team(ClientId);
		if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
			continue;
		const int CurTick = Client()->GameTick(Connection);
		for(int Number = 1; Number < 256; ++Number)
		{
			const int EndTick = m_SwitchCountdownTracker.m_aaEndTick[Team][Number];
			const int TriggerTick = m_SwitchCountdownTracker.m_aaTouchTick[Team][Number];
			if(EndTick <= CurTick || TriggerTick <= 0 ||
				m_SwitchCountdownTracker.m_aaClientId[Team][Number] != ClientId ||
				m_SwitchCountdownTracker.m_aaConnection[Team][Number] != Connection)
				continue;
			aCandidates[CandidateCount++] = {Team, Number, ClientId, Connection, TriggerTick, EndTick, CurTick};
		}
	}

	std::array<SHudSwitchCountdownEntry, SWITCH_COUNTDOWN_MAX_LINES> aSelected{};
	const int SelectedCount = QmHudSelectLatestSwitchCountdowns(
		aCandidates.data(),
		CandidateCount,
		aSelected.data(),
		aSelected.size());
	for(auto &Ring : m_aSwitchCountdownRings)
		Ring.m_Seen = false;

	std::array<int, SWITCH_COUNTDOWN_MAX_LINES> aStateIndices{};
	aStateIndices.fill(-1);
	for(int i = 0; i < SelectedCount; ++i)
	{
		for(int StateIndex = 0; StateIndex < SWITCH_COUNTDOWN_MAX_LINES; ++StateIndex)
		{
			const auto &Ring = m_aSwitchCountdownRings[StateIndex];
			if(Ring.m_ClientId == aSelected[i].m_ClientId && Ring.m_Team == aSelected[i].m_Team && Ring.m_Number == aSelected[i].m_Number)
			{
				aStateIndices[i] = StateIndex;
				m_aSwitchCountdownRings[StateIndex].m_Seen = true;
				break;
			}
		}
	}
	for(int i = 0; i < SelectedCount; ++i)
	{
		if(aStateIndices[i] < 0)
		{
			for(int StateIndex = 0; StateIndex < SWITCH_COUNTDOWN_MAX_LINES; ++StateIndex)
			{
				if(m_aSwitchCountdownRings[StateIndex].m_Seen)
					continue;
				aStateIndices[i] = StateIndex;
				m_aSwitchCountdownRings[StateIndex].Reset();
				m_aSwitchCountdownRings[StateIndex].m_Seen = true;
				break;
			}
		}
		if(aStateIndices[i] < 0)
			continue;

		auto &Ring = m_aSwitchCountdownRings[aStateIndices[i]];
		Ring.m_Team = aSelected[i].m_Team;
		Ring.m_Number = aSelected[i].m_Number;
		Ring.m_ClientId = aSelected[i].m_ClientId;
		Ring.m_Connection = aSelected[i].m_Connection;
		Ring.m_TriggerTick = aSelected[i].m_TriggerTick;
		Ring.m_EndTick = aSelected[i].m_EndTick;
		Ring.m_LayoutSlot = 0;
		for(int Previous = 0; Previous < i; ++Previous)
		{
			if(aSelected[Previous].m_ClientId == aSelected[i].m_ClientId)
				++Ring.m_LayoutSlot;
		}
	}

	float SavedScreenX0, SavedScreenY0, SavedScreenX1, SavedScreenY1;
	Graphics()->GetScreen(&SavedScreenX0, &SavedScreenY0, &SavedScreenX1, &SavedScreenY1);
	Graphics()->MapScreenToGameInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, GameClient()->m_Camera.m_Zoom);
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float Delta = Client()->RenderFrameTime();
	// Preserve the previous ring's outer footprint while adopting the satellite proportions.
	constexpr float SatelliteRadius = 9.0f + 2.5f * 0.5f;
	constexpr float RingRadius = SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_RADIUS_SCALE;
	const float RingThickness = std::max(QmHudMediaIslandScaled(1.25f), SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_THICKNESS_SCALE);
	const float ScreenPixelSize = std::max(
		(ScreenX1 - ScreenX0) / std::max(1, Graphics()->ScreenWidth()),
		(ScreenY1 - ScreenY0) / std::max(1, Graphics()->ScreenHeight()));
	const ColorRGBA RingColor = MediaIslandCountdownColor(EHudMediaIslandCountdownType::SWITCH);
	ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHudIslandBgColor));
	BackgroundColor.a = std::clamp(g_Config.m_QmHudIslandBgOpacity / 100.0f, 0.0f, 1.0f);
	for(auto &Ring : m_aSwitchCountdownRings)
	{
		if(Ring.m_Seen && (Ring.m_ClientId < 0 || Ring.m_ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[Ring.m_ClientId].m_Active))
			Ring.m_Seen = false;
		if(Ring.m_Seen)
		{
			const vec2 TeePosition = GameClient()->m_aClients[Ring.m_ClientId].m_RenderPos;
			const bool PetVisible = g_Config.m_TcPetShow > 0 && GameClient()->m_Pet.IsVisibleForClient(Ring.m_ClientId);
			const vec2 PetPosition = PetVisible ? GameClient()->m_Pet.Position() : vec2();
			const int Side = QmHudSwitchCountdownFollowSide(TeePosition.x, PetVisible, PetPosition.x);
			const float Now = Client()->GameTick(Ring.m_Connection) / static_cast<float>(TickSpeed);
			const vec2 Target = QmHudSwitchCountdownFollowTarget(TeePosition, Side, Ring.m_LayoutSlot, Now);
			if(!Ring.m_Initialized)
			{
				Ring.m_Position = Target;
				Ring.m_Velocity = vec2();
				Ring.m_Initialized = true;
			}
			QmTClientPetAdvanceSpring(Ring.m_Position, Ring.m_Velocity, Target, Delta);
			Ring.m_Alpha = std::min(1.0f, Ring.m_Alpha + Delta);
		}
		else
		{
			Ring.m_Alpha = std::max(0.0f, Ring.m_Alpha - Delta);
		}

		if(Ring.m_Alpha <= 0.0f)
		{
			if(!Ring.m_Seen)
				Ring.Reset();
			continue;
		}
		if(!in_range(Ring.m_Position.x, ScreenX0 - SatelliteRadius, ScreenX1 + SatelliteRadius) ||
			!in_range(Ring.m_Position.y, ScreenY0 - SatelliteRadius, ScreenY1 + SatelliteRadius))
			continue;

		const int CurTick = Client()->GameTick(std::clamp(Ring.m_Connection, 0, NUM_DUMMIES - 1));
		const int DurationTicks = Ring.m_EndTick - Ring.m_TriggerTick;
		const float Progress = DurationTicks > 0 ? std::clamp((Ring.m_EndTick - CurTick) / static_cast<float>(DurationTicks), 0.0f, 1.0f) : 0.0f;
		DrawMediaIslandCountdownSatellite(
			Graphics(),
			Ring.m_Position,
			SatelliteRadius,
			RingRadius,
			RingThickness,
			Progress,
			Ring.m_Alpha,
			ScreenPixelSize,
			BackgroundColor,
			RingColor);
	}
	Graphics()->MapScreen(SavedScreenX0, SavedScreenY0, SavedScreenX1, SavedScreenY1);
}

void CHud::RenderConnectionWarning()
{
	if(Client()->ConnectionProblems())
	{
		const char *pText = Localize("Connection Problems…");
		float w = TextRender()->TextWidth(24, pText, -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() - w / 2, 50, 24, pText, -1.0f);
	}
}

void CHud::RenderTeambalanceWarning()
{
	// render prompt about team-balance
	bool Flash = time() / (time_freq() / 2) % 2 == 0;
	if(GameClient()->IsTeamPlay())
	{
		int TeamDiff = GameClient()->m_Snap.m_aTeamSize[TEAM_RED] - GameClient()->m_Snap.m_aTeamSize[TEAM_BLUE];
		if(g_Config.m_ClWarningTeambalance && (TeamDiff >= 2 || TeamDiff <= -2))
		{
			const char *pText = Localize("Please balance teams!");
			if(Flash)
				TextRender()->TextColor(1, 1, 0.5f, 1);
			else
				TextRender()->TextColor(0.7f, 0.7f, 0.2f, 1.0f);
			TextRender()->Text(5, 50, 6, pText, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderCursor()
{
	if(GameClient()->m_HudEditor.IsActive())
		return;

	const float Scale = (float)g_Config.m_TcCursorScale / 100.0f;
	if(Scale <= 0.0f)
		return;

	int CurWeapon = 0;
	vec2 TargetPos;
	float Alpha = 1.0f;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	float aPoints[4];
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->GameScreenAspect(), 1.0f, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Render local cursor
		CurWeapon = maximum(0, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Predicted.m_ActiveWeapon);
		TargetPos = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
	}
	else
	{
		// Render spec cursor
		if(!g_Config.m_ClSpecCursor || !GameClient()->m_CursorInfo.IsAvailable())
			return;

		bool RenderSpecCursor = (GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW) || Client()->State() == IClient::STATE_DEMOPLAYBACK;

		if(!RenderSpecCursor)
			return;

		// Calculate factor to keep cursor on screen
		const vec2 HalfSize = vec2(Center.x - aPoints[0], Center.y - aPoints[1]);
		const vec2 ScreenPos = (GameClient()->m_CursorInfo.WorldTarget() - Center) / GameClient()->m_Camera.m_Zoom;
		const float ClampFactor = maximum(
			1.0f,
			absolute(ScreenPos.x / HalfSize.x),
			absolute(ScreenPos.y / HalfSize.y));

		CurWeapon = maximum(0, GameClient()->m_CursorInfo.Weapon() % NUM_WEAPONS);
		TargetPos = ScreenPos / ClampFactor + Center;
		if(ClampFactor != 1.0f)
			Alpha /= 2.0f;
	}

	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], TargetPos.x, TargetPos.y, Scale, Scale);
}

void CHud::PrepareAmmoHealthAndArmorQuads()
{
	float x = 5;
	float y = 5;
	IGraphics::CQuadItem Array[10];

	// ammo of the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		// 0.6
		for(int n = 0; n < 10; n++)
			Array[n] = IGraphics::CQuadItem(x + n * 12, y, 10, 10);

		m_aAmmoOffset[i] = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

		// 0.7
		if(i == WEAPON_GRENADE)
		{
			// special case for 0.7 grenade
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(1 + x + n * 12, y, 10, 10);
		}
		else
		{
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(x + n * 12, y, 12, 12);
		}

		Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
	}

	// health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_HealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_EmptyHealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_ArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_EmptyArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
}

void CHud::RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter)
{
	if(!pCharacter)
		return;

	bool IsSixupGameSkin = GameClient()->m_GameSkin.IsSixup();
	int QuadOffsetSixup = (IsSixupGameSkin ? 10 : 0);

	if(GameClient()->m_GameInfo.m_HudAmmo)
	{
		// ammo display
		float AmmoOffsetY = GameClient()->m_GameInfo.m_HudHealthArmor ? 24 : 0;
		int CurWeapon = pCharacter->m_Weapon % NUM_WEAPONS;
		// 0.7 only
		if(CurWeapon == WEAPON_NINJA)
		{
			if(!GameClient()->m_GameInfo.m_HudDDRace && Client()->IsSixup())
			{
				const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
				float NinjaProgress = std::clamp(pCharacter->m_AmmoCount - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
				RenderNinjaBarPos(5 + 10 * 12, 5, 6.f, 24.f, NinjaProgress);
			}
		}
		else if(CurWeapon >= 0 && GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon].IsValid())
		{
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon]);
			if(AmmoOffsetY > 0)
			{
				Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, std::clamp(pCharacter->m_AmmoCount, 0, 10), 0, AmmoOffsetY);
			}
			else
			{
				Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, std::clamp(pCharacter->m_AmmoCount, 0, 10));
			}
		}
	}

	if(GameClient()->m_GameInfo.m_HudHealthArmor)
	{
		// health display
		const int DisplayHealth = std::min(pCharacter->m_Health, 10);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_HealthOffset + QuadOffsetSixup, DisplayHealth);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_EmptyHealthOffset + QuadOffsetSixup + DisplayHealth, 10 - DisplayHealth);

		// armor display
		const int DisplayArmor = std::min(pCharacter->m_Armor, 10);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup, DisplayArmor);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup + DisplayArmor, 10 - DisplayArmor);
	}
}

void CHud::PreparePlayerStateQuads()
{
	float x = 5;
	float y = 5 + 24;
	IGraphics::CQuadItem Array[10];

	// Quads for displaying the available and used jumps
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpEmptyOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// Quads for displaying weapons
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
	{
		const CDataWeaponspec &WeaponSpec = g_pData->m_Weapons.m_aId[Weapon];
		float ScaleX, ScaleY;
		Graphics()->GetSpriteScale(WeaponSpec.m_pSpriteBody, ScaleX, ScaleY);
		constexpr float HudWeaponScale = 0.25f;
		float Width = WeaponSpec.m_VisualSize * ScaleX * HudWeaponScale;
		float Height = WeaponSpec.m_VisualSize * ScaleY * HudWeaponScale;
		m_aWeaponOffset[Weapon] = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, Width, Height);
	}

	// Quads for displaying capabilities
	m_EndlessJumpOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_EndlessHookOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_JetpackOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGrenadeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGunOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportLaserOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying prohibited capabilities
	m_SoloOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_CollisionDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HookHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HammerHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GunHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_ShotgunHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GrenadeHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LaserHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying freeze status
	m_DeepFrozenOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LiveFrozenOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying dummy actions
	m_DummyHammerOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_DummyCopyOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying team modes
	m_PracticeModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LockModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_Team0ModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
}

void CHud::EnsureMediaIslandFrameCache() const
{
	const uint64_t CurrentFrame = Client()->PerfFrame();
	SHudMediaIslandFrameCache &Cache = m_MediaIslandFrameCache;
	if(Cache.m_Valid && Cache.m_Frame == CurrentFrame)
		return;

	Cache.Reset();
	Cache.m_Frame = CurrentFrame;
	Cache.m_Valid = true;
	const bool MediaHudEnabled = g_Config.m_QmSmtcShowHud && SystemMediaControls::AnyMediaSourceEnabled(g_Config.m_QmSmtcEnable != 0, g_Config.m_QmNeteaseHookEnable != 0 || g_Config.m_QmSodaHookEnable != 0 || g_Config.m_QmSpotifyEnable != 0);
	Cache.m_HasMediaState = MediaHudEnabled && GameClient()->m_SystemMediaControls.GetStateSnapshot(Cache.m_MediaState);
	if(g_Config.m_QmHudIslandUseOriginalStyle)
		return;

	// 歌词来源选择:各来源可同时开启(菜单已保证同一时间只启用一个 Hook)。
	// 若用户手动同时开启,网易云优先,汽水兜底,Spotify 再后备。
	if(g_Config.m_QmNeteaseHookEnable != 0)
	{
		Cache.m_LyricsActive = GameClient()->m_NeteaseIntegration.HasActiveLyrics();
		Cache.m_ShowLyrics = GameClient()->m_NeteaseIntegration.GetCurrentLyric(Cache.m_aLyrics, sizeof(Cache.m_aLyrics), &Cache.m_LyricsColor);
	}
	if(!Cache.m_ShowLyrics && g_Config.m_QmSodaHookEnable != 0)
	{
		// 汽水音乐歌词作为网易云无歌词时的备选来源。
		Cache.m_LyricsActive = GameClient()->m_MusicLyricsIntegration.HasActiveLyrics();
		Cache.m_ShowLyrics = GameClient()->m_MusicLyricsIntegration.GetCurrentLyric(Cache.m_aLyrics, sizeof(Cache.m_aLyrics));
	}
	if(!Cache.m_ShowLyrics && g_Config.m_QmSpotifyEnable != 0)
	{
		// Spotify 歌词(官方内置 color-lyrics)作为再后备来源。
		Cache.m_LyricsActive = GameClient()->m_SpotifyIntegration.HasActiveLyrics();
		Cache.m_ShowLyrics = GameClient()->m_SpotifyIntegration.GetCurrentLyric(Cache.m_aLyrics, sizeof(Cache.m_aLyrics));
	}
	Cache.m_SpectatorCount = GetMediaIslandSpectatorCount(*GameClient(), *Client());

	if(m_MediaIslandAnimState.HasVisibleSatellite())
	{
		Cache.m_HasVisible = true;
		return;
	}
	if(m_MediaIslandMuteState.m_Confirmed && time_get() < m_MediaIslandMuteState.m_EndTick)
	{
		Cache.m_HasVisible = true;
		return;
	}
	if(Cache.m_SpectatorCount > 0)
	{
		Cache.m_HasVisible = true;
		return;
	}

	SSwapCountdownList SwapList;
	if(BuildSwapCountdownList(*GameClient(), *Client(), SwapList))
	{
		Cache.m_HasVisible = true;
		return;
	}

	if(HasActiveSwitchCountdown())
	{
		Cache.m_HasVisible = true;
		return;
	}

	if(ShouldRenderHudLocalTime(*GameClient()))
	{
		Cache.m_HasVisible = true;
		return;
	}
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
	char aFrozenSummaryBuf[64];
	if(BuildHudFrozenSummaryText(FrozenInfo, aFrozenSummaryBuf, sizeof(aFrozenSummaryBuf)))
	{
		Cache.m_HasVisible = true;
		return;
	}

	char aTeamBuf[32];
	if(BuildHudTeamText(*GameClient(), aTeamBuf, sizeof(aTeamBuf)))
	{
		Cache.m_HasVisible = true;
		return;
	}

	if(g_Config.m_ClShowhudTimer)
	{
		const SHudGameTimerInfo TimerInfo = BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width);
		if(TimerInfo.m_Visible)
		{
			Cache.m_HasVisible = true;
			return;
		}
	}

	Cache.m_HasVisible = Cache.m_ShowLyrics || Cache.m_LyricsActive || Cache.m_HasMediaState;
}

bool CHud::HasVisibleMediaIsland() const
{
	if(g_Config.m_QmHudIslandUseOriginalStyle)
		return false;
	EnsureMediaIslandFrameCache();
	return m_MediaIslandFrameCache.m_HasVisible;
}

float CHud::GetTopIslandAvoidanceRight() const
{
	if(g_Config.m_QmHudIslandUseOriginalStyle)
		return 0.0f;
	EnsureMediaIslandFrameCache();
	if(m_MediaIslandFrameCache.m_AvoidanceValid)
		return m_MediaIslandFrameCache.m_AvoidanceRight;

	const bool ShowLocalTime = ShouldRenderHudLocalTime(*GameClient());
	const SHudGameTimerInfo TimerInfo = g_Config.m_ClShowhudTimer ? BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width) : SHudGameTimerInfo{};
	const SHudTopTimerCapsuleInfo TimerCapsule = BuildHudTopTimerCapsuleInfo(TimerInfo);
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
	char aFrozenSummaryBuf[64];
	const bool ShowFrozenSummary = BuildHudFrozenSummaryText(FrozenInfo, aFrozenSummaryBuf, sizeof(aFrozenSummaryBuf));
	const bool ShowInfoStack = ShowLocalTime || ShowFrozenSummary;

	char aRecordingBuf[512];
	const bool ShowRecordingStatus = TimerCapsule.m_Visible && BuildHudRecordingStatusText(*GameClient(), aRecordingBuf, sizeof(aRecordingBuf));
	const bool ScoreboardExpanded = GameClient()->m_Scoreboard.IsActive();
	const int SpectatorCount = m_MediaIslandFrameCache.m_SpectatorCount;
	const bool ShowSpectator = SpectatorCount > 0;
	const bool ShowSpectatorSatellite = ShowSpectator || m_MediaIslandAnimState.m_SpectatorLiquidProgress > 0.0f;
	char aTeamBuf[32];
	const bool ShowTeam = BuildHudTeamText(*GameClient(), aTeamBuf, sizeof(aTeamBuf));

	const CSystemMediaControls::SState &MediaState = m_MediaIslandFrameCache.m_MediaState;
	const bool HasMediaState = m_MediaIslandFrameCache.m_HasMediaState;
	const bool ShowTopRow = HasMediaState || ShowInfoStack || TimerCapsule.m_Visible || ShowRecordingStatus || ShowSpectatorSatellite || ShowTeam;
	if(!ShowTopRow)
	{
		m_MediaIslandFrameCache.m_AvoidanceValid = true;
		m_MediaIslandFrameCache.m_AvoidanceRight = 0.0f;
		return 0.0f;
	}

	constexpr float BaseIslandHeight = QmHudMediaIslandScaled(16.0f);
	constexpr float CoverSize = QmHudMediaIslandScaled(12.0f);
	constexpr float PaddingX = QmHudMediaIslandScaled(2.0f);
	constexpr float Gap = QmHudMediaIslandScaled(2.0f);
	constexpr float SpectatorGap = QmHudMediaIslandScaled(1.0f);
	constexpr float SpectatorSatellitePaddingX = QmHudMediaIslandScaled(2.0f);
	constexpr float SpectatorSatelliteIconSize = QmHudMediaIslandScaled(6.4f);
	constexpr float SpectatorSatelliteRestGap = QmHudMediaIslandScaled(3.0f);
	constexpr float TitleFontSize = QmHudMediaIslandScaled(5.8f);
	constexpr float MetaFontSize = QmHudMediaIslandScaled(5.3f);
	constexpr float ScreenPadding = 5.0f;
	constexpr float GapToTimer = QmHudMediaIslandScaled(3.0f);
	constexpr float TimerToStatusGap = QmHudMediaIslandScaled(3.0f);
	constexpr float StatusPaddingLeft = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusPaddingRight = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotSize = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotGap = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusFontSize = QmHudMediaIslandScaled(5.3f);
	constexpr float StackedStatusFontSize = QmHudMediaIslandScaled(4.4f);
	constexpr float WaveformSlotWidth = QmHudMediaIslandScaled(12.0f);

	char aSpectatorBuf[16];
	str_format(aSpectatorBuf, sizeof(aSpectatorBuf), "%d", ShowSpectator ? SpectatorCount : std::max(0, m_MediaIslandAnimState.m_SpectatorDisplayCount));

	char aTimeBuf[16];
	str_timestamp_format(aTimeBuf, sizeof(aTimeBuf), "%H:%M");
	if(aTimeBuf[0] == '\0')
		str_copy(aTimeBuf, "00:00", sizeof(aTimeBuf));

	const bool ShowWaveform = HasMediaState;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	const float TeamIconWidth = TextRender()->TextWidth(MetaFontSize, FontIcons::FONT_ICON_USERS);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	const float SpectatorTextWidth = ShowSpectatorSatellite ? TextRender()->TextWidth(MetaFontSize, aSpectatorBuf) : 0.0f;
	const float SpectatorSatelliteContentWidth = SpectatorSatelliteIconSize + SpectatorGap + SpectatorTextWidth;
	const float SpectatorSatelliteWidth = std::max(BaseIslandHeight, SpectatorSatelliteContentWidth + SpectatorSatellitePaddingX * 2.0f);
	const float TeamTextWidth = ShowTeam ? TextRender()->TextWidth(MetaFontSize, aTeamBuf) : 0.0f;
	const float TeamWidth = ShowTeam ? TeamIconWidth + SpectatorGap + TeamTextWidth : 0.0f;
	const float StatusTextWidth = ShowRecordingStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aRecordingBuf).m_W) : 0.0f;
	const float RawCollapsedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusPaddingRight;
	const float RawExpandedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusDotGap + StatusTextWidth + StatusPaddingRight;
	const bool StackHasTwoRows = ShowLocalTime && ShowFrozenSummary;
	const float InfoStackFontSize = StackHasTwoRows ? StackedStatusFontSize : StatusFontSize;
	const float LocalTimeTextWidth = ShowLocalTime ? std::round(TextRender()->TextBoundingBox(InfoStackFontSize, aTimeBuf).m_W) : 0.0f;
	const float FrozenSummaryTextWidth = ShowFrozenSummary ? std::round(TextRender()->TextBoundingBox(InfoStackFontSize, aFrozenSummaryBuf).m_W) : 0.0f;
	const float InfoStackWidth = ShowInfoStack ? StatusPaddingLeft + std::max(LocalTimeTextWidth, FrozenSummaryTextWidth) + StatusPaddingRight : 0.0f;
	const bool ShowCover = HasMediaState;

	const int MetaItemCount = (ShowTeam ? 1 : 0) + (ShowWaveform ? 1 : 0);
	float BaseWidth = PaddingX;
	if(ShowCover)
		BaseWidth += CoverSize;
	if(ShowCover && MetaItemCount > 0)
		BaseWidth += Gap;
	if(ShowTeam)
		BaseWidth += TeamWidth;
	if(ShowTeam && ShowWaveform)
		BaseWidth += Gap;
	if(ShowWaveform)
		BaseWidth += WaveformSlotWidth;
	BaseWidth += PaddingX;
	if(!ShowCover && MetaItemCount == 0)
		BaseWidth = 0.0f;
	if(ShowSpectatorSatellite)
		BaseWidth = std::max(BaseWidth, BaseIslandHeight);

	const int64_t Now = time_get();
	const bool TrackDetailsExpanded = HasMediaState && QmHudMediaIslandShouldShowTrackDetails(Now, m_MediaIslandAnimState.m_TrackDetailsUntilTick);
	const float MaxTitleWidth = std::clamp(m_Width * 0.18f * QmHudMediaIslandDesignScale, QmHudMediaIslandScaled(42.0f), QmHudMediaIslandScaled(88.0f));
	float RightSlotWidth = 0.0f;
	if(ShowInfoStack)
		RightSlotWidth = InfoStackWidth;
	else if(ShowRecordingStatus)
		RightSlotWidth = ScoreboardExpanded ? RawExpandedStatusWidth : RawCollapsedStatusWidth;
	const float RightSlotGap = RightSlotWidth > 0.0f && (TimerCapsule.m_Visible || BaseWidth > 0.0f) ? TimerToStatusGap : 0.0f;
	const float TimerBoxX = TimerCapsule.m_Visible ? std::round(m_Width * 0.5f - TimerCapsule.m_BoxW * 0.5f) : TimerCapsule.m_BoxX;
	const float TimerBoxRight = TimerBoxX + TimerCapsule.m_BoxW;
	const float MaxIslandWidth = TimerCapsule.m_Visible ?
					     std::max(BaseWidth, TimerBoxX - GapToTimer - RightSlotGap - RightSlotWidth - ScreenPadding) :
					     BaseWidth + (ShowCover ? (Gap + MaxTitleWidth) : 0.0f);
	const float MaxExpandedTitleWidth = std::max(0.0f, MaxIslandWidth - BaseWidth - Gap);
	const char *pDisplayTitle = "";
	if(HasMediaState)
	{
		if(MediaState.m_aTitle[0] != '\0')
			pDisplayTitle = MediaState.m_aTitle;
		else if(MediaState.m_aArtist[0] != '\0')
			pDisplayTitle = MediaState.m_aArtist;
		else if(MediaState.m_aAlbum[0] != '\0')
			pDisplayTitle = MediaState.m_aAlbum;
	}
	char aAvoidanceTrackMeta[256];
	aAvoidanceTrackMeta[0] = '\0';
	if(HasMediaState && MediaState.m_aArtist[0] != '\0' && MediaState.m_aAlbum[0] != '\0')
		str_format(aAvoidanceTrackMeta, sizeof(aAvoidanceTrackMeta), "%s - %s", MediaState.m_aArtist, MediaState.m_aAlbum);
	else if(HasMediaState && MediaState.m_aArtist[0] != '\0')
		str_copy(aAvoidanceTrackMeta, MediaState.m_aArtist, sizeof(aAvoidanceTrackMeta));
	else if(HasMediaState && MediaState.m_aAlbum[0] != '\0')
		str_copy(aAvoidanceTrackMeta, MediaState.m_aAlbum, sizeof(aAvoidanceTrackMeta));
	const float NaturalTitleWidth = std::round(std::max(TextRender()->TextBoundingBox(TitleFontSize, pDisplayTitle).m_W, aAvoidanceTrackMeta[0] != '\0' ? TextRender()->TextBoundingBox(MetaFontSize, aAvoidanceTrackMeta).m_W : 0.0f));
	const float TitleWidth = (TrackDetailsExpanded && ShowCover) ? std::clamp(NaturalTitleWidth, 0.0f, std::min(MaxTitleWidth, MaxExpandedTitleWidth)) : 0.0f;

	float TargetWidth = BaseWidth;
	if(TrackDetailsExpanded && TitleWidth > 0.0f)
		TargetWidth += Gap + TitleWidth;

	const float PlannedUnifiedWidth = TimerCapsule.m_Visible ?
						  TargetWidth + GapToTimer + TimerCapsule.m_BoxW + RightSlotGap + RightSlotWidth :
						  TargetWidth + RightSlotGap + RightSlotWidth;
	float TargetX = m_Width * 0.5f - TargetWidth * 0.5f;
	if(TimerCapsule.m_Visible)
		TargetX = TargetWidth > 0.0f ? std::max(ScreenPadding, TimerBoxX - GapToTimer - TargetWidth) : TimerBoxX;
	else if(RightSlotWidth > 0.0f)
		TargetX = m_Width * 0.5f - PlannedUnifiedWidth * 0.5f;
	else
	{
		const float MaxTargetX = std::max(ScreenPadding, m_Width - ScreenPadding - TargetWidth);
		TargetX = std::clamp(TargetX, ScreenPadding, MaxTargetX);
	}
	TargetX = std::max(ScreenPadding, TargetX);

	const float StatusAnchorRight = TimerCapsule.m_Visible ? TimerBoxRight : TargetX + TargetWidth;
	const float UnifiedRight = StatusAnchorRight + RightSlotGap + RightSlotWidth;
	const float AvoidanceRight = ShowSpectatorSatellite ? UnifiedRight + SpectatorSatelliteRestGap + SpectatorSatelliteWidth : UnifiedRight;
	m_MediaIslandFrameCache.m_AvoidanceValid = true;
	m_MediaIslandFrameCache.m_AvoidanceRight = AvoidanceRight;
	return AvoidanceRight;
}

void CHud::RenderMediaIsland()
{
	EnsureMediaIslandFrameCache();
	auto &AnimState = m_MediaIslandAnimState;
	const int64_t Now = time_get();
	const CSystemMediaControls::SState &MediaState = m_MediaIslandFrameCache.m_MediaState;
	const bool HasMediaState = m_MediaIslandFrameCache.m_HasMediaState;
	const bool LyricsActive = m_MediaIslandFrameCache.m_LyricsActive;
	if(!HasMediaState)
	{
		AnimState.m_WaveformWasPlaying = false;
		AnimState.m_WaveformSettling = false;
		AnimState.m_WaveformSettleStartTick = 0;
		AnimState.m_WaveformSettleSampleTime = 0.0f;
	}
	SSwapCountdownList SwapList;
	BuildSwapCountdownList(*GameClient(), *Client(), SwapList);
	std::array<const SSwapCountdownInfo *, NUM_DUMMIES * MAX_CLIENTS> aIncomingSwapInfos{};
	int IncomingSwapCount = 0;
	for(int i = 0; i < SwapList.m_Count && IncomingSwapCount < (int)aIncomingSwapInfos.size(); ++i)
	{
		if(!SwapList.m_aInfos[i].m_Outgoing)
			aIncomingSwapInfos[IncomingSwapCount++] = &SwapList.m_aInfos[i];
	}

	std::array<SHudMediaIslandCountdownInput, SHudMediaIslandAnimState::SATELLITE_LIVE_MAX_ITEMS> aCountdownInputs{};
	int CountdownInputCount = 0;
	for(int i = 0; i < SwapList.m_Count && CountdownInputCount < (int)aCountdownInputs.size(); ++i)
	{
		const SSwapCountdownInfo &Info = SwapList.m_aInfos[i];
		aCountdownInputs[CountdownInputCount++] = QmHudMediaIslandSwapCountdownInput(Info.m_InstanceId, Info.m_StartTick, Info.m_Lifecycle, Info.m_Outgoing);
	}

	const int TickSpeed = Client()->GameTickSpeed();
	const int Team = GameClient()->SwitchStateTeam();
	const int SwitchNow = Client()->GameTick(g_Config.m_ClDummy);
	if(g_Config.m_QmSwitchCountdown &&
		QmHudSwitchCountdownShowsMediaIsland(g_Config.m_QmSwitchCountdownMode) &&
		TickSpeed > 0 && Team >= 0 && Team < NUM_DDRACE_TEAMS)
	{
		std::array<SHudMediaIslandCountdownInput, 255> aSwitchInputs{};
		int SwitchInputCount = 0;
		for(int Number = 1; Number < 256; ++Number)
		{
			const int EndTick = m_SwitchCountdownTracker.m_aaEndTick[Team][Number];
			const int TouchTick = m_SwitchCountdownTracker.m_aaTouchTick[Team][Number];
			if(EndTick <= SwitchNow || TouchTick <= 0)
				continue;
			SHudMediaIslandCountdownInput &Input = aSwitchInputs[SwitchInputCount++];
			const int InstanceId = QmHudMediaIslandSwitchInstanceId(Team, Number);
			Input = {EHudMediaIslandCountdownType::SWITCH, InstanceId, TouchTick, EndTick, EndTick - TouchTick};
			Input.m_Progress = QmHudMediaIslandCountdownProgress(Input, SwitchNow);
		}
		QmHudSortMediaIslandCountdowns(aSwitchInputs.data(), SwitchInputCount);
		const int VisibleSwitchCount = minimum(SwitchInputCount, SWITCH_COUNTDOWN_MAX_LINES);
		const int FirstVisibleSwitch = QmHudMediaIslandVisibleSuffixStart(SwitchInputCount, VisibleSwitchCount);
		for(int i = FirstVisibleSwitch; i < SwitchInputCount && CountdownInputCount < (int)aCountdownInputs.size(); ++i)
			aCountdownInputs[CountdownInputCount++] = aSwitchInputs[i];
	}

	if(m_MediaIslandMuteState.m_Confirmed && Now < m_MediaIslandMuteState.m_EndTick && CountdownInputCount < (int)aCountdownInputs.size())
	{
		SHudMediaIslandCountdownInput &Input = aCountdownInputs[CountdownInputCount++];
		Input = {EHudMediaIslandCountdownType::MUTE, 0, m_MediaIslandMuteState.m_TriggerTick, m_MediaIslandMuteState.m_EndTick, m_MediaIslandMuteState.m_DurationTicks};
		Input.m_Progress = QmHudMediaIslandCountdownProgress(Input, Now);
	}
	else if(m_MediaIslandMuteState.m_Confirmed && Now >= m_MediaIslandMuteState.m_EndTick)
	{
		m_MediaIslandMuteState.Reset();
	}
	QmHudSortMediaIslandCountdowns(aCountdownInputs.data(), CountdownInputCount);

	for(auto &Item : AnimState.m_aSatelliteItems)
		Item.m_Seen = false;
	for(int i = 0; i < CountdownInputCount; ++i)
	{
		const SHudMediaIslandCountdownInput &Input = aCountdownInputs[i];
		auto *pItem = static_cast<SHudMediaIslandAnimState::SSatelliteItem *>(nullptr);
		for(auto &Item : AnimState.m_aSatelliteItems)
		{
			if(Item.m_Used && Item.m_Type == Input.m_Type && Item.m_Id == Input.m_Id)
			{
				pItem = &Item;
				break;
			}
		}
		if(pItem == nullptr)
		{
			for(auto &Item : AnimState.m_aSatelliteItems)
			{
				if(!Item.m_Used)
				{
					pItem = &Item;
					break;
				}
			}
		}
		if(pItem == nullptr)
		{
			for(auto &Item : AnimState.m_aSatelliteItems)
			{
				if(!Item.m_Active)
				{
					Item.Reset();
					pItem = &Item;
					break;
				}
			}
		}
		if(pItem == nullptr)
			continue;

		if(!pItem->m_Used)
		{
			pItem->Reset();
			pItem->m_Used = true;
			pItem->m_Type = Input.m_Type;
			pItem->m_Id = Input.m_Id;
			pItem->m_TriggerTick = Input.m_TriggerTick;
		}
		pItem->m_Active = true;
		pItem->m_Seen = true;
		pItem->m_ExitStartTick = 0;
		pItem->m_Progress = Input.m_Progress;
		pItem->m_Completed = Input.m_Completed;
		pItem->m_SwapOutgoing = Input.m_SwapOutgoing;
	}

	for(auto &Item : AnimState.m_aSatelliteItems)
	{
		if(Item.m_Used && Item.m_Active && !Item.m_Seen)
		{
			Item.m_Active = false;
			Item.m_ExitStartTick = Now;
		}
	}
	const bool HadSatellitePresentation = AnimState.HasVisibleSatellite();
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
	char aFrozenSummaryBuf[64];
	const bool ShowFrozenSummary = BuildHudFrozenSummaryText(FrozenInfo, aFrozenSummaryBuf, sizeof(aFrozenSummaryBuf));

	const bool ShowLocalTime = ShouldRenderHudLocalTime(*GameClient());
	const bool ShowInfoStack = ShowLocalTime || ShowFrozenSummary;
	const SHudGameTimerInfo TimerInfo = g_Config.m_ClShowhudTimer ? BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width) : SHudGameTimerInfo{};
	SHudTopTimerCapsuleInfo TimerCapsule = BuildHudTopTimerCapsuleInfo(TimerInfo);
	char aRecordingBuf[512];
	const bool ShowRecordingStatus = TimerCapsule.m_Visible && BuildHudRecordingStatusText(*GameClient(), aRecordingBuf, sizeof(aRecordingBuf));
	const bool ScoreboardExpanded = GameClient()->m_Scoreboard.IsActive();
	const int SpectatorCount = m_MediaIslandFrameCache.m_SpectatorCount;
	const bool ShowSpectator = SpectatorCount > 0;
	const float SpectatorLiquidProgressBeforeUpdate = AnimState.m_SpectatorLiquidProgress;
	const bool AnimateSpectatorEyeOpen = QmHudMediaIslandShouldAnimateSpectatorEyeOpen(ShowSpectator, AnimState.m_SpectatorHadWatchers, SpectatorLiquidProgressBeforeUpdate);
	if(ShowSpectator)
		AnimState.m_SpectatorDisplayCount = SpectatorCount;
	if(!ShowSpectator && AnimState.m_SpectatorHadWatchers)
	{
		AnimState.m_SpectatorExitLiquidStart = SpectatorLiquidProgressBeforeUpdate;
		AnimState.m_SpectatorExitIconStart = AnimState.m_SpectatorIconProgress;
	}
	float SpectatorLiquidDeltaSeconds = 0.0f;
	if(AnimState.m_SpectatorLiquidLastTick > 0 && Now >= AnimState.m_SpectatorLiquidLastTick)
		SpectatorLiquidDeltaSeconds = std::min((Now - AnimState.m_SpectatorLiquidLastTick) / (float)time_freq(), 0.10f);
	AnimState.m_SpectatorLiquidLastTick = Now;
	AnimState.m_SpectatorLiquidProgress = QmHudAdvanceMediaIslandLiquidProgress(AnimState.m_SpectatorLiquidProgress, ShowSpectator, SpectatorLiquidDeltaSeconds, g_Config.m_QmUiMotionLevel > 0);
	if(!ShowSpectator && AnimState.m_SpectatorLiquidProgress <= 0.0f)
		AnimState.m_SpectatorLiquidLastTick = 0;
	if(!ShowSpectator)
	{
		AnimState.m_SpectatorIconProgress = QmHudMediaIslandSpectatorIconProgressDuringExit(AnimState.m_SpectatorExitIconStart, AnimState.m_SpectatorExitLiquidStart, AnimState.m_SpectatorLiquidProgress);
		AnimState.m_SpectatorIconLastTick = 0;
	}
	else if(AnimateSpectatorEyeOpen)
	{
		AnimState.m_SpectatorIconLastTick = Now;
	}
	else if(!AnimState.m_SpectatorHadWatchers)
	{
		AnimState.m_SpectatorIconProgress = 1.0f;
		AnimState.m_SpectatorIconLastTick = 0;
	}
	if(ShowSpectator && AnimState.m_SpectatorIconProgress < 1.0f)
	{
		float SpectatorIconDeltaSeconds = 0.0f;
		if(AnimState.m_SpectatorIconLastTick > 0 && Now >= AnimState.m_SpectatorIconLastTick)
			SpectatorIconDeltaSeconds = std::min((Now - AnimState.m_SpectatorIconLastTick) / (float)time_freq(), 0.10f);
		AnimState.m_SpectatorIconLastTick = Now;
		AnimState.m_SpectatorIconProgress = QmHudAdvanceMediaIslandSpectatorIconProgress(AnimState.m_SpectatorIconProgress, SpectatorIconDeltaSeconds, g_Config.m_QmUiMotionLevel);
		if(AnimState.m_SpectatorIconProgress >= 1.0f)
			AnimState.m_SpectatorIconLastTick = 0;
	}
	AnimState.m_SpectatorHadWatchers = ShowSpectator;
	const bool HasSpectatorSatellitePresentation = ShowSpectator || AnimState.m_SpectatorLiquidProgress > 0.0f;
	const bool HasSatellitePresentation = HadSatellitePresentation || HasSpectatorSatellitePresentation;
	char aTeamBuf[32];
	const bool ShowTeam = BuildHudTeamText(*GameClient(), aTeamBuf, sizeof(aTeamBuf));
	const char *pLyricsIslandBuf = m_MediaIslandFrameCache.m_aLyrics;
	const ColorRGBA &LyricsIslandColor = m_MediaIslandFrameCache.m_LyricsColor;
	const bool ShowLyricsIslandLine = m_MediaIslandFrameCache.m_ShowLyrics || LyricsActive;
	const SHudMediaIslandSwapRows SwapRows = QmHudMediaIslandSwapRows(IncomingSwapCount, TimerCapsule.m_Visible, ShowLyricsIslandLine);
	const bool ShowTopRow = HasMediaState || ShowInfoStack || TimerCapsule.m_Visible || ShowRecordingStatus || ShowSpectator || ShowTeam;

	if(!ShowTopRow && !ShowLyricsIslandLine && !HasSatellitePresentation)
	{
		m_MediaIslandAnimState.Reset();
		m_MediaIslandLastVisibleRectValid = false;
		return;
	}

	const int64_t AutoCollapseTicks = std::max<int64_t>(1, (int64_t)3000 * time_freq() / 1000);
	const bool WasLyricsActive = AnimState.m_LyricsActive;

	if(HasMediaState)
	{
		const bool HadMeaningfulTrackIdentity = AnimState.m_HasTrackIdentity && AnimState.m_CurrentTrack.HasMeaningfulIdentity();
		SHudMediaIslandTrackInput TrackInput;
		TrackInput.m_pTitle = MediaState.m_aTitle;
		TrackInput.m_pArtist = MediaState.m_aArtist;
		TrackInput.m_pAlbum = MediaState.m_aAlbum;
		TrackInput.m_Cover = MediaState.m_AlbumArtCircular;
		TrackInput.m_HasCover = MediaState.m_AlbumArtCircular.IsValid() && !MediaState.m_AlbumArtCircular.IsNullTexture();
		TrackInput.m_DurationMs = MediaState.m_DurationMs;
		const EHudMediaIslandTrackUpdate TrackUpdate = QmHudMediaIslandUpdateTrackSnapshots(
			AnimState.m_CurrentTrack,
			AnimState.m_OutgoingTrack,
			AnimState.m_HasTrackIdentity,
			AnimState.m_TrackTransitionActive,
			AnimState.m_TrackTransitionNeedsNodeReset,
			AnimState.m_TrackTransitionStartTick,
			Now,
			TrackInput);

		if(QmHudMediaIslandShouldRevealTrackDetails(
			   TrackUpdate,
			   HadMeaningfulTrackIdentity,
			   AnimState.m_CurrentTrack.HasMeaningfulIdentity()))
		{
			AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::EXPANDED;
			AnimState.m_ExpandUntilTick = Now + AutoCollapseTicks;
			AnimState.m_TrackDetailsUntilTick = Now + AutoCollapseTicks;
			AnimState.StartCapsuleMorph(Now);
		}
	}
	else if(AnimState.m_HasTrackIdentity && !LyricsActive)
	{
		AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::MINIMIZED;
		AnimState.m_ExpandUntilTick = 0;
		AnimState.m_HasTrackIdentity = false;
		AnimState.m_CurrentTrack.Reset();
		AnimState.m_OutgoingTrack.Reset();
		AnimState.m_TrackTransitionActive = false;
		AnimState.m_TrackTransitionNeedsNodeReset = false;
		AnimState.m_TrackTransitionStartTick = 0;
		AnimState.m_OldTrackExitProgress = 1.0f;
		AnimState.m_NewTrackEnterProgress = 1.0f;
		AnimState.m_TrackDetailsUntilTick = 0;
	}

	if(LyricsActive)
	{
		if(AnimState.m_VisualState != SHudMediaIslandAnimState::EVisualState::EXPANDED)
		{
			AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::EXPANDED;
			AnimState.StartCapsuleMorph(Now);
		}
	}
	else
	{
		if(WasLyricsActive && HasMediaState && AnimState.m_VisualState == SHudMediaIslandAnimState::EVisualState::EXPANDED)
			AnimState.m_ExpandUntilTick = Now + AutoCollapseTicks;
		if(AnimState.m_VisualState == SHudMediaIslandAnimState::EVisualState::EXPANDED && AnimState.m_ExpandUntilTick > 0 && Now >= AnimState.m_ExpandUntilTick)
		{
			AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::MINIMIZED;
			AnimState.m_ExpandUntilTick = 0;
			AnimState.m_TrackDetailsUntilTick = 0;
			AnimState.StartCapsuleMorph(Now);
		}
	}
	AnimState.m_LyricsActive = LyricsActive;
	const bool TrackDetailsExpanded = HasMediaState && QmHudMediaIslandShouldShowTrackDetails(Now, AnimState.m_TrackDetailsUntilTick);
	const char *pDisplayTitle = "";
	if(HasMediaState)
	{
		if(AnimState.m_CurrentTrack.m_aTitle[0] != '\0')
			pDisplayTitle = AnimState.m_CurrentTrack.m_aTitle;
		else if(AnimState.m_CurrentTrack.m_aArtist[0] != '\0')
			pDisplayTitle = AnimState.m_CurrentTrack.m_aArtist;
		else if(AnimState.m_CurrentTrack.m_aAlbum[0] != '\0')
			pDisplayTitle = AnimState.m_CurrentTrack.m_aAlbum;
	}

	char aSpectatorBuf[16];
	str_format(aSpectatorBuf, sizeof(aSpectatorBuf), "%d", std::max(0, AnimState.m_SpectatorDisplayCount));

	char aTimeBuf[16];
	str_timestamp_format(aTimeBuf, sizeof(aTimeBuf), "%H:%M");
	if(aTimeBuf[0] == '\0')
		str_copy(aTimeBuf, "00:00", sizeof(aTimeBuf));

	const int Checkpoint = GetDisplayedCheckpoint(*GameClient());
	char aCheckpointBuf[16];
	aCheckpointBuf[0] = '\0';
	if(Checkpoint > 0)
		str_format(aCheckpointBuf, sizeof(aCheckpointBuf), "CP%d", Checkpoint);

	constexpr float BaseIslandHeight = QmHudMediaIslandScaled(16.0f);
	const float IslandY = 1.0f;
	constexpr float CoverSize = QmHudMediaIslandScaled(12.0f);
	constexpr float PaddingX = QmHudMediaIslandScaled(2.0f);
	constexpr float Gap = QmHudMediaIslandScaled(2.0f);
	constexpr float SpectatorGap = QmHudMediaIslandScaled(1.0f);
	constexpr float SpectatorSatellitePaddingX = QmHudMediaIslandScaled(2.0f);
	constexpr float SpectatorSatelliteIconSize = QmHudMediaIslandScaled(6.4f);
	constexpr float SpectatorSatelliteRestGap = QmHudMediaIslandScaled(3.0f);
	constexpr float TitleFontSize = QmHudMediaIslandScaled(5.8f);
	constexpr float MetaFontSize = QmHudMediaIslandScaled(5.3f);
	constexpr float ScreenPadding = 5.0f;
	constexpr float GapToTimer = QmHudMediaIslandScaled(3.0f);
	constexpr float TimerToStatusGap = QmHudMediaIslandScaled(3.0f);
	constexpr float StatusPaddingLeft = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusPaddingRight = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotSize = QmHudMediaIslandScaled(5.0f);
	constexpr float StatusDotGap = QmHudMediaIslandScaled(4.0f);
	constexpr float StatusFontSize = QmHudMediaIslandScaled(5.3f);
	constexpr float StackedStatusFontSize = QmHudMediaIslandScaled(4.4f);
	constexpr float InfoStackGap = QmHudMediaIslandScaled(0.8f);
	constexpr float WaveformSlotWidth = QmHudMediaIslandScaled(12.0f);
	constexpr float BottomFontSize = QmHudMediaIslandScaled(5.2f);
	constexpr float BottomRowPaddingX = QmHudMediaIslandScaled(7.0f);
	constexpr float BottomRowLineHeight = QmHudMediaIslandScaled(7.0f);
	constexpr float BottomRowPaddingY = QmHudMediaIslandScaled(2.5f);
	constexpr float BottomRowDividerInset = QmHudMediaIslandScaled(7.0f);
	const float MaxUnifiedWidth = std::max(0.0f, m_Width - ScreenPadding * 2.0f);
	const float MaxTitleWidth = std::clamp(m_Width * 0.18f * QmHudMediaIslandDesignScale, QmHudMediaIslandScaled(42.0f), QmHudMediaIslandScaled(88.0f));
	float IncomingSwapTextWidth = 0.0f;
	for(int i = 0; i < IncomingSwapCount; ++i)
		IncomingSwapTextWidth = std::max(IncomingSwapTextWidth, std::round(TextRender()->TextBoundingBox(BottomFontSize, aIncomingSwapInfos[i]->m_aRequestText).m_W));

	const bool ShowWaveform = HasMediaState;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	const float TeamIconWidth = TextRender()->TextWidth(MetaFontSize, FontIcons::FONT_ICON_USERS);
	const float PlaceholderWidth = TextRender()->TextWidth(MetaFontSize, FontIcons::FONT_ICON_MUSIC);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	const float SpectatorTextWidth = HasSpectatorSatellitePresentation ? TextRender()->TextWidth(MetaFontSize, aSpectatorBuf) : 0.0f;
	const float SpectatorSatelliteContentWidth = SpectatorSatelliteIconSize + SpectatorGap + SpectatorTextWidth;
	const float SpectatorSatelliteWidth = std::max(BaseIslandHeight, SpectatorSatelliteContentWidth + SpectatorSatellitePaddingX * 2.0f);
	const float TeamTextWidth = ShowTeam ? TextRender()->TextWidth(MetaFontSize, aTeamBuf) : 0.0f;
	const float TeamWidth = ShowTeam ? TeamIconWidth + SpectatorGap + TeamTextWidth : 0.0f;
	const float StatusTextWidth = ShowRecordingStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aRecordingBuf).m_W) : 0.0f;
	const float RawCollapsedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusPaddingRight;
	const float RawExpandedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusDotGap + StatusTextWidth + StatusPaddingRight;
	const bool StackHasTwoRows = ShowLocalTime && ShowFrozenSummary;
	const float InfoStackFontSize = StackHasTwoRows ? StackedStatusFontSize : StatusFontSize;
	const float LocalTimeTextWidth = ShowLocalTime ? std::round(TextRender()->TextBoundingBox(InfoStackFontSize, aTimeBuf).m_W) : 0.0f;
	const float FrozenSummaryTextWidth = ShowFrozenSummary ? std::round(TextRender()->TextBoundingBox(InfoStackFontSize, aFrozenSummaryBuf).m_W) : 0.0f;
	const float InfoStackWidth = ShowInfoStack ? StatusPaddingLeft + std::max(LocalTimeTextWidth, FrozenSummaryTextWidth) + StatusPaddingRight : 0.0f;
	float PlannedStatusWidth = 0.0f;
	if(ShowInfoStack)
		PlannedStatusWidth = InfoStackWidth;
	else if(ShowRecordingStatus)
		PlannedStatusWidth = ScoreboardExpanded ? RawExpandedStatusWidth : RawCollapsedStatusWidth;
	const bool ShowBottomRow = SwapRows.m_BottomLineCount > 0;
	const int BottomRowLineCount = SwapRows.m_BottomLineCount;
	const float DesiredBottomUnifiedWidth = QmHudMediaIslandDesiredBottomWidth(
		ShowLyricsIslandLine,
		ShowTopRow,
		IncomingSwapCount > 0,
		IncomingSwapTextWidth,
		MaxTitleWidth,
		MaxUnifiedWidth,
		BottomRowPaddingX);
	const bool ShowCover = HasMediaState;
	const int MetaItemCount = (ShowTeam ? 1 : 0) + (ShowWaveform ? 1 : 0);
	float BaseWidth = PaddingX;
	if(ShowCover)
		BaseWidth += CoverSize;
	if(ShowCover && MetaItemCount > 0)
		BaseWidth += Gap;
	if(ShowTeam)
		BaseWidth += TeamWidth;
	if(ShowTeam && ShowWaveform)
		BaseWidth += Gap;
	if(ShowWaveform)
		BaseWidth += WaveformSlotWidth;
	BaseWidth += PaddingX;
	if(!ShowCover && MetaItemCount == 0)
		BaseWidth = 0.0f;
	if(HasSatellitePresentation)
		BaseWidth = std::max(BaseWidth, BaseIslandHeight);
	if(SwapRows.m_InlineSwapCount > 0)
	{
		const float ReservedWidth = BaseWidth + (BaseWidth > 0.0f ? GapToTimer : 0.0f) + PlannedStatusWidth + (PlannedStatusWidth > 0.0f ? TimerToStatusGap : 0.0f);
		const float MaxTimerWidth = std::max(TimerCapsule.m_BoxW, MaxUnifiedWidth - ReservedWidth);
		TimerCapsule.m_BoxW = std::min(MaxTimerWidth, std::max(TimerCapsule.m_BoxW, IncomingSwapTextWidth + BottomRowPaddingX * 2.0f));
	}
	const float TimerBoxX = TimerCapsule.m_Visible ? std::round(m_Width * 0.5f - TimerCapsule.m_BoxW * 0.5f) : TimerCapsule.m_BoxX;
	const float TimerBoxRight = TimerBoxX + TimerCapsule.m_BoxW;
	const float MaxIslandWidth = TimerCapsule.m_Visible ?
					     std::max(BaseWidth, TimerBoxX - GapToTimer - ScreenPadding) :
					     BaseWidth + (ShowCover ? (Gap + MaxTitleWidth) : 0.0f);
	const float MaxExpandedTitleWidth = std::max(0.0f, MaxIslandWidth - BaseWidth - Gap);
	char aLayoutTrackMeta[256];
	aLayoutTrackMeta[0] = '\0';
	if(AnimState.m_CurrentTrack.m_aArtist[0] != '\0' && AnimState.m_CurrentTrack.m_aAlbum[0] != '\0')
		str_format(aLayoutTrackMeta, sizeof(aLayoutTrackMeta), "%s - %s", AnimState.m_CurrentTrack.m_aArtist, AnimState.m_CurrentTrack.m_aAlbum);
	else if(AnimState.m_CurrentTrack.m_aArtist[0] != '\0')
		str_copy(aLayoutTrackMeta, AnimState.m_CurrentTrack.m_aArtist, sizeof(aLayoutTrackMeta));
	else if(AnimState.m_CurrentTrack.m_aAlbum[0] != '\0')
		str_copy(aLayoutTrackMeta, AnimState.m_CurrentTrack.m_aAlbum, sizeof(aLayoutTrackMeta));
	const float NaturalTitleWidth = std::round(std::max(TextRender()->TextBoundingBox(TitleFontSize, pDisplayTitle).m_W, aLayoutTrackMeta[0] != '\0' ? TextRender()->TextBoundingBox(MetaFontSize, aLayoutTrackMeta).m_W : 0.0f));
	const float TitleWidth = (TrackDetailsExpanded && ShowCover) ? std::clamp(NaturalTitleWidth, 0.0f, std::min(MaxTitleWidth, MaxExpandedTitleWidth)) : 0.0f;
	float TargetWidth = BaseWidth;
	if(TrackDetailsExpanded && TitleWidth > 0.0f)
		TargetWidth += Gap + TitleWidth;
	float PlannedUnifiedWidth = TimerCapsule.m_Visible ?
					    (TargetWidth + GapToTimer + TimerCapsule.m_BoxW + (PlannedStatusWidth > 0.0f ? (TimerToStatusGap + PlannedStatusWidth) : 0.0f)) :
					    (TargetWidth + (TargetWidth > 0.0f && PlannedStatusWidth > 0.0f ? TimerToStatusGap : 0.0f) + PlannedStatusWidth);
	if(DesiredBottomUnifiedWidth > PlannedUnifiedWidth)
	{
		const float ExtraWidth = DesiredBottomUnifiedWidth - PlannedUnifiedWidth;
		TargetWidth += ExtraWidth;
		PlannedUnifiedWidth += ExtraWidth;
	}
	float TargetX = m_Width * 0.5f - TargetWidth * 0.5f;
	if(TimerCapsule.m_Visible)
		TargetX = TargetWidth > 0.0f ? std::max(ScreenPadding, TimerBoxX - GapToTimer - TargetWidth) : TimerBoxX;
	else if(PlannedStatusWidth > 0.0f)
		TargetX = m_Width * 0.5f - PlannedUnifiedWidth * 0.5f;
	else
	{
		const float MaxTargetX = std::max(ScreenPadding, m_Width - ScreenPadding - TargetWidth);
		TargetX = std::clamp(TargetX, ScreenPadding, MaxTargetX);
	}
	TargetX = std::max(ScreenPadding, TargetX);
	const float TargetBottomHeight = ShowBottomRow ? (BottomRowPaddingY * 2.0f + BottomRowLineHeight * BottomRowLineCount) : 0.0f;
	const float TargetHeight = BaseIslandHeight + TargetBottomHeight;
	const float TitleAlphaTarget = TrackDetailsExpanded && TitleWidth > 0.0f ? 1.0f : 0.0f;
	const float TitleOffsetTarget = TrackDetailsExpanded ? 0.0f : QmHudMediaIslandScaled(4.0f);
	const float BottomAlphaTarget = ShowBottomRow ? 1.0f : 0.0f;
	const int MotionLevel = std::clamp(g_Config.m_QmUiMotionLevel, 0, 2);
	float EntranceDeltaSeconds = 0.0f;
	if(AnimState.m_EntranceLastTick > 0 && Now >= AnimState.m_EntranceLastTick)
		EntranceDeltaSeconds = std::min((Now - AnimState.m_EntranceLastTick) / (float)time_freq(), 0.10f);
	AnimState.m_EntranceLastTick = Now;
	const SHudMediaIslandEntranceTimeline EntranceTimeline = QmHudAdvanceMediaIslandEntranceTimeline(
		{AnimState.m_EntranceDropProgress, AnimState.m_EntranceProgress}, EntranceDeltaSeconds, MotionLevel);
	AnimState.m_EntranceDropProgress = EntranceTimeline.m_DropProgress;
	AnimState.m_EntranceProgress = EntranceTimeline.m_ExpandProgress;
	const bool FullTrackMotion = MotionLevel >= 2;
	const float TrackTextOffset = FullTrackMotion ? QmHudMediaIslandScaled(5.0f) : 0.0f;
	const float CoverEnterScale = FullTrackMotion ? 0.95f : 1.0f;
	const float CoverExitScale = FullTrackMotion ? 0.96f : 1.0f;
	SUiSpringConfig CapsuleSpring;
	CapsuleSpring.m_Stiffness = FullTrackMotion ? 430.0f : 520.0f;
	CapsuleSpring.m_Damping = FullTrackMotion ? 38.0f : 48.0f;
	CapsuleSpring.m_RestEpsilon = 0.025f;
	CapsuleSpring.m_RestVelocity = 0.16f;
	SUiSpringConfig SatelliteSpring = CapsuleSpring;
	SatelliteSpring.m_Stiffness = FullTrackMotion ? 500.0f : 620.0f;
	SatelliteSpring.m_Damping = FullTrackMotion ? 39.0f : 54.0f;
	SatelliteSpring.m_RestEpsilon = 0.012f;
	SatelliteSpring.m_RestVelocity = 0.10f;
	const auto BuildContentSpring = [FullTrackMotion](bool Entering) {
		SUiSpringConfig Spring;
		Spring.m_Stiffness = FullTrackMotion ? 360.0f : 520.0f;
		Spring.m_Damping = FullTrackMotion ? 34.0f : 52.0f;
		Spring.m_RestEpsilon = 0.008f;
		Spring.m_RestVelocity = 0.08f;
		if(!Entering)
		{
			constexpr float ExitTimeScale = 0.34f;
			Spring.m_Stiffness /= ExitTimeScale * ExitTimeScale;
			Spring.m_Damping /= ExitTimeScale;
			Spring.m_RestVelocity /= ExitTimeScale;
		}
		return Spring;
	};
	SUiSpringConfig ContentSpring = BuildContentSpring(true);
	SUiSpringConfig ContentExitSpring = BuildContentSpring(false);
	const SUiSpringConfig TitleSpring = TitleAlphaTarget > 0.0f ? ContentSpring : ContentExitSpring;
	const SUiSpringConfig BottomSpring = BottomAlphaTarget > 0.0f ? ContentSpring : ContentExitSpring;
	SUiSpringConfig TrackSpring;
	TrackSpring.m_Stiffness = FullTrackMotion ? 520.0f : 650.0f;
	TrackSpring.m_Damping = FullTrackMotion ? 38.0f : 58.0f;
	TrackSpring.m_RestEpsilon = 0.008f;
	TrackSpring.m_RestVelocity = 0.10f;
	SUiSpringConfig TrackExitSpring = TrackSpring;
	{
		constexpr float ExitTimeScale = 0.42f;
		TrackExitSpring.m_Stiffness /= ExitTimeScale * ExitTimeScale;
		TrackExitSpring.m_Damping /= ExitTimeScale;
		TrackExitSpring.m_RestVelocity /= ExitTimeScale;
	}
	float EffectiveTargetX = TargetX;
	float EffectiveTargetWidth = TargetWidth;
	float EffectiveTargetHeight = TargetHeight;

	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	const uint64_t CapsuleNode = HudMediaIslandNodeKey("capsule");
	const uint64_t TitleNode = HudMediaIslandNodeKey("title");
	const uint64_t BottomNode = HudMediaIslandNodeKey("bottom");
	const uint64_t CoverInNode = HudMediaIslandNodeKey("cover_in");
	const uint64_t CoverOutNode = HudMediaIslandNodeKey("cover_out");
	const uint64_t TrackTitleInNode = HudMediaIslandNodeKey("track_title_in");
	const uint64_t TrackTitleOutNode = HudMediaIslandNodeKey("track_title_out");
	const uint64_t TrackMetaInNode = HudMediaIslandNodeKey("track_meta_in");
	const uint64_t TrackMetaOutNode = HudMediaIslandNodeKey("track_meta_out");
	if(!AnimState.m_LayoutInitialized)
	{
		AnimState.m_TargetX = TargetX;
		AnimState.m_TargetWidth = TargetWidth;
		AnimState.m_TargetHeight = TargetHeight;
		AnimState.m_TargetTitleAlpha = TitleAlphaTarget;
		AnimState.m_TargetTitleOffset = TitleOffsetTarget;
		AnimState.m_TargetSpectatorAlpha = 0.0f;
		AnimState.m_TargetBottomAlpha = BottomAlphaTarget;
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, TargetX);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, TargetWidth);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, TargetHeight);
		SetUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::ALPHA, TitleAlphaTarget);
		SetUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::POS_X, TitleOffsetTarget);
		SetUiPresentationStateValue(AnimRuntime, BottomNode, EUiAnimProperty::ALPHA, BottomAlphaTarget);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::ALPHA, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::SCALE, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::SCALE, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::ALPHA, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::POS_X, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::POS_X, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::ALPHA, 1.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::POS_X, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::POS_X, 0.0f);
		AnimState.m_LayoutInitialized = true;
	}

	if(AnimState.m_TrackTransitionNeedsNodeReset)
	{
		const float CurrentCoverAlpha = AnimRuntime.GetValue(CoverInNode, EUiAnimProperty::ALPHA, 1.0f);
		const float CurrentCoverScale = AnimRuntime.GetValue(CoverInNode, EUiAnimProperty::SCALE, 1.0f);
		const float CurrentTitleAlpha = AnimRuntime.GetValue(TrackTitleInNode, EUiAnimProperty::ALPHA, 1.0f);
		const float CurrentTitleOffset = AnimRuntime.GetValue(TrackTitleInNode, EUiAnimProperty::POS_X, 0.0f);
		const float CurrentMetaAlpha = AnimRuntime.GetValue(TrackMetaInNode, EUiAnimProperty::ALPHA, 1.0f);
		const float CurrentMetaOffset = AnimRuntime.GetValue(TrackMetaInNode, EUiAnimProperty::POS_X, 0.0f);

		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::ALPHA, CurrentCoverAlpha);
		SetUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::SCALE, CurrentCoverScale);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::ALPHA, CurrentTitleAlpha);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::POS_X, CurrentTitleOffset);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::ALPHA, CurrentMetaAlpha);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::POS_X, CurrentMetaOffset);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::SCALE, CoverEnterScale);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::POS_X, TrackTextOffset);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::POS_X, TrackTextOffset);

		AnimState.m_TargetCoverOutAlpha = CurrentCoverAlpha;
		AnimState.m_TargetCoverOutScale = CurrentCoverScale;
		AnimState.m_TargetTrackTitleOutAlpha = CurrentTitleAlpha;
		AnimState.m_TargetTrackTitleOutOffset = CurrentTitleOffset;
		AnimState.m_TargetTrackMetaOutAlpha = CurrentMetaAlpha;
		AnimState.m_TargetTrackMetaOutOffset = CurrentMetaOffset;
		AnimState.m_TargetCoverInAlpha = 0.0f;
		AnimState.m_TargetCoverInScale = CoverEnterScale;
		AnimState.m_TargetTrackTitleInAlpha = 0.0f;
		AnimState.m_TargetTrackTitleInOffset = TrackTextOffset;
		AnimState.m_TargetTrackMetaInAlpha = 0.0f;
		AnimState.m_TargetTrackMetaInOffset = TrackTextOffset;
		AnimState.m_TrackTransitionNeedsNodeReset = false;
	}

	if(MotionLevel <= 0)
	{
		AnimState.m_CapsuleMorphActive = false;
		AnimState.m_CapsuleMorphNeedsCapture = false;
	}
	else if(AnimState.m_CapsuleMorphActive)
	{
		if(AnimState.m_CapsuleMorphNeedsCapture)
		{
			AnimState.m_CapsuleMorphFromX = AnimRuntime.GetValue(CapsuleNode, EUiAnimProperty::POS_X, AnimState.m_TargetX);
			AnimState.m_CapsuleMorphFromWidth = std::max(0.0f, AnimRuntime.GetValue(CapsuleNode, EUiAnimProperty::WIDTH, AnimState.m_TargetWidth));
			AnimState.m_CapsuleMorphFromHeight = std::max(0.0f, AnimRuntime.GetValue(CapsuleNode, EUiAnimProperty::HEIGHT, AnimState.m_TargetHeight));
			if(AnimState.m_CapsuleMorphFromWidth <= 0.01f)
				AnimState.m_CapsuleMorphFromWidth = TargetWidth;
			if(AnimState.m_CapsuleMorphFromHeight <= 0.01f)
				AnimState.m_CapsuleMorphFromHeight = TargetHeight;
			AnimState.m_CapsuleMorphNeedsCapture = false;
		}

		const float MorphElapsedSec = (Now - AnimState.m_CapsuleMorphStartTick) / (float)time_freq();
		constexpr float MorphCompressSec = 0.085f;
		constexpr float MorphMaxSec = 0.75f;
		if(FullTrackMotion && MorphElapsedSec < MorphCompressSec)
		{
			const float FromCenterX = AnimState.m_CapsuleMorphFromX + AnimState.m_CapsuleMorphFromWidth * 0.5f;
			const float WidthSqueeze = std::clamp(AnimState.m_CapsuleMorphFromWidth * 0.08f, QmHudMediaIslandScaled(2.0f), QmHudMediaIslandScaled(7.0f));
			const float HeightSqueeze = std::clamp(AnimState.m_CapsuleMorphFromHeight * 0.10f, QmHudMediaIslandScaled(1.0f), QmHudMediaIslandScaled(2.4f));
			EffectiveTargetWidth = std::max(PaddingX * 2.0f + QmHudMediaIslandScaled(4.0f), AnimState.m_CapsuleMorphFromWidth - WidthSqueeze);
			EffectiveTargetHeight = std::max(BaseIslandHeight - QmHudMediaIslandScaled(2.0f), AnimState.m_CapsuleMorphFromHeight - HeightSqueeze);
			EffectiveTargetX = std::clamp(FromCenterX - EffectiveTargetWidth * 0.5f, ScreenPadding, std::max(ScreenPadding, m_Width - ScreenPadding - EffectiveTargetWidth));
		}
		else if(MorphElapsedSec > MorphMaxSec)
		{
			AnimState.m_CapsuleMorphActive = false;
		}
	}

	AnimState.m_TargetX = EffectiveTargetX;
	AnimState.m_TargetWidth = EffectiveTargetWidth;
	AnimState.m_TargetHeight = EffectiveTargetHeight;
	AnimState.m_TargetTitleAlpha = TitleAlphaTarget;
	AnimState.m_TargetTitleOffset = TitleOffsetTarget;
	AnimState.m_TargetSpectatorAlpha = 0.0f;
	AnimState.m_TargetBottomAlpha = BottomAlphaTarget;
	AnimState.m_TargetCoverInAlpha = 1.0f;
	AnimState.m_TargetCoverOutAlpha = 0.0f;
	AnimState.m_TargetCoverInScale = 1.0f;
	AnimState.m_TargetCoverOutScale = CoverExitScale;
	AnimState.m_TargetTrackTitleInAlpha = 1.0f;
	AnimState.m_TargetTrackTitleOutAlpha = 0.0f;
	AnimState.m_TargetTrackTitleInOffset = 0.0f;
	AnimState.m_TargetTrackTitleOutOffset = -TrackTextOffset;
	AnimState.m_TargetTrackMetaInAlpha = 1.0f;
	AnimState.m_TargetTrackMetaOutAlpha = 0.0f;
	AnimState.m_TargetTrackMetaInOffset = 0.0f;
	AnimState.m_TargetTrackMetaOutOffset = -TrackTextOffset;

	const float IslandX = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, AnimState.m_TargetX, CapsuleSpring, 3, 0.01f);
	const float IslandWidth = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, AnimState.m_TargetWidth, CapsuleSpring, 3, 0.01f);
	const float AnimatedIslandHeight = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, AnimState.m_TargetHeight, CapsuleSpring, 3, 0.01f);
	const float TitleAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTitleAlpha, TitleSpring, 2, 0.004f), 0.0f, 1.0f);
	const float TitleOffset = ResolveUiPresentationStateValue(AnimRuntime, TitleNode, EUiAnimProperty::POS_X, AnimState.m_TargetTitleOffset, TitleSpring, 2, 0.01f);
	const float BottomAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, BottomNode, EUiAnimProperty::ALPHA, AnimState.m_TargetBottomAlpha, BottomSpring, 2, 0.004f), 0.0f, 1.0f);
	const float CoverInAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::ALPHA, AnimState.m_TargetCoverInAlpha, TrackSpring, 2, 0.003f), 0.0f, 1.0f);
	const float CoverOutAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::ALPHA, AnimState.m_TargetCoverOutAlpha, TrackExitSpring, 2, 0.003f), 0.0f, 1.0f);
	const float CoverInScale = ResolveUiPresentationStateValue(AnimRuntime, CoverInNode, EUiAnimProperty::SCALE, AnimState.m_TargetCoverInScale, TrackSpring, 2, 0.004f);
	const float CoverOutScale = ResolveUiPresentationStateValue(AnimRuntime, CoverOutNode, EUiAnimProperty::SCALE, AnimState.m_TargetCoverOutScale, TrackExitSpring, 2, 0.004f);
	const float TrackTitleInAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackTitleInAlpha, TrackSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackTitleOutAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackTitleOutAlpha, TrackExitSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackTitleInOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackTitleInNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackTitleInOffset, TrackSpring, 2, 0.01f);
	const float TrackTitleOutOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackTitleOutNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackTitleOutOffset, TrackExitSpring, 2, 0.01f);
	const float TrackMetaInAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackMetaInAlpha, TrackSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackMetaOutAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::ALPHA, AnimState.m_TargetTrackMetaOutAlpha, TrackExitSpring, 2, 0.003f), 0.0f, 1.0f);
	const float TrackMetaInOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackMetaInNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackMetaInOffset, TrackSpring, 2, 0.01f);
	const float TrackMetaOutOffset = ResolveUiPresentationStateValue(AnimRuntime, TrackMetaOutNode, EUiAnimProperty::POS_X, AnimState.m_TargetTrackMetaOutOffset, TrackExitSpring, 2, 0.01f);
	AnimState.m_OldTrackExitProgress = 1.0f - CoverOutAlpha;
	AnimState.m_NewTrackEnterProgress = CoverInAlpha;
	if(AnimState.m_TrackTransitionActive && CoverOutAlpha <= 0.01f && TrackTitleOutAlpha <= 0.01f && CoverInAlpha >= 0.99f && TrackTitleInAlpha >= 0.99f)
	{
		AnimState.m_TrackTransitionActive = false;
		AnimState.m_OutgoingTrack.Reset();
		AnimState.m_OldTrackExitProgress = 1.0f;
		AnimState.m_NewTrackEnterProgress = 1.0f;
	}

	const float Radius = BaseIslandHeight * 0.5f;
	const float CoverX = IslandX + PaddingX;
	const float CoverY = IslandY + (BaseIslandHeight - CoverSize) * 0.5f;
	const vec2 CoverCenter(CoverX + CoverSize * 0.5f, CoverY + CoverSize * 0.5f);
	const float CoverRadius = CoverSize * 0.5f;
	const float RightMetaX = IslandX + IslandWidth - PaddingX;
	const float WaveformSlotX = ShowWaveform ? (RightMetaX - WaveformSlotWidth) : RightMetaX;
	const float MetaY = IslandY + (BaseIslandHeight - MetaFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);
	const float TeamAnchorX = ShowWaveform ? WaveformSlotX : RightMetaX;
	const float TeamX = ShowTeam ? (TeamAnchorX - (ShowWaveform ? Gap : 0.0f) - TeamWidth) : TeamAnchorX;
	const float TitleBaseX = CoverX + CoverSize + Gap;
	const float TitleX = TitleBaseX + TitleOffset;
	const float TitleRight = (ShowTeam ? TeamX : (ShowWaveform ? WaveformSlotX : (IslandX + IslandWidth - PaddingX))) - Gap;
	const float TitleAvailableWidth = std::max(0.0f, TitleRight - TitleX);
	const float TitleY = IslandY + (BaseIslandHeight - TitleFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);

	const float SatelliteRadius = Radius;
	const float SatelliteDiameter = SatelliteRadius * 2.0f;
	constexpr float SatelliteItemGap = QmHudMediaIslandScaled(2.0f);
	constexpr float SatelliteRestGap = QmHudMediaIslandScaled(3.0f);
	const float SatelliteRingRadius = SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_RADIUS_SCALE;
	const float SatelliteRingThickness = std::max(QmHudMediaIslandScaled(1.25f), SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_THICKNESS_SCALE);
	const float SatelliteIconSize = SatelliteRadius * 0.94f;
	const float SatelliteCenterY = IslandY + BaseIslandHeight * 0.5f;

	std::array<SHudMediaIslandAnimState::SSatelliteItem *, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aActiveSatelliteItems{};
	std::array<float, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aActiveSatelliteTargetWidths{};
	std::array<float, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aActiveSatelliteTargetCenters{};
	int ActiveSatelliteCount = 0;
	for(int i = 0; i < CountdownInputCount; ++i)
	{
		for(auto &Item : AnimState.m_aSatelliteItems)
		{
			if(Item.m_Used && Item.m_Active && Item.m_Type == aCountdownInputs[i].m_Type && Item.m_Id == aCountdownInputs[i].m_Id)
			{
				aActiveSatelliteItems[ActiveSatelliteCount++] = &Item;
				break;
			}
		}
	}

	float LogicalSatelliteWidth = 0.0f;
	for(int i = 0; i < ActiveSatelliteCount; ++i)
	{
		aActiveSatelliteTargetWidths[i] = SatelliteDiameter;
		LogicalSatelliteWidth += aActiveSatelliteTargetWidths[i];
	}
	if(ActiveSatelliteCount > 1)
		LogicalSatelliteWidth += SatelliteItemGap * (ActiveSatelliteCount - 1);
	const float TargetSatelliteX = ActiveSatelliteCount > 0 ? IslandX - SatelliteRestGap - LogicalSatelliteWidth : IslandX;
	float SatelliteCursorX = TargetSatelliteX;
	for(int i = 0; i < ActiveSatelliteCount; ++i)
	{
		aActiveSatelliteTargetCenters[i] = SatelliteCursorX + aActiveSatelliteTargetWidths[i] * 0.5f;
		SatelliteCursorX += aActiveSatelliteTargetWidths[i] + SatelliteItemGap;
	}
	AnimState.m_TargetSatelliteX = TargetSatelliteX;
	AnimState.m_TargetSatelliteWidth = LogicalSatelliteWidth;
	AnimState.m_TargetSatelliteAlpha = ActiveSatelliteCount > 0 ? 1.0f : 0.0f;

	struct SSatelliteRenderItem
	{
		EHudMediaIslandCountdownType m_Type = EHudMediaIslandCountdownType::SWAP;
		vec2 m_Center{};
		vec2 m_Radii{};
		float m_SmoothUnion = 0.0f;
		float m_ContentAlpha = 0.0f;
		float m_ContentScale = 1.0f;
		float m_Progress = 0.0f;
		bool m_Completed = false;
		bool m_SwapOutgoing = false;
	};
	static_assert(SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS == QmHudMediaIslandSdfMaxItems);
	std::array<SSatelliteRenderItem, SHudMediaIslandAnimState::SATELLITE_MAX_ITEMS> aSatelliteRenderItems{};
	int SatelliteRenderItemCount = 0;
	for(auto &Item : AnimState.m_aSatelliteItems)
	{
		if(!Item.m_Used)
			continue;

		int ActiveIndex = -1;
		for(int i = 0; i < ActiveSatelliteCount; ++i)
		{
			if(aActiveSatelliteItems[i] == &Item)
			{
				ActiveIndex = i;
				break;
			}
		}
		const bool Active = Item.m_Active && ActiveIndex >= 0;
		float LiquidDeltaSeconds = 0.0f;
		if(Item.m_LiquidLastTick > 0 && Now >= Item.m_LiquidLastTick)
			LiquidDeltaSeconds = std::min((Now - Item.m_LiquidLastTick) / (float)time_freq(), 0.10f);
		Item.m_LiquidLastTick = Now;
		Item.m_LiquidProgress = QmHudAdvanceMediaIslandLiquidProgress(Item.m_LiquidProgress, Active, LiquidDeltaSeconds, MotionLevel > 0);
		if(!Active && Item.m_LiquidProgress <= 0.0f)
		{
			Item.Reset();
			continue;
		}

		float FinalCenterX = Item.m_LiquidOriginCenterX;
		float FinalWidth = Item.m_LiquidOriginWidth;
		if(Active)
		{
			const float TargetCenterX = aActiveSatelliteTargetCenters[ActiveIndex];
			const float TargetWidth = aActiveSatelliteTargetWidths[ActiveIndex];
			const uint64_t ItemNode = HudMediaIslandSatelliteNodeKey(Item.m_Type, Item.m_Id);
			if(!Item.m_NodeInitialized)
			{
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::POS_X, TargetCenterX);
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::WIDTH, TargetWidth);
				Item.m_NodeInitialized = true;
			}
			if(MotionLevel <= 0)
			{
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::POS_X, TargetCenterX);
				SetUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::WIDTH, TargetWidth);
			}
			FinalCenterX = ResolveUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::POS_X, TargetCenterX, SatelliteSpring, 3, 0.004f);
			FinalWidth = ResolveUiPresentationStateValue(AnimRuntime, ItemNode, EUiAnimProperty::WIDTH, TargetWidth, SatelliteSpring, 3, 0.01f);
			Item.m_LiquidOriginCenterX = FinalCenterX;
			Item.m_LiquidOriginWidth = FinalWidth;
		}
		if(FinalCenterX == 0.0f)
			FinalCenterX = IslandX - SatelliteRestGap - SatelliteRadius;
		if(FinalWidth <= 0.0f)
			FinalWidth = SatelliteDiameter;

		const SHudMediaIslandBlobPose BlobPose = QmHudMediaIslandBlobPose(Item.m_LiquidProgress);
		const float SpawnCenterX = IslandX + SatelliteRadius * 0.15f;
		const float ItemCenterX = mix(SpawnCenterX, FinalCenterX, BlobPose.m_Travel);
		const float BlobWidth = mix(SatelliteDiameter, FinalWidth, BlobPose.m_ContentAlpha);

		SSatelliteRenderItem &RenderItem = aSatelliteRenderItems[SatelliteRenderItemCount++];
		RenderItem.m_Type = Item.m_Type;
		RenderItem.m_Center = vec2(ItemCenterX, SatelliteCenterY);
		RenderItem.m_Radii = vec2(
			BlobWidth * 0.5f * BlobPose.m_RadiusScale * BlobPose.m_StretchX,
			SatelliteRadius * BlobPose.m_RadiusScale * BlobPose.m_StretchY);
		RenderItem.m_SmoothUnion = QmHudMediaIslandBlobBlend(SatelliteRadius, BlobPose.m_RadiusScale) * QmHudMediaIslandBlobConnectionStrength(BlobPose.m_Travel);
		RenderItem.m_ContentAlpha = BlobPose.m_ContentAlpha;
		RenderItem.m_ContentScale = std::clamp(BlobPose.m_RadiusScale, 0.0f, 1.0f);
		RenderItem.m_Progress = Item.m_Progress;
		RenderItem.m_Completed = Item.m_Completed;
		RenderItem.m_SwapOutgoing = Item.m_SwapOutgoing;
	}

	float SatelliteVisibleLeft = IslandX;
	for(int i = 0; i < SatelliteRenderItemCount; ++i)
	{
		const SSatelliteRenderItem &Item = aSatelliteRenderItems[i];
		SatelliteVisibleLeft = std::min(SatelliteVisibleLeft, Item.m_Center.x - Item.m_Radii.x - QmHudMediaIslandScaled(1.0f));
	}
	const unsigned int PrevFlags = TextRender()->GetRenderFlags();
	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);

	const float StatusAnchorRight = TimerCapsule.m_Visible ? TimerBoxRight : (IslandX + IslandWidth);
	const float StatusSectionGap = PlannedStatusWidth > 0.0f && (TimerCapsule.m_Visible || IslandWidth > 1.0f) ? TimerToStatusGap : 0.0f;
	const float StatusSectionX = StatusAnchorRight + StatusSectionGap;
	const float TargetStatusWidth = PlannedStatusWidth;
	const float TargetStatusAlpha = (ShowInfoStack || ShowRecordingStatus) ? 1.0f : 0.0f;
	const float TargetTextAlpha = ShowInfoStack ? 1.0f : (ShowRecordingStatus && ScoreboardExpanded ? 1.0f : 0.0f);

	const uint64_t StatusBoxNode = HudRecordingStatusNodeKey("box");
	const uint64_t StatusTextNode = HudRecordingStatusNodeKey("text");
	if(!m_RecordingStatusAnimState.m_Initialized)
	{
		m_RecordingStatusAnimState.m_TargetWidth = TargetStatusWidth;
		m_RecordingStatusAnimState.m_TargetAlpha = TargetStatusAlpha;
		m_RecordingStatusAnimState.m_TargetTextAlpha = TargetTextAlpha;
		SetUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::WIDTH, TargetStatusWidth);
		SetUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::ALPHA, TargetStatusAlpha);
		SetUiPresentationStateValue(AnimRuntime, StatusTextNode, EUiAnimProperty::ALPHA, TargetTextAlpha);
		m_RecordingStatusAnimState.m_Initialized = true;
	}

	m_RecordingStatusAnimState.m_TargetWidth = TargetStatusWidth;
	m_RecordingStatusAnimState.m_TargetAlpha = TargetStatusAlpha;
	m_RecordingStatusAnimState.m_TargetTextAlpha = TargetTextAlpha;
	const SUiSpringConfig StatusSpring = TargetStatusAlpha > 0.0f ? ContentSpring : ContentExitSpring;
	const float StatusWidth = ResolveUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::WIDTH, m_RecordingStatusAnimState.m_TargetWidth, StatusSpring, 2, 0.01f);
	const float StatusAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, StatusBoxNode, EUiAnimProperty::ALPHA, m_RecordingStatusAnimState.m_TargetAlpha, StatusSpring, 2, 0.004f), 0.0f, 1.0f);
	const float StatusTextAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, StatusTextNode, EUiAnimProperty::ALPHA, m_RecordingStatusAnimState.m_TargetTextAlpha, StatusSpring, 2, 0.004f), 0.0f, 1.0f);
	const bool RenderStatusSection = StatusWidth > 1.0f && StatusAlpha > 0.01f;
	const float UnifiedRight = StatusAnchorRight + (RenderStatusSection ? StatusSectionGap + StatusWidth : 0.0f);
	const float UnifiedWidth = std::max(IslandWidth, UnifiedRight - IslandX);
	const SHudMediaIslandBlobPose SpectatorBlobPose = QmHudMediaIslandBlobPose(AnimState.m_SpectatorLiquidProgress);
	const SHudMediaIslandSpectatorIconPose SpectatorIconPose = QmHudMediaIslandSpectatorIconPose(AnimState.m_SpectatorIconProgress);
	const SHudMediaIslandLiquidCapsule SpectatorLiquidCapsule = QmHudMediaIslandRightBlobCapsule(
		UnifiedRight,
		SatelliteCenterY,
		Radius,
		SpectatorSatelliteWidth,
		SpectatorSatelliteRestGap,
		SpectatorBlobPose);
	const float SpectatorVisibleRight = SpectatorLiquidCapsule.m_Rect.x + SpectatorLiquidCapsule.m_Rect.w + QmHudMediaIslandScaled(1.0f);
	const float EditorX = TimerCapsule.m_Visible ? TimerBoxX : IslandX;
	const float EditorRight = TimerCapsule.m_Visible ? UnifiedRight : (IslandX + UnifiedWidth);
	const float EditorWidth = std::max(0.0f, EditorRight - EditorX);
	const CUIRect EditorTransformRect = {EditorX, IslandY, EditorWidth, AnimatedIslandHeight};
	const float EditorVisibleRight = std::max(UnifiedRight, SpectatorVisibleRight);
	const CUIRect EditorVisibleRect = {SatelliteVisibleLeft, IslandY, EditorVisibleRight - SatelliteVisibleLeft, AnimatedIslandHeight};
	const bool RenderLeftSection = ShowCover || ShowTeam || ShowWaveform;
	const bool ShowTimerSecondaryLine = SwapRows.m_InlineSwapCount > 0 || Checkpoint > 0;
	const SHudMediaIslandTimerRowLayout TimerRows = QmHudMediaIslandTimerRows(TimerCapsule.m_BoxY, TimerCapsule.m_BoxH, ShowTimerSecondaryLine);
	const float TimerRaceFontSize = std::min(TimerCapsule.m_FontSize, TimerRows.m_RaceH);
	const float TimerRaceTextWidth = TextRender()->TextWidth(TimerRaceFontSize, TimerCapsule.m_aText);
	const float TimerTextX = TimerBoxX + std::max(0.0f, (TimerCapsule.m_BoxW - TimerRaceTextWidth) * 0.5f);
	const float TimerRaceTextY = ShowTimerSecondaryLine ? TimerRows.m_RaceY + (TimerRows.m_RaceH - TimerRaceFontSize) * 0.5f : TimerCapsule.m_TextY;
	const float CheckpointFontSize = std::min(TimerCapsule.m_FontSize * 0.40f, TimerRows.m_CheckpointH);
	const float CheckpointTextY = TimerRows.m_CheckpointY + (TimerRows.m_CheckpointH - CheckpointFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);
	ColorRGBA IslandBackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHudIslandBgColor));
	IslandBackgroundColor.a = std::clamp(g_Config.m_QmHudIslandBgOpacity / 100.0f, 0.0f, 1.0f);
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::MediaIsland, EditorTransformRect, EditorVisibleRect);
	m_MediaIslandLastVisibleRect = HudEditorScope.m_VisibleRect;
	m_MediaIslandLastVisibleRectValid = true;

	float TransformedScreenX0, TransformedScreenY0, TransformedScreenX1, TransformedScreenY1;
	Graphics()->GetScreen(&TransformedScreenX0, &TransformedScreenY0, &TransformedScreenX1, &TransformedScreenY1);
	const CUIRect TargetMainIslandSdfRect = {IslandX, IslandY, UnifiedWidth, AnimatedIslandHeight};
	const SHudMediaIslandEntrancePose EntrancePose = QmHudMediaIslandEntrancePose(TargetMainIslandSdfRect, Radius, IslandBackgroundColor, AnimState.m_EntranceProgress, AnimState.m_EntranceDropProgress, TransformedScreenY0);
	const float EntranceContentAlpha = EntrancePose.m_ContentAlpha;
	const CUIRect MainIslandSdfRect = EntrancePose.m_Rect;
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.42f * EntranceContentAlpha);
	const float SpectatorCenterX = SpectatorLiquidCapsule.m_Rect.x + SpectatorLiquidCapsule.m_Rect.w * 0.5f;
	const float SpectatorCenterY = SpectatorLiquidCapsule.m_Rect.y + SpectatorLiquidCapsule.m_Rect.h * 0.5f;
	const SHudMediaIslandSdfCapsule SpectatorSdfCapsule = {
		{SpectatorCenterX - SpectatorLiquidCapsule.m_Rect.w * EntranceContentAlpha * 0.5f,
			SpectatorCenterY - SpectatorLiquidCapsule.m_Rect.h * EntranceContentAlpha * 0.5f,
			SpectatorLiquidCapsule.m_Rect.w * EntranceContentAlpha,
			SpectatorLiquidCapsule.m_Rect.h * EntranceContentAlpha},
		SpectatorLiquidCapsule.m_Radius * EntranceContentAlpha,
		SpectatorLiquidCapsule.m_SmoothUnion * EntranceContentAlpha};
	SHudMediaIslandSdfRenderState CurrentSdfState;
	for(int i = 0; i < SatelliteRenderItemCount; ++i)
	{
		const SSatelliteRenderItem &Item = aSatelliteRenderItems[i];
		SHudMediaIslandSdfItem &SdfItem = CurrentSdfState.m_Items[i];
		SdfItem.m_Center = Item.m_Center;
		SdfItem.m_Radii = Item.m_Radii * EntranceContentAlpha;
		SdfItem.m_SmoothUnion = Item.m_SmoothUnion * EntranceContentAlpha;
		SdfItem.m_ContentAlpha = Item.m_ContentAlpha * EntranceContentAlpha;
		SdfItem.m_ContentScale = Item.m_ContentScale * EntranceContentAlpha;
		SdfItem.m_CountdownProgress = Item.m_Progress;
		SdfItem.m_RingColor = MediaIslandCountdownColor(Item.m_Type);
	}

	const float ScreenPixelSize = std::max(
		(TransformedScreenX1 - TransformedScreenX0) / std::max(1, Graphics()->ScreenWidth()),
		(TransformedScreenY1 - TransformedScreenY0) / std::max(1, Graphics()->ScreenHeight()));
	CurrentSdfState.m_MainRect = MainIslandSdfRect;
	CurrentSdfState.m_MainRadius = EntrancePose.m_Radius;
	CurrentSdfState.m_MainCorners = HudEditorScope.m_Corners;
	CurrentSdfState.m_MainDisabledCornerRadius = EntrancePose.m_DisabledCornerRadius;
	CurrentSdfState.m_ItemCount = SatelliteRenderItemCount;
	CurrentSdfState.m_HasRightCapsule = SpectatorSdfCapsule.m_Rect.w > 0.0f && SpectatorSdfCapsule.m_Rect.h > 0.0f;
	CurrentSdfState.m_RightCapsule = SpectatorSdfCapsule;
	CurrentSdfState.m_RingRadius = SatelliteRingRadius;
	CurrentSdfState.m_RingThickness = SatelliteRingThickness;
	CurrentSdfState.m_BackgroundColor = EntrancePose.m_BackgroundColor;
	CurrentSdfState.m_ScreenPixelSize = ScreenPixelSize;
	CurrentSdfState.m_OuterShadowSize = ScreenPixelSize * MEDIA_ISLAND_OUTER_SHADOW_PIXELS;
	CurrentSdfState.m_OuterShadowOpacity = MEDIA_ISLAND_OUTER_SHADOW_OPACITY * EntrancePose.m_BackgroundColor.a;
	CurrentSdfState.m_Rect = QmHudMediaIslandSdfOuterRect(CurrentSdfState);
	IGraphics::CRenderTargetHandle Backdrop;
	if(PrepareMediaIslandBlur())
	{
		Backdrop = m_MediaIslandBlurTarget;
		const CUIRect BackdropScreenRect = {
			TransformedScreenX0,
			TransformedScreenY0,
			TransformedScreenX1 - TransformedScreenX0,
			TransformedScreenY1 - TransformedScreenY0};
		CurrentSdfState.m_BackdropUv = QmHudMediaIslandBackdropUv(CurrentSdfState.m_Rect, BackdropScreenRect);
	}
	IGraphics::SMediaIslandSdfParams GpuSdfParams;
	if(QmHudMediaIslandBuildGpuSdfParams(CurrentSdfState, GpuSdfParams))
	{
		if(Graphics()->HasMediaIslandSdf())
			Graphics()->RenderMediaIslandSdf(GpuSdfParams, Backdrop);
		else
			DrawMediaIslandGeometryFallback(Graphics(), CurrentSdfState);
	}

	if(SpectatorLiquidCapsule.m_ContentAlpha * EntranceContentAlpha > 0.001f && SpectatorLiquidCapsule.m_Rect.w > 0.01f)
	{
		const float ContentScale = std::clamp(SpectatorBlobPose.m_RadiusScale, 0.0f, 1.0f) * EntranceContentAlpha;
		const float IconSize = SpectatorSatelliteIconSize * ContentScale;
		const float FontSize = MetaFontSize * ContentScale;
		const float TextWidth = SpectatorTextWidth * ContentScale;
		const float ContentGap = SpectatorGap * ContentScale;
		const float ContentWidth = IconSize + ContentGap + TextWidth;
		const float CenterX = SpectatorLiquidCapsule.m_Rect.x + SpectatorLiquidCapsule.m_Rect.w * 0.5f;
		const float ContentX = CenterX - ContentWidth * 0.5f;
		const auto IconRect = [&](float ScaleX, float ScaleY) {
			const float Width = IconSize * ScaleX;
			const float Height = IconSize * ScaleY;
			return CUIRect{ContentX + (IconSize - Width) * 0.5f, SatelliteCenterY - Height * 0.5f, Width, Height};
		};
		const float IconAlpha = 0.88f * SpectatorLiquidCapsule.m_ContentAlpha * EntranceContentAlpha;
		// Keep the spectator eye on the text path when OpenGL loses this atlas draw.
		if(IsOpenGlBackend())
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const auto RenderTextEye = [&](const char *pGlyph, const CUIRect &Rect, float Alpha) {
				if(Alpha <= 0.001f)
					return;
				const float GlyphSize = Rect.h;
				const float GlyphWidth = TextRender()->TextWidth(GlyphSize, pGlyph);
				TextRender()->TextColor(0.98f, 0.99f, 1.0f, IconAlpha * Alpha);
				TextRender()->Text(Rect.x + (Rect.w - GlyphWidth) * 0.5f, Rect.y, GlyphSize, pGlyph, -1.0f);
			};
			RenderTextEye(FontIcons::FONT_ICON_EYE_SLASH, IconRect(SpectatorIconPose.m_ClosedScale, SpectatorIconPose.m_ClosedScale), SpectatorIconPose.m_ClosedAlpha);
			RenderTextEye(FontIcons::FONT_ICON_EYE, IconRect(SpectatorIconPose.m_OpenScaleX, SpectatorIconPose.m_OpenScaleY), SpectatorIconPose.m_OpenAlpha);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else if(CQmIconManager *pIconManager = GameClient()->QmIconManager())
		{
			pIconManager->RenderIcon(EQmIcon::SATELLITE_SPECTATOR_EYE_CLOSED, IconRect(SpectatorIconPose.m_ClosedScale, SpectatorIconPose.m_ClosedScale), ColorRGBA(0.98f, 0.99f, 1.0f, IconAlpha * SpectatorIconPose.m_ClosedAlpha));
			pIconManager->RenderIcon(EQmIcon::SATELLITE_SPECTATOR_EYE, IconRect(SpectatorIconPose.m_OpenScaleX, SpectatorIconPose.m_OpenScaleY), ColorRGBA(0.98f, 0.99f, 1.0f, IconAlpha * SpectatorIconPose.m_OpenAlpha));
		}
		const float CountAlpha = QmHudMediaIslandSpectatorCountAlpha(ShowSpectator, SpectatorIconPose);
		if(CountAlpha > 0.001f)
		{
			TextRender()->TextColor(0.98f, 0.99f, 1.0f, 0.86f * SpectatorLiquidCapsule.m_ContentAlpha * EntranceContentAlpha * CountAlpha);
			TextRender()->Text(ContentX + IconSize + ContentGap + SpectatorIconPose.m_CountOffsetX * ContentScale, SatelliteCenterY - FontSize * 0.5f - QmHudMediaIslandScaled(0.5f), FontSize, aSpectatorBuf, -1.0f);
		}
	}

	for(int i = 0; i < SatelliteRenderItemCount; ++i)
	{
		const SSatelliteRenderItem &Item = aSatelliteRenderItems[i];
		if(Item.m_ContentAlpha * EntranceContentAlpha <= 0.001f || Item.m_ContentScale <= 0.01f)
			continue;
		const float IconSize = SatelliteIconSize * Item.m_ContentScale * EntranceContentAlpha;
		const CUIRect IconRect = {Item.m_Center.x - IconSize * 0.5f, Item.m_Center.y - IconSize * 0.5f, IconSize, IconSize};
		if(CQmIconManager *pIconManager = GameClient()->QmIconManager())
		{
			const ColorRGBA IconColor = Item.m_Completed ? ColorRGBA(0.20f, 1.0f, 0.42f, 0.96f * Item.m_ContentAlpha * EntranceContentAlpha) : ColorRGBA(0.98f, 0.99f, 1.0f, 0.94f * Item.m_ContentAlpha * EntranceContentAlpha);
			pIconManager->RenderIcon(MediaIslandCountdownIcon(Item.m_Type, Item.m_Completed, Item.m_SwapOutgoing), IconRect, IconColor);
		}
	}

	const auto BuildTrackMetaText = [](const SHudMediaIslandTrackSnapshot &Track, char *pBuf, size_t BufSize) {
		pBuf[0] = '\0';
		if(Track.m_aArtist[0] != '\0' && Track.m_aAlbum[0] != '\0')
			str_format(pBuf, BufSize, "%s - %s", Track.m_aArtist, Track.m_aAlbum);
		else if(Track.m_aArtist[0] != '\0')
			str_copy(pBuf, Track.m_aArtist, BufSize);
		else if(Track.m_aAlbum[0] != '\0')
			str_copy(pBuf, Track.m_aAlbum, BufSize);
		return pBuf[0] != '\0';
	};
	const auto TrackDisplayTitle = [](const SHudMediaIslandTrackSnapshot &Track) {
		if(Track.m_aTitle[0] != '\0')
			return Track.m_aTitle;
		if(Track.m_aArtist[0] != '\0')
			return Track.m_aArtist;
		if(Track.m_aAlbum[0] != '\0')
			return Track.m_aAlbum;
		return "";
	};
	char aCurrentTrackMeta[256];
	char aOutgoingTrackMeta[256];
	const bool HasCurrentTrackMeta = BuildTrackMetaText(AnimState.m_CurrentTrack, aCurrentTrackMeta, sizeof(aCurrentTrackMeta));
	const bool HasOutgoingTrackMeta = BuildTrackMetaText(AnimState.m_OutgoingTrack, aOutgoingTrackMeta, sizeof(aOutgoingTrackMeta));
	const auto RenderTrackCover = [&](const SHudMediaIslandTrackSnapshot &Track, float Alpha, float Scale) {
		if(!ShowCover || Alpha <= 0.001f)
			return;

		const float CoverDrawRadius = CoverRadius * std::max(0.01f, Scale);
		if(Track.m_HasCover && Track.m_Cover.IsValid())
		{
			DrawTexturedQuad(Graphics(), Track.m_Cover, CoverCenter, CoverDrawRadius, Alpha);
			return;
		}

		DrawSmoothCircle(Graphics(), CoverCenter, CoverDrawRadius, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha));
		const float PlaceholderFontSize = MetaFontSize * Scale;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.35f * Alpha);
		TextRender()->Text(CoverCenter.x - PlaceholderWidth * Scale * 0.5f, CoverCenter.y - PlaceholderFontSize * 0.5f - QmHudMediaIslandScaled(0.5f), PlaceholderFontSize, FontIcons::FONT_ICON_MUSIC, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};
	const auto RenderTrackText = [&](const SHudMediaIslandTrackSnapshot &Track, const char *pMeta, bool HasMeta, float TitleLayerAlpha, float MetaLayerAlpha, float TitleLayerOffset, float MetaLayerOffset) {
		if(!ShowCover || TitleAlpha <= 0.001f || TitleAvailableWidth <= QmHudMediaIslandScaled(2.0f))
			return;

		const char *pTitle = TrackDisplayTitle(Track);
		const float EffectiveTitleAlpha = TitleAlpha * TitleLayerAlpha;
		const float EffectiveMetaAlpha = TitleAlpha * MetaLayerAlpha;
		const bool RenderMeta = HasMeta && EffectiveMetaAlpha > 0.001f && TitleAvailableWidth > QmHudMediaIslandScaled(12.0f);
		const float TitleDrawY = RenderMeta ? (IslandY + QmHudMediaIslandScaled(2.0f)) : TitleY;
		if(pTitle[0] != '\0' && EffectiveTitleAlpha > 0.001f)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = TitleFontSize;
			Cursor.m_LineWidth = TitleAvailableWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(TitleX + TitleLayerOffset, TitleDrawY));
			TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.94f * EffectiveTitleAlpha);
			TextRender()->TextEx(&Cursor, pTitle);
		}
		if(RenderMeta)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = MetaFontSize;
			Cursor.m_LineWidth = TitleAvailableWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(TitleX + MetaLayerOffset, IslandY + QmHudMediaIslandScaled(8.3f)));
			TextRender()->TextColor(0.85f, 0.88f, 0.94f, 0.68f * EffectiveMetaAlpha);
			TextRender()->TextEx(&Cursor, pMeta);
		}
	};

	RenderTrackCover(AnimState.m_OutgoingTrack, CoverOutAlpha * EntranceContentAlpha, CoverOutScale);
	RenderTrackCover(AnimState.m_CurrentTrack, CoverInAlpha * EntranceContentAlpha, CoverInScale);

	if(ShowTeam)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.82f * EntranceContentAlpha);
		TextRender()->Text(TeamX, MetaY, MetaFontSize, FontIcons::FONT_ICON_USERS, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.82f * EntranceContentAlpha);
		TextRender()->Text(TeamX + TeamIconWidth + SpectatorGap, MetaY, MetaFontSize, aTeamBuf, -1.0f);
	}

	RenderTrackText(AnimState.m_OutgoingTrack, aOutgoingTrackMeta, HasOutgoingTrackMeta, TrackTitleOutAlpha * EntranceContentAlpha, TrackMetaOutAlpha * 0.88f * EntranceContentAlpha, TrackTitleOutOffset, TrackMetaOutOffset);
	RenderTrackText(AnimState.m_CurrentTrack, aCurrentTrackMeta, HasCurrentTrackMeta, TrackTitleInAlpha * EntranceContentAlpha, TrackMetaInAlpha * EntranceContentAlpha, TrackTitleInOffset, TrackMetaInOffset);

	if(ShowWaveform)
	{
		constexpr int WaveBarCount = 7;
		constexpr int WaveTargetBarCount = 6;
		constexpr float OriginalWaveBarWidth = QmHudMediaIslandScaled(1.15f);
		constexpr float OriginalWaveBarGap = QmHudMediaIslandScaled(0.70f);
		constexpr float WaveTargetWidth = WaveTargetBarCount * OriginalWaveBarWidth + (WaveTargetBarCount - 1) * OriginalWaveBarGap;
		constexpr float WaveNaturalWidth = WaveBarCount * OriginalWaveBarWidth + (WaveBarCount - 1) * OriginalWaveBarGap;
		constexpr float WaveCompression = WaveTargetWidth / WaveNaturalWidth;
		constexpr float WaveBarWidth = OriginalWaveBarWidth * WaveCompression;
		constexpr float WaveBarGap = OriginalWaveBarGap * WaveCompression;
		constexpr float WaveMaxHeight = QmHudMediaIslandScaled(7.2f);
		const float WaveWidth = WaveBarCount * WaveBarWidth + (WaveBarCount - 1) * WaveBarGap;
		const float WaveX = WaveformSlotX + (WaveformSlotWidth - WaveWidth) * 0.5f;
		const float WaveCenterY = IslandY + BaseIslandHeight * 0.5f;
		const float WaveTime = Now / static_cast<float>(time_freq());
		if(MediaState.m_Playing)
		{
			AnimState.m_WaveformWasPlaying = true;
			AnimState.m_WaveformSettling = false;
		}
		else if(AnimState.m_WaveformWasPlaying)
		{
			AnimState.m_WaveformWasPlaying = false;
			AnimState.m_WaveformSettling = true;
			AnimState.m_WaveformSettleStartTick = Now;
			AnimState.m_WaveformSettleSampleTime = WaveTime;
		}
		float WaveSettleElapsed = 0.0f;
		if(AnimState.m_WaveformSettling && Now >= AnimState.m_WaveformSettleStartTick)
			WaveSettleElapsed = (Now - AnimState.m_WaveformSettleStartTick) / static_cast<float>(time_freq());
		const float WaveSampleTime = AnimState.m_WaveformSettling ? AnimState.m_WaveformSettleSampleTime : WaveTime;
		const bool WaveMotionEnabled = g_Config.m_QmUiMotionLevel > 0;
		for(int Bar = 0; Bar < WaveBarCount; ++Bar)
		{
			float SettleProgress = 1.0f;
			if(WaveMotionEnabled && MediaState.m_Playing)
				SettleProgress = 0.0f;
			else if(WaveMotionEnabled && AnimState.m_WaveformSettling)
				SettleProgress = QmHudMediaIslandWaveBarSettleProgress(Bar, WaveBarCount, WaveSettleElapsed);
			const float Height = WaveMaxHeight * QmHudMediaIslandWaveBarHeight(Bar, WaveSampleTime, 1.0f - SettleProgress);
			Graphics()->DrawRect(
				WaveX + Bar * (WaveBarWidth + WaveBarGap),
				WaveCenterY - Height * 0.5f,
				WaveBarWidth,
				Height,
				ColorRGBA(0.92f, 0.96f, 1.0f, 0.88f * EntranceContentAlpha),
				IGraphics::CORNER_ALL,
				WaveBarWidth * 0.5f);
		}
	}

	if(TimerCapsule.m_Visible)
	{
		if(RenderLeftSection)
		{
			const float LeftDividerX = TimerBoxX - GapToTimer * 0.5f;
			Graphics()->DrawRect(LeftDividerX, IslandY + QmHudMediaIslandScaled(4.0f), QmHudMediaIslandScaled(0.75f), BaseIslandHeight - QmHudMediaIslandScaled(8.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * EntranceContentAlpha), IGraphics::CORNER_ALL, QmHudMediaIslandScaled(0.375f));
		}

		if(TimerCapsule.m_IsCritical)
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, TimerCapsule.m_Alpha * EntranceContentAlpha);
		else
			TextRender()->TextColor(0.98f, 0.99f, 1.0f, 0.98f * EntranceContentAlpha);
		TextRender()->Text(TimerTextX, TimerRaceTextY, TimerRaceFontSize, TimerCapsule.m_aText, -1.0f);
		if(SwapRows.m_InlineSwapCount > 0)
		{
			const char *pSwapText = aIncomingSwapInfos[0]->m_aRequestText;
			const float SwapTextWidth = TextRender()->TextWidth(CheckpointFontSize, pSwapText);
			const float SwapTextMaxWidth = std::max(0.0f, TimerCapsule.m_BoxW - BottomRowPaddingX * 2.0f);
			TextRender()->TextColor(0.86f, 0.94f, 1.0f, 0.92f * EntranceContentAlpha);
			if(SwapTextWidth <= SwapTextMaxWidth)
			{
				const float SwapTextX = TimerBoxX + (TimerCapsule.m_BoxW - SwapTextWidth) * 0.5f;
				TextRender()->Text(SwapTextX, CheckpointTextY, CheckpointFontSize, pSwapText, -1.0f);
			}
			else
			{
				CTextCursor Cursor;
				Cursor.m_FontSize = CheckpointFontSize;
				Cursor.m_LineWidth = SwapTextMaxWidth;
				Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
				Cursor.SetPosition(vec2(TimerBoxX + BottomRowPaddingX, CheckpointTextY));
				TextRender()->TextEx(&Cursor, pSwapText);
			}
		}
		else if(Checkpoint > 0)
		{
			const float CheckpointWidth = TextRender()->TextWidth(CheckpointFontSize, aCheckpointBuf);
			const float CheckpointTextX = TimerBoxX + std::max(0.0f, (TimerCapsule.m_BoxW - CheckpointWidth) * 0.5f);
			TextRender()->TextColor(0.86f, 0.89f, 0.95f, 0.82f * EntranceContentAlpha);
			TextRender()->Text(CheckpointTextX, CheckpointTextY, CheckpointFontSize, aCheckpointBuf, -1.0f);
		}
	}

	if(RenderStatusSection)
	{
		if(StatusSectionGap > 0.0f)
		{
			const float DividerX = StatusAnchorRight + StatusSectionGap * 0.5f;
			Graphics()->DrawRect(DividerX, IslandY + QmHudMediaIslandScaled(4.0f), QmHudMediaIslandScaled(0.75f), BaseIslandHeight - QmHudMediaIslandScaled(8.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * StatusAlpha * EntranceContentAlpha), IGraphics::CORNER_ALL, QmHudMediaIslandScaled(0.375f));
		}

		if(ShowInfoStack)
		{
			const auto RenderInfoLine = [&](const char *pText, float CenterY, const ColorRGBA &Color) {
				if(pText == nullptr || pText[0] == '\0')
					return;
				const float TextWidth = std::round(TextRender()->TextBoundingBox(InfoStackFontSize, pText).m_W);
				const float TextX = StatusSectionX + std::max(0.0f, (StatusWidth - TextWidth) * 0.5f);
				const float TextY = CenterY - InfoStackFontSize * 0.5f - QmHudMediaIslandScaled(0.4f);
				TextRender()->TextColor(Color);
				TextRender()->Text(TextX, TextY, InfoStackFontSize, pText, -1.0f);
			};

			if(StackHasTwoRows)
			{
				const SHudMediaIslandInfoStackLayout InfoStackLayout = QmHudMediaIslandMirroredInfoStack(IslandY, BaseIslandHeight, InfoStackFontSize, InfoStackGap);
				RenderInfoLine(aTimeBuf, InfoStackLayout.m_TopCenterY, ColorRGBA(0.98f, 0.99f, 1.0f, 0.94f * StatusAlpha * EntranceContentAlpha));
				RenderInfoLine(aFrozenSummaryBuf, InfoStackLayout.m_BottomCenterY, ColorRGBA(0.86f, 0.90f, 0.97f, 0.86f * StatusAlpha * EntranceContentAlpha));
			}
			else if(ShowLocalTime)
				RenderInfoLine(aTimeBuf, IslandY + BaseIslandHeight * 0.5f, ColorRGBA(0.98f, 0.99f, 1.0f, 0.94f * StatusAlpha * EntranceContentAlpha));
			else
				RenderInfoLine(aFrozenSummaryBuf, IslandY + BaseIslandHeight * 0.5f, ColorRGBA(0.97f, 0.98f, 1.0f, 0.92f * StatusAlpha * EntranceContentAlpha));
		}
		else
		{
			const vec2 DotCenter(StatusSectionX + StatusPaddingLeft + StatusDotSize * 0.5f, IslandY + BaseIslandHeight * 0.5f);
			DrawSmoothCircle(Graphics(), DotCenter, StatusDotSize * 0.5f, ColorRGBA(1.0f, 0.15f, 0.15f, 0.95f * StatusAlpha * EntranceContentAlpha));

			if(StatusTextAlpha > 0.001f && StatusWidth > RawCollapsedStatusWidth + QmHudMediaIslandScaled(2.0f))
			{
				const float StatusTextX = StatusSectionX + StatusPaddingLeft + StatusDotSize + StatusDotGap;
				const float StatusTextY = IslandY + (BaseIslandHeight - StatusFontSize) * 0.5f - QmHudMediaIslandScaled(0.5f);
				TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.90f * StatusTextAlpha * EntranceContentAlpha);
				TextRender()->Text(StatusTextX, StatusTextY, StatusFontSize, aRecordingBuf, -1.0f);
			}
		}
	}

	const float VisibleBottomAlpha = BottomAlpha * EntranceContentAlpha;
	if(VisibleBottomAlpha > 0.01f && AnimatedIslandHeight > BaseIslandHeight + QmHudMediaIslandScaled(0.5f))
	{
		const float BottomRowY = IslandY + BaseIslandHeight;
		const float DividerInset = std::min(BottomRowDividerInset, UnifiedWidth * 0.25f);
		const float DividerWidth = std::max(0.0f, UnifiedWidth - DividerInset * 2.0f);
		if(DividerWidth > 0.0f)
		{
			Graphics()->DrawRect(IslandX + DividerInset, BottomRowY, DividerWidth, QmHudMediaIslandScaled(0.75f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * VisibleBottomAlpha), IGraphics::CORNER_ALL, QmHudMediaIslandScaled(0.375f));
		}

		float BottomTextY = BottomRowY + BottomRowPaddingY;
		const float ContentX = IslandX + BottomRowPaddingX;
		const float ContentWidth = std::max(0.0f, UnifiedWidth - BottomRowPaddingX * 2.0f);
		if(!AnimState.m_LyricsMarqueeInitialized || QmHudMediaIslandShouldResetMarquee(AnimState.m_aLyricsMarquee, pLyricsIslandBuf))
		{
			str_copy(AnimState.m_aLyricsMarquee, pLyricsIslandBuf, sizeof(AnimState.m_aLyricsMarquee));
			AnimState.m_LyricsMarqueeStartTick = Now;
			AnimState.m_LyricsMarqueeInitialized = true;
		}
		const auto EnableMappedClip = [&](const CUIRect &Rect) {
			float ScreenX0 = 0.0f;
			float ScreenY0 = 0.0f;
			float ScreenX1 = 0.0f;
			float ScreenY1 = 0.0f;
			Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
			const float MappedWidth = std::max(0.001f, ScreenX1 - ScreenX0);
			const float MappedHeight = std::max(0.001f, ScreenY1 - ScreenY0);
			const float PixelScaleX = Graphics()->ScreenWidth() / MappedWidth;
			const float PixelScaleY = Graphics()->ScreenHeight() / MappedHeight;
			const int ClipX0 = std::clamp((int)std::floor((Rect.x - ScreenX0) * PixelScaleX), 0, Graphics()->ScreenWidth());
			const int ClipY0 = std::clamp((int)std::floor((Rect.y - ScreenY0) * PixelScaleY), 0, Graphics()->ScreenHeight());
			const int ClipX1 = std::clamp((int)std::ceil((Rect.x + Rect.w - ScreenX0) * PixelScaleX), 0, Graphics()->ScreenWidth());
			const int ClipY1 = std::clamp((int)std::ceil((Rect.y + Rect.h - ScreenY0) * PixelScaleY), 0, Graphics()->ScreenHeight());
			if(ClipX1 > ClipX0 && ClipY1 > ClipY0)
				Graphics()->ClipEnable(ClipX0, ClipY0, ClipX1 - ClipX0, ClipY1 - ClipY0);
			return ClipX1 > ClipX0 && ClipY1 > ClipY0;
		};
		const auto RenderBottomTextBlock = [&](float X, float Y, float MaxWidth, const char *pText, const ColorRGBA &Color, bool AlignRight) {
			if(pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
				return;

			const float TextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
			TextRender()->TextColor(Color);
			if(TextWidth <= MaxWidth + 0.01f)
			{
				const float DrawX = AlignRight ? (X + MaxWidth - TextWidth) : X;
				TextRender()->Text(DrawX, Y, BottomFontSize, pText, -1.0f);
				return;
			}

			CTextCursor Cursor;
			Cursor.m_FontSize = BottomFontSize;
			Cursor.m_LineWidth = MaxWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(X, Y));
			TextRender()->TextEx(&Cursor, pText);
		};

		const auto RenderBottomTextCentered = [&](float Y, const char *pText, const ColorRGBA &Color) {
			if(pText == nullptr || pText[0] == '\0' || ContentWidth <= 0.0f)
				return;

			const float TextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
			if(TextWidth <= ContentWidth + 0.01f)
			{
				TextRender()->TextColor(Color);
				TextRender()->Text(IslandX + (UnifiedWidth - TextWidth) * 0.5f, Y, BottomFontSize, pText, -1.0f);
				return;
			}

			RenderBottomTextBlock(ContentX, Y, ContentWidth, pText, Color, false);
		};

		for(int i = SwapRows.m_InlineSwapCount; i < IncomingSwapCount; ++i)
		{
			const int LineIndex = i - SwapRows.m_InlineSwapCount;
			RenderBottomTextCentered(
				BottomTextY + BottomRowLineHeight * LineIndex,
				aIncomingSwapInfos[i]->m_aRequestText,
				ColorRGBA(0.86f, 0.94f, 1.0f, 0.92f * VisibleBottomAlpha));
		}

		if(ShowLyricsIslandLine)
		{
			const float LyricsTextY = BottomTextY + BottomRowLineHeight * SwapRows.m_LyricsLineIndex;
			ColorRGBA LyricsTextColor = LyricsIslandColor;
			LyricsTextColor.a *= VisibleBottomAlpha;
			if(pLyricsIslandBuf[0] != '\0' && ContentWidth > 0.0f)
			{
				const float LyricsTextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pLyricsIslandBuf).m_W);
				const float ElapsedSeconds = AnimState.m_LyricsMarqueeStartTick > 0 && Now >= AnimState.m_LyricsMarqueeStartTick ?
								     (Now - AnimState.m_LyricsMarqueeStartTick) / (float)time_freq() :
								     0.0f;
				const float LyricsOffset = QmHudMediaIslandMarqueeOffset(LyricsTextWidth, ContentWidth, ElapsedSeconds, QmHudMediaIslandScaled(32.0f));
				const CUIRect LyricsRect = {ContentX, LyricsTextY, ContentWidth, BottomRowLineHeight};
				const float DrawX = LyricsTextWidth <= ContentWidth ? LyricsRect.x + (LyricsRect.w - LyricsTextWidth) * 0.5f : LyricsRect.x - LyricsOffset;
				TextRender()->TextColor(LyricsTextColor);
				if(EnableMappedClip(LyricsRect))
				{
					TextRender()->Text(DrawX, LyricsTextY, BottomFontSize, pLyricsIslandBuf, -1.0f);
					Graphics()->ClipDisable();
				}
			}
		}
	}

	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevOutlineColor);
	TextRender()->SetRenderFlags(PrevFlags);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderPlayerState(const int ClientId)
{
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	const bool FastPracticeParticipant = GameClient()->m_FastPractice.IsPracticeParticipant(ClientId);

	// pCharacter contains the predicted character for local players or the last snap for players who are spectated
	CCharacterCore *pCharacter = &GameClient()->m_aClients[ClientId].m_Predicted;
	CNetObj_Character *pPlayer = &GameClient()->m_aClients[ClientId].m_RenderCur;
	int TotalJumpsToDisplay = 0;
	if(g_Config.m_ClShowhudJumpsIndicator)
	{
		int AvailableJumpsToDisplay;
		if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
		{
			const bool Grounded = Collision()->IsOnGround(vec2(pPlayer->m_X, pPlayer->m_Y), CCharacterCore::PhysicalSize());
			int UsedJumps = pCharacter->m_JumpedTotal;
			if(pCharacter->m_Jumps > 1)
			{
				UsedJumps += !Grounded;
			}
			else if(pCharacter->m_Jumps == 1)
			{
				// If the player has only one jump, each jump is the last one
				UsedJumps = pPlayer->m_Jumped & 2;
			}
			else if(pCharacter->m_Jumps == -1)
			{
				// The player has only one ground jump
				UsedJumps = !Grounded;
			}

			if(pCharacter->m_EndlessJump && UsedJumps >= absolute(pCharacter->m_Jumps))
			{
				UsedJumps = absolute(pCharacter->m_Jumps) - 1;
			}

			int UnusedJumps = absolute(pCharacter->m_Jumps) - UsedJumps;
			if(!(pPlayer->m_Jumped & 2) && UnusedJumps <= 0)
			{
				// In some edge cases when the player just got another number of jumps, UnusedJumps is not correct
				UnusedJumps = 1;
			}
			TotalJumpsToDisplay = std::clamp(absolute(pCharacter->m_Jumps), 0, 10);
			AvailableJumpsToDisplay = std::clamp(UnusedJumps, 0, TotalJumpsToDisplay);
		}
		else
		{
			TotalJumpsToDisplay = AvailableJumpsToDisplay = absolute(GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Jumps);
		}

		// render available and used jumps
		int JumpsOffsetY = ((GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
				    (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));
		if(JumpsOffsetY > 0)
		{
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay, 0, JumpsOffsetY);
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay, 0, JumpsOffsetY);
		}
		else
		{
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay);
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay);
		}
	}

	float x = 5 + 12;
	float y = (5 + 12 + (GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
		   (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));

	// render weapons
	{
		constexpr float aWeaponWidth[NUM_WEAPONS] = {16, 12, 12, 12, 12, 12};
		constexpr float aWeaponInitialOffset[NUM_WEAPONS] = {-3, -4, -1, -1, -2, -4};
		float aWeaponTargetX[NUM_WEAPONS] = {};
		bool aWeaponVisible[NUM_WEAPONS] = {};
		float WeaponLayoutX = x;
		bool InitialOffsetAdded = false;
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		{
			if(!pCharacter->m_aWeapons[Weapon].m_Got)
				continue;
			if(!InitialOffsetAdded)
			{
				WeaponLayoutX += aWeaponInitialOffset[Weapon];
				InitialOffsetAdded = true;
			}
			aWeaponVisible[Weapon] = true;
			aWeaponTargetX[Weapon] = WeaponLayoutX;
			WeaponLayoutX += aWeaponWidth[Weapon];
		}

		const float WeaponLayoutEndX = WeaponLayoutX;
		SUiSpringConfig WeaponSpring;
		WeaponSpring.m_Stiffness = 420.0f;
		WeaponSpring.m_Damping = 40.0f;
		WeaponSpring.m_RestEpsilon = 0.008f;
		WeaponSpring.m_RestVelocity = 0.05f;

		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		{
			CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
			SHudWeaponPresentationState &Presentation = m_WeaponPresentationState;
			if(!Presentation.m_aClientInitialized[ClientId])
			{
				for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
				{
					const bool ActiveWeapon = pPlayer->m_Weapon == Weapon;
					const float TargetX = aWeaponVisible[Weapon] ? aWeaponTargetX[Weapon] : WeaponLayoutEndX;
					const float TargetAlpha = aWeaponVisible[Weapon] ? (ActiveWeapon ? 1.0f : 0.4f) : 0.0f;
					const float TargetScale = aWeaponVisible[Weapon] ? (ActiveWeapon ? HUD_CURRENT_WEAPON_SCALE : 1.0f) : 0.92f;
					const uint64_t WeaponNode = HudWeaponPresentationNodeKey(ClientId, Weapon);
					Presentation.m_aaTargetX[ClientId][Weapon] = TargetX;
					Presentation.m_aaTargetY[ClientId][Weapon] = y;
					Presentation.m_aaTargetAlpha[ClientId][Weapon] = TargetAlpha;
					Presentation.m_aaTargetScale[ClientId][Weapon] = TargetScale;
					SetUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::POS_X, TargetX);
					SetUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::POS_Y, y);
					SetUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::ALPHA, TargetAlpha);
					SetUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::SCALE, TargetScale);
				}
				Presentation.m_aClientInitialized[ClientId] = true;
			}

			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
			{
				const bool ActiveWeapon = pPlayer->m_Weapon == Weapon;
				const float TargetX = aWeaponVisible[Weapon] ? aWeaponTargetX[Weapon] : Presentation.m_aaTargetX[ClientId][Weapon];
				const float TargetAlpha = aWeaponVisible[Weapon] ? (ActiveWeapon ? 1.0f : 0.4f) : 0.0f;
				const float TargetScale = aWeaponVisible[Weapon] ? (ActiveWeapon ? HUD_CURRENT_WEAPON_SCALE : 1.0f) : 0.92f;
				const uint64_t WeaponNode = HudWeaponPresentationNodeKey(ClientId, Weapon);
				Presentation.m_aaTargetX[ClientId][Weapon] = TargetX;
				Presentation.m_aaTargetY[ClientId][Weapon] = y;
				Presentation.m_aaTargetAlpha[ClientId][Weapon] = TargetAlpha;
				Presentation.m_aaTargetScale[ClientId][Weapon] = TargetScale;
				const float WeaponX = ResolveUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::POS_X, Presentation.m_aaTargetX[ClientId][Weapon], WeaponSpring, 2, 0.01f);
				const float WeaponY = ResolveUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::POS_Y, Presentation.m_aaTargetY[ClientId][Weapon], WeaponSpring, 2, 0.01f);
				const float WeaponAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::ALPHA, Presentation.m_aaTargetAlpha[ClientId][Weapon], WeaponSpring, 2, 0.004f), 0.0f, 1.0f);
				const float WeaponScale = std::max(0.01f, ResolveUiPresentationStateValue(AnimRuntime, WeaponNode, EUiAnimProperty::SCALE, Presentation.m_aaTargetScale[ClientId][Weapon], WeaponSpring, 2, 0.004f));
				if(!aWeaponVisible[Weapon] && WeaponAlpha <= 0.01f && !AnimRuntime.HasActiveAnimation(WeaponNode, EUiAnimProperty::ALPHA))
					continue;

				Graphics()->SetColor(1.0f, 1.0f, 1.0f, WeaponAlpha);
				Graphics()->QuadsSetRotation(pi * 7 / 4);
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
				Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], WeaponX, WeaponY, WeaponScale, WeaponScale);
				Graphics()->QuadsSetRotation(0);
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
		else
		{
			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
			{
				if(!aWeaponVisible[Weapon])
					continue;
				const bool ActiveWeapon = pPlayer->m_Weapon == Weapon;
				const float WeaponScale = ActiveWeapon ? HUD_CURRENT_WEAPON_SCALE : 1.0f;
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, ActiveWeapon ? 1.0f : 0.4f);
				Graphics()->QuadsSetRotation(pi * 7 / 4);
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
				Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], aWeaponTargetX[Weapon], y, WeaponScale, WeaponScale);
				Graphics()->QuadsSetRotation(0);
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
		x = WeaponLayoutEndX;

		if(pCharacter->m_aWeapons[WEAPON_NINJA].m_Got)
		{
			const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
			float NinjaProgress = std::clamp(pCharacter->m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000 - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
			if(NinjaProgress > 0.0f && GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
			{
				RenderNinjaBarPos(x, y - 12, 6.f, 24.f, NinjaProgress);
			}
		}
	}

	if(g_Config.m_QmHudIslandUseOriginalStyle)
	{
		constexpr float LegacyMediaBottomGap = 4.0f;
		const float MediaBottomY = RenderLegacyMediaInfoAt(x + 4.0f, y);
		y = maximum(y, MediaBottomY - 12.0f + LegacyMediaBottomGap);
	}

	// render capabilities
	x = 5;
	y += 12;
	if(TotalJumpsToDisplay > 0)
	{
		y += 12;
	}
	bool HasCapabilities = false;
	if(pCharacter->m_EndlessJump)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudEndlessJump);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessJumpOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_EndlessHook)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudEndlessHook);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessHookOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_Jetpack)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudJetpack);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_JetpackOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportGun);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGunOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGrenade && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportGrenade);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGrenadeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunLaser && pCharacter->m_aWeapons[WEAPON_LASER].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportLaser);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportLaserOffset, x, y);
	}

	// render prohibited capabilities
	x = 5;
	if(HasCapabilities)
	{
		y += 12;
	}
	bool HasProhibitedCapabilities = false;
	if(pCharacter->m_Solo)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudSolo);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_SoloOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_CollisionDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudCollisionDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_CollisionDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HookHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudHookHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HookHitDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HammerHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudHammerHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HammerHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudGunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_ShotgunHitDisabled && pCharacter->m_aWeapons[WEAPON_SHOTGUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudShotgunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_ShotgunHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudGrenadeHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_GrenadeHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_LaserHitDisabled && pCharacter->m_aWeapons[WEAPON_LASER].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLaserHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
	}

	// render dummy actions and freeze state
	x = 5;
	if(HasProhibitedCapabilities)
	{
		y += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_LOCK_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLockMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LockModeOffset, x, y);
		x += 12;
	}
	if((GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE) || FastPracticeParticipant)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudPracticeMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_PracticeModeOffset, x, y);
		x += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_TEAM0_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeam0Mode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_Team0ModeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_DeepFrozen)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDeepFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DeepFrozenOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_LiveFrozen)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLiveFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LiveFrozenOffset, x, y);
	}
}

void CHud::RenderNinjaBarPos(const float x, float y, const float Width, const float Height, float Progress, const float Alpha)
{
	Progress = std::clamp(Progress, 0.0f, 1.0f);

	// what percentage of the end pieces is used for the progress indicator and how much is the rest
	// half of the ends are used for the progress display
	const float RestPct = 0.5f;
	const float ProgPct = 0.5f;

	const float EndHeight = Width; // to keep the correct scale - the width of the sprite is as long as the height
	const float BarWidth = Width;
	const float WholeBarHeight = Height;
	const float MiddleBarHeight = WholeBarHeight - (EndHeight * 2.0f);
	const float EndProgressHeight = EndHeight * ProgPct;
	const float EndRestHeight = EndHeight * RestPct;
	const float ProgressBarHeight = WholeBarHeight - (EndProgressHeight * 2.0f);
	const float EndProgressProportion = EndProgressHeight / ProgressBarHeight;
	const float MiddleProgressProportion = MiddleBarHeight / ProgressBarHeight;

	// beginning piece
	float BeginningPieceProgress = 1;
	if(Progress <= 1)
	{
		if(Progress <= (EndProgressProportion + MiddleProgressProportion))
		{
			BeginningPieceProgress = 0;
		}
		else
		{
			BeginningPieceProgress = (Progress - EndProgressProportion - MiddleProgressProportion) / EndProgressProportion;
		}
	}
	// empty
	Graphics()->WrapClamp();
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 1);
	IGraphics::CQuadItem QuadEmptyBeginning(x, y, BarWidth, EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress));
	Graphics()->QuadsDrawTL(&QuadEmptyBeginning, 1);
	Graphics()->QuadsEnd();
	// full
	if(BeginningPieceProgress > 0.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * (1.0f - BeginningPieceProgress), 1, RestPct + ProgPct * (1.0f - BeginningPieceProgress), 0, 1, 0, 1, 1);
		IGraphics::CQuadItem QuadFullBeginning(x, y + (EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress)), BarWidth, EndProgressHeight * BeginningPieceProgress);
		Graphics()->QuadsDrawTL(&QuadFullBeginning, 1);
		Graphics()->QuadsEnd();
	}

	// middle piece
	y += EndHeight;

	float MiddlePieceProgress = 1;
	if(Progress <= EndProgressProportion + MiddleProgressProportion)
	{
		if(Progress <= EndProgressProportion)
		{
			MiddlePieceProgress = 0;
		}
		else
		{
			MiddlePieceProgress = (Progress - EndProgressProportion) / MiddleProgressProportion;
		}
	}

	const float FullMiddleBarHeight = MiddleBarHeight * MiddlePieceProgress;
	const float EmptyMiddleBarHeight = MiddleBarHeight - FullMiddleBarHeight;

	// empty ninja bar
	if(EmptyMiddleBarHeight > 0.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmpty);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// select the middle portion of the sprite so we don't get edge bleeding
		if(EmptyMiddleBarHeight <= EndHeight)
		{
			// prevent pixel puree, select only a small slice
			// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 1);
		}
		else
		{
			// Subset: btm_r, top_r, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 0, 0, 0, 1);
		}
		IGraphics::CQuadItem QuadEmpty(x, y, BarWidth, EmptyMiddleBarHeight);
		Graphics()->QuadsDrawTL(&QuadEmpty, 1);
		Graphics()->QuadsEnd();
	}

	// full ninja bar
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFull);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// select the middle portion of the sprite so we don't get edge bleeding
	if(FullMiddleBarHeight <= EndHeight)
	{
		// prevent pixel puree, select only a small slice
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(1.0f - (FullMiddleBarHeight / EndHeight), 1, 1.0f - (FullMiddleBarHeight / EndHeight), 0, 1, 0, 1, 1);
	}
	else
	{
		// Subset: btm_l, top_l, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, 1, 0, 1, 1);
	}
	IGraphics::CQuadItem QuadFull(x, y + EmptyMiddleBarHeight, BarWidth, FullMiddleBarHeight);
	Graphics()->QuadsDrawTL(&QuadFull, 1);
	Graphics()->QuadsEnd();

	// ending piece
	y += MiddleBarHeight;
	float EndingPieceProgress = 1;
	if(Progress <= EndProgressProportion)
	{
		EndingPieceProgress = Progress / EndProgressProportion;
	}
	// empty
	if(EndingPieceProgress < 1.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_l, top_l, top_m, btm_m | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, ProgPct - ProgPct * EndingPieceProgress, 0, ProgPct - ProgPct * EndingPieceProgress, 1);
		IGraphics::CQuadItem QuadEmptyEnding(x, y, BarWidth, EndProgressHeight * (1.0f - EndingPieceProgress));
		Graphics()->QuadsDrawTL(&QuadEmptyEnding, 1);
		Graphics()->QuadsEnd();
	}
	// full
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_m, top_m, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * EndingPieceProgress, 1, RestPct + ProgPct * EndingPieceProgress, 0, 0, 0, 0, 1);
	IGraphics::CQuadItem QuadFullEnding(x, y + (EndProgressHeight * (1.0f - EndingPieceProgress)), BarWidth, EndRestHeight + EndProgressHeight * EndingPieceProgress);
	Graphics()->QuadsDrawTL(&QuadFullEnding, 1);
	Graphics()->QuadsEnd();

	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->WrapNormal();
}

void CHud::RenderSpectatorCount()
{
	if(!g_Config.m_ClShowhudSpectatorCount)
	{
		return;
	}

	const bool Preview = GameClient()->m_HudEditor.IsActive();
	int Count = 0;
	if(Client()->IsSixup())
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == GameClient()->m_aLocalIds[0] || (GameClient()->Client()->DummyConnected() && i == GameClient()->m_aLocalIds[1]))
				continue;

			if(Client()->m_TranslationContext.m_aClients[i].m_PlayerFlags7 & protocol7::PLAYERFLAG_WATCHING)
			{
				Count++;
			}
		}
	}
	else
	{
		const CNetObj_SpectatorCount *pSpectatorCount = GameClient()->m_Snap.m_pSpectatorCount;
		if(!pSpectatorCount)
		{
			m_LastSpectatorCountTick = Client()->GameTick(g_Config.m_ClDummy);
			return;
		}
		Count = pSpectatorCount->m_NumSpectators;
	}

	if(Count == 0 && !Preview)
	{
		m_LastSpectatorCountTick = Client()->GameTick(g_Config.m_ClDummy);
		return;
	}
	if(Count == 0 && Preview)
		Count = 8;

	// 1 second delay
	if(Client()->GameTick(g_Config.m_ClDummy) < m_LastSpectatorCountTick + Client()->GameTickSpeed())
		return;

	char aBuf[16];
	str_format(aBuf, sizeof(aBuf), "%d", Count);

	float StartX = 0.0f;
	float StartY = 0.0f;
	if(g_Config.m_QmHudIslandUseOriginalStyle)
	{
		const float Fontsize = 6.0f;
		const float BoxHeight = 14.0f;
		const float BoxWidth = 13.0f + TextRender()->TextWidth(Fontsize, aBuf);

		if(m_MovementInfoBoxValid)
		{
			StartX = m_MovementInfoBoxX + m_MovementInfoBoxW - BoxWidth;
			StartY = m_MovementInfoBoxY - BoxHeight - 4.0f;
		}
		else
		{
			StartX = m_Width - BoxWidth;
			StartY = 285.0f - BoxHeight - 4.0f;
			if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
				StartY -= 4.0f;
			StartY -= GetMovementInformationBoxHeight();

			if(g_Config.m_ClShowhudScore)
				StartY -= 56.0f;

			if(g_Config.m_ClShowhudDummyActions && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && Client()->DummyConnected())
				StartY -= 29.0f + 4.0f;
		}

		StartX = std::clamp(StartX, 0.0f, maximum(0.0f, m_Width - BoxWidth));
		StartY = maximum(0.0f, StartY);
		const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::SpectatorCount, {StartX, StartY, BoxWidth, BoxHeight});

		Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);

		const float y = StartY + BoxHeight / 3.0f;
		const float x = StartX + 2.0f;

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->Text(x, y, Fontsize, FontIcons::FONT_ICON_EYE, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->Text(x + Fontsize + 3.0f, y, Fontsize, aBuf, -1.0f);
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
		return;
	}
	else
	{
		const float Fontsize = 5.0f;
		const float BoxHeight = 12.5f;
		const float IconWidth = TextRender()->TextWidth(Fontsize, FontIcons::FONT_ICON_EYE);
		const float TextWidth = TextRender()->TextWidth(Fontsize, aBuf);
		const float BoxWidth = IconWidth + 3.0f + TextWidth + 10.0f;

		const float TimeAnchorX = (m_Width / 7.0f) * 3.0f;
		const bool Seconds = g_Config.m_TcShowLocalTimeSeconds; // TClient
		char aTimeStr[16];
		str_timestamp_format(aTimeStr, sizeof(aTimeStr), Seconds ? "%H:%M.%S" : "%H:%M");
		const float TimeWidth = std::round(TextRender()->TextBoundingBox(5.0f, aTimeStr).m_W);
		const float TimeLeft = TimeAnchorX - (TimeWidth + 15.0f);
		StartX = TimeLeft - 5.0f - BoxWidth;
		StartY = 0.0f;
		const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::SpectatorCount, {StartX, StartY, BoxWidth, BoxHeight});

		Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);

		const float y = StartY + (BoxHeight - Fontsize) / 2.0f;
		float x = StartX + 5.0f;

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->Text(x, y, Fontsize, FontIcons::FONT_ICON_EYE, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		x += IconWidth + 3.0f;
		TextRender()->Text(x, y, Fontsize, aBuf, -1.0f);
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
		return;
	}
}

void CHud::RenderDummyActions()
{
	if(!g_Config.m_ClShowhudDummyActions || (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) || !Client()->DummyConnected())
	{
		return;
	}

	// render small dummy actions hud
	const float BoxHeight = 29.0f;
	const float BoxWidth = 16.0f;

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 距离显示下一个还有4个单位；

	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::DummyActions, {StartX, StartY, BoxWidth, BoxHeight});

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);

	float y = StartY + 2;
	float x = StartX + 2;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyHammer)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDummyHammer);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyHammerOffset, x, y);
	y += 13;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
	if(g_Config.m_ClDummyCopyMoves)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDummyCopy);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyCopyOffset, x, y);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

namespace
{
	struct SKeyStatusLines
	{
		const char *m_pKeyStatusText;
		bool m_ShowKey;
		char m_aHammerLine[64];
		bool m_ShowHammer;
		char m_aControlLine[64];
		bool m_ShowControl;
		char m_aSyncLine[64];
		bool m_ShowSync;
	};

	struct SKeyStatusLayout
	{
		float m_X;
		float m_Y;
		float m_W;
		float m_H;
		float m_FontSize;
		float m_LineHeight;
		float m_PaddingX;
		float m_PaddingY;
	};

	constexpr float KEY_STATUS_RIGHT_MARGIN = 0.0f;

	SKeyStatusLines GetKeyStatusLines(const CGameClient *pGameClient)
	{
		SKeyStatusLines Lines{};
		Lines.m_ShowKey = g_Config.m_ClShowhudKeyStatusReset != 0;
		Lines.m_ShowHammer = g_Config.m_ClShowhudKeyStatusHammer != 0;
		Lines.m_ShowControl = g_Config.m_ClShowhudKeyStatusControl != 0;
		Lines.m_ShowSync = g_Config.m_ClShowhudKeyStatusSync != 0;
		const CGameClient::SDemoHudPlaybackState *pDemoState = pGameClient != nullptr ? pGameClient->DemoHudPlaybackState() : nullptr;
		const int DummyResetOnSwitch = pDemoState != nullptr ? pDemoState->m_DummyResetOnSwitch : g_Config.m_ClDummyResetOnSwitch;
		const int DeepflyMode = pDemoState != nullptr ? pDemoState->m_DeepflyMode : g_Config.m_QmDeepflyMode;
		const bool DummyControl = pDemoState != nullptr ? pDemoState->m_DummyControl : g_Config.m_ClDummyControl != 0;
		const bool DummyCopyMoves = pDemoState != nullptr ? pDemoState->m_DummyCopyMoves : g_Config.m_ClDummyCopyMoves != 0;

		if(Lines.m_ShowKey)
		{
			Lines.m_pKeyStatusText = Localize("Key Sticking: ?");
			if(DummyResetOnSwitch == 0)
				Lines.m_pKeyStatusText = Localize("Key Sticking: On");
			else if(DummyResetOnSwitch == 1)
				Lines.m_pKeyStatusText = Localize("Key Sticking: Off");
			else if(DummyResetOnSwitch == 2)
				Lines.m_pKeyStatusText = Localize("Key Sticking: Reset Self");
		}

		if(Lines.m_ShowHammer)
		{
			const char *pHammerState = Localize("Normal");
			if(DeepflyMode == 1)
				pHammerState = Localize("DF");
			else if(DeepflyMode == 2)
				pHammerState = Localize("HDF");
			else if(DeepflyMode == 3)
				pHammerState = Localize("Custom");
			str_format(Lines.m_aHammerLine, sizeof(Lines.m_aHammerLine), Localize("Hammer: %s"), pHammerState);
		}

		if(Lines.m_ShowControl)
		{
			const char *pControlState = DummyControl ? Localize("On") : Localize("Off");
			str_format(Lines.m_aControlLine, sizeof(Lines.m_aControlLine), Localize("Dummy Control: %s"), pControlState);
		}

		if(Lines.m_ShowSync)
		{
			const char *pSyncState = DummyCopyMoves ? Localize("On") : Localize("Off");
			// 对应 cl_dummy_copy_moves：英文用 Dummy copy，中文术语「分身同步」
			str_format(Lines.m_aSyncLine, sizeof(Lines.m_aSyncLine), Localize("Dummy copy: %s"), pSyncState);
		}

		return Lines;
	}

	SKeyStatusLayout GetKeyStatusLayout(ITextRender *pTextRender, const SKeyStatusLines &Lines, float HudWidth)
	{
		SKeyStatusLayout Layout{};
		Layout.m_FontSize = 7.0f;
		Layout.m_LineHeight = 9.0f;
		Layout.m_PaddingX = 4.0f;
		Layout.m_PaddingY = 3.0f;
		Layout.m_Y = 38.0f;

		int LineCount = 0;
		float MaxWidth = 0.0f;
		if(Lines.m_ShowKey)
		{
			MaxWidth = maximum(MaxWidth, pTextRender->TextWidth(Layout.m_FontSize, Lines.m_pKeyStatusText, -1, -1.0f));
			LineCount++;
		}
		if(Lines.m_ShowHammer)
		{
			MaxWidth = maximum(MaxWidth, pTextRender->TextWidth(Layout.m_FontSize, Lines.m_aHammerLine, -1, -1.0f));
			LineCount++;
		}
		if(Lines.m_ShowControl)
		{
			MaxWidth = maximum(MaxWidth, pTextRender->TextWidth(Layout.m_FontSize, Lines.m_aControlLine, -1, -1.0f));
			LineCount++;
		}
		if(Lines.m_ShowSync)
		{
			MaxWidth = maximum(MaxWidth, pTextRender->TextWidth(Layout.m_FontSize, Lines.m_aSyncLine, -1, -1.0f));
			LineCount++;
		}

		if(LineCount == 0)
		{
			Layout.m_W = 0.0f;
			Layout.m_H = 0.0f;
			return Layout;
		}

		Layout.m_W = MaxWidth + Layout.m_PaddingX * 2.0f;
		Layout.m_H = Layout.m_LineHeight * LineCount + Layout.m_PaddingY * 2.0f;
		const float MaxX = maximum(HudWidth - Layout.m_W, 0.0f);
		Layout.m_X = std::clamp(HudWidth - Layout.m_W - KEY_STATUS_RIGHT_MARGIN, 0.0f, MaxX);
		return Layout;
	}

	SKeyStatusLayout GetKeyStatusLayout(ITextRender *pTextRender, const std::vector<std::string> &vLines, float HudWidth)
	{
		SKeyStatusLayout Layout{};
		Layout.m_FontSize = 7.0f;
		Layout.m_LineHeight = 9.0f;
		Layout.m_PaddingX = 4.0f;
		Layout.m_PaddingY = 3.0f;
		Layout.m_Y = 38.0f;

		const int LineCount = (int)vLines.size();
		if(LineCount == 0)
		{
			Layout.m_W = 0.0f;
			Layout.m_H = 0.0f;
			return Layout;
		}

		float MaxWidth = 0.0f;
		for(const std::string &Line : vLines)
		{
			MaxWidth = maximum(MaxWidth, pTextRender->TextWidth(Layout.m_FontSize, Line.c_str(), -1, -1.0f));
		}

		Layout.m_W = MaxWidth + Layout.m_PaddingX * 2.0f;
		Layout.m_H = Layout.m_LineHeight * LineCount + Layout.m_PaddingY * 2.0f;
		const float MaxX = maximum(HudWidth - Layout.m_W, 0.0f);
		Layout.m_X = std::clamp(HudWidth - Layout.m_W - KEY_STATUS_RIGHT_MARGIN, 0.0f, MaxX);
		return Layout;
	}
}

void CHud::RenderKeyStatus()
{
	const SKeyStatusLines Lines = GetKeyStatusLines(GameClient());
	const SKeyStatusLayout Layout = GetKeyStatusLayout(TextRender(), Lines, m_Width);
	if(Layout.m_H <= 0.0f)
		return;

	Ui()->RenderGaussianBlur({Layout.m_X, Layout.m_Y, Layout.m_W, Layout.m_H}, 1.0f, IGraphics::CORNER_ALL, ui_token::radius::BASE);
	Graphics()->DrawRect(Layout.m_X, Layout.m_Y, Layout.m_W, Layout.m_H, ui_token::color::SURFACE_GLASS, IGraphics::CORNER_ALL, ui_token::radius::BASE);

	float TextX = Layout.m_X + Layout.m_PaddingX;
	float TextY = Layout.m_Y + Layout.m_PaddingY;

	const float KeyTime = Client()->GlobalTime();
	const float KeyHue = std::fmod(KeyTime * 0.2f, 1.0f);
	ColorHSLA KeyRainbowHsla(KeyHue, 0.75f, 0.6f, 1.0f);
	ColorRGBA KeyRainbowColor = color_cast<ColorRGBA>(KeyRainbowHsla);

	TextRender()->TextColor(g_Config.m_ClHudRainbowColors ? KeyRainbowColor : TextRender()->DefaultTextColor());
	if(Lines.m_ShowKey)
	{
		TextRender()->Text(TextX, TextY, Layout.m_FontSize, Lines.m_pKeyStatusText, -1.0f);
		TextY += Layout.m_LineHeight;
	}
	if(Lines.m_ShowHammer)
	{
		TextRender()->Text(TextX, TextY, Layout.m_FontSize, Lines.m_aHammerLine, -1.0f);
		TextY += Layout.m_LineHeight;
	}
	if(Lines.m_ShowControl)
	{
		TextRender()->Text(TextX, TextY, Layout.m_FontSize, Lines.m_aControlLine, -1.0f);
		TextY += Layout.m_LineHeight;
	}
	if(Lines.m_ShowSync)
	{
		TextRender()->Text(TextX, TextY, Layout.m_FontSize, Lines.m_aSyncLine, -1.0f);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

inline int CHud::GetDigitsIndex(int Value, int Max)
{
	if(Value < 0)
	{
		Value *= -1;
	}
	int DigitsIndex = std::log10((Value ? Value : 1));
	if(DigitsIndex > Max)
	{
		DigitsIndex = Max;
	}
	if(DigitsIndex < 0)
	{
		DigitsIndex = 0;
	}
	return DigitsIndex;
}

inline float CHud::GetMovementInformationBoxHeight()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && (GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW || GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId].m_SpecCharPresent))
		return g_Config.m_ClShowhudPlayerPosition ? 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT + 2.0f : 0.0f;
	float BoxHeight = 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * (g_Config.m_ClShowhudPlayerPosition + g_Config.m_ClShowhudPlayerSpeed) + 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * g_Config.m_ClShowhudPlayerAngle;
	// 新增玩家统计显示行（3行：存活时长、救醒/落水、出钩比例）
	if(g_Config.m_QmPlayerStatsHud)
	{
		BoxHeight += 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
		if(g_Config.m_QmPlayerStatsMapProgressStyle != 0 && GameClient()->m_TClient.IsGoresMapProgressEnabled())
			BoxHeight += 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
	}
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
	{
		BoxHeight += 2.0f;
	}
	return BoxHeight;
}

void CHud::UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, float &PrevValue)
{
	Value = std::round(Value * 100.0f) / 100.0f; // Round to 2dp
	if(TextContainer.Valid() && PrevValue == Value)
		return;
	PrevValue = Value;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%.2f", Value);

	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize;
	TextRender()->RecreateTextContainer(TextContainer, &Cursor, aBuf);
}

void CHud::RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y)
{
	if(TextContainer.Valid())
	{
		TextRender()->RenderTextContainer(TextContainer, Color, TextRender()->DefaultTextOutlineColor(), X - TextRender()->GetBoundingBoxTextContainer(TextContainer).m_W, Y);
	}
}

CHud::CMovementInformation CHud::GetMovementInformation(int ClientId, int Conn) const
{
	CMovementInformation Out;
	if(ClientId == SPEC_FREEVIEW)
	{
		Out.m_Pos = GameClient()->m_Camera.m_Center / 32.0f;
	}
	else if(GameClient()->m_aClients[ClientId].m_SpecCharPresent)
	{
		Out.m_Pos = GameClient()->m_aClients[ClientId].m_SpecChar / 32.0f;
	}
	else
	{
		const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
		const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		const float IntraTick = Client()->IntraGameTick(Conn);

		// To make the player position relative to blocks we need to divide by the block size
		Out.m_Pos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick) / 32.0f;

		const vec2 Vel = mix(vec2(pPrevChar->m_VelX, pPrevChar->m_VelY), vec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

		float VelspeedX = Vel.x / 256.0f * Client()->GameTickSpeed();
		if(Vel.x >= -1.0f && Vel.x <= 1.0f)
		{
			VelspeedX = 0.0f;
		}
		float VelspeedY = Vel.y / 256.0f * Client()->GameTickSpeed();
		if(Vel.y >= -128.0f && Vel.y <= 128.0f)
		{
			VelspeedY = 0.0f;
		}
		// We show the speed in Blocks per Second (Bps) and therefore have to divide by the block size
		Out.m_Speed.x = VelspeedX / 32.0f;
		float VelspeedLength = length(vec2(Vel.x, Vel.y) / 256.0f) * Client()->GameTickSpeed();
		// Todo: Use Velramp tuning of each individual player
		// Since these tuning parameters are almost never changed, the default values are sufficient in most cases
		float Ramp = VelocityRamp(VelspeedLength, GameClient()->m_aTuning[Conn].m_VelrampStart, GameClient()->m_aTuning[Conn].m_VelrampRange, GameClient()->m_aTuning[Conn].m_VelrampCurvature);
		Out.m_Speed.x *= Ramp;
		Out.m_Speed.y = VelspeedY / 32.0f;

		float Angle = GameClient()->m_Players.GetPlayerTargetAngle(pPrevChar, pCurChar, ClientId, IntraTick);
		if(Angle < 0.0f)
		{
			Angle += 2.0f * pi;
		}
		Out.m_Angle = Angle * 180.0f / pi;
	}
	return Out;
}

void CHud::RenderMovementInformation()
{
	m_MovementInfoBoxValid = false;

	const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
	const bool FastPracticeParticipant = GameClient()->m_FastPractice.IsPracticeParticipant(ClientId);
	const bool PosOnly = ClientId == SPEC_FREEVIEW || (GameClient()->m_aClients[ClientId].m_SpecCharPresent);
	const bool ShowPosition = g_Config.m_ClShowhudPlayerPosition;
	const bool ShowSpeed = !PosOnly && (g_Config.m_ClShowhudPlayerSpeed || FastPracticeParticipant);
	const bool ShowAngle = !PosOnly && g_Config.m_ClShowhudPlayerAngle;
	const bool ShowStats = !PosOnly && g_Config.m_QmPlayerStatsHud;
	const bool ShowMovementInfo = ShowPosition || ShowSpeed || ShowAngle || ShowStats;

	const float LineSpacer = 1.0f; // above and below each entry
	const float Fontsize = 6.0f;
	const float KeyStatusGap = 2.0f;

	const SKeyStatusLines KeyStatusLines = GetKeyStatusLines(GameClient());
	SKeyStatusLayout KeyStatusLayout = GetKeyStatusLayout(TextRender(), KeyStatusLines, m_Width);
	// 自定义 bind 状态列表非空时完全替换内置四项
	std::vector<std::string> vCustomKeyStatusLines;
	if(GameClient()->m_QmBindStatusHud.IsCustomListActive())
	{
		vCustomKeyStatusLines = GameClient()->m_QmBindStatusHud.GetVisibleLines();
		KeyStatusLayout = GetKeyStatusLayout(TextRender(), vCustomKeyStatusLines, m_Width);
	}
	const bool ShowKeyStatus = KeyStatusLayout.m_H > 0.0f;

	float MovementBoxHeight = ShowMovementInfo ? GetMovementInformationBoxHeight() : 0.0f;
	bool HasDummyInfo = false;
	CMovementInformation DummyInfo{};

	if(ShowMovementInfo && Client()->DummyConnected())
	{
		int DummyClientId = -1;

		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int SpectId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

			if(SpectId == GameClient()->m_aLocalIds[0])
			{
				DummyClientId = GameClient()->m_aLocalIds[1];
			}
			else if(SpectId == GameClient()->m_aLocalIds[1])
			{
				DummyClientId = GameClient()->m_aLocalIds[0];
			}
			else
			{
				DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
			}
		}
		else
		{
			DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
		}

		if(DummyClientId >= 0 && DummyClientId < MAX_CLIENTS &&
			GameClient()->m_aClients[DummyClientId].m_Active)
		{
			DummyInfo = GetMovementInformation(
				DummyClientId,
				DummyClientId == GameClient()->m_aLocalIds[1]);
			HasDummyInfo = true;
		}
	}

	const bool ShowDummyPos = HasDummyInfo && ShowPosition && g_Config.m_TcShowhudDummyPosition;
	const bool ShowDummySpeed = HasDummyInfo && ShowSpeed && g_Config.m_TcShowhudDummySpeed;
	const bool ShowDummyAngle = HasDummyInfo && ShowAngle && g_Config.m_TcShowhudDummyAngle;

	if(ShowDummyPos)
		MovementBoxHeight += 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
	if(ShowDummySpeed)
		MovementBoxHeight += 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
	if(ShowDummyAngle)
		MovementBoxHeight += 1.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;

	float BoxWidth = 62.0f;
	BoxWidth = maximum(BoxWidth, KeyStatusLayout.m_W);

	float BoxHeight = 0.0f;
	if(ShowKeyStatus)
		BoxHeight += KeyStatusLayout.m_H;
	if(ShowMovementInfo && MovementBoxHeight > 0.0f)
	{
		if(ShowKeyStatus)
			BoxHeight += KeyStatusGap;
		BoxHeight += MovementBoxHeight;
	}
	if(BoxHeight <= 0.0f)
		return;

	const bool ShowDummyActionsHud = g_Config.m_ClShowhudDummyActions &&
					 !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) &&
					 Client()->DummyConnected();
	const float DummyActionsReserveY = ShowDummyActionsHud ? (29.0f + 4.0f) : 0.0f;

	const float MaxStartX = maximum(0.0f, m_Width - BoxWidth);
	float StartX = std::clamp(m_Width - BoxWidth - KEY_STATUS_RIGHT_MARGIN, 0.0f, MaxStartX);
	float StartY = 285.0f - BoxHeight - 4.0f;
	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56.0f;
	}
	StartY -= DummyActionsReserveY;
	if(StartY < 0.0f)
		StartY = 0.0f;

	m_MovementInfoBoxValid = true;
	m_MovementInfoBoxX = StartX;
	m_MovementInfoBoxY = StartY;
	m_MovementInfoBoxW = BoxWidth;
	m_MovementInfoBoxH = BoxHeight;
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::MovementInfo, {StartX, StartY, BoxWidth, BoxHeight});

	Ui()->RenderGaussianBlur({StartX, StartY, BoxWidth, BoxHeight}, 1.0f, HudEditorScope.m_Corners, ui_token::radius::BASE);
	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);

	const bool HasMovementContent = ShowMovementInfo && MovementBoxHeight > 0.0f;
	if(HasMovementContent)
	{
		const CMovementInformation Info = GetMovementInformation(ClientId, g_Config.m_ClDummy);

		float y = StartY + LineSpacer * 2.0f;
		const float LeftX = StartX + 2.0f;
		const float RightX = StartX + BoxWidth - 2.0f;

		if(ShowPosition)
		{
			TextRender()->Text(LeftX, y, Fontsize, Localize("Position:"), -1.0f);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;

			TextRender()->Text(LeftX, y, Fontsize, "X:", -1.0f);
			UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[0], Fontsize, Info.m_Pos.x, m_aPlayerPrevPosition[0]);

			ColorRGBA TextColor = TextRender()->DefaultTextColor();
			if(ShowDummyPos && fabsf(Info.m_Pos.x - DummyInfo.m_Pos.x) < 0.01f)
				TextColor = ColorRGBA(0.2f, 1.0f, 0.2f, 1.0f);

			RenderMovementInformationTextContainer(m_aPlayerPositionContainers[0], TextColor, RightX, y);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;

			TextRender()->Text(LeftX, y, Fontsize, "Y:", -1.0f);
			UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[1], Fontsize, Info.m_Pos.y, m_aPlayerPrevPosition[1]);
			RenderMovementInformationTextContainer(m_aPlayerPositionContainers[1], TextRender()->DefaultTextColor(), RightX, y);
			y += MOVEMENT_INFORMATION_LINE_HEIGHT;

			if(ShowDummyPos)
			{
				char aBuf[32];

				TextRender()->Text(LeftX, y, Fontsize, "DX:", -1.0f);
				str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Pos.x);

				ColorRGBA DummyTextColor = TextRender()->DefaultTextColor();
				if(fabsf(Info.m_Pos.x - DummyInfo.m_Pos.x) < 0.01f)
					DummyTextColor = ColorRGBA(0.2f, 1.0f, 0.2f, 1.0f);

				TextRender()->TextColor(DummyTextColor);
				TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				TextRender()->Text(LeftX, y, Fontsize, "DY:", -1.0f);
				str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Pos.y);
				TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;
			}
		}

		if(!PosOnly)
		{
			if(ShowSpeed)
			{
				TextRender()->Text(LeftX, y, Fontsize, Localize("Speed:"), -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				const char aaCoordinates[][4] = {"X:", "Y:"};
				for(int i = 0; i < 2; i++)
				{
					ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
					if(m_aLastPlayerSpeedChange[i] == ESpeedChange::INCREASE)
						Color = ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f);
					if(m_aLastPlayerSpeedChange[i] == ESpeedChange::DECREASE)
						Color = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
					TextRender()->Text(LeftX, y, Fontsize, aaCoordinates[i], -1.0f);
					UpdateMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Fontsize, i == 0 ? Info.m_Speed.x : Info.m_Speed.y, m_aPlayerPrevSpeed[i]);
					RenderMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Color, RightX, y);
					y += MOVEMENT_INFORMATION_LINE_HEIGHT;
				}

				if(ShowDummySpeed)
				{
					char aBuf[32];

					TextRender()->Text(LeftX, y, Fontsize, "DX:", -1.0f);
					str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Speed.x);
					TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
					y += MOVEMENT_INFORMATION_LINE_HEIGHT;

					TextRender()->Text(LeftX, y, Fontsize, "DY:", -1.0f);
					str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Speed.y);
					TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
					y += MOVEMENT_INFORMATION_LINE_HEIGHT;
				}

				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}

			if(ShowAngle)
			{
				TextRender()->Text(LeftX, y, Fontsize, Localize("Angle:"), -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				UpdateMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, Fontsize, Info.m_Angle, m_PlayerPrevAngle);
				RenderMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, TextRender()->DefaultTextColor(), RightX, y);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				if(ShowDummyAngle)
				{
					char aBuf[32];

					TextRender()->Text(LeftX, y, Fontsize, "DA:", -1.0f);
					str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Angle);
					TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
					y += MOVEMENT_INFORMATION_LINE_HEIGHT;
				}
			}

			// 玩家统计HUD显示
			if(ShowStats)
			{
				const auto &Stats = GameClient()->m_TClient.GetPlayerStats(g_Config.m_ClDummy);
				int TickSpeed = Client()->GameTickSpeed();
				char aBuf[128];

				// 彩虹动态颜色
				const float StatsTime = Client()->GlobalTime();
				const float StatsHue = std::fmod(StatsTime * 0.2f + 0.1f, 1.0f);
				ColorHSLA RainbowHsla(StatsHue, 0.75f, 0.6f, 1.0f);
				ColorRGBA RainbowColor = color_cast<ColorRGBA>(RainbowHsla);
				const ColorRGBA DefaultHudTextColor = TextRender()->DefaultTextColor();

				// 平均/最大存活时长
				float AvgAlive = Stats.GetAverageAliveTime(TickSpeed);
				float MaxAlive = Stats.GetMaxAliveTime(TickSpeed);
				str_format(aBuf, sizeof(aBuf), Localize("Alive: %.1fs/%.1fs"), AvgAlive, MaxAlive);
				TextRender()->TextColor(g_Config.m_ClHudRainbowColors ? RainbowColor : DefaultHudTextColor);
				TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				// 被救醒次数/落水次数
				const float Hue2 = std::fmod(StatsTime * 0.2f + 0.2f, 1.0f);
				ColorHSLA RainbowHsla2(Hue2, 0.75f, 0.6f, 1.0f);
				ColorRGBA RainbowColor2 = color_cast<ColorRGBA>(RainbowHsla2);
				str_format(aBuf, sizeof(aBuf), Localize("Rescue/Freeze: %d/%d"), Stats.m_RescueCount, Stats.m_FreezeCount);
				TextRender()->TextColor(g_Config.m_ClHudRainbowColors ? RainbowColor2 : DefaultHudTextColor);
				TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				// 左侧/右侧出钩比例
				const float Hue3 = std::fmod(StatsTime * 0.2f + 0.3f, 1.0f);
				ColorHSLA RainbowHsla3(Hue3, 0.75f, 0.6f, 1.0f);
				ColorRGBA RainbowColor3 = color_cast<ColorRGBA>(RainbowHsla3);
				float LeftRatio = Stats.GetHookLeftRatio() * 100.0f;
				float RightRatio = Stats.GetHookRightRatio() * 100.0f;
				str_format(aBuf, sizeof(aBuf), Localize("Hooking L/R: %.0f%%/%.0f%%"), LeftRatio, RightRatio);
				TextRender()->TextColor(g_Config.m_ClHudRainbowColors ? RainbowColor3 : DefaultHudTextColor);
				TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				if(g_Config.m_QmPlayerStatsMapProgressStyle != 0 && GameClient()->m_TClient.IsGoresMapProgressEnabled())
				{
					const int DummyIndex = g_Config.m_ClDummy ? 1 : 0;
					const bool HasProgress = GameClient()->m_TClient.HasGoresMapProgress(DummyIndex);
					const float Progress = HasProgress ? GameClient()->m_TClient.GetGoresMapProgress(DummyIndex) : 0.0f;

					if(HasProgress)
						str_format(aBuf, sizeof(aBuf), Localize("Map Progress: %.1f%%"), Progress * 100.0f);
					else
						str_copy(aBuf, Localize("Map Progress: --"));

					const float Hue4 = std::fmod(StatsTime * 0.2f + 0.4f, 1.0f);
					ColorHSLA RainbowHsla4(Hue4, 0.75f, 0.6f, 1.0f);
					ColorRGBA RainbowColor4 = color_cast<ColorRGBA>(RainbowHsla4);
					const ColorRGBA ProgressColor = g_Config.m_ClHudRainbowColors ? RainbowColor4 : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmPlayerStatsMapProgressColor, true));
					TextRender()->TextColor(g_Config.m_ClHudRainbowColors ? RainbowColor4 : DefaultHudTextColor);
					TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					y += MOVEMENT_INFORMATION_LINE_HEIGHT;

					const float BarWidth = 42.0f;
					const float BarHeight = 3.0f;
					const float BarX = RightX - BarWidth;
					const float BarY = y + (MOVEMENT_INFORMATION_LINE_HEIGHT - BarHeight) * 0.5f;
					Graphics()->DrawRect(BarX, BarY, BarWidth, BarHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), IGraphics::CORNER_ALL, 1.0f);
					if(HasProgress)
						Graphics()->DrawRect(BarX, BarY, BarWidth * std::clamp(Progress, 0.0f, 1.0f), BarHeight, ProgressColor.WithAlpha(0.85f), IGraphics::CORNER_ALL, 1.0f);
					y += MOVEMENT_INFORMATION_LINE_HEIGHT;
				}
			}
		}
	}

	if(ShowKeyStatus)
	{
		float KeyStatusY = StartY + BoxHeight - KeyStatusLayout.m_H;
		float KeyTextX = StartX + KeyStatusLayout.m_PaddingX;
		float KeyTextY = KeyStatusY + KeyStatusLayout.m_PaddingY;

		const float KeyTime = Client()->GlobalTime();
		const float KeyHue = std::fmod(KeyTime * 0.2f, 1.0f);
		ColorHSLA KeyRainbowHsla(KeyHue, 0.75f, 0.6f, 1.0f);
		ColorRGBA KeyRainbowColor = color_cast<ColorRGBA>(KeyRainbowHsla);

		TextRender()->TextColor(g_Config.m_ClHudRainbowColors ? KeyRainbowColor : TextRender()->DefaultTextColor());
		if(GameClient()->m_QmBindStatusHud.IsCustomListActive())
		{
			for(const std::string &Line : vCustomKeyStatusLines)
			{
				TextRender()->Text(KeyTextX, KeyTextY, KeyStatusLayout.m_FontSize, Line.c_str(), -1.0f);
				KeyTextY += KeyStatusLayout.m_LineHeight;
			}
		}
		else if(KeyStatusLines.m_ShowKey)
		{
			TextRender()->Text(KeyTextX, KeyTextY, KeyStatusLayout.m_FontSize, KeyStatusLines.m_pKeyStatusText, -1.0f);
			KeyTextY += KeyStatusLayout.m_LineHeight;
		}
		if(KeyStatusLines.m_ShowHammer)
		{
			TextRender()->Text(KeyTextX, KeyTextY, KeyStatusLayout.m_FontSize, KeyStatusLines.m_aHammerLine, -1.0f);
			KeyTextY += KeyStatusLayout.m_LineHeight;
		}
		if(KeyStatusLines.m_ShowControl)
		{
			TextRender()->Text(KeyTextX, KeyTextY, KeyStatusLayout.m_FontSize, KeyStatusLines.m_aControlLine, -1.0f);
			KeyTextY += KeyStatusLayout.m_LineHeight;
		}
		if(KeyStatusLines.m_ShowSync)
		{
			TextRender()->Text(KeyTextX, KeyTextY, KeyStatusLayout.m_FontSize, KeyStatusLines.m_aSyncLine, -1.0f);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderJumpHint()
{
	if(!g_Config.m_QmJumpHint)
		return;

	char aText[sizeof(g_Config.m_QmJumpHintText)];
	DecodeEscapedNewlines(g_Config.m_QmJumpHintText[0] != '\0' ? g_Config.m_QmJumpHintText : JUMP_HINT_DEFAULT_TEXT, aText, sizeof(aText));
	if(aText[0] == '\0')
		return;

	constexpr float PaddingX = 4.0f;
	constexpr float PaddingY = 3.0f;
	constexpr float LineSpacing = 0.0f;
	const float FontSize = std::clamp((float)g_Config.m_QmJumpHintSize, 1.0f, 50.0f);
	const STextBoundingBox TextBox = TextRender()->TextBoundingBox(FontSize, aText, -1, -1.0f, LineSpacing);
	const float BoxWidth = maximum(24.0f, TextBox.m_W + PaddingX * 2.0f);
	const float BoxHeight = maximum(FontSize + PaddingY * 2.0f, TextBox.m_H + PaddingY * 2.0f);
	const float MaxX = maximum(0.0f, m_Width - BoxWidth);
	const float MaxY = maximum(0.0f, m_Height - BoxHeight);
	const float StartX = std::clamp(m_Width * std::clamp(g_Config.m_QmJumpHintX, 0, 100) / 100.0f, 0.0f, MaxX);
	const float StartY = std::clamp(m_Height * std::clamp(g_Config.m_QmJumpHintY, 0, 100) / 100.0f, 0.0f, MaxY);

	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::JumpHint, {StartX, StartY, BoxWidth, BoxHeight});
	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);

	CTextCursor Cursor;
	Cursor.SetPosition(vec2(StartX + PaddingX, StartY + PaddingY));
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineSpacing = LineSpacing;
	const int TextColorConfig = g_Config.m_QmJumpHintColor == JUMP_HINT_LEGACY_BLACK_COLOR ? JUMP_HINT_DEFAULT_COLOR : g_Config.m_QmJumpHintColor;
	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(TextColorConfig)));
	TextRender()->TextEx(&Cursor, aText);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderMapProgressBar()
{
	const bool Preview = GameClient()->m_HudEditor.IsActive();
	if(ShouldHideFocusMapProgress(g_Config.m_QmFocusMode != 0, g_Config.m_QmFocusModeHideMapProgress != 0) && !Preview)
		return;
	if(!g_Config.m_QmPlayerStatsMapProgress && !Preview)
		return;
	if(!GameClient()->m_TClient.IsGoresMapProgressEnabled() && !Preview)
		return;
	if(g_Config.m_QmPlayerStatsMapProgressStyle != 0 && g_Config.m_QmPlayerStatsHud)
		return;

	const int DummyIndex = g_Config.m_ClDummy ? 1 : 0;
	const bool HasProgress = Preview || GameClient()->m_TClient.HasGoresMapProgress(DummyIndex);
	const float TargetProgress = Preview ? 0.426f : (HasProgress ? std::clamp(GameClient()->m_TClient.GetGoresMapProgress(DummyIndex), 0.0f, 1.0f) : 0.0f);
	const bool ProgressWasInitialized = m_aMapProgressInitialized[DummyIndex];
	const float PreviousDisplayedProgress = std::clamp(m_aMapProgressDisplayed[DummyIndex], 0.0f, 1.0f);
	if(!m_aMapProgressInitialized[DummyIndex])
	{
		m_aMapProgressDisplayed[DummyIndex] = TargetProgress;
		m_aMapProgressInitialized[DummyIndex] = true;
	}
	else
	{
		const float Blend = std::clamp(Client()->RenderFrameTime() * 8.0f, 0.0f, 1.0f);
		m_aMapProgressDisplayed[DummyIndex] = mix(m_aMapProgressDisplayed[DummyIndex], TargetProgress, Blend);
	}

	const float DisplayedProgress = std::clamp(m_aMapProgressDisplayed[DummyIndex], 0.0f, 1.0f);
	const bool ProgressIncreased = ProgressWasInitialized && DisplayedProgress > PreviousDisplayedProgress + 0.000001f;
	const ColorRGBA ConfiguredColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmPlayerStatsMapProgressColor, true));
	const ColorRGBA FillColor = ColorRGBA(ConfiguredColor.r, ConfiguredColor.g, ConfiguredColor.b, std::clamp(maximum(ConfiguredColor.a, 0.65f), 0.0f, 1.0f));
	const ColorRGBA TrackColor = LerpColor(ColorRGBA(0.02f, 0.03f, 0.03f, 0.78f), FillColor.WithAlpha(0.26f), 0.32f);
	const ColorRGBA TextColor = LerpColor(ColorRGBA(0.92f, 0.97f, 1.0f, 1.0f), FillColor.WithAlpha(1.0f), 0.72f);

	const float WidthRatio = std::clamp(g_Config.m_QmPlayerStatsMapProgressWidth / 100.0f, 0.10f, 0.80f);
	const float BarWidth = std::clamp(m_Width * WidthRatio, 80.0f, maximum(80.0f, m_Width - 12.0f));
	const float BarHeight = (float)g_Config.m_QmPlayerStatsMapProgressHeight;
	const float BarRadius = BarHeight * 0.5f;
	const float RawBarX = m_Width * (g_Config.m_QmPlayerStatsMapProgressPosX / 100.0f) - BarWidth * 0.5f;
	const float RawBarY = m_Height * (g_Config.m_QmPlayerStatsMapProgressPosY / 100.0f);
	const float BarX = std::round(std::clamp(RawBarX, 6.0f, maximum(6.0f, m_Width - BarWidth - 6.0f)));
	const float BarY = std::round(std::clamp(RawBarY, 6.0f, maximum(6.0f, m_Height - BarHeight - 6.0f)));
	const float FillWidth = BarWidth * DisplayedProgress;

	char aProgressText[32];
	if(HasProgress)
		str_format(aProgressText, sizeof(aProgressText), "%.1f%%", DisplayedProgress * 100.0f);
	else
		str_copy(aProgressText, "--");

	const float TextSize = std::clamp(BarHeight * 0.72f, 6.0f, 16.0f);
	const float TextWidth = TextRender()->TextWidth(TextSize, aProgressText, -1, -1.0f);
	const float TextGap = std::clamp(BarHeight * 0.35f, 2.0f, 8.0f);
	const float TextX = std::round(BarX + BarWidth * 0.5f - TextWidth * 0.5f);
	const float TextY = std::round(std::clamp(BarY - TextSize - TextGap, 2.0f, maximum(2.0f, m_Height - TextSize - 2.0f)));
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::MapProgressBar, {BarX, TextY, BarWidth, BarY + BarHeight - TextY});

	DrawSmoothRoundedRect(Graphics(), BarX, BarY, BarWidth, BarHeight, BarRadius, TrackColor, HudEditorScope.m_Corners);
	if(FillWidth > 0.0f)
		DrawSmoothRoundedRect(Graphics(), BarX, BarY, FillWidth, BarHeight, BarRadius, FillColor, HudEditorScope.m_Corners);

	const unsigned int PrevTextFlags = TextRender()->GetRenderFlags();
	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.45f);
	TextRender()->TextColor(TextColor);
	TextRender()->Text(TextX, TextY, TextSize, aProgressText, -1.0f);
	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevOutlineColor);
	TextRender()->SetRenderFlags(PrevTextFlags);
	GameClient()->m_HudEditor.UpdateVisibleRect(EHudEditorElement::MapProgressBar, {BarX, TextY, BarWidth, BarY + BarHeight - TextY});

	int TeeClientId = GameClient()->m_aLocalIds[DummyIndex];
	if(TeeClientId < 0 || TeeClientId >= MAX_CLIENTS)
		TeeClientId = GameClient()->m_Snap.m_LocalClientId;

	if(TeeClientId >= 0 && TeeClientId < MAX_CLIENTS)
	{
		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[TeeClientId].m_RenderInfo;
		TeeInfo.m_Size = std::clamp(BarHeight * 1.7f, 14.0f, 30.0f);

		const float TeePadding = TeeInfo.m_Size * 0.28f;
		const float TeeX = std::clamp(BarX + BarWidth * DisplayedProgress, BarX + TeePadding, BarX + BarWidth - TeePadding);
		const float TeeAnchorY = BarY + BarHeight * 0.5f - std::clamp(BarHeight * 0.15f, 1.0f, 3.0f);
		const CAnimState *pTeeState = CAnimState::GetIdle();
		CAnimState RunState;
		if(ProgressIncreased)
		{
			const float RunTime = std::fmod(time_get() / (float)time_freq() * 2.5f, 1.0f);
			RunState.Set(&g_pData->m_aAnimations[ANIM_BASE], 0.0f);
			RunState.Add(&g_pData->m_aAnimations[ANIM_RUN_RIGHT], RunTime, 1.0f);
			pTeeState = &RunState;
		}
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pTeeState, &TeeInfo, OffsetToMid);

		DrawSmoothCircle(Graphics(), vec2(TeeX, TeeAnchorY), std::clamp(BarHeight * 0.48f, 4.0f, 10.0f), FillColor.WithAlpha(0.22f));
		RenderTools()->RenderTee(pTeeState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(TeeX, TeeAnchorY + OffsetToMid.y));
	}
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderSpectatorHud()
{
	if(!g_Config.m_ClShowhudSpectator)
		return;

	// TClient
	float AdjustedHeight = m_Height - (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f);
	float BoundsTop = AdjustedHeight - 15.0f;
	float BoundsBottom = AdjustedHeight;
	const bool ShowAutoTag = Client()->State() != IClient::STATE_DEMOPLAYBACK &&
				 GameClient()->m_Camera.SpectatingPlayer() &&
				 GameClient()->m_Camera.CanUseAutoSpecCamera() &&
				 g_Config.m_ClSpecAutoSync;
	if(ShowAutoTag)
	{
		BoundsTop = minimum(BoundsTop, m_Height - 12.0f);
		BoundsBottom = maximum(BoundsBottom, m_Height - 2.0f);
	}
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::SpectatorHud, {m_Width - 180.0f, BoundsTop, 180.0f, BoundsBottom - BoundsTop});

	// draw the box
	Graphics()->DrawRect(m_Width - 180.0f, AdjustedHeight - 15.0f, 180.0f, 15.0f, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);

	// draw the text
	char aBuf[128];
	if(GameClient()->m_MultiViewActivated)
	{
		str_copy(aBuf, Localize("Multi-View"));
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
	{
		const auto &Player = GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId];
		char aNameBuf[MAX_NAME_LENGTH];
		GameClient()->FormatStreamerName(Player.ClientId(), aNameBuf, sizeof(aNameBuf));
		const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(Player.ClientId());
		if(g_Config.m_ClShowIds && !HideIdentity)
			str_format(aBuf, sizeof(aBuf), Localize("Following %d: %s", "Spectating"), Player.ClientId(), aNameBuf);
		else
			str_format(aBuf, sizeof(aBuf), Localize("Following %s", "Spectating"), aNameBuf);
	}
	else
	{
		str_copy(aBuf, Localize("Free-View"));
	}
	TextRender()->Text(m_Width - 174.0f, AdjustedHeight - 15.0f + (15.f - 8.f) / 2.f, 8.0f, aBuf, -1.0f);

	// draw the camera info
	if(ShowAutoTag)
	{
		bool AutoSpecCameraEnabled = GameClient()->m_Camera.m_AutoSpecCamera;
		const char *pLabelText = Localize("AUTO", "Spectating Camera Mode Icon");
		const float TextWidth = TextRender()->TextWidth(6.0f, pLabelText);

		constexpr float RightMargin = 4.0f;
		constexpr float IconWidth = 6.0f;
		constexpr float Padding = 3.0f;
		const float TagWidth = IconWidth + TextWidth + Padding * 3.0f;
		const float TagX = m_Width - RightMargin - TagWidth;
		Graphics()->DrawRect(TagX, m_Height - 12.0f, TagWidth, 10.0f, ColorRGBA(1.0f, 1.0f, 1.0f, AutoSpecCameraEnabled ? 0.50f : 0.10f), IGraphics::CORNER_ALL, 2.5f);
		TextRender()->TextColor(1, 1, 1, AutoSpecCameraEnabled ? 1.0f : 0.65f);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->Text(TagX + Padding, m_Height - 10.0f, 6.0f, FontIcons::FONT_ICON_CAMERA, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->Text(TagX + Padding + IconWidth + Padding, m_Height - 10.0f, 6.0f, pLabelText, -1.0f);
		TextRender()->TextColor(1, 1, 1, 1);
	}

	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
}

void CHud::RenderLocalTime(float x)
{
	if(!g_Config.m_ClShowLocalTimeAlways && !GameClient()->m_Scoreboard.IsActive())
	{
		m_LocalTimeV2AnimState.Reset();
		return;
	}

	if(g_Config.m_QmHudIslandUseOriginalStyle)
	{
		m_LocalTimeV2AnimState.Reset();

		char aTimeStr[6];
		str_timestamp_format(aTimeStr, sizeof(aTimeStr), "%H:%M");
		const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::LocalTime, {x - 30.0f, 0.0f, 25.0f, 12.5f});
		Graphics()->DrawRect(x - 30.0f, 0.0f, 25.0f, 12.5f, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);
		TextRender()->Text(x - 25.0f, (12.5f - 5.f) / 2.f, 5.0f, aTimeStr, -1.0f);
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
		return;
	}

	const bool UseV2LocalTime = true;
	CUiV2AnimationRuntime *pAnimRuntime = nullptr;
	if(UseV2LocalTime)
		pAnimRuntime = &GameClient()->UiRuntimeV2()->AnimRuntime();
	else
		m_LocalTimeV2AnimState.Reset();

	const bool Seconds = g_Config.m_TcShowLocalTimeSeconds; // TClient

	char aTimeStr[16];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), Seconds ? "%H:%M.%S" : "%H:%M");
	const float TextWidth = std::round(TextRender()->TextBoundingBox(5.0f, aTimeStr).m_W);

	if(!UseV2LocalTime)
	{
		const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::LocalTime, {x - (TextWidth + 15.0f), 0.0f, TextWidth + 10.0f, 12.5f});
		Graphics()->DrawRect(x - (TextWidth + 15.0f), 0.0f, TextWidth + 10.0f, 12.5f, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);
		TextRender()->Text(x - (TextWidth + 10.0f), (12.5f - 5.f) / 2.f, 5.0f, aTimeStr, -1.0f);
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
		return;
	}

	SUiLayoutBox TimeRootBox;
	TimeRootBox.m_X = x - (TextWidth + 15.0f);
	TimeRootBox.m_Y = 0.0f;
	TimeRootBox.m_W = TextWidth + 10.0f;
	TimeRootBox.m_H = 12.5f;

	CUiV2LayoutEngine LayoutEngine;
	SUiStyle TimeContainerStyle;
	TimeContainerStyle.m_Axis = EUiAxis::ROW;
	TimeContainerStyle.m_AlignItems = EUiAlign::START;
	TimeContainerStyle.m_JustifyContent = EUiAlign::START;
	TimeContainerStyle.m_Padding.m_Left = 5.0f;
	TimeContainerStyle.m_Padding.m_Right = 5.0f;
	TimeContainerStyle.m_Padding.m_Top = (12.5f - 5.0f) / 2.0f;
	TimeContainerStyle.m_Padding.m_Bottom = (12.5f - 5.0f) / 2.0f;

	std::vector<SUiLayoutChild> &vChildren = m_vLocalTimeLayoutChildrenScratch;
	if(vChildren.empty())
		vChildren.resize(1);
	vChildren[0] = SUiLayoutChild{};
	vChildren[0].m_Style.m_Width = SUiLength::Px(TextWidth);
	vChildren[0].m_Style.m_Height = SUiLength::Px(5.0f);
	LayoutEngine.ComputeChildren(TimeContainerStyle, TimeRootBox, vChildren);

	float BoxX = TimeRootBox.m_X;
	float BoxW = TimeRootBox.m_W;
	float TextX = vChildren[0].m_Box.m_X;
	const float TextY = vChildren[0].m_Box.m_Y;

	if(pAnimRuntime != nullptr)
	{
		const uint64_t BoxNode = HudLocalTimeNodeKey("box");
		const uint64_t TextNode = HudLocalTimeNodeKey("text");
		if(!m_LocalTimeV2AnimState.m_Initialized)
		{
			m_LocalTimeV2AnimState.m_TargetBoxX = BoxX;
			m_LocalTimeV2AnimState.m_TargetBoxW = BoxW;
			m_LocalTimeV2AnimState.m_TargetTextX = TextX;
			SetUiPresentationStateValue(*pAnimRuntime, BoxNode, EUiAnimProperty::POS_X, BoxX);
			SetUiPresentationStateValue(*pAnimRuntime, BoxNode, EUiAnimProperty::WIDTH, BoxW);
			SetUiPresentationStateValue(*pAnimRuntime, TextNode, EUiAnimProperty::POS_X, TextX);
			m_LocalTimeV2AnimState.m_Initialized = true;
		}

		BoxX = ResolveAnimatedLayoutValue(*pAnimRuntime, BoxNode, EUiAnimProperty::POS_X, BoxX, m_LocalTimeV2AnimState.m_TargetBoxX);
		BoxW = ResolveAnimatedLayoutValue(*pAnimRuntime, BoxNode, EUiAnimProperty::WIDTH, BoxW, m_LocalTimeV2AnimState.m_TargetBoxW);
		TextX = ResolveAnimatedLayoutValue(*pAnimRuntime, TextNode, EUiAnimProperty::POS_X, TextX, m_LocalTimeV2AnimState.m_TargetTextX);
	}

	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::LocalTime, {BoxX, 0.0f, BoxW, 12.5f});
	Graphics()->DrawRect(BoxX, 0.0f, BoxW, 12.5f, ui_token::color::SURFACE_GLASS, HudEditorScope.m_Corners, ui_token::radius::BASE);
	TextRender()->Text(TextX, TextY, 5.0f, aTimeStr, -1.0f);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);

	// Graphics()->DrawRect(x - 30.0f, 0.0f, 25.0f, 12.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);
	// TextRender()->Text(x - 25.0f, (12.5f - 5.f) / 2.f, 5.0f, aTimeStr, -1.0f);
}

float CHud::RenderLegacyMediaInfoAt(float AnchorX, float CenterY)
{
	const bool Preview = GameClient()->m_HudEditor.IsActive();
	if(m_LegacyMediaInfoRendered || !g_Config.m_QmHudIslandUseOriginalStyle || !g_Config.m_QmSmtcShowHud || !SystemMediaControls::AnyMediaSourceEnabled(g_Config.m_QmSmtcEnable != 0, g_Config.m_QmNeteaseHookEnable != 0))
		return CenterY;
	EnsureMediaIslandFrameCache();

	CSystemMediaControls::SState PreviewMediaState{};
	const CSystemMediaControls::SState *pMediaState = nullptr;
	if(m_MediaIslandFrameCache.m_HasMediaState)
		pMediaState = &m_MediaIslandFrameCache.m_MediaState;
	else
	{
		if(!Preview)
			return CenterY;
		str_copy(PreviewMediaState.m_aTitle, "Pure Music", sizeof(PreviewMediaState.m_aTitle));
		str_copy(PreviewMediaState.m_aArtist, "QmClient", sizeof(PreviewMediaState.m_aArtist));
		PreviewMediaState.m_PositionMs = 56 * 1000;
		PreviewMediaState.m_DurationMs = 3 * 60 * 1000;
		pMediaState = &PreviewMediaState;
	}
	const CSystemMediaControls::SState &MediaState = *pMediaState;

	const bool HasTitle = MediaState.m_aTitle[0] != '\0';
	const bool HasArtist = MediaState.m_aArtist[0] != '\0';
	if(!HasTitle && !HasArtist)
		return CenterY;

	m_LegacyMediaInfoRendered = true;

	constexpr float IslandHeight = 16.0f;
	constexpr float CoverSize = 14.0f;
	constexpr float PaddingX = 2.0f;
	constexpr float IconGap = 3.0f;
	constexpr float TextMaxWidth = 70.0f;
	constexpr float TitleSize = 7.0f;
	constexpr float ArtistSize = 6.0f;
	const float IslandWidth = PaddingX + CoverSize + IconGap + TextMaxWidth + PaddingX;
	const float IslandX = std::clamp(AnchorX, 0.0f, maximum(0.0f, m_Width - IslandWidth));
	const float IslandY = std::clamp(CenterY - IslandHeight * 0.5f, 0.0f, maximum(0.0f, m_Height - IslandHeight));
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::LegacyMediaInfo, {IslandX, IslandY, IslandWidth, IslandHeight});

	Graphics()->DrawRect(IslandX, IslandY, IslandWidth, IslandHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), HudEditorScope.m_Corners, 4.0f);

	const float CoverX = IslandX + PaddingX;
	const float CoverY = IslandY + (IslandHeight - CoverSize) * 0.5f;
	if(MediaState.m_AlbumArt.IsValid())
	{
		Graphics()->WrapClamp();
		Graphics()->TextureSet(MediaState.m_AlbumArt);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem QuadItem(CoverX, CoverY, CoverSize, CoverSize);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	}
	else
	{
		Graphics()->DrawRect(CoverX, CoverY, CoverSize, CoverSize, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 2.0f);
	}

	const float TextX = CoverX + CoverSize + IconGap;
	const float TextAreaW = IslandX + IslandWidth - PaddingX - TextX;
	const float TitleY = IslandY + 1.0f;
	const float ArtistY = TitleY + TitleSize;

	const unsigned int PrevFlags = TextRender()->GetRenderFlags();
	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.35f);

	if(HasTitle)
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = TitleSize;
		Cursor.m_LineWidth = TextAreaW;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.SetPosition(vec2(TextX, TitleY));
		TextRender()->TextEx(&Cursor, MediaState.m_aTitle);
	}
	if(HasArtist)
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = ArtistSize;
		Cursor.m_LineWidth = TextAreaW;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.SetPosition(vec2(TextX, ArtistY));
		TextRender()->TextEx(&Cursor, MediaState.m_aArtist);
	}

	float ContentBottomY = IslandY + IslandHeight;

	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevOutlineColor);
	TextRender()->SetRenderFlags(PrevFlags);

	constexpr float BarHeight = 2.0f;
	const float BarY = IslandY + IslandHeight - BarHeight - 1.0f;
	Graphics()->DrawRect(TextX, BarY, TextAreaW, BarHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), IGraphics::CORNER_ALL, 1.0f);
	if(MediaState.m_DurationMs > 0)
	{
		const float Progress = std::clamp((float)MediaState.m_PositionMs / (float)MediaState.m_DurationMs, 0.0f, 1.0f);
		if(Progress > 0.0f)
			Graphics()->DrawRect(TextX, BarY, TextAreaW * Progress, BarHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f), IGraphics::CORNER_ALL, 1.0f);
	}
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);
	return ContentBottomY;
}

bool CHud::GetLegacyMediaInfoAnchor(float &AnchorX, float &CenterY) const
{
	if(GameClient()->m_Snap.m_pGameInfoObj == nullptr)
		return false;

	int ClientId = -1;
	if(GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		ClientId = GameClient()->m_Snap.m_LocalClientId;
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		ClientId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	if(ClientId < 0 || ClientId == SPEC_FREEVIEW || ClientId >= MAX_CLIENTS)
		return false;

	const CCharacterCore *pCharacter = &GameClient()->m_aClients[ClientId].m_Predicted;
	float x = 5.0f + 12.0f;
	float y = 5.0f + 12.0f +
		  (GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24.0f : 0.0f) +
		  (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12.0f : 0.0f);

	constexpr float aWeaponWidth[NUM_WEAPONS] = {16.0f, 12.0f, 12.0f, 12.0f, 12.0f, 12.0f};
	constexpr float aWeaponInitialOffset[NUM_WEAPONS] = {-3.0f, -4.0f, -1.0f, -1.0f, -2.0f, -4.0f};
	bool InitialOffsetAdded = false;
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
	{
		if(!pCharacter->m_aWeapons[Weapon].m_Got)
			continue;

		if(!InitialOffsetAdded)
		{
			x += aWeaponInitialOffset[Weapon];
			InitialOffsetAdded = true;
		}
		x += aWeaponWidth[Weapon];
	}

	AnchorX = x + 4.0f;
	CenterY = y;
	return true;
}

void CHud::RenderLegacyMediaInfo()
{
	if(m_LegacyMediaInfoRendered)
		return;

	float AnchorX = 5.0f + 12.0f + 32.0f;
	float CenterY = 5.0f + 12.0f +
			(GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24.0f : 0.0f) +
			(GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12.0f : 0.0f);
	GetLegacyMediaInfoAnchor(AnchorX, CenterY);
	RenderLegacyMediaInfoAt(AnchorX, CenterY);
}

void CHud::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	int ClientId = -1;
	if(GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		ClientId = GameClient()->m_Snap.m_LocalClientId;
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		ClientId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	if(ClientId == -1)
		return;

	const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
	const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	ivec2 Vel = mix(ivec2(pPrevChar->m_VelX, pPrevChar->m_VelY), ivec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

	CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(ClientId);
	if(pChar && pChar->IsGrounded())
		Vel.y = 0;

	int aVels[2] = {Vel.x, Vel.y};

	for(int i = 0; i < 2; i++)
	{
		int AbsVel = abs(aVels[i]);
		if(AbsVel > m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::INCREASE;
		}
		if(AbsVel < m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::DECREASE;
		}
		if(AbsVel < 2)
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::NONE;
		}
		m_aPlayerSpeed[i] = AbsVel;
	}
}

void CHud::OnRender()
{
	if((!QmHudMediaIslandShouldPrepareBackdropBlur(g_Config.m_QmHudIslandBgOpacity, g_Config.m_QmGaussianBlur != 0) || g_Config.m_QmHudIslandUseOriginalStyle || !Graphics()->HasMediaIslandSdf()) &&
		(m_MediaIslandBlurSource.IsValid() || m_MediaIslandBlurTemporary.IsValid() || m_MediaIslandBlurTarget.IsValid()))
		DestroyMediaIslandBlurTargets();

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	m_Width = 300.0f * Graphics()->ScreenAspect();
	m_Height = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
	m_MovementInfoBoxValid = false;
	m_LegacyMediaInfoRendered = false;
	UpdateSwitchCountdownTracker();
	const bool ShowMediaIsland = HasVisibleMediaIsland();
	if(!ShowMediaIsland)
	{
		m_MediaIslandAnimState.Reset();
		m_MediaIslandLastVisibleRectValid = false;
	}

#if defined(CONF_VIDEORECORDER)
	const bool MainHudVisible = (IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud);
#else
	const bool MainHudVisible = g_Config.m_ClShowhud != 0;
#endif
	const bool FocusSpectatorHudVisible = ShouldRenderFocusSpectatorHud(
		GameClient()->m_Snap.m_SpecInfo.m_Active,
		g_Config.m_ClShowhudSpectator != 0,
		MainHudVisible,
		g_Config.m_QmFocusMode != 0,
		g_Config.m_QmFocusModeHideHud != 0);
	const bool LocalCharacterHudVisible = GameClient()->m_Snap.m_pLocalCharacter &&
					      !GameClient()->m_Snap.m_SpecInfo.m_Active &&
					      !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER);
	if(MainHudVisible)
	{
		if(LocalCharacterHudVisible)
		{
			if(g_Config.m_ClShowhudHealthAmmo)
			{
				float HudMainHeight = 0.0f;
				if(GameClient()->m_GameInfo.m_HudHealthArmor)
					HudMainHeight = maximum(HudMainHeight, 24.0f);
				if(GameClient()->m_GameInfo.m_HudAmmo)
					HudMainHeight = maximum(HudMainHeight, GameClient()->m_GameInfo.m_HudHealthArmor ? 36.0f : 12.0f);
				const auto HudMainScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::HudMain, {5.0f, 5.0f, 144.0f, maximum(HudMainHeight, 12.0f)});
				RenderAmmoHealthAndArmor(GameClient()->m_Snap.m_pLocalCharacter);
				GameClient()->m_HudEditor.EndTransform(HudMainScope);
			}
			if(GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_HasExtendedData && g_Config.m_ClShowhudDDRace && GameClient()->m_GameInfo.m_HudDDRace)
			{
				const auto HudPlayerStateScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::HudPlayerState, {0.0f, 5.0f, 180.0f, 110.0f});
				RenderPlayerState(GameClient()->m_Snap.m_LocalClientId);
				GameClient()->m_HudEditor.EndTransform(HudPlayerStateScope);
			}
			if(!ShowMediaIsland && g_Config.m_QmHudIslandUseOriginalStyle)
			{
				RenderMovementInformation();
				RenderSpectatorCount();
			}
			else
			{
				if(!ShowMediaIsland)
					RenderSpectatorCount();
				RenderMovementInformation();
			}
			RenderJumpHint();
		}
		else if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
			if(SpectatorId != SPEC_FREEVIEW && g_Config.m_ClShowhudHealthAmmo)
			{
				float HudMainHeight = 0.0f;
				if(GameClient()->m_GameInfo.m_HudHealthArmor)
					HudMainHeight = maximum(HudMainHeight, 24.0f);
				if(GameClient()->m_GameInfo.m_HudAmmo)
					HudMainHeight = maximum(HudMainHeight, GameClient()->m_GameInfo.m_HudHealthArmor ? 36.0f : 12.0f);
				const auto HudMainScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::HudMain, {5.0f, 5.0f, 144.0f, maximum(HudMainHeight, 12.0f)});
				RenderAmmoHealthAndArmor(&GameClient()->m_Snap.m_aCharacters[SpectatorId].m_Cur);
				GameClient()->m_HudEditor.EndTransform(HudMainScope);
			}
			if(SpectatorId != SPEC_FREEVIEW &&
				GameClient()->m_Snap.m_aCharacters[SpectatorId].m_HasExtendedData &&
				g_Config.m_ClShowhudDDRace &&
				(!GameClient()->m_MultiViewActivated || GameClient()->m_MultiViewShowHud) &&
				GameClient()->m_GameInfo.m_HudDDRace)
			{
				const auto HudPlayerStateScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::HudPlayerState, {0.0f, 5.0f, 180.0f, 110.0f});
				RenderPlayerState(SpectatorId);
				GameClient()->m_HudEditor.EndTransform(HudPlayerStateScope);
			}
			RenderMovementInformation();
			RenderJumpHint();
			RenderSpectatorHud();
		}

		RenderMapProgressBar();
		if(g_Config.m_ClShowhudTimer && !ShowMediaIsland)
			RenderGameTimer();
		RenderSpeedrunTimer();
		RenderPauseNotification();
		RenderSuddenDeath();
		if(g_Config.m_ClShowhudScore)
			RenderScoreHud();
		RenderDummyActions();
		RenderWarmupTimer();
		RenderDummyMiniMap();
		RenderTextInfo();
		GameClient()->m_TClient.RenderCenterLines();
		RenderFollowSwitchCountdowns();
		if(ShowMediaIsland)
			RenderMediaIsland();
		else
		{
			RenderLocalTime((m_Width / 7) * 3);
			RenderLegacyMediaInfo();
		}
		if(LocalCharacterHudVisible)
			RenderDDRaceEffects();
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
			RenderConnectionWarning();
		RenderTeambalanceWarning();
		GameClient()->m_Voting.Render();
		if(g_Config.m_ClShowRecord)
			RenderRecord();
	}
	else if(FocusSpectatorHudVisible)
	{
		RenderSpectatorHud();
	}
	GameClient()->m_Voice.RenderOverlay();
	RenderCursor();
}

void CHud::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_DDRACETIME || MsgType == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		CNetMsg_Sv_DDRaceTime *pMsg = (CNetMsg_Sv_DDRaceTime *)pRawMsg;

		m_DDRaceTime = pMsg->m_Time;

		m_ShowFinishTime = pMsg->m_Finish != 0;

		if(!m_ShowFinishTime)
		{
			m_TimeCpDiff = (float)pMsg->m_Check / 100;
			m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
		else
		{
			m_FinishTimeDiff = (float)pMsg->m_Check / 100;
			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RECORD || MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_Record *pMsg = (CNetMsg_Sv_Record *)pRawMsg;

		// NETMSGTYPE_SV_RACETIME on old race servers
		if(MsgType == NETMSGTYPE_SV_RECORDLEGACY && GameClient()->m_GameInfo.m_DDRaceRecordMessage)
		{
			m_DDRaceTime = pMsg->m_ServerTimeBest; // First value: m_Time

			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);

			if(pMsg->m_PlayerTimeBest) // Second value: m_Check
			{
				m_TimeCpDiff = (float)pMsg->m_PlayerTimeBest / 100;
				m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
			}
		}
		else if(MsgType == NETMSGTYPE_SV_RECORD || GameClient()->m_GameInfo.m_RaceRecordMessage)
		{
			// ignore m_ServerTimeBest, it's handled by the game client
			m_aPlayerRecord[g_Config.m_ClDummy] = (float)pMsg->m_PlayerTimeBest / 100;
		}
	}
}

void CHud::RenderDDRaceEffects()
{
	if(m_DDRaceTime)
	{
		char aBuf[64];
		char aTime[32];
		if(m_ShowFinishTime && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			str_time(m_DDRaceTime, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "Finish time: %s", aTime);

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			TextRender()->TextColor(1, 1, 1, Alpha);
			const float EffectWidth = TextRender()->TextWidth(12, aBuf);
			const float EffectX = 150 * Graphics()->ScreenAspect() - EffectWidth / 2;
			const float EffectHeight = m_FinishTimeDiff != 0.0f ? 24.0f : 12.0f;
			const float EffectY = QmHudTopEffectY(20.0f, EffectHeight, EffectX, EffectX + EffectWidth, m_MediaIslandLastVisibleRect, m_MediaIslandLastVisibleRectValid);
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(EffectX, EffectY));
			Cursor.m_FontSize = 12.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);
			if(m_FinishTimeDiff != 0.0f && m_DDRaceEffectsTextContainerIndex.Valid())
			{
				if(m_FinishTimeDiff < 0)
				{
					str_time_float(-m_FinishTimeDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "-%s", aTime);
					TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
				}
				else
				{
					str_time_float(m_FinishTimeDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "+%s", aTime);
					TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
				}
				CTextCursor DiffCursor;
				DiffCursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf) / 2, EffectY + 14.0f));
				DiffCursor.m_FontSize = 10.0f;
				TextRender()->AppendTextContainer(m_DDRaceEffectsTextContainerIndex, &DiffCursor, aBuf);
			}
			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(g_Config.m_ClShowhudTimeCpDiff && !m_ShowFinishTime && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			if(m_TimeCpDiff < 0)
			{
				str_time_float(-m_TimeCpDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "-%s", aTime);
			}
			else
			{
				str_time_float(m_TimeCpDiff, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "+%s", aTime);
			}

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			if(m_TimeCpDiff > 0)
				TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
			else if(m_TimeCpDiff < 0)
				TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
			else if(!m_TimeCpDiff)
				TextRender()->TextColor(1, 1, 1, Alpha); // white

			const float EffectWidth = TextRender()->TextWidth(10, aBuf);
			const float EffectX = 150 * Graphics()->ScreenAspect() - EffectWidth / 2;
			const float EffectY = QmHudTopEffectY(20.0f, 10.0f, EffectX, EffectX + EffectWidth, m_MediaIslandLastVisibleRect, m_MediaIslandLastVisibleRectValid);
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(EffectX, EffectY));
			Cursor.m_FontSize = 10.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);

			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderRecord()
{
	if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET && GameClient()->m_MapBestTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
	{
		char aBuf[64];
		TextRender()->Text(5, 75, 6, Localize("Server best:"), -1.0f);
		char aTime[32];
		int64_t TimeCentiseconds = static_cast<int64_t>(GameClient()->m_MapBestTimeSeconds) * 100 + static_cast<int64_t>(GameClient()->m_MapBestTimeMillis) / 10;
		str_time(TimeCentiseconds, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", GameClient()->m_MapBestTimeSeconds > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 75, 6, aBuf, -1.0f);
	}

	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const int PlayerTimeSeconds = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_FinishTimeSeconds;
		if(PlayerTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
		{
			char aBuf[64];
			TextRender()->Text(5, 82, 6, Localize("Personal best:"), -1.0f);
			char aTime[32];
			const int PlayerTimeMillis = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_FinishTimeMillis;
			int64_t TimeCentiseconds = static_cast<int64_t>(PlayerTimeSeconds) * 100 + static_cast<int64_t>(PlayerTimeMillis) / 10;
			str_time(TimeCentiseconds, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "%s%s", PlayerTimeSeconds > 3600 ? "" : "   ", aTime);
			TextRender()->Text(53, 82, 6, aBuf, -1.0f);
		}
	}
	else
	{
		const float PlayerRecord = m_aPlayerRecord[g_Config.m_ClDummy];
		if(PlayerRecord > 0.0f)
		{
			char aBuf[64];
			TextRender()->Text(5, 82, 6, Localize("Personal best:"), -1.0f);
			char aTime[32];
			str_time_float(PlayerRecord, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "%s%s", PlayerRecord > 3600 ? "" : "   ", aTime);
			TextRender()->Text(53, 82, 6, aBuf, -1.0f);
		}
	}
}
