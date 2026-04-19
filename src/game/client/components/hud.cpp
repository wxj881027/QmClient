/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hud.h"

#include "binds.h"
#include "camera.h"
#include "controls.h"
#include "voting.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/QmUi/QmLayout.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
bool IsVulkanAmdBackend(IGraphics *pGraphics)
{
	if(str_comp_nocase(g_Config.m_GfxBackend, "Vulkan") != 0 || pGraphics == nullptr)
		return false;

	const char *pVendor = pGraphics->GetVendorString();
	const char *pRenderer = pGraphics->GetRendererString();

	const bool IsAmdVendor = pVendor != nullptr &&
		(str_find_nocase(pVendor, "AMD") != nullptr || str_find_nocase(pVendor, "ATI") != nullptr);
	const bool IsAmdRenderer = pRenderer != nullptr &&
		(str_find_nocase(pRenderer, "Radeon") != nullptr || str_find_nocase(pRenderer, "AMD") != nullptr);

	return IsAmdVendor || IsAmdRenderer;
}

struct SHudTextInfoLayout
{
	float m_FpsX = 0.0f;
	float m_FpsY = 5.0f;
	float m_PredX = 0.0f;
	float m_PredY = 5.0f;
};

SHudTextInfoLayout ComputeHudTextInfoLayoutV2(bool ShowFps, bool ShowPred, bool UseMiniLayout, float HudWidth, float MiniX, float MiniY, float MiniW, float MiniH, float FpsWidth, float PredWidth, std::vector<SUiLayoutChild> &vChildrenScratch)
{
	SHudTextInfoLayout Result;
	if(!ShowFps && !ShowPred)
		return Result;

	CUiV2LayoutEngine LayoutEngine;
	std::vector<SUiLayoutChild> &vChildren = vChildrenScratch;
	vChildren.clear();
	vChildren.reserve(2);

	if(ShowFps)
	{
		SUiLayoutChild Child;
		Child.m_Style.m_Width = SUiLength::Px(FpsWidth);
		Child.m_Style.m_Height = SUiLength::Px(12.0f);
		vChildren.push_back(Child);
	}
	if(ShowPred)
	{
		SUiLayoutChild Child;
		Child.m_Style.m_Width = SUiLength::Px(PredWidth);
		Child.m_Style.m_Height = SUiLength::Px(12.0f);
		vChildren.push_back(Child);
	}

	SUiStyle ContainerStyle;
	SUiLayoutBox ContainerBox;
	if(UseMiniLayout)
	{
		ContainerStyle.m_Axis = EUiAxis::ROW;
		ContainerStyle.m_Gap = (ShowFps && ShowPred) ? 6.0f : 0.0f;
		ContainerStyle.m_AlignItems = EUiAlign::START;
		ContainerStyle.m_JustifyContent = EUiAlign::START;

		const float TotalWidth = FpsWidth + PredWidth + ContainerStyle.m_Gap;
		ContainerBox.m_X = MiniX + MiniW - TotalWidth;
		ContainerBox.m_Y = MiniY + MiniH + 4.0f;
		ContainerBox.m_W = TotalWidth;
		ContainerBox.m_H = 12.0f;
	}
	else
	{
		ContainerStyle.m_Axis = EUiAxis::COLUMN;
		ContainerStyle.m_Gap = (ShowFps && ShowPred) ? 3.0f : 0.0f;
		ContainerStyle.m_AlignItems = EUiAlign::END;
		ContainerStyle.m_JustifyContent = EUiAlign::START;

		const float MaxWidth = maximum(FpsWidth, PredWidth);
		ContainerBox.m_X = HudWidth - 10.0f - MaxWidth;
		ContainerBox.m_Y = 5.0f;
		ContainerBox.m_W = MaxWidth;
		ContainerBox.m_H = (ShowFps && ShowPred) ? 27.0f : 12.0f;
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
	char m_aText[64] = {0};
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
			if(GameClient.m_aClients[i].m_FreezeEnd > 0 || GameClient.m_aClients[i].m_DeepFrozen)
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

bool ShouldShowHudFrozenSummaryInStatus(bool ShowFrozenSummary, bool TimerVisible, bool StatusExpanded)
{
	return ShowFrozenSummary && TimerVisible && !StatusExpanded;
}

bool ShouldShowHudFrozenSummaryInBottomRow(bool ShowFrozenSummary, bool TimerVisible)
{
	return ShowFrozenSummary && !TimerVisible;
}

SHudFrozenHudRect BuildFrozenHudRect(const CGameClient &GameClient, float HudWidth, float HudHeight, float TopIslandAvoidanceRight)
{
	SHudFrozenHudRect Result;
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(GameClient);
	if(!g_Config.m_TcShowFrozenHud || !FrozenInfo.m_Available || FrozenInfo.m_NumInTeam <= 0 || GameClient.m_Scoreboard.IsActive() || (FrozenInfo.m_LocalTeamId == 0 && g_Config.m_TcFrozenHudTeamOnly))
		return Result;

	const float TeeSize = g_Config.m_TcFrozenHudTeeSize;
	int MaxTees = (int)(8.3f * (HudWidth / HudHeight) * 13.0f / TeeSize);
	if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred)
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
	constexpr float BoxH = 16.0f;
	constexpr float PaddingX = 8.0f;

	Result.m_Visible = true;
	Result.m_IsCritical = TimerInfo.m_IsCritical;
	Result.m_Alpha = TimerInfo.m_Alpha;
	Result.m_FontSize = TimerInfo.m_FontSize;
	Result.m_BoxY = BoxY;
	Result.m_BoxH = BoxH;
	Result.m_BoxW = std::round(TimerInfo.m_W + PaddingX * 2.0f);
	Result.m_BoxX = std::round(TimerInfo.m_X + TimerInfo.m_W * 0.5f - Result.m_BoxW * 0.5f);
	Result.m_TextX = std::round(Result.m_BoxX + (Result.m_BoxW - TimerInfo.m_W) * 0.5f);
	Result.m_TextY = std::round(BoxY + (BoxH - TimerInfo.m_FontSize) * 0.5f - 0.5f);
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

void DrawTexturedCircle(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, vec2 Center, float Radius, float Rotation, float Alpha = 1.0f)
{
	if(pGraphics == nullptr || !Texture.IsValid() || Radius <= 0.0f)
		return;

	constexpr int NumSegments = 48;
	constexpr float Pi = 3.14159265359f;
	const float SegmentAngle = 2.0f * Pi / NumSegments;

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);

	for(int i = 0; i < NumSegments; i += 2)
	{
		const float A1 = i * SegmentAngle;
		const float A2 = (i + 1) * SegmentAngle;
		const float A3 = (i + 2) * SegmentAngle;

		const vec2 P1 = Center + RotatePoint(vec2(std::cos(A1), std::sin(A1)) * Radius, Rotation);
		const vec2 P2 = Center + RotatePoint(vec2(std::cos(A3), std::sin(A3)) * Radius, Rotation);
		const vec2 P3 = Center + RotatePoint(vec2(std::cos(A2), std::sin(A2)) * Radius, Rotation);

		pGraphics->QuadsSetSubsetFree(
			0.5f, 0.5f,
			0.5f + std::cos(A1) * 0.5f, 0.5f + std::sin(A1) * 0.5f,
			0.5f + std::cos(A3) * 0.5f, 0.5f + std::sin(A3) * 0.5f,
			0.5f + std::cos(A2) * 0.5f, 0.5f + std::sin(A2) * 0.5f);

		const IGraphics::CFreeformItem Item(
			Center.x, Center.y,
			P1.x, P1.y,
			P2.x, P2.y,
			P3.x, P3.y);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	}

	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
	pGraphics->TextureClear();
}

bool BuildSwapCountdownInfo(const CGameClient &GameClient, const IClient &Client, SSwapCountdownInfo &Out)
{
	if(!GameClient.m_TClient.HasSwapCountdown())
		return false;

	const int StartTick = GameClient.m_TClient.GetSwapCountdownStartTick();
	if(StartTick <= 0)
		return false;

	const int TickSpeed = Client.GameTickSpeed();
	const int CurTick = Client.GameTick(g_Config.m_ClDummy);
	const int ElapsedTicks = CurTick - StartTick;
	if(ElapsedTicks < 0 || TickSpeed <= 0)
		return false;

	static constexpr int SWAP_COUNTDOWN_SECONDS = 30;
	static constexpr int SWAP_READY_DISPLAY_SECONDS = 30;
	static constexpr int SWAP_HIDE_AFTER_SECONDS = SWAP_COUNTDOWN_SECONDS + SWAP_READY_DISPLAY_SECONDS;
	const int ElapsedSeconds = ElapsedTicks / TickSpeed;
	if(ElapsedSeconds >= SWAP_HIDE_AFTER_SECONDS)
		return false;

	const int SecondsLeft = SWAP_COUNTDOWN_SECONDS - ElapsedSeconds;
	if(SecondsLeft > 0)
	{
		str_format(Out.m_aText, sizeof(Out.m_aText), "Swap倒计时:%d秒", SecondsLeft);
		Out.m_TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		str_copy(Out.m_aText, "可交换!");
		Out.m_TextColor = ColorRGBA(0.5f, 1.0f, 0.5f, 1.0f);
	}

	return true;
}

float ResolveAnimatedLayoutValueEx(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, float &LastTarget, float DurationSec, float DelaySec, EEasing Easing)
{
	constexpr float Epsilon = 0.01f;
	const float Current = AnimRuntime.GetValue(NodeKey, Property, Target);
	const bool TargetChanged = std::abs(Target - LastTarget) > Epsilon;
	const bool NeedsSync = !AnimRuntime.HasActiveAnimation(NodeKey, Property) && std::abs(Target - Current) > Epsilon;

	if(TargetChanged || NeedsSync)
	{
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition.m_DurationSec = DurationSec;
		Request.m_Transition.m_DelaySec = DelaySec;
		Request.m_Transition.m_Priority = 1;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Request.m_Transition.m_Easing = Easing;
		AnimRuntime.RequestAnimation(Request);
		LastTarget = Target;
	}

	return AnimRuntime.GetValue(NodeKey, Property, Target);
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
	m_RecordingStatusAnimState.Reset();
	m_SwitchCountdownAnimState.Reset();
	m_SwitchCountdownTracker.Reset();
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
	m_RecordingStatusAnimState.Reset();
	m_SwitchCountdownAnimState.Reset();
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
	m_ServerRecord = -1.0f;
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

	constexpr float TimerRadius = 8.0f;
	constexpr float StatusSectionGap = 6.0f;
	constexpr float StatusPaddingLeft = 7.0f;
	constexpr float StatusPaddingRight = 8.0f;
	constexpr float StatusDotSize = 5.0f;
	constexpr float StatusDotGap = 5.0f;
	constexpr float StatusFontSize = 5.3f;

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
		AnimRuntime.SetValue(StatusBoxNode, EUiAnimProperty::WIDTH, TargetStatusWidth);
		AnimRuntime.SetValue(StatusBoxNode, EUiAnimProperty::ALPHA, TargetStatusAlpha);
		AnimRuntime.SetValue(StatusTextNode, EUiAnimProperty::ALPHA, TargetTextAlpha);
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

	DrawSmoothRoundedRect(Graphics(), TimerBoxX, TimerCapsule.m_BoxY, CombinedWidth, TimerCapsule.m_BoxH, TimerRadius, ColorRGBA(0.04f, 0.05f, 0.07f, 0.80f));
	if(TimerCapsule.m_IsCritical)
		TextRender()->TextColor(1.0f, 0.25f, 0.25f, TimerCapsule.m_Alpha);
	else
		TextRender()->TextColor(0.98f, 0.99f, 1.0f, 0.98f);
	TextRender()->Text(TimerTextX, TimerCapsule.m_TextY, TimerCapsule.m_FontSize, TimerCapsule.m_aText, -1.0f);

	if(RenderStatusSection)
	{
		const float DividerX = TimerBoxX + TimerCapsule.m_BoxW + StatusSectionGap * 0.5f;
		Graphics()->DrawRect(DividerX, TimerCapsule.m_BoxY + 4.0f, 0.75f, TimerCapsule.m_BoxH - 8.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * StatusAlpha), IGraphics::CORNER_ALL, 0.375f);

		const vec2 DotCenter(StatusSectionX + StatusPaddingLeft + StatusDotSize * 0.5f, TimerCapsule.m_BoxY + TimerCapsule.m_BoxH * 0.5f);
		DrawSmoothCircle(Graphics(), DotCenter, StatusDotSize * 0.5f, ColorRGBA(1.0f, 0.15f, 0.15f, 0.95f * StatusAlpha));

		if(StatusTextAlpha > 0.001f && StatusWidth > RawCollapsedWidth + 2.0f)
		{
			const float TextX = StatusSectionX + StatusPaddingLeft + StatusDotSize + StatusDotGap;
			const float TextY = TimerCapsule.m_BoxY + (TimerCapsule.m_BoxH - StatusFontSize) * 0.5f - 0.5f;
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
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(m_Width - ScoreWidthMax - ImageSize - 2 * Split, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split, ScoreSingleBoxHeight, 5.0f, IGraphics::CORNER_L);
				}
				Graphics()->TextureClear();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);

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
						if(apPlayerInfo[t]->m_Score != -9999)
							str_time((int64_t)absolute(apPlayerInfo[t]->m_Score) * 100, TIME_HOURS, aScore[t], sizeof(aScore[t]));
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
					m_aScoreInfo[t].m_RoundRectQuadContainerIndex = Graphics()->CreateRectQuadContainer(m_Width - ScoreWidthMax - ImageSize - 2 * Split - PosSize, StartY + t * 20, ScoreWidthMax + ImageSize + 2 * Split + PosSize, ScoreSingleBoxHeight, 5.0f, IGraphics::CORNER_L);
				}
				Graphics()->TextureClear();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				if(m_aScoreInfo[t].m_RoundRectQuadContainerIndex != -1)
					Graphics()->RenderQuadContainer(m_aScoreInfo[t].m_RoundRectQuadContainerIndex, -1);

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
	w = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);
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
		str_copy(State.m_aPlaceholderSubtitle, Localize("Connect dummy to activate"), sizeof(State.m_aPlaceholderSubtitle));
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
	if(!g_Config.m_ClDummyMiniViewAuto || pGraphics == nullptr)
		return true;
	if(MiniViewClientId < 0 || MiniViewClientId >= MAX_CLIENTS)
		return true;

	vec2 TargetPos;
	if(!TryGetDummyMiniViewTargetPos(GameClient, *GameClient.Client(), MiniViewClientId, TargetPos))
		return true;

	float ViewWidth = 0.0f;
	float ViewHeight = 0.0f;
	pGraphics->CalcScreenParams(pGraphics->ScreenAspect(), GameClient.m_Camera.m_Zoom, &ViewWidth, &ViewHeight);

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
	if(!g_Config.m_ClDummyMiniView)
		return false;
	if(GameClient()->m_HudEditor.IsActive())
	{
		const float SizeScale = g_Config.m_ClDummyMiniViewSize / 100.0f;
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
	if(IsVulkanAmdBackend(Graphics()))
	{
		static bool s_LoggedDisableReason = false;
		if(!s_LoggedDisableReason)
		{
			dbg_msg("hud", "dummy mini view disabled on Vulkan + AMD due known driver crash");
			s_LoggedDisableReason = true;
		}
		return false;
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

	const float SizeScale = g_Config.m_ClDummyMiniViewSize / 100.0f;
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
		return;
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
	DrawSmoothRoundedRect(Graphics(), MiniX + 0.8f, MiniY + 1.2f, MiniW, MiniH, Radius, ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f));
	DrawSmoothRoundedRect(Graphics(), MiniX, MiniY, MiniW, MiniH, Radius, ColorRGBA(0.02f, 0.03f, 0.05f, 0.92f));
	DrawSmoothRoundedRect(Graphics(), MiniX + 0.75f, MiniY + 0.75f, maximum(0.0f, MiniW - 1.5f), maximum(0.0f, MiniH - 1.5f), maximum(0.0f, Radius - 0.55f), ViewState.m_TargetAccent.WithAlpha(0.16f));

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
			Graphics()->FlushVertices();
			Graphics()->ClipDisable();
			Graphics()->UpdateViewport(ClampedX, ClampedY, ClampedW, ClampedH, false);

			const CGameClient::CClientData &MiniClient = GameClient()->m_aClients[MiniViewClientId];
			vec2 MiniPos(0.0f, 0.0f);
			bool HasSnapshotSignal = false;
			if(!TryGetDummyMiniViewTargetPos(*GameClient(), *Client(), MiniViewClientId, MiniPos, &HasSnapshotSignal))
			{
				Graphics()->FlushVertices();
				Graphics()->ClipDisable();
				Graphics()->UpdateViewport(0, 0, ScreenW, ScreenH, false);
				Graphics()->MapScreen(SavedX0, SavedY0, SavedX1, SavedY1);
				GameClient()->m_HudEditor.EndTransform(HudEditorScope);
				return;
			}

			const float ZoomScale = maximum(0.1f, g_Config.m_ClDummyMiniViewZoom / 100.0f);
			const float MiniZoom = GameClient()->m_Camera.m_Zoom * ZoomScale;

			bool RenderedBackground = false;
			if(g_Config.m_ClOverlayEntities == 100)
				RenderedBackground = GameClient()->m_Background.RenderCustom(MiniPos, MiniZoom);
			if(!RenderedBackground)
				GameClient()->m_MapLayersBackground.RenderCustom(MiniPos, MiniZoom);

			float aPoints[4];
			Graphics()->MapScreenToWorld(MiniPos.x, MiniPos.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), MiniZoom, aPoints);
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
			Graphics()->UpdateViewport(0, 0, ScreenW, ScreenH, false);
			Graphics()->MapScreen(SavedX0, SavedY0, SavedX1, SavedY1);
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
		AnimState.m_AlphaInitialized = false;
	}

	const uint64_t FpsNode = HudTextInfoNodeKey("fps");
	const uint64_t PredNode = HudTextInfoNodeKey("pred");
	if(UseV2TextInfoLayout && pAnimRuntime != nullptr && !AnimState.m_AlphaInitialized)
	{
		AnimState.m_FpsTargetAlpha = Showfps ? 1.0f : 0.0f;
		AnimState.m_PredTargetAlpha = Showpred ? 1.0f : 0.0f;
		pAnimRuntime->SetValue(FpsNode, EUiAnimProperty::ALPHA, AnimState.m_FpsTargetAlpha);
		pAnimRuntime->SetValue(PredNode, EUiAnimProperty::ALPHA, AnimState.m_PredTargetAlpha);
		AnimState.m_AlphaInitialized = true;
	}

	char aFpsBuf[16] = {0};
	char aPredBuf[64] = {0};
	float FpsWidth = 0.0f;
	float PredWidth = 0.0f;
	int DigitIndex = 0;
	if(Showfps)
	{
		const int FramesPerSecond = round_to_int(1.0f / Client()->FrameTimeAverage());
		str_format(aFpsBuf, sizeof(aFpsBuf), "%d", FramesPerSecond);

		static float s_TextWidth0 = TextRender()->TextWidth(12.f, "0", -1, -1.0f);
		static float s_TextWidth00 = TextRender()->TextWidth(12.f, "00", -1, -1.0f);
		static float s_TextWidth000 = TextRender()->TextWidth(12.f, "000", -1, -1.0f);
		static float s_TextWidth0000 = TextRender()->TextWidth(12.f, "0000", -1, -1.0f);
		static float s_TextWidth00000 = TextRender()->TextWidth(12.f, "00000", -1, -1.0f);
		static const float s_aTextWidth[5] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000};

		DigitIndex = GetDigitsIndex(FramesPerSecond, 4);
		FpsWidth = s_aTextWidth[DigitIndex];
		str_copy(AnimState.m_aLastFpsText, aFpsBuf);
		AnimState.m_LastFpsWidth = FpsWidth;
	}
	if(Showpred)
	{
		str_format(aPredBuf, sizeof(aPredBuf), "%d", Client()->GetPredictionTime());
		PredWidth = TextRender()->TextWidth(12.0f, aPredBuf, -1, -1.0f);
		str_copy(AnimState.m_aLastPredText, aPredBuf);
		AnimState.m_LastPredWidth = PredWidth;
	}

	float FpsAlpha = Showfps ? 1.0f : 0.0f;
	float PredAlpha = Showpred ? 1.0f : 0.0f;
	if(UseV2TextInfoLayout && pAnimRuntime != nullptr)
	{
		FpsAlpha = ResolveAnimatedLayoutValue(*pAnimRuntime, FpsNode, EUiAnimProperty::ALPHA, Showfps ? 1.0f : 0.0f, AnimState.m_FpsTargetAlpha);
		PredAlpha = ResolveAnimatedLayoutValue(*pAnimRuntime, PredNode, EUiAnimProperty::ALPHA, Showpred ? 1.0f : 0.0f, AnimState.m_PredTargetAlpha);
	}

	const bool RenderFps = Showfps || (UseV2TextInfoLayout && FpsAlpha > 0.01f && AnimState.m_aLastFpsText[0] != '\0');
	const bool RenderPred = Showpred || (UseV2TextInfoLayout && PredAlpha > 0.01f && AnimState.m_aLastPredText[0] != '\0');
	const float DisplayFpsWidth = Showfps ? FpsWidth : (RenderFps ? AnimState.m_LastFpsWidth : 0.0f);
	const float DisplayPredWidth = Showpred ? PredWidth : (RenderPred ? AnimState.m_LastPredWidth : 0.0f);
	const char *pFpsText = Showfps ? aFpsBuf : AnimState.m_aLastFpsText;
	const char *pPredText = Showpred ? aPredBuf : AnimState.m_aLastPredText;
	const bool UseMiniLayout = HasMiniMap && (RenderFps || RenderPred);

	SHudTextInfoLayout V2Layout;
	if(UseV2TextInfoLayout)
	{
		V2Layout = ComputeHudTextInfoLayoutV2(RenderFps, RenderPred, UseMiniLayout, m_Width, MiniX, MiniY, MiniW, MiniH, DisplayFpsWidth, DisplayPredWidth, m_vTextInfoLayoutChildrenScratch);
	}

	if(UseV2TextInfoLayout && pAnimRuntime != nullptr)
	{
		if(RenderFps && !AnimState.m_FpsPositionInitialized)
		{
			AnimState.m_FpsTargetX = V2Layout.m_FpsX;
			AnimState.m_FpsTargetY = V2Layout.m_FpsY;
			pAnimRuntime->SetValue(FpsNode, EUiAnimProperty::POS_X, AnimState.m_FpsTargetX);
			pAnimRuntime->SetValue(FpsNode, EUiAnimProperty::POS_Y, AnimState.m_FpsTargetY);
			AnimState.m_FpsPositionInitialized = true;
		}
		if(RenderPred && !AnimState.m_PredPositionInitialized)
		{
			AnimState.m_PredTargetX = V2Layout.m_PredX;
			AnimState.m_PredTargetY = V2Layout.m_PredY;
			pAnimRuntime->SetValue(PredNode, EUiAnimProperty::POS_X, AnimState.m_PredTargetX);
			pAnimRuntime->SetValue(PredNode, EUiAnimProperty::POS_Y, AnimState.m_PredTargetY);
			AnimState.m_PredPositionInitialized = true;
		}
	}

	float StartX = 0.0f;
	float TextY = 5.0f;
	float Gap = 0.0f;
	if(UseMiniLayout && !UseV2TextInfoLayout)
	{
		Gap = (RenderFps && RenderPred) ? 6.0f : 0.0f;
		const float TotalWidth = DisplayFpsWidth + DisplayPredWidth + Gap;
		StartX = MiniX + MiniW - TotalWidth;
		TextY = MiniY + MiniH + 4.0f;
	}
	CHudEditor::STransformScope TextInfoScope;
	if(RenderFps || RenderPred)
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
		float PredRectY = RenderFps ? 20.0f : 5.0f;
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

		if(RenderFps)
			ExtendBounds(FpsRectX, FpsRectY, DisplayFpsWidth, 12.0f);
		if(RenderPred)
			ExtendBounds(PredRectX, PredRectY, DisplayPredWidth, 12.0f);
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
		Cursor.m_FontSize = 12.0f;
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
		float PredY = RenderFps ? 20.0f : 5.0f;
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
			ColorRGBA PredTextColor = TextRender()->DefaultTextColor();
			ColorRGBA PredOutlineColor = TextRender()->DefaultTextOutlineColor();
			PredTextColor.a *= PredAlpha;
			PredOutlineColor.a *= PredAlpha;
			TextRender()->TextColor(PredTextColor);
			TextRender()->TextOutlineColor(PredOutlineColor);
			TextRender()->Text(PredX, PredY, 12.0f, pPredText, -1.0f);
			TextRender()->TextColor(OldColor);
			TextRender()->TextOutlineColor(OldOutlineColor);
		}
		else
		{
			TextRender()->Text(PredX, PredY, 12.0f, pPredText, -1.0f);
		}
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
			if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred)
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
						if(GameClient()->m_aClients[i].m_FreezeEnd > 0 || GameClient()->m_aClients[i].m_DeepFrozen)
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
	SSwapCountdownInfo Info;
	if(!BuildSwapCountdownInfo(*GameClient(), *Client(), Info))
		return;

	const float FontSize = 8.0f;
	const float X = 5.0f;
	const float Y = m_Height - 12.0f;

	TextRender()->TextColor(Info.m_TextColor);
	TextRender()->Text(X, Y, FontSize, Info.m_aText, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CHud::UpdateSwitchCountdownTracker()
{
	const int TickSpeed = Client()->GameTickSpeed();
	if(TickSpeed <= 0 || Collision() == nullptr)
		return;

	const int CurTick = Client()->GameTick(g_Config.m_ClDummy);
	const auto UpdateSwitchCountdownFromClient = [&](int ClientId, bool UsePredictedPos) {
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return;

		vec2 Pos;
		if(UsePredictedPos)
		{
			if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
				return;
			Pos = GameClient()->m_aClients[ClientId].m_Predicted.m_Pos;
		}
		else
		{
			if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
				return;
			Pos = vec2(GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);
		}

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

		const int Delay = Collision()->GetSwitchDelay(MapIndex);
		const int EndTick = CurTick + 1 + Delay * TickSpeed;
		m_SwitchCountdownTracker.m_aaEndTick[Team][SwitchNumber] = EndTick;
		m_SwitchCountdownTracker.m_aaTouchTick[Team][SwitchNumber] = CurTick;
	};

	const int LocalId = GameClient()->m_aLocalIds[0];
	const int DummyId = GameClient()->m_aLocalIds[1];
	UpdateSwitchCountdownFromClient(LocalId, GameClient()->Predict());
	if(Client()->DummyConnected())
		UpdateSwitchCountdownFromClient(DummyId, GameClient()->PredictDummy());

	const int Team = GameClient()->SwitchStateTeam();
	if(Team < 0 || Team >= NUM_DDRACE_TEAMS)
		return;

	for(int i = 1; i < 256; ++i)
	{
		if(m_SwitchCountdownTracker.m_aaEndTick[Team][i] > CurTick)
			continue;

		m_SwitchCountdownTracker.m_aaEndTick[Team][i] = 0;
		m_SwitchCountdownTracker.m_aaTouchTick[Team][i] = 0;
	}
}

bool CHud::HasActiveSwitchCountdown() const
{
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
	SSwapCountdownInfo SwapInfo;
	if(BuildSwapCountdownInfo(*GameClient(), *Client(), SwapInfo))
	{
		const float SwapWidth = TextRender()->TextWidth(FontSize, SwapInfo.m_aText, -1, -1.0f);
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
				pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::POS_X, TargetX);
				pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::POS_Y, TargetY);
				AnimState.m_aTargetX[i] = TargetX;
				AnimState.m_aTargetY[i] = TargetY;
				AnimState.m_aPositionInitialized[i] = true;
			}
			if(!AnimState.m_aAlphaInitialized[i])
			{
				pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::ALPHA, TargetAlpha);
				AnimState.m_aTargetAlpha[i] = TargetAlpha;
				AnimState.m_aAlphaInitialized[i] = true;
			}

			if(IsNewEntry)
			{
				pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::POS_X, TargetX);
				pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::POS_Y, BaseY - SlideOffsetY);
				pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
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
					pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::POS_X, TargetX - SlideOffsetX);
					pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
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
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Render local cursor
		CurWeapon = maximum(0, GameClient()->m_Snap.m_pLocalCharacter->m_Weapon % NUM_WEAPONS);
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
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_HealthOffset + QuadOffsetSixup, minimum(pCharacter->m_Health, 10));
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_EmptyHealthOffset + QuadOffsetSixup + minimum(pCharacter->m_Health, 10), 10 - minimum(pCharacter->m_Health, 10));

		// armor display
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup, minimum(pCharacter->m_Armor, 10));
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup + minimum(pCharacter->m_Armor, 10), 10 - minimum(pCharacter->m_Armor, 10));
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

bool CHud::HasVisibleMediaIsland() const
{
	if(g_Config.m_QmHudIslandUseOriginalStyle)
		return false;

	SSwapCountdownInfo SwapInfo;
	if(BuildSwapCountdownInfo(*GameClient(), *Client(), SwapInfo))
		return true;

	if(HasActiveSwitchCountdown())
		return true;

	if(ShouldRenderHudLocalTime(*GameClient()))
		return true;

	if(g_Config.m_ClShowhudTimer)
	{
		const SHudGameTimerInfo TimerInfo = BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width);
		if(TimerInfo.m_Visible)
			return true;
	}

	if(!(g_Config.m_ClSmtcEnable && g_Config.m_ClSmtcShowHud))
		return false;

	CSystemMediaControls::SState MediaState;
	return GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState);
}

float CHud::GetTopIslandAvoidanceRight() const
{
	if(g_Config.m_QmHudIslandUseOriginalStyle)
		return 0.0f;

	const bool ShowLocalTime = ShouldRenderHudLocalTime(*GameClient());
	const SHudGameTimerInfo TimerInfo = g_Config.m_ClShowhudTimer ? BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width) : SHudGameTimerInfo{};
	const SHudTopTimerCapsuleInfo TimerCapsule = BuildHudTopTimerCapsuleInfo(TimerInfo);
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
	char aFrozenSummaryBuf[64];
	const bool ShowFrozenSummary = BuildHudFrozenSummaryText(FrozenInfo, aFrozenSummaryBuf, sizeof(aFrozenSummaryBuf));

	char aRecordingBuf[512];
	const bool ShowRecordingStatus = TimerCapsule.m_Visible && BuildHudRecordingStatusText(*GameClient(), aRecordingBuf, sizeof(aRecordingBuf));
	const bool ScoreboardExpanded = GameClient()->m_Scoreboard.IsActive();
	const bool ShowFrozenSummaryInStatus = ShouldShowHudFrozenSummaryInStatus(ShowFrozenSummary, TimerCapsule.m_Visible, ScoreboardExpanded);
	const int SpectatorCount = GetMediaIslandSpectatorCount(*GameClient(), *Client());
	const bool ShowSpectator = SpectatorCount > 0;

	CSystemMediaControls::SState MediaState;
	const bool MediaHudEnabled = g_Config.m_ClSmtcEnable && g_Config.m_ClSmtcShowHud;
	const bool HasMediaState = MediaHudEnabled && GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState);
	const bool ShowTopRow = HasMediaState || ShowLocalTime || TimerCapsule.m_Visible || ShowRecordingStatus || ShowSpectator;
	if(!ShowTopRow)
		return 0.0f;

	constexpr float CoverSize = 12.0f;
	constexpr float PaddingX = 3.0f;
	constexpr float Gap = 3.0f;
	constexpr float SpectatorGap = 1.5f;
	constexpr float TitleFontSize = 5.8f;
	constexpr float MetaFontSize = 5.3f;
	constexpr float ScreenPadding = 5.0f;
	constexpr float GapToTimer = 6.0f;
	constexpr float TimerToStatusGap = 6.0f;
	constexpr float StatusPaddingLeft = 7.0f;
	constexpr float StatusPaddingRight = 8.0f;
	constexpr float StatusDotSize = 5.0f;
	constexpr float StatusDotGap = 5.0f;
	constexpr float StatusFontSize = 5.3f;

	char aSpectatorBuf[16];
	str_format(aSpectatorBuf, sizeof(aSpectatorBuf), "%d", SpectatorCount);

	char aTimeBuf[16];
	str_timestamp_format(aTimeBuf, sizeof(aTimeBuf), "%H:%M");
	if(aTimeBuf[0] == '\0')
		str_copy(aTimeBuf, "00:00", sizeof(aTimeBuf));

	char aTimeSlotBuf[32];
	const int Checkpoint = GetDisplayedCheckpoint(*GameClient());
	if(Checkpoint > 0)
		str_format(aTimeSlotBuf, sizeof(aTimeSlotBuf), "00:00 CP%d", Checkpoint);
	else
		str_copy(aTimeSlotBuf, "00:00", sizeof(aTimeSlotBuf));

	const float TimeSlotWidth = std::round(TextRender()->TextBoundingBox(MetaFontSize, aTimeSlotBuf).m_W);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	const float SpectatorIconWidth = TextRender()->TextWidth(MetaFontSize, FontIcons::FONT_ICON_EYE);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	const float SpectatorTextWidth = ShowSpectator ? TextRender()->TextWidth(MetaFontSize, aSpectatorBuf) : 0.0f;
	const float SpectatorWidth = ShowSpectator ? SpectatorIconWidth + SpectatorGap + SpectatorTextWidth : 0.0f;
	const float StatusTextWidth = ShowRecordingStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aRecordingBuf).m_W) : 0.0f;
	const float RawCollapsedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusPaddingRight;
	const float RawExpandedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusDotGap + StatusTextWidth + StatusPaddingRight;
	const float FrozenSummaryTextWidth = ShowFrozenSummaryInStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aFrozenSummaryBuf).m_W) : 0.0f;
	const float FrozenSummaryStatusWidth = ShowFrozenSummaryInStatus ? (StatusPaddingLeft + FrozenSummaryTextWidth + StatusPaddingRight) : 0.0f;
	const bool ShowCover = HasMediaState;

	float BaseWidth = PaddingX;
	if(ShowCover)
		BaseWidth += CoverSize;
	if(ShowCover && (ShowSpectator || ShowLocalTime))
		BaseWidth += Gap;
	if(ShowSpectator)
		BaseWidth += SpectatorWidth;
	if(ShowSpectator && ShowLocalTime)
		BaseWidth += Gap;
	if(ShowLocalTime)
		BaseWidth += TimeSlotWidth;
	BaseWidth += PaddingX;
	if(!ShowCover && !ShowSpectator && !ShowLocalTime)
		BaseWidth = 0.0f;

	const bool Expanded = HasMediaState && m_MediaIslandAnimState.m_VisualState == SHudMediaIslandAnimState::EVisualState::EXPANDED;
	const float MaxTitleWidth = std::clamp(m_Width * 0.18f, 42.0f, 88.0f);
	const float RightSlotWidth = ShowFrozenSummaryInStatus ? FrozenSummaryStatusWidth :
		(ShowRecordingStatus ? (ScoreboardExpanded ? RawExpandedStatusWidth : RawCollapsedStatusWidth) : 0.0f);
	const float RightSlotGap = TimerCapsule.m_Visible && RightSlotWidth > 0.0f ? TimerToStatusGap : 0.0f;
	const float MaxIslandWidth = TimerCapsule.m_Visible ?
		std::max(BaseWidth, TimerCapsule.m_BoxX - GapToTimer - RightSlotGap - RightSlotWidth - ScreenPadding) :
		BaseWidth + (ShowCover ? (Gap + MaxTitleWidth) : 0.0f);
	const float MaxExpandedTitleWidth = std::max(0.0f, MaxIslandWidth - BaseWidth - Gap);
	const char *pDisplayTitle = HasMediaState && MediaState.m_aTitle[0] != '\0' ? MediaState.m_aTitle : "";
	const float NaturalTitleWidth = std::round(TextRender()->TextBoundingBox(TitleFontSize, pDisplayTitle).m_W);
	const float TitleWidth = (Expanded && ShowCover) ? std::clamp(NaturalTitleWidth, 0.0f, std::min(MaxTitleWidth, MaxExpandedTitleWidth)) : 0.0f;

	float TargetWidth = BaseWidth;
	if(Expanded && TitleWidth > 0.0f)
		TargetWidth += Gap + TitleWidth;

	float TargetX = m_Width * 0.5f - TargetWidth * 0.5f;
	if(TimerCapsule.m_Visible)
		TargetX = TargetWidth > 0.0f ? std::max(ScreenPadding, TimerCapsule.m_BoxX - GapToTimer - TargetWidth) : TimerCapsule.m_BoxX;
	else
	{
		const float MaxTargetX = std::max(ScreenPadding, m_Width - ScreenPadding - TargetWidth);
		TargetX = std::clamp(TargetX, ScreenPadding, MaxTargetX);
	}

	const float UnifiedRight = TimerCapsule.m_Visible ?
		(TimerCapsule.m_BoxX + TimerCapsule.m_BoxW + (RightSlotWidth > 0.0f ? (RightSlotGap + RightSlotWidth) : 0.0f)) :
		(TargetX + TargetWidth);
	return UnifiedRight;
}

void CHud::RenderMediaIsland()
{
	CSystemMediaControls::SState MediaState;
	const bool MediaHudEnabled = g_Config.m_ClSmtcEnable && g_Config.m_ClSmtcShowHud;
	const bool HasMediaState = MediaHudEnabled && GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState);

	SSwapCountdownInfo SwapInfo;
	const bool ShowSwapCountdown = BuildSwapCountdownInfo(*GameClient(), *Client(), SwapInfo);

	char aSwitchCountdownBuf[256];
	const bool ShowSwitchCountdown = BuildSwitchCountdownSummary(aSwitchCountdownBuf, sizeof(aSwitchCountdownBuf));
	const SHudFrozenTeamInfo FrozenInfo = BuildHudFrozenTeamInfo(*GameClient());
	char aFrozenSummaryBuf[64];
	const bool ShowFrozenSummary = BuildHudFrozenSummaryText(FrozenInfo, aFrozenSummaryBuf, sizeof(aFrozenSummaryBuf));

	const bool ShowLocalTime = ShouldRenderHudLocalTime(*GameClient());
	const SHudGameTimerInfo TimerInfo = g_Config.m_ClShowhudTimer ? BuildHudGameTimerInfo(*GameClient(), *Client(), TextRender(), m_Width) : SHudGameTimerInfo{};
	const SHudTopTimerCapsuleInfo TimerCapsule = BuildHudTopTimerCapsuleInfo(TimerInfo);
	char aRecordingBuf[512];
	const bool ShowRecordingStatus = TimerCapsule.m_Visible && BuildHudRecordingStatusText(*GameClient(), aRecordingBuf, sizeof(aRecordingBuf));
	const bool ScoreboardExpanded = GameClient()->m_Scoreboard.IsActive();
	const bool ShowFrozenSummaryInStatus = ShouldShowHudFrozenSummaryInStatus(ShowFrozenSummary, TimerCapsule.m_Visible, ScoreboardExpanded);
	const bool ShowFrozenSummaryInBottomRow = ShouldShowHudFrozenSummaryInBottomRow(ShowFrozenSummary, TimerCapsule.m_Visible);
	const bool ShowBottomRow = ShowSwapCountdown || ShowSwitchCountdown || ShowFrozenSummaryInBottomRow;
	const int SpectatorCount = GetMediaIslandSpectatorCount(*GameClient(), *Client());
	const bool ShowSpectator = SpectatorCount > 0;
	const bool ShowTopRow = HasMediaState || ShowLocalTime || TimerCapsule.m_Visible || ShowRecordingStatus || ShowSpectator;

	if(!ShowTopRow && !ShowBottomRow)
	{
		m_MediaIslandAnimState.Reset();
		return;
	}

	auto &AnimState = m_MediaIslandAnimState;
	const char *pDisplayTitle = HasMediaState && MediaState.m_aTitle[0] != '\0' ? MediaState.m_aTitle : "";
	const int64_t Now = time_get();
	const int64_t AutoCollapseTicks = std::max<int64_t>(1, (int64_t)3000 * time_freq() / 1000);

	if(HasMediaState)
	{
		const bool TitleChanged = str_comp(AnimState.m_aLastTrackTitle, MediaState.m_aTitle) != 0;
		const bool ArtistChanged = str_comp(AnimState.m_aLastTrackArtist, MediaState.m_aArtist) != 0;
		const bool AlbumChanged = str_comp(AnimState.m_aLastTrackAlbum, MediaState.m_aAlbum) != 0;
		const bool HadMeaningfulIdentity = AnimState.m_aLastTrackTitle[0] != '\0';
		const bool HasStableArtistIdentity = AnimState.m_aLastTrackArtist[0] != '\0' && MediaState.m_aArtist[0] != '\0';
		const bool HasStableAlbumIdentity = AnimState.m_aLastTrackAlbum[0] != '\0' && MediaState.m_aAlbum[0] != '\0';
		const bool TrackChanged = AnimState.m_HasTrackIdentity && HadMeaningfulIdentity &&
			(TitleChanged || (HasStableArtistIdentity && ArtistChanged) || (HasStableAlbumIdentity && AlbumChanged));

		if(!AnimState.m_HasTrackIdentity)
		{
			str_copy(AnimState.m_aLastTrackTitle, MediaState.m_aTitle, sizeof(AnimState.m_aLastTrackTitle));
			str_copy(AnimState.m_aLastTrackArtist, MediaState.m_aArtist, sizeof(AnimState.m_aLastTrackArtist));
			str_copy(AnimState.m_aLastTrackAlbum, MediaState.m_aAlbum, sizeof(AnimState.m_aLastTrackAlbum));
			AnimState.m_LastTrackDurationMs = MediaState.m_DurationMs;
			AnimState.m_HasTrackIdentity = true;
		}
		else if(TrackChanged)
		{
			str_copy(AnimState.m_aLastTrackTitle, MediaState.m_aTitle, sizeof(AnimState.m_aLastTrackTitle));
			str_copy(AnimState.m_aLastTrackArtist, MediaState.m_aArtist, sizeof(AnimState.m_aLastTrackArtist));
			str_copy(AnimState.m_aLastTrackAlbum, MediaState.m_aAlbum, sizeof(AnimState.m_aLastTrackAlbum));
			AnimState.m_LastTrackDurationMs = MediaState.m_DurationMs;
			AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::EXPANDED;
			AnimState.m_ExpandUntilTick = Now + AutoCollapseTicks;
		}
	}

	if(AnimState.m_VisualState == SHudMediaIslandAnimState::EVisualState::EXPANDED && Now >= AnimState.m_ExpandUntilTick)
		AnimState.m_VisualState = SHudMediaIslandAnimState::EVisualState::MINIMIZED;
	const bool Expanded = HasMediaState && AnimState.m_VisualState == SHudMediaIslandAnimState::EVisualState::EXPANDED;

	char aSpectatorBuf[16];
	str_format(aSpectatorBuf, sizeof(aSpectatorBuf), "%d", SpectatorCount);

	char aTimeBuf[16];
	str_timestamp_format(aTimeBuf, sizeof(aTimeBuf), "%H:%M");
	if(aTimeBuf[0] == '\0')
		str_copy(aTimeBuf, "00:00", sizeof(aTimeBuf));

	char aTimeDisplayBuf[32];
	char aTimeSlotBuf[32];
	const int Checkpoint = GetDisplayedCheckpoint(*GameClient());
	if(Checkpoint > 0)
	{
		str_format(aTimeDisplayBuf, sizeof(aTimeDisplayBuf), "%s CP%d", aTimeBuf, Checkpoint);
		str_format(aTimeSlotBuf, sizeof(aTimeSlotBuf), "00:00 CP%d", Checkpoint);
	}
	else
	{
		str_copy(aTimeDisplayBuf, aTimeBuf, sizeof(aTimeDisplayBuf));
		str_copy(aTimeSlotBuf, "00:00", sizeof(aTimeSlotBuf));
	}

	constexpr float BaseIslandHeight = 16.0f;
	constexpr float BottomRowExpandedHeight = 10.0f;
	constexpr float IslandY = 1.0f;
	constexpr float CoverSize = 12.0f;
	constexpr float PaddingX = 3.0f;
	constexpr float Gap = 3.0f;
	constexpr float SpectatorGap = 1.5f;
	constexpr float TitleFontSize = 5.8f;
	constexpr float MetaFontSize = 5.3f;
	constexpr float ScreenPadding = 5.0f;
	constexpr float GapToTimer = 6.0f;
	constexpr float TimerToStatusGap = 6.0f;
	constexpr float StatusPaddingLeft = 7.0f;
	constexpr float StatusPaddingRight = 8.0f;
	constexpr float StatusDotSize = 5.0f;
	constexpr float StatusDotGap = 5.0f;
	constexpr float StatusFontSize = 5.3f;
	constexpr float BottomFontSize = 5.2f;
	constexpr float BottomRowPaddingX = 10.0f;
	constexpr float BottomRowItemGap = 8.0f;
	constexpr float BottomRowDividerInset = 10.0f;
	constexpr float CoverRotationSpeed = 0.75f;
	constexpr float Tau = 6.28318530718f;

	const float TimeSlotWidth = std::round(TextRender()->TextBoundingBox(MetaFontSize, aTimeSlotBuf).m_W);
	const float TimeTextWidth = std::round(TextRender()->TextBoundingBox(MetaFontSize, aTimeDisplayBuf).m_W);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	const float SpectatorIconWidth = TextRender()->TextWidth(MetaFontSize, FontIcons::FONT_ICON_EYE);
	const float PlaceholderWidth = TextRender()->TextWidth(MetaFontSize, FontIcons::FONT_ICON_MUSIC);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	const float SpectatorTextWidth = ShowSpectator ? TextRender()->TextWidth(MetaFontSize, aSpectatorBuf) : 0.0f;
	const float SpectatorWidth = ShowSpectator ? SpectatorIconWidth + SpectatorGap + SpectatorTextWidth : 0.0f;
	const float StatusTextWidth = ShowRecordingStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aRecordingBuf).m_W) : 0.0f;
	const float RawCollapsedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusPaddingRight;
	const float RawExpandedStatusWidth = StatusPaddingLeft + StatusDotSize + StatusDotGap + StatusTextWidth + StatusPaddingRight;
	const float FrozenSummaryTextWidth = ShowFrozenSummaryInStatus ? std::round(TextRender()->TextBoundingBox(StatusFontSize, aFrozenSummaryBuf).m_W) : 0.0f;
	const float FrozenSummaryStatusWidth = ShowFrozenSummaryInStatus ? (StatusPaddingLeft + FrozenSummaryTextWidth + StatusPaddingRight) : 0.0f;
	struct SBottomTextLayoutItem
	{
		const char *m_pText = nullptr;
		float m_Width = 0.0f;
	};
	std::array<SBottomTextLayoutItem, 3> aBottomLayoutItems{};
	int BottomLayoutItemCount = 0;
	const auto AddBottomLayoutItem = [&](const char *pText) {
		if(pText == nullptr || pText[0] == '\0' || BottomLayoutItemCount >= (int)aBottomLayoutItems.size())
			return;
		aBottomLayoutItems[BottomLayoutItemCount].m_pText = pText;
		aBottomLayoutItems[BottomLayoutItemCount].m_Width = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
		++BottomLayoutItemCount;
	};
	if(ShowSwapCountdown)
		AddBottomLayoutItem(SwapInfo.m_aText);
	if(ShowFrozenSummaryInBottomRow)
		AddBottomLayoutItem(aFrozenSummaryBuf);
	if(ShowSwitchCountdown)
		AddBottomLayoutItem(aSwitchCountdownBuf);
	float NaturalBottomContentWidth = 0.0f;
	for(int i = 0; i < BottomLayoutItemCount; ++i)
		NaturalBottomContentWidth += aBottomLayoutItems[i].m_Width;
	if(BottomLayoutItemCount > 1)
		NaturalBottomContentWidth += BottomRowItemGap * (BottomLayoutItemCount - 1);
	const float DesiredBottomUnifiedWidth = BottomLayoutItemCount > 0 ? (NaturalBottomContentWidth + BottomRowPaddingX * 2.0f) : 0.0f;
	const bool ShowCover = HasMediaState;
	float BaseWidth = PaddingX;
	if(ShowCover)
		BaseWidth += CoverSize;
	if(ShowCover && (ShowSpectator || ShowLocalTime))
		BaseWidth += Gap;
	if(ShowSpectator)
		BaseWidth += SpectatorWidth;
	if(ShowSpectator && ShowLocalTime)
		BaseWidth += Gap;
	if(ShowLocalTime)
		BaseWidth += TimeSlotWidth;
	BaseWidth += PaddingX;
	if(!ShowCover && !ShowSpectator && !ShowLocalTime)
		BaseWidth = 0.0f;
	const float MaxTitleWidth = std::clamp(m_Width * 0.18f, 42.0f, 88.0f);
	const float MaxIslandWidth = TimerCapsule.m_Visible ?
		std::max(BaseWidth, TimerCapsule.m_BoxX - GapToTimer - ScreenPadding) :
		BaseWidth + (ShowCover ? (Gap + MaxTitleWidth) : 0.0f);
	const float MaxExpandedTitleWidth = std::max(0.0f, MaxIslandWidth - BaseWidth - Gap);
	const float NaturalTitleWidth = std::round(TextRender()->TextBoundingBox(TitleFontSize, pDisplayTitle).m_W);
	const float TitleWidth = (Expanded && ShowCover) ? std::clamp(NaturalTitleWidth, 0.0f, std::min(MaxTitleWidth, MaxExpandedTitleWidth)) : 0.0f;
	const float PlannedStatusWidth = ShowFrozenSummaryInStatus ? FrozenSummaryStatusWidth :
		(ShowRecordingStatus ? (ScoreboardExpanded ? RawExpandedStatusWidth : RawCollapsedStatusWidth) : 0.0f);
	float TargetWidth = BaseWidth;
	if(Expanded && TitleWidth > 0.0f)
		TargetWidth += Gap + TitleWidth;
	float PlannedUnifiedWidth = TimerCapsule.m_Visible ?
		(TargetWidth + GapToTimer + TimerCapsule.m_BoxW + (PlannedStatusWidth > 0.0f ? (TimerToStatusGap + PlannedStatusWidth) : 0.0f)) :
		TargetWidth;
	if(DesiredBottomUnifiedWidth > PlannedUnifiedWidth)
		TargetWidth += DesiredBottomUnifiedWidth - PlannedUnifiedWidth;
	float TargetX = m_Width * 0.5f - TargetWidth * 0.5f;
	if(TimerCapsule.m_Visible)
		TargetX = TargetWidth > 0.0f ? std::max(ScreenPadding, TimerCapsule.m_BoxX - GapToTimer - TargetWidth) : TimerCapsule.m_BoxX;
	else
	{
		const float MaxTargetX = std::max(ScreenPadding, m_Width - ScreenPadding - TargetWidth);
		TargetX = std::clamp(TargetX, ScreenPadding, MaxTargetX);
	}
	const float TargetHeight = ShowBottomRow ? (BaseIslandHeight + BottomRowExpandedHeight) : BaseIslandHeight;
	const float TitleAlphaTarget = Expanded && TitleWidth > 0.0f ? 1.0f : 0.0f;
	const float TitleOffsetTarget = Expanded ? 0.0f : 4.0f;
	const float SpectatorAlphaTarget = ShowSpectator ? 1.0f : 0.0f;
	const float BottomAlphaTarget = ShowBottomRow ? 1.0f : 0.0f;

	CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
	const uint64_t CapsuleNode = HudMediaIslandNodeKey("capsule");
	const uint64_t TitleNode = HudMediaIslandNodeKey("title");
	const uint64_t SpectatorNode = HudMediaIslandNodeKey("spectator");
	const uint64_t BottomNode = HudMediaIslandNodeKey("bottom");
	if(!AnimState.m_LayoutInitialized)
	{
		AnimState.m_TargetX = TargetX;
		AnimState.m_TargetWidth = TargetWidth;
		AnimState.m_TargetHeight = TargetHeight;
		AnimState.m_TargetTitleAlpha = TitleAlphaTarget;
		AnimState.m_TargetTitleOffset = TitleOffsetTarget;
		AnimState.m_TargetSpectatorAlpha = SpectatorAlphaTarget;
		AnimState.m_TargetBottomAlpha = BottomAlphaTarget;
		AnimRuntime.SetValue(CapsuleNode, EUiAnimProperty::POS_X, TargetX);
		AnimRuntime.SetValue(CapsuleNode, EUiAnimProperty::WIDTH, TargetWidth);
		AnimRuntime.SetValue(CapsuleNode, EUiAnimProperty::HEIGHT, TargetHeight);
		AnimRuntime.SetValue(TitleNode, EUiAnimProperty::ALPHA, TitleAlphaTarget);
		AnimRuntime.SetValue(TitleNode, EUiAnimProperty::POS_X, TitleOffsetTarget);
		AnimRuntime.SetValue(SpectatorNode, EUiAnimProperty::ALPHA, SpectatorAlphaTarget);
		AnimRuntime.SetValue(BottomNode, EUiAnimProperty::ALPHA, BottomAlphaTarget);
		AnimState.m_LayoutInitialized = true;
	}

	const float IslandX = ResolveAnimatedLayoutValueEx(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, TargetX, AnimState.m_TargetX, 0.18f, 0.0f, EEasing::EASE_OUT);
	const float IslandWidth = ResolveAnimatedLayoutValueEx(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, TargetWidth, AnimState.m_TargetWidth, 0.18f, 0.0f, EEasing::EASE_OUT);
	const float AnimatedIslandHeight = ResolveAnimatedLayoutValueEx(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, TargetHeight, AnimState.m_TargetHeight, 0.18f, 0.0f, EEasing::EASE_OUT);
	const float TitleAlpha = std::clamp(ResolveAnimatedLayoutValueEx(AnimRuntime, TitleNode, EUiAnimProperty::ALPHA, TitleAlphaTarget, AnimState.m_TargetTitleAlpha, 0.12f, 0.0f, EEasing::EASE_OUT), 0.0f, 1.0f);
	const float TitleOffset = ResolveAnimatedLayoutValueEx(AnimRuntime, TitleNode, EUiAnimProperty::POS_X, TitleOffsetTarget, AnimState.m_TargetTitleOffset, 0.12f, 0.0f, EEasing::EASE_OUT);
	const float SpectatorAlpha = std::clamp(ResolveAnimatedLayoutValueEx(AnimRuntime, SpectatorNode, EUiAnimProperty::ALPHA, SpectatorAlphaTarget, AnimState.m_TargetSpectatorAlpha, 0.08f, 0.0f, EEasing::EASE_OUT), 0.0f, 1.0f);
	const float BottomAlpha = std::clamp(ResolveAnimatedLayoutValueEx(AnimRuntime, BottomNode, EUiAnimProperty::ALPHA, BottomAlphaTarget, AnimState.m_TargetBottomAlpha, 0.12f, 0.0f, EEasing::EASE_OUT), 0.0f, 1.0f);

	const float Radius = BaseIslandHeight * 0.5f;
	const float CoverX = IslandX + PaddingX;
	const float CoverY = IslandY + (BaseIslandHeight - CoverSize) * 0.5f;
	const vec2 CoverCenter(CoverX + CoverSize * 0.5f, CoverY + CoverSize * 0.5f);
	const float CoverRadius = CoverSize * 0.5f;
	const float RightMetaX = IslandX + IslandWidth - PaddingX;
	const float TimeSlotX = ShowLocalTime ? (RightMetaX - TimeSlotWidth) : RightMetaX;
	const float TimeY = IslandY + (BaseIslandHeight - MetaFontSize) * 0.5f - 0.5f;
	const float TimeTextX = TimeSlotX + std::max(0.0f, TimeSlotWidth - TimeTextWidth);
	const float SpectatorAnchorX = ShowLocalTime ? TimeSlotX : RightMetaX;
	const float SpectatorX = ShowSpectator ? (SpectatorAnchorX - (ShowLocalTime ? Gap : 0.0f) - SpectatorWidth) : SpectatorAnchorX;
	const float TitleBaseX = CoverX + CoverSize + Gap;
	const float TitleX = TitleBaseX + TitleOffset;
	const float TitleRight = (ShowSpectator ? SpectatorX : (ShowLocalTime ? TimeSlotX : (IslandX + IslandWidth - PaddingX))) - Gap;
	const float TitleAvailableWidth = std::max(0.0f, TitleRight - TitleX);
	const float TitleY = IslandY + (BaseIslandHeight - TitleFontSize) * 0.5f - 0.5f;

	if(AnimState.m_LastCoverRotationTick == 0)
		AnimState.m_LastCoverRotationTick = Now;
	if(MediaState.m_Playing)
	{
		const float DeltaSec = (Now - AnimState.m_LastCoverRotationTick) / (float)time_freq();
		AnimState.m_CoverRotation = std::fmod(AnimState.m_CoverRotation + DeltaSec * CoverRotationSpeed, Tau);
	}
	AnimState.m_LastCoverRotationTick = Now;

	const unsigned int PrevFlags = TextRender()->GetRenderFlags();
	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.42f);

	const float StatusSectionX = TimerCapsule.m_BoxX + TimerCapsule.m_BoxW + TimerToStatusGap;
	const float CollapsedStatusWidth = RawCollapsedStatusWidth;
	const float ExpandedStatusWidth = RawExpandedStatusWidth;
	const float TargetStatusWidth = PlannedStatusWidth;
	const float TargetStatusAlpha = (ShowFrozenSummaryInStatus || ShowRecordingStatus) ? 1.0f : 0.0f;
	const float TargetTextAlpha = ShowFrozenSummaryInStatus ? 1.0f : (ShowRecordingStatus && ScoreboardExpanded ? 1.0f : 0.0f);

	const uint64_t StatusBoxNode = HudRecordingStatusNodeKey("box");
	const uint64_t StatusTextNode = HudRecordingStatusNodeKey("text");
	if(!m_RecordingStatusAnimState.m_Initialized)
	{
		m_RecordingStatusAnimState.m_TargetWidth = TargetStatusWidth;
		m_RecordingStatusAnimState.m_TargetAlpha = TargetStatusAlpha;
		m_RecordingStatusAnimState.m_TargetTextAlpha = TargetTextAlpha;
		AnimRuntime.SetValue(StatusBoxNode, EUiAnimProperty::WIDTH, TargetStatusWidth);
		AnimRuntime.SetValue(StatusBoxNode, EUiAnimProperty::ALPHA, TargetStatusAlpha);
		AnimRuntime.SetValue(StatusTextNode, EUiAnimProperty::ALPHA, TargetTextAlpha);
		m_RecordingStatusAnimState.m_Initialized = true;
	}

	const float StatusWidth = ResolveAnimatedLayoutValueEx(AnimRuntime, StatusBoxNode, EUiAnimProperty::WIDTH, TargetStatusWidth, m_RecordingStatusAnimState.m_TargetWidth, 0.16f, 0.0f, EEasing::EASE_OUT);
	const float StatusAlpha = std::clamp(ResolveAnimatedLayoutValueEx(AnimRuntime, StatusBoxNode, EUiAnimProperty::ALPHA, TargetStatusAlpha, m_RecordingStatusAnimState.m_TargetAlpha, 0.10f, 0.0f, EEasing::EASE_OUT), 0.0f, 1.0f);
	const float StatusTextAlpha = std::clamp(ResolveAnimatedLayoutValueEx(AnimRuntime, StatusTextNode, EUiAnimProperty::ALPHA, TargetTextAlpha, m_RecordingStatusAnimState.m_TargetTextAlpha, 0.08f, 0.0f, EEasing::EASE_OUT), 0.0f, 1.0f);
	const bool RenderStatusSection = StatusWidth > 1.0f && StatusAlpha > 0.01f;
	const float UnifiedRight = TimerCapsule.m_Visible ?
		(TimerCapsule.m_BoxX + TimerCapsule.m_BoxW + (RenderStatusSection ? (TimerToStatusGap + StatusWidth) : 0.0f)) :
		(IslandX + IslandWidth);
	const float UnifiedWidth = std::max(IslandWidth, UnifiedRight - IslandX);
	const bool RenderLeftSection = ShowCover || ShowSpectator || ShowLocalTime;
	const float TimerTextY = TimerCapsule.m_TextY;
	ColorRGBA IslandBackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHudIslandBgColor));
	IslandBackgroundColor.a = std::clamp(g_Config.m_QmHudIslandBgOpacity / 100.0f, 0.0f, 1.0f);
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::MediaIsland, {IslandX, IslandY, UnifiedWidth, AnimatedIslandHeight});

	DrawSmoothRoundedRect(Graphics(), IslandX, IslandY, UnifiedWidth, AnimatedIslandHeight, Radius, IslandBackgroundColor);

	if(ShowCover && !MediaState.m_AlbumArt.IsValid())
	{
		DrawSmoothCircle(Graphics(), CoverCenter, CoverRadius, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f));
		const float PlaceholderFontSize = MetaFontSize;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.35f);
		TextRender()->Text(CoverX + (CoverSize - PlaceholderWidth) * 0.5f, CoverY + (CoverSize - PlaceholderFontSize) * 0.5f - 0.5f, PlaceholderFontSize, FontIcons::FONT_ICON_MUSIC, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	else if(ShowCover)
	{
		DrawTexturedCircle(Graphics(), MediaState.m_AlbumArt, CoverCenter, CoverRadius, AnimState.m_CoverRotation);
	}

	if(ShowSpectator)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.82f * SpectatorAlpha);
		TextRender()->Text(SpectatorX, TimeY, MetaFontSize, FontIcons::FONT_ICON_EYE, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.82f * SpectatorAlpha);
		TextRender()->Text(SpectatorX + SpectatorIconWidth + SpectatorGap, TimeY, MetaFontSize, aSpectatorBuf, -1.0f);
	}

	if(ShowCover && TitleAlpha > 0.001f && TitleAvailableWidth > 2.0f)
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = TitleFontSize;
		Cursor.m_LineWidth = TitleAvailableWidth;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.SetPosition(vec2(TitleX, TitleY));
		TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.94f * TitleAlpha);
		TextRender()->TextEx(&Cursor, pDisplayTitle);
	}

	if(ShowLocalTime)
	{
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.86f);
		TextRender()->Text(TimeTextX, TimeY, MetaFontSize, aTimeDisplayBuf, -1.0f);
	}

	if(TimerCapsule.m_Visible)
	{
		if(RenderLeftSection)
		{
			const float LeftDividerX = TimerCapsule.m_BoxX - GapToTimer * 0.5f;
			Graphics()->DrawRect(LeftDividerX, IslandY + 4.0f, 0.75f, BaseIslandHeight - 8.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f), IGraphics::CORNER_ALL, 0.375f);
		}

		if(TimerCapsule.m_IsCritical)
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, TimerCapsule.m_Alpha);
		else
			TextRender()->TextColor(0.98f, 0.99f, 1.0f, 0.98f);
		TextRender()->Text(TimerCapsule.m_TextX, TimerTextY, TimerCapsule.m_FontSize, TimerCapsule.m_aText, -1.0f);
	}

	if(RenderStatusSection)
	{
		const float DividerX = TimerCapsule.m_BoxX + TimerCapsule.m_BoxW + TimerToStatusGap * 0.5f;
		Graphics()->DrawRect(DividerX, IslandY + 4.0f, 0.75f, BaseIslandHeight - 8.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * StatusAlpha), IGraphics::CORNER_ALL, 0.375f);

		if(ShowFrozenSummaryInStatus)
		{
			const float StatusTextY = IslandY + (BaseIslandHeight - StatusFontSize) * 0.5f - 0.5f;
			const float FrozenTextWidth = std::round(TextRender()->TextBoundingBox(StatusFontSize, aFrozenSummaryBuf).m_W);
			const float FrozenTextX = StatusSectionX + std::max(0.0f, (StatusWidth - FrozenTextWidth) * 0.5f);
			TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.92f * StatusAlpha);
			TextRender()->Text(FrozenTextX, StatusTextY, StatusFontSize, aFrozenSummaryBuf, -1.0f);
		}
		else
		{
			const vec2 DotCenter(StatusSectionX + StatusPaddingLeft + StatusDotSize * 0.5f, IslandY + BaseIslandHeight * 0.5f);
			DrawSmoothCircle(Graphics(), DotCenter, StatusDotSize * 0.5f, ColorRGBA(1.0f, 0.15f, 0.15f, 0.95f * StatusAlpha));

			if(StatusTextAlpha > 0.001f && StatusWidth > RawCollapsedStatusWidth + 2.0f)
			{
				const float StatusTextX = StatusSectionX + StatusPaddingLeft + StatusDotSize + StatusDotGap;
				const float StatusTextY = IslandY + (BaseIslandHeight - StatusFontSize) * 0.5f - 0.5f;
				TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.90f * StatusTextAlpha);
				TextRender()->Text(StatusTextX, StatusTextY, StatusFontSize, aRecordingBuf, -1.0f);
			}
		}
	}

	if(BottomAlpha > 0.01f && AnimatedIslandHeight > BaseIslandHeight + 0.5f)
	{
		const float BottomRowY = IslandY + BaseIslandHeight;
		const float BottomRowHeight = std::max(0.0f, AnimatedIslandHeight - BaseIslandHeight);
		const float DividerInset = std::min(BottomRowDividerInset, UnifiedWidth * 0.25f);
		const float DividerWidth = std::max(0.0f, UnifiedWidth - DividerInset * 2.0f);
		if(DividerWidth > 0.0f)
		{
			Graphics()->DrawRect(IslandX + DividerInset, BottomRowY, DividerWidth, 0.75f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * BottomAlpha), IGraphics::CORNER_ALL, 0.375f);
		}

		const float BottomTextY = BottomRowY + (BottomRowHeight - BottomFontSize) * 0.5f - 0.5f;
		const float ContentX = IslandX + BottomRowPaddingX;
		const float ContentWidth = std::max(0.0f, UnifiedWidth - BottomRowPaddingX * 2.0f);
		const ColorRGBA SwitchTextColor(0.97f, 0.98f, 1.0f, 0.90f * BottomAlpha);
		ColorRGBA SwapTextColor = SwapInfo.m_TextColor;
		SwapTextColor.a *= 0.92f * BottomAlpha;

		const auto RenderBottomTextBlock = [&](float X, float MaxWidth, const char *pText, const ColorRGBA &Color, bool AlignRight) {
			if(pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
				return;

			const float TextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
			TextRender()->TextColor(Color);
			if(TextWidth <= MaxWidth + 0.01f)
			{
				const float DrawX = AlignRight ? (X + MaxWidth - TextWidth) : X;
				TextRender()->Text(DrawX, BottomTextY, BottomFontSize, pText, -1.0f);
				return;
			}

			CTextCursor Cursor;
			Cursor.m_FontSize = BottomFontSize;
			Cursor.m_LineWidth = MaxWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(X, BottomTextY));
			TextRender()->TextEx(&Cursor, pText);
		};

		const auto RenderBottomTextCentered = [&](const char *pText, const ColorRGBA &Color) {
			if(pText == nullptr || pText[0] == '\0' || ContentWidth <= 0.0f)
				return;

			const float TextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
			if(TextWidth <= ContentWidth + 0.01f)
			{
				TextRender()->TextColor(Color);
				TextRender()->Text(IslandX + (UnifiedWidth - TextWidth) * 0.5f, BottomTextY, BottomFontSize, pText, -1.0f);
				return;
			}

			RenderBottomTextBlock(ContentX, ContentWidth, pText, Color, false);
		};

		const auto RenderBottomTextCenteredInBlock = [&](float X, float MaxWidth, const char *pText, const ColorRGBA &Color) {
			if(pText == nullptr || pText[0] == '\0' || MaxWidth <= 0.0f)
				return;

			const float TextWidth = std::round(TextRender()->TextBoundingBox(BottomFontSize, pText).m_W);
			TextRender()->TextColor(Color);
			if(TextWidth <= MaxWidth + 0.01f)
			{
				TextRender()->Text(X + (MaxWidth - TextWidth) * 0.5f, BottomTextY, BottomFontSize, pText, -1.0f);
				return;
			}

			RenderBottomTextBlock(X, MaxWidth, pText, Color, false);
		};

		struct SBottomTextItem
		{
			const char *m_pText = nullptr;
			ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			float m_Width = 0.0f;
		};

		std::array<SBottomTextItem, 3> aBottomItems{};
		int BottomItemCount = 0;
		if(ShowSwapCountdown)
			aBottomItems[BottomItemCount++] = {SwapInfo.m_aText, SwapTextColor, std::round(TextRender()->TextBoundingBox(BottomFontSize, SwapInfo.m_aText).m_W)};
		if(ShowFrozenSummaryInBottomRow)
			aBottomItems[BottomItemCount++] = {aFrozenSummaryBuf, SwitchTextColor, std::round(TextRender()->TextBoundingBox(BottomFontSize, aFrozenSummaryBuf).m_W)};
		if(ShowSwitchCountdown)
			aBottomItems[BottomItemCount++] = {aSwitchCountdownBuf, SwitchTextColor, std::round(TextRender()->TextBoundingBox(BottomFontSize, aSwitchCountdownBuf).m_W)};

		if(BottomItemCount == 1)
		{
			RenderBottomTextCentered(aBottomItems[0].m_pText, aBottomItems[0].m_Color);
		}
		else if(BottomItemCount > 1)
		{
			float SequenceWidth = 0.0f;
			for(int i = 0; i < BottomItemCount; ++i)
				SequenceWidth += aBottomItems[i].m_Width;
			SequenceWidth += BottomRowItemGap * (BottomItemCount - 1);

			if(SequenceWidth <= ContentWidth + 0.01f)
			{
				float DrawX = ContentX + (ContentWidth - SequenceWidth) * 0.5f;
				for(int i = 0; i < BottomItemCount; ++i)
				{
					TextRender()->TextColor(aBottomItems[i].m_Color);
					TextRender()->Text(DrawX, BottomTextY, BottomFontSize, aBottomItems[i].m_pText, -1.0f);
					DrawX += aBottomItems[i].m_Width + BottomRowItemGap;
				}
			}
			else
			{
				const float AvailableBlockWidth = std::max(0.0f, ContentWidth - BottomRowItemGap * (BottomItemCount - 1));
				float TotalNaturalBlockWidth = 0.0f;
				for(int i = 0; i < BottomItemCount; ++i)
					TotalNaturalBlockWidth += aBottomItems[i].m_Width;

				float CursorX = ContentX;
				float RemainingWidth = AvailableBlockWidth;
				float RemainingNaturalWidth = std::max(1.0f, TotalNaturalBlockWidth);
				for(int i = 0; i < BottomItemCount; ++i)
				{
					const bool LastItem = i == BottomItemCount - 1;
					float BlockWidth = RemainingWidth;
					if(!LastItem)
					{
						BlockWidth = std::max(0.0f, std::round(RemainingWidth * (aBottomItems[i].m_Width / RemainingNaturalWidth)));
						BlockWidth = std::min(BlockWidth, RemainingWidth);
					}

					if(BottomItemCount == 3 && i == 1)
						RenderBottomTextCenteredInBlock(CursorX, BlockWidth, aBottomItems[i].m_pText, aBottomItems[i].m_Color);
					else if(LastItem)
						RenderBottomTextBlock(CursorX, BlockWidth, aBottomItems[i].m_pText, aBottomItems[i].m_Color, true);
					else
						RenderBottomTextBlock(CursorX, BlockWidth, aBottomItems[i].m_pText, aBottomItems[i].m_Color, false);

					CursorX += BlockWidth + BottomRowItemGap;
					RemainingWidth = std::max(0.0f, RemainingWidth - BlockWidth);
					RemainingNaturalWidth = std::max(1.0f, RemainingNaturalWidth - aBottomItems[i].m_Width);
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

	// pCharacter contains the predicted character for local players or the last snap for players who are spectated
	CCharacterCore *pCharacter = &GameClient()->m_aClients[ClientId].m_Predicted;
	CNetObj_Character *pPlayer = &GameClient()->m_aClients[ClientId].m_RenderCur;
	int TotalJumpsToDisplay = 0;
	if(g_Config.m_ClShowhudJumpsIndicator)
	{
		int AvailableJumpsToDisplay;
		if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
		{
			bool Grounded = false;
			if(Collision()->CheckPoint(pPlayer->m_X + CCharacterCore::PhysicalSize() / 2,
				   pPlayer->m_Y + CCharacterCore::PhysicalSize() / 2 + 5))
			{
				Grounded = true;
			}
			if(Collision()->CheckPoint(pPlayer->m_X - CCharacterCore::PhysicalSize() / 2,
				   pPlayer->m_Y + CCharacterCore::PhysicalSize() / 2 + 5))
			{
				Grounded = true;
			}

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
			TotalJumpsToDisplay = maximum(minimum(absolute(pCharacter->m_Jumps), 10), 0);
			AvailableJumpsToDisplay = maximum(minimum(UnusedJumps, TotalJumpsToDisplay), 0);
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
				if(pPlayer->m_Weapon != Weapon)
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
				Graphics()->QuadsSetRotation(pi * 7 / 4);
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
				Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], x, y);
				Graphics()->QuadsSetRotation(0);
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
				x += aWeaponWidth[Weapon];
			}
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
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE)
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

	if(g_Config.m_QmFocusMode && g_Config.m_QmFocusModeHideUI)
		return;

	int Count = 0;
	const bool Preview = GameClient()->m_HudEditor.IsActive();
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

		Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

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

		Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);

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

	if(g_Config.m_QmFocusMode && g_Config.m_QmFocusModeHideUI)
		return;
	// render small dummy actions hud
	const float BoxHeight = 29.0f;
	const float BoxWidth = 16.0f;

	float StartX = m_Width - BoxWidth;
	float StartY = 285.0f - BoxHeight - 4; // 4 units distance to the next display;

	if(g_Config.m_ClShowhudScore)
	{
		StartY -= 56;
	}

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

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
		Lines.m_pKeyStatusText = "卡键: ?";
		if(DummyResetOnSwitch == 0)
			Lines.m_pKeyStatusText = "卡键: 开";
		else if(DummyResetOnSwitch == 1)
			Lines.m_pKeyStatusText = "卡键: 关";
		else if(DummyResetOnSwitch == 2)
			Lines.m_pKeyStatusText = "卡键: 重置本体";
	}

	if(Lines.m_ShowHammer)
	{
		const char *pHammerState = "正常";
		if(DeepflyMode == 1)
			pHammerState = "DF";
		else if(DeepflyMode == 2)
			pHammerState = "HDF";
		else if(DeepflyMode == 3)
			pHammerState = "自定义";
		str_format(Lines.m_aHammerLine, sizeof(Lines.m_aHammerLine), "锤: %s", pHammerState);
	}

	if(Lines.m_ShowControl)
	{
		const char *pControlState = DummyControl ? "开" : "关";
		str_format(Lines.m_aControlLine, sizeof(Lines.m_aControlLine), "分身控制: %s", pControlState);
	}

	if(Lines.m_ShowSync)
	{
		const char *pSyncState = DummyCopyMoves ? "开" : "关";
		str_format(Lines.m_aSyncLine, sizeof(Lines.m_aSyncLine), "分身同步: %s", pSyncState);
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
}

void CHud::RenderKeyStatus()
{
	const SKeyStatusLines Lines = GetKeyStatusLines(GameClient());
	const SKeyStatusLayout Layout = GetKeyStatusLayout(TextRender(), Lines, m_Width);
	if(Layout.m_H <= 0.0f)
		return;

	Graphics()->DrawRect(Layout.m_X, Layout.m_Y, Layout.m_W, Layout.m_H, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_ALL, 5.0f);

	float TextX = Layout.m_X + Layout.m_PaddingX;
	float TextY = Layout.m_Y + Layout.m_PaddingY;

	const float KeyTime = Client()->GlobalTime();
	const float KeyHue = std::fmod(KeyTime * 0.2f, 1.0f);
	ColorHSLA KeyRainbowHsla(KeyHue, 0.75f, 0.6f, 1.0f);
	ColorRGBA KeyRainbowColor = color_cast<ColorRGBA>(KeyRainbowHsla);

	TextRender()->TextColor(KeyRainbowColor);
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
	if(g_Config.m_TcJumpHint)
	{
		BoxHeight += 5.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
	}
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
	const bool PosOnly = ClientId == SPEC_FREEVIEW || (GameClient()->m_aClients[ClientId].m_SpecCharPresent);
	const bool ShowPosition = g_Config.m_ClShowhudPlayerPosition;
	const bool ShowSpeed = !PosOnly && g_Config.m_ClShowhudPlayerSpeed;
	const bool ShowAngle = !PosOnly && g_Config.m_ClShowhudPlayerAngle;
	const bool ShowJumpHint = !PosOnly && g_Config.m_TcJumpHint;
	const bool ShowStats = !PosOnly && g_Config.m_QmPlayerStatsHud;
	const bool ShowMovementInfo = ShowPosition || ShowSpeed || ShowAngle || ShowJumpHint || ShowStats;

	const float LineSpacer = 1.0f; // above and below each entry
	const float Fontsize = 6.0f;
	const float KeyStatusGap = 2.0f;

	const SKeyStatusLines KeyStatusLines = GetKeyStatusLines(GameClient());
	const SKeyStatusLayout KeyStatusLayout = GetKeyStatusLayout(TextRender(), KeyStatusLines, m_Width);
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
	if(ShowJumpHint)
	{
		const float TitleWidth = TextRender()->TextWidth(Fontsize, "三格edge:");
		const float LeftJumpWidth = TextRender()->TextWidth(Fontsize, "左跳");
		const float LeftDoubleJumpWidth = TextRender()->TextWidth(Fontsize, "左二跳");
		const float RightJumpWidth = TextRender()->TextWidth(Fontsize, "右跳");
		const float RightDoubleJumpWidth = TextRender()->TextWidth(Fontsize, "右二跳");
		const float LeftJumpValueWidth = TextRender()->TextWidth(Fontsize, ".34|.31|.16");
		const float LeftDoubleJumpValueWidth = TextRender()->TextWidth(Fontsize, ".41|.28|.25|.13");
		const float RightJumpValueWidth = TextRender()->TextWidth(Fontsize, ".63|.66|.81");
		const float RightDoubleJumpValueWidth = TextRender()->TextWidth(Fontsize, ".56|.69|.72|.84");
		const float NeededWidth = maximum(
			TitleWidth + 4.0f,
			maximum(LeftJumpWidth + LeftJumpValueWidth, LeftDoubleJumpWidth + LeftDoubleJumpValueWidth) + 6.0f,
			maximum(RightJumpWidth + RightJumpValueWidth, RightDoubleJumpWidth + RightDoubleJumpValueWidth) + 6.0f);
		BoxWidth = maximum(BoxWidth, NeededWidth);
	}

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

	Graphics()->DrawRect(StartX, StartY, BoxWidth, BoxHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_L, 5.0f);

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

			if(ShowJumpHint)
			{
				TextRender()->Text(LeftX, y, Fontsize, "三格edge:", -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				const char *pLeftJump = ".34|.31|.16";
				const char *pLeftDoubleJump = ".41|.28|.25|.13";
				const char *pRightJump = ".63|.66|.81";
				const char *pRightDoubleJump = ".56|.69|.72|.84";

				TextRender()->Text(LeftX, y, Fontsize, "左跳", -1.0f);
				TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, pLeftJump), y, Fontsize, pLeftJump, -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				TextRender()->Text(LeftX, y, Fontsize, "左二跳", -1.0f);
				TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, pLeftDoubleJump), y, Fontsize, pLeftDoubleJump, -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				TextRender()->Text(LeftX, y, Fontsize, "右跳", -1.0f);
				TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, pRightJump), y, Fontsize, pRightJump, -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				TextRender()->Text(LeftX, y, Fontsize, "右二跳", -1.0f);
				TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, pRightDoubleJump), y, Fontsize, pRightDoubleJump, -1.0f);
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;
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

				// 平均/最大存活时长
				float AvgAlive = Stats.GetAverageAliveTime(TickSpeed);
				float MaxAlive = Stats.GetMaxAliveTime(TickSpeed);
				str_format(aBuf, sizeof(aBuf), "存活: %.1fs/%.1fs", AvgAlive, MaxAlive);
				TextRender()->TextColor(RainbowColor);
				TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				// 被救醒次数/落水次数
				const float Hue2 = std::fmod(StatsTime * 0.2f + 0.2f, 1.0f);
				ColorHSLA RainbowHsla2(Hue2, 0.75f, 0.6f, 1.0f);
				ColorRGBA RainbowColor2 = color_cast<ColorRGBA>(RainbowHsla2);
				str_format(aBuf, sizeof(aBuf), "被救/落水: %d/%d", Stats.m_RescueCount, Stats.m_FreezeCount);
				TextRender()->TextColor(RainbowColor2);
				TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				// 左侧/右侧出钩比例
				const float Hue3 = std::fmod(StatsTime * 0.2f + 0.3f, 1.0f);
				ColorHSLA RainbowHsla3(Hue3, 0.75f, 0.6f, 1.0f);
				ColorRGBA RainbowColor3 = color_cast<ColorRGBA>(RainbowHsla3);
				float LeftRatio = Stats.GetHookLeftRatio() * 100.0f;
				float RightRatio = Stats.GetHookRightRatio() * 100.0f;
				str_format(aBuf, sizeof(aBuf), "出钩L/R: %.0f%%/%.0f%%", LeftRatio, RightRatio);
				TextRender()->TextColor(RainbowColor3);
				TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				y += MOVEMENT_INFORMATION_LINE_HEIGHT;

				if(g_Config.m_QmPlayerStatsMapProgressStyle != 0 && GameClient()->m_TClient.IsGoresMapProgressEnabled())
				{
					const int DummyIndex = g_Config.m_ClDummy ? 1 : 0;
					const bool HasProgress = GameClient()->m_TClient.HasGoresMapProgress(DummyIndex);
					const float Progress = HasProgress ? GameClient()->m_TClient.GetGoresMapProgress(DummyIndex) : 0.0f;

					if(HasProgress)
						str_format(aBuf, sizeof(aBuf), "地图进度: %.1f%%", Progress * 100.0f);
					else
						str_copy(aBuf, "地图进度: --");

					const float Hue4 = std::fmod(StatsTime * 0.2f + 0.4f, 1.0f);
					ColorHSLA RainbowHsla4(Hue4, 0.75f, 0.6f, 1.0f);
					ColorRGBA RainbowColor4 = color_cast<ColorRGBA>(RainbowHsla4);
					TextRender()->TextColor(RainbowColor4);
					TextRender()->Text(LeftX, y, Fontsize, aBuf, -1.0f);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					y += MOVEMENT_INFORMATION_LINE_HEIGHT;

					const float BarWidth = 42.0f;
					const float BarHeight = 3.0f;
					const float BarX = RightX - BarWidth;
					const float BarY = y + (MOVEMENT_INFORMATION_LINE_HEIGHT - BarHeight) * 0.5f;
					Graphics()->DrawRect(BarX, BarY, BarWidth, BarHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), IGraphics::CORNER_ALL, 1.0f);
					if(HasProgress)
						Graphics()->DrawRect(BarX, BarY, BarWidth * std::clamp(Progress, 0.0f, 1.0f), BarHeight, RainbowColor4.WithAlpha(0.85f), IGraphics::CORNER_ALL, 1.0f);
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

		TextRender()->TextColor(KeyRainbowColor);
		if(KeyStatusLines.m_ShowKey)
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

void CHud::RenderMapProgressBar()
{
	const bool Preview = GameClient()->m_HudEditor.IsActive();
	if(!GameClient()->m_TClient.IsGoresMapProgressEnabled() && !Preview)
		return;
	if(g_Config.m_QmPlayerStatsMapProgressStyle != 0 && g_Config.m_QmPlayerStatsHud)
		return;

	const int DummyIndex = g_Config.m_ClDummy ? 1 : 0;
	const bool HasProgress = Preview || GameClient()->m_TClient.HasGoresMapProgress(DummyIndex);
	const float TargetProgress = Preview ? 0.426f : (HasProgress ? std::clamp(GameClient()->m_TClient.GetGoresMapProgress(DummyIndex), 0.0f, 1.0f) : 0.0f);
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

	DrawSmoothRoundedRect(Graphics(), BarX, BarY, BarWidth, BarHeight, BarRadius, TrackColor);
	if(FillWidth > 0.0f)
		DrawSmoothRoundedRect(Graphics(), BarX, BarY, FillWidth, BarHeight, BarRadius, FillColor);

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
		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);

		DrawSmoothCircle(Graphics(), vec2(TeeX, TeeAnchorY), std::clamp(BarHeight * 0.48f, 4.0f, 10.0f), FillColor.WithAlpha(0.22f));
		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(TeeX, TeeAnchorY + OffsetToMid.y));
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
	Graphics()->DrawRect(m_Width - 180.0f, AdjustedHeight - 15.0f, 180.0f, 15.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_TL, 5.0f);

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

	if(g_Config.m_QmFocusMode && g_Config.m_QmFocusModeHideUI)
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
		Graphics()->DrawRect(x - 30.0f, 0.0f, 25.0f, 12.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);
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
		Graphics()->DrawRect(x - (TextWidth + 15.0f), 0.0f, TextWidth + 10.0f, 12.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);
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
			pAnimRuntime->SetValue(BoxNode, EUiAnimProperty::POS_X, BoxX);
			pAnimRuntime->SetValue(BoxNode, EUiAnimProperty::WIDTH, BoxW);
			pAnimRuntime->SetValue(TextNode, EUiAnimProperty::POS_X, TextX);
			m_LocalTimeV2AnimState.m_Initialized = true;
		}

		BoxX = ResolveAnimatedLayoutValue(*pAnimRuntime, BoxNode, EUiAnimProperty::POS_X, BoxX, m_LocalTimeV2AnimState.m_TargetBoxX);
		BoxW = ResolveAnimatedLayoutValue(*pAnimRuntime, BoxNode, EUiAnimProperty::WIDTH, BoxW, m_LocalTimeV2AnimState.m_TargetBoxW);
		TextX = ResolveAnimatedLayoutValue(*pAnimRuntime, TextNode, EUiAnimProperty::POS_X, TextX, m_LocalTimeV2AnimState.m_TargetTextX);
	}

	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::LocalTime, {BoxX, 0.0f, BoxW, 12.5f});
	Graphics()->DrawRect(BoxX, 0.0f, BoxW, 12.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);
	TextRender()->Text(TextX, TextY, 5.0f, aTimeStr, -1.0f);
	GameClient()->m_HudEditor.EndTransform(HudEditorScope);

	// Graphics()->DrawRect(x - 30.0f, 0.0f, 25.0f, 12.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 3.75f);
	// TextRender()->Text(x - 25.0f, (12.5f - 5.f) / 2.f, 5.0f, aTimeStr, -1.0f);
}

float CHud::RenderLegacyMediaInfoAt(float AnchorX, float CenterY)
{
	if(m_LegacyMediaInfoRendered || !g_Config.m_QmHudIslandUseOriginalStyle || !g_Config.m_ClSmtcEnable || !g_Config.m_ClSmtcShowHud)
		return CenterY;

	CSystemMediaControls::SState MediaState{};
	const bool Preview = GameClient()->m_HudEditor.IsActive();
	if(!GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState))
	{
		if(!Preview)
			return CenterY;
		str_copy(MediaState.m_aTitle, "Pure Music", sizeof(MediaState.m_aTitle));
		str_copy(MediaState.m_aArtist, "QmClient", sizeof(MediaState.m_aArtist));
		MediaState.m_PositionMs = 56 * 1000;
		MediaState.m_DurationMs = 3 * 60 * 1000;
	}

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
	char aLyricBuf[256] = {};
	constexpr float LyricFontSize = 6.0f;
	const float LyricHeight = LyricFontSize + 3.0f;
	const bool HasLyric = GameClient()->m_Lyrics.GetCurrentLine(aLyricBuf, sizeof(aLyricBuf), MediaState.m_PositionMs);
	const float LyricY = HasLyric ? std::clamp(IslandY + IslandHeight + 2.0f, 0.0f, maximum(0.0f, m_Height - LyricHeight)) : 0.0f;
	const float ContentBottomTarget = HasLyric ? maximum(IslandY + IslandHeight, LyricY + LyricHeight) : (IslandY + IslandHeight);
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::LegacyMediaInfo, {IslandX, IslandY, IslandWidth, ContentBottomTarget - IslandY});

	Graphics()->DrawRect(IslandX, IslandY, IslandWidth, IslandHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 4.0f);

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
	if(HasLyric)
	{
		const float LyricX = IslandX;
		const float LyricW = IslandWidth;
		ContentBottomY = maximum(ContentBottomY, LyricY + LyricHeight);
		Graphics()->DrawRect(LyricX, LyricY, LyricW, LyricHeight, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);

		CTextCursor Cursor;
		Cursor.m_FontSize = LyricFontSize;
		Cursor.m_LineWidth = LyricW - PaddingX * 2.0f;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.SetPosition(vec2(LyricX + PaddingX, LyricY + 1.0f));
		TextRender()->TextEx(&Cursor, aLyricBuf);
	}

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
		m_MediaIslandAnimState.Reset();

#if defined(CONF_VIDEORECORDER)
	if((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud))
#else
	if(g_Config.m_ClShowhud)
#endif
	{
		if(GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
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
			RenderDDRaceEffects();
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
			RenderSpectatorHud();
		}

		RenderMapProgressBar();
		if(g_Config.m_ClShowhudTimer && !ShowMediaIsland)
			RenderGameTimer();
		RenderPauseNotification();
		RenderSuddenDeath();
		if(g_Config.m_ClShowhudScore)
			RenderScoreHud();
		RenderDummyActions();
		RenderWarmupTimer();
		RenderDummyMiniMap();
		RenderTextInfo();
		GameClient()->m_TClient.RenderCenterLines();
		if(ShowMediaIsland)
			RenderMediaIsland();
		else
		{
			RenderLocalTime((m_Width / 7) * 3);
			RenderLegacyMediaInfo();
		}
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
			RenderConnectionWarning();
		RenderTeambalanceWarning();
		GameClient()->m_Voting.Render();
		if(g_Config.m_ClShowRecord)
			RenderRecord();
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
			m_ServerRecord = (float)pMsg->m_ServerTimeBest / 100;
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
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(12, aBuf) / 2, 20));
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
				DiffCursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf) / 2, 34));
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

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf) / 2, 20));
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
	if(m_ServerRecord > 0.0f)
	{
		char aBuf[64];
		TextRender()->Text(5, 75, 6, Localize("Server best:"), -1.0f);
		char aTime[32];
		str_time_float(m_ServerRecord, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", m_ServerRecord > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 75, 6, aBuf, -1.0f);
	}

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
