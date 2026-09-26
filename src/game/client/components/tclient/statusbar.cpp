#include "statusbar.h"

#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <algorithm>
#include <array>

namespace
{
	static_assert(STATUSBAR_MAX_SIZE < sizeof(g_Config.m_TcStatusBarScheme));

	const char *ConnectionGradeLabel(EQmConnectionGrade Grade)
	{
		switch(Grade)
		{
		case EQmConnectionGrade::NORMAL: return "Normal";
		case EQmConnectionGrade::ELEVATED: return "Elevated";
		case EQmConnectionGrade::SEVERE: return "Severe";
		case EQmConnectionGrade::DISCONNECTED: return "Disconnected";
		}
		return "Disconnected";
	}
}

CStatusItem::CStatusItem(std::function<void()> Render, std::function<float()> Width, const char *pLetters, const char *pName, const char *pDisplayName, const char *pDesc, bool ShowLabel)
{
	m_RenderItem = std::move(Render);
	m_GetWidth = std::move(Width);
	str_copy(m_aLetters, pLetters);
	str_copy(m_aName, pName);
	if(str_comp(pDisplayName, "") != 0)
		str_copy(m_aDisplayName, pDisplayName);
	else
		str_copy(m_aDisplayName, pName);
	str_copy(m_aDesc, pDesc);
	m_ShowLabel = ShowLabel;
}

int CStatusBar::GetDigitsIndex(const int Value, const int Max)
{
	int NormalizedValue = Value;
	if(NormalizedValue < 0) // Normalize
		NormalizedValue *= -1;

	int DigitsIndex = static_cast<int>(log10((NormalizedValue ? NormalizedValue : 1)));

	if(DigitsIndex > Max)
		DigitsIndex = Max;
	if(DigitsIndex < 0)
		DigitsIndex = 0;

	return DigitsIndex;
}
float CStatusBar::CachedTextWidth(const char *pText)
{
	float X0, Y0, X1, Y1;
	Graphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	const vec2 ScreenScale(Graphics()->ScreenWidth() / (X1 - X0), Graphics()->ScreenHeight() / (Y1 - Y0));
	const unsigned RenderFlags = TextRender()->GetRenderFlags();
	const int FontPreset = (int)TextRender()->GetFontPreset();
	if(m_MetricsFontSize != m_FontSize || m_MetricsScreenScale != ScreenScale || m_MetricsRenderFlags != RenderFlags || m_MetricsFontPreset != FontPreset)
	{
		m_TextWidths.clear();
		m_MetricsFontSize = m_FontSize;
		m_MetricsScreenScale = ScreenScale;
		m_MetricsRenderFlags = RenderFlags;
		m_MetricsFontPreset = FontPreset;
	}
	const auto It = m_TextWidths.find(pText);
	if(It != m_TextWidths.end())
		return It->second;
	const float Width = TextRender()->TextWidth(m_FontSize, pText);
	// 分数与语言可能持续变化，限制缓存大小。
	if(m_TextWidths.size() >= 128)
		m_TextWidths.clear();
	m_TextWidths.emplace(pText, Width);
	return Width;
}
float CStatusBar::GetDurationWidth(int Duration)
{
	return CachedTextWidth(Duration >= 3600 * 24 * 100 ? "000d 00:00:00" : Duration >= 3600 * 24 * 10 ? "00d 00:00:00" :
								       Duration >= 3600 * 24              ? "0d 00:00:00" :
								       Duration >= 3600                   ? "00:00:00" :
													    "00:00");
}

float CStatusBar::AngleWidth()
{
	if(!tclient_statusbar::IsValidPlayerId(m_PlayerId))
		return 0.0f;

	return CachedTextWidth("000.00");
}
void CStatusBar::AngleRender()
{
	CNetObj_Character *pCharacter = &GameClient()->m_Snap.m_aCharacters[m_PlayerId].m_Cur;
	float Angle = 0.0f;
	if(GameClient()->m_Snap.m_aCharacters[m_PlayerId].m_HasExtendedDisplayInfo)
	{
		CNetObj_DDNetCharacter *pExtendedData = &GameClient()->m_Snap.m_aCharacters[m_PlayerId].m_ExtendedData;
		Angle = atan2f(pExtendedData->m_TargetY, pExtendedData->m_TargetX);
	}
	else
	{
		Angle = pCharacter->m_Angle / 256.0f;
	}
	if(Angle < 0)
		Angle += 2.0f * pi;
	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%.2f", Angle * 180.0f / pi);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::PingWidth()
{
	if(!tclient_statusbar::IsValidPlayerId(m_PlayerId) || !GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId])
		return 0.0f;

	return CachedTextWidth("0000");
}
void CStatusBar::PingRender()
{
	const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId];
	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%d", pInfo->m_Latency);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
	m_PingActive = true;
}

float CStatusBar::PredictionWidth()
{
	return CachedTextWidth("0000");
}
void CStatusBar::PredictionRender()
{
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%d", Client()->GetPredictionTime());
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::LocalTimeWidth()
{
	return CachedTextWidth(
		g_Config.m_TcStatusBar12HourClock ? (g_Config.m_TcStatusBarLocalTimeSeconds ? "00:00:00 XX" : "00:00 XX") : (g_Config.m_TcStatusBarLocalTimeSeconds ? "00:00:00" : "00:00"));
}
void CStatusBar::LocalTimeRender()
{
	static char s_aTimeBuf[12];
	str_timestamp_format(s_aTimeBuf, sizeof(s_aTimeBuf), g_Config.m_TcStatusBar12HourClock ? (g_Config.m_TcStatusBarLocalTimeSeconds ? "%I:%M:%S %p" : "%I:%M %p") : (g_Config.m_TcStatusBarLocalTimeSeconds ? "%H:%M:%S" : "%H:%M"));
	if(s_aTimeBuf[0] == '0')
		str_copy(s_aTimeBuf, &s_aTimeBuf[1], sizeof(s_aTimeBuf) - 1);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, s_aTimeBuf);
}

float CStatusBar::RaceTimeWidth()
{
	return GetDurationWidth(m_CurrentRaceTime);
}
int CStatusBar::CalculateRaceTime()
{
	int RaceTime = 0;
	if(GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit && (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
	{
		RaceTime = GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit * 60 - ((Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed());

		if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
			RaceTime = 0;
	}
	else if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
	{
		RaceTime = (Client()->GameTick(g_Config.m_ClDummy) + GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();
	}
	else
	{
		RaceTime = (Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed();
	}
	return RaceTime;
}
void CStatusBar::RaceTimeRender()
{
	const int RaceTime = m_CurrentRaceTime;
	char aTimeBuf[64];
	str_time((int64_t)RaceTime * 100, TIME_DAYS, aTimeBuf, sizeof(aTimeBuf));

	if(GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit && RaceTime <= 60 && (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
	{
		const float Alpha = RaceTime <= 10 && (2 * time() / time_freq()) % 2 ? 0.5f : 1.0f;
		TextRender()->TextColor(1.0f, 0.25f, 0.25f, Alpha);
	}
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aTimeBuf);
	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcStatusBarTextColor)));
}

float CStatusBar::FPSWidth()
{
	return CachedTextWidth("00000");
}
void CStatusBar::FPSRender()
{
	m_FrameTimeAverage = m_FrameTimeAverage * 0.9f + Client()->RenderFrameTime() * 0.1f;
	int FPS = (int)(1.0f / m_FrameTimeAverage + 0.5f);
	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%d", FPS);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::PositionWidth()
{
	if(!tclient_statusbar::IsValidPlayerId(m_PlayerId) || !GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId])
		return 0.0f;

	return CachedTextWidth("-0000.00, -0000.00");
}
void CStatusBar::PositionRender()
{
	const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[m_PlayerId].m_Prev;
	const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[m_PlayerId].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	// To make the player position relative to blocks we need to divide by the block size
	const vec2 Pos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick) / 32.0f;
	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%2.2f, %2.2f", Pos.x, Pos.y);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::VelocityWidth()
{
	if(!tclient_statusbar::IsValidPlayerId(m_PlayerId) || !GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId])
		return 0.0f;

	return CachedTextWidth("+00.00, +00.00");
}
void CStatusBar::VelocityRender()
{
	const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[m_PlayerId].m_Prev;
	const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[m_PlayerId].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	const vec2 Vel = mix(vec2(pPrevChar->m_VelX, pPrevChar->m_VelY), vec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);
	float VelspeedX = Vel.x / 256.0f * Client()->GameTickSpeed();
	if(Vel.x >= -1 && Vel.x <= 1)
		VelspeedX = 0;
	float VelspeedY = Vel.y / 256.0f * Client()->GameTickSpeed();
	if(Vel.y >= -128 && Vel.y <= 128)
		VelspeedY = 0;
	float DisplaySpeedX = VelspeedX / 32;
	float VelspeedLength = length(vec2(Vel.x, Vel.y) / 256.0f) * Client()->GameTickSpeed();
	float Ramp = VelocityRamp(VelspeedLength, GameClient()->m_aTuning[g_Config.m_ClDummy].m_VelrampStart, GameClient()->m_aTuning[g_Config.m_ClDummy].m_VelrampRange, GameClient()->m_aTuning[g_Config.m_ClDummy].m_VelrampCurvature);
	DisplaySpeedX *= Ramp;
	float DisplaySpeedY = VelspeedY / 32;

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%+06.2f, %+06.2f", DisplaySpeedX, DisplaySpeedY);
	if(DisplaySpeedX > 100 || DisplaySpeedY > 100)
		str_format(aBuf, sizeof(aBuf), "%+06.2f, %+06.2f", DisplaySpeedX, DisplaySpeedY);

	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::ZoomWidth() { return CachedTextWidth("00.00"); }
void CStatusBar::ZoomRender()
{
	char aBuf[32];
	const double ZoomStep = std::cos((30.0f * pi) / 180.0f);
	const float ConsoleZoom = std::log(GameClient()->m_Camera.m_Zoom * std::pow(ZoomStep, 10)) / std::log(ZoomStep);
	str_format(aBuf, sizeof(aBuf), "%.2f", ConsoleZoom);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::ScoreWidth()
{
	if(!m_HasFormattedPoints)
		return 0.0f;

	return CachedTextWidth(m_aFormattedPoints);
}

void CStatusBar::ScoreRender()
{
	if(!m_HasFormattedPoints)
		return;

	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, m_aFormattedPoints);
}

void CStatusBar::UpdateFormattedPoints()
{
	m_HasFormattedPoints = false;
	m_aFormattedPoints[0] = '\0';
	if(m_PlayerId < 0 || m_PlayerId >= MAX_CLIENTS)
		return;

	if(!GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId])
		return;

	const char *pPlayerName = GameClient()->m_aClients[m_PlayerId].m_aName;
	if(pPlayerName[0] == '\0')
		return;

	GameClient()->m_PlayerPoints.EnsureQueried(pPlayerName);
	const SPlayerPointsResult Points = GameClient()->m_PlayerPoints.GetPoints(pPlayerName);
	m_HasFormattedPoints = tclient_statusbar::FormatPlayerPoints(m_aFormattedPoints, sizeof(m_aFormattedPoints), Points.m_Status, Points.m_Points);
}

float CStatusBar::DownstreamWidth()
{
	return CachedTextWidth("000ms");
}

void CStatusBar::DownstreamRender()
{
	const CServerInfo &CurrentServerInfo = Client()->ServerInfo();
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "ms", (float)CurrentServerInfo.m_Latency);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::RttWidth()
{
	return CachedTextWidth("000ms");
}

void CStatusBar::RttRender()
{
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "ms", GameClient()->m_QmMonitoring.Snapshot().m_Network.m_PingMs);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::UpstreamWidth()
{
	return CachedTextWidth("000ms");
}

void CStatusBar::UpstreamRender()
{
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "ms", GameClient()->m_QmMonitoring.Snapshot().m_Network.m_PredictionLeadMs);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::JitterWidth()
{
	return CachedTextWidth("000ms");
}

void CStatusBar::JitterRender()
{
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "ms", GameClient()->m_QmMonitoring.Snapshot().m_Network.m_PredictionJitterMs);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::SnapshotGapWidth()
{
	return CachedTextWidth("000ms");
}

void CStatusBar::SnapshotGapRender()
{
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "ms", GameClient()->m_QmMonitoring.Snapshot().m_Network.m_SnapshotGapMs);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::PacketLossWidth()
{
	return CachedTextWidth("000");
}

void CStatusBar::PacketLossRender()
{
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "", (float)GameClient()->m_QmMonitoring.Snapshot().m_Network.m_VitalResendCount);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::DownRateWidth()
{
	return CachedTextWidth("000.0KiB/s");
}

void CStatusBar::DownRateRender()
{
	char aBuf[32];
	const float EstimatedBytesPerSec = GameClient()->m_QmMonitoring.Snapshot().m_Network.m_Recv.m_RateKibPerSec * 1024.0f;
	FormatRateValue(aBuf, sizeof(aBuf), EstimatedBytesPerSec);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::UpRateWidth()
{
	return CachedTextWidth("000.0KiB/s");
}

void CStatusBar::UpRateRender()
{
	char aBuf[32];
	const float EstimatedBytesPerSec = GameClient()->m_QmMonitoring.Snapshot().m_Network.m_Send.m_RateKibPerSec * 1024.0f;
	FormatRateValue(aBuf, sizeof(aBuf), EstimatedBytesPerSec);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::ConnectionGradeWidth()
{
	return CachedTextWidth(Localize(ConnectionGradeLabel(GameClient()->m_QmMonitoring.Snapshot().m_Verdict.m_Grade)));
}

void CStatusBar::ConnectionGradeRender()
{
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, Localize(ConnectionGradeLabel(GameClient()->m_QmMonitoring.Snapshot().m_Verdict.m_Grade)));
}

float CStatusBar::CpuWidth()
{
	return CachedTextWidth("100%/100%");
}

void CStatusBar::CpuRender()
{
	char aBuf[32];
	const SQmPerformanceMetrics &Perf = GameClient()->m_QmMonitoring.Snapshot().m_Performance;
	FormatCpuRatioValue(aBuf, sizeof(aBuf), Perf.m_CpuUsagePct, Perf.m_TotalCpuUsagePct);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::MemoryWidth()
{
	return CachedTextWidth("4096MB");
}

void CStatusBar::MemoryRender()
{
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "MB", GameClient()->m_QmMonitoring.Snapshot().m_Performance.m_MemoryUsageMb);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}

float CStatusBar::SpaceWidth() { return 0.0f; }
void CStatusBar::SpaceRender() {}

void CStatusBar::UpdateStatusBarSize()
{
	m_Width = 300.0f * Graphics()->ScreenAspect();
	m_Height = 300.0f;
	m_BarHeight = g_Config.m_TcStatusBarHeight;
	m_Margin = m_BarHeight * 0.2f;
	m_BarY = m_Height - m_BarHeight;
	m_FontSize = m_BarHeight - (m_Margin * 2);
	m_Margin *= 1.5f;
}

void CStatusBar::OnInit()
{
	UpdateStatusBarSize();
	ApplyStatusBarScheme(g_Config.m_TcStatusBarScheme);
}

void CStatusBar::LabelRender(const char *pLabel)
{
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%s:", pLabel);
	TextRender()->Text(m_CursorX, m_CursorY, m_FontSize, aBuf);
}
float CStatusBar::LabelWidth(const char *pLabel)
{
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", pLabel);
	return CachedTextWidth(aBuf);
}

void CStatusBar::ApplyStatusBarScheme(const char *pScheme)
{
	m_StatusBarItems.clear();
	for(int i = 0; pScheme[i] != '\0'; ++i)
	{
		char SchemeLetter = pScheme[i];
		for(CStatusItem &ItemType : m_vStatusItemTypes)
		{
			for(char ItemLetter : ItemType.m_aLetters)
			{
				if(ItemLetter == SchemeLetter)
				{
					m_StatusBarItems.push_back(&ItemType);
					break;
				}
			}
		}
	}
	str_copy(m_aAppliedStatusBarScheme, pScheme, sizeof(m_aAppliedStatusBarScheme));
}

void CStatusBar::UpdateStatusBarScheme(char *pScheme)
{
	int Index = 0;
	for(CStatusItem *&pItem : m_StatusBarItems)
	{
		if(Index >= STATUSBAR_MAX_SIZE)
			break;
		pScheme[Index++] = pItem->m_aLetters[0];
	}
	pScheme[Index] = '\0';
	str_copy(m_aAppliedStatusBarScheme, pScheme, sizeof(m_aAppliedStatusBarScheme));
}

void CStatusBar::OnRender()
{
	m_PingActive = false;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!g_Config.m_TcStatusBar || !GameClient()->m_Snap.m_pGameInfoObj)
		return;

#if defined(CONF_VIDEORECORDER)
	// 渲染（录制）demo 视频时跟随 cl_video_showhud，避免状态栏被一起录进视频
	if(IVideo::Current() && !g_Config.m_ClVideoShowhud)
		return;
#endif

	if(str_comp(m_aAppliedStatusBarScheme, g_Config.m_TcStatusBarScheme) != 0)
		ApplyStatusBarScheme(g_Config.m_TcStatusBarScheme);

	m_PlayerId = GameClient()->m_Snap.m_LocalClientId;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		m_PlayerId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
	m_HasFormattedPoints = false;
	if(std::find(m_StatusBarItems.begin(), m_StatusBarItems.end(), &m_Score) != m_StatusBarItems.end())
		UpdateFormattedPoints();

	UpdateStatusBarSize();
	m_CurrentRaceTime = CalculateRaceTime();

	const float BackgroundAlpha = g_Config.m_TcStatusBarAlpha / 100.0f;
	Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);
	Ui()->RenderGaussianBlur({m_BarX, m_BarY, m_Width, m_BarHeight}, BackgroundAlpha, IGraphics::CORNER_NONE, 0.0f);
	Graphics()->DrawRect(m_BarX, m_BarY, m_Width, m_BarHeight, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcStatusBarColor)).WithAlpha(BackgroundAlpha), 0, 0);
	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcStatusBarTextColor)).WithAlpha(g_Config.m_TcStatusBarTextAlpha / 100.0f));

	struct SStatusLayoutItem
	{
		const CStatusItem *m_pItem = nullptr;
		float m_ItemWidth = 0.0f;
		float m_LabelWidth = 0.0f;
		bool m_IsSpace = false;
	};

	int SpaceCount = 0;
	const int ItemCount = std::min((int)m_StatusBarItems.size(), (int)STATUSBAR_MAX_SIZE);
	float UsedWidth = 0.0f;
	float AvailableWidth = m_Width - m_Margin * 2.0f; // 1 extra margin on the sides
	std::array<SStatusLayoutItem, STATUSBAR_MAX_SIZE> aLayoutItems;
	int LayoutItemCount = 0;
	// Count the number of spaces and determine how much unused space there is
	for(int ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex)
	{
		const CStatusItem *pItem = m_StatusBarItems[ItemIndex];
		SStatusLayoutItem &LayoutItem = aLayoutItems[LayoutItemCount++];
		LayoutItem.m_pItem = pItem;
		LayoutItem.m_IsSpace = str_comp(pItem->m_aName, "Space") == 0;
		if(LayoutItem.m_IsSpace)
		{
			++SpaceCount;
		}
		else
		{
			LayoutItem.m_ItemWidth = pItem->m_GetWidth();
			if(g_Config.m_TcStatusBarLabels && pItem->m_ShowLabel && LayoutItem.m_ItemWidth > 0.0f)
				LayoutItem.m_LabelWidth = LabelWidth(Localize(pItem->m_aDisplayName));
			UsedWidth += LayoutItem.m_ItemWidth + LayoutItem.m_LabelWidth;
		}
	}
	UsedWidth += m_Margin * (ItemCount + 1);
	AvailableWidth -= UsedWidth;
	// AvailableWidth can be negative so might as well not make it even worse
	float SpaceWidth = SpaceCount > 0 ? std::max(AvailableWidth / (float)SpaceCount, 0.0f) : 0.0f;

	float SpaceBetweenItems = ItemCount > 1 ? std::max(AvailableWidth / (float)(ItemCount - 1), 0.0f) : 0.0f;
	if(SpaceCount > 0)
		SpaceBetweenItems = 0;

	m_CursorY = m_BarY + (m_BarHeight - m_FontSize) / 2;

	m_CursorX = m_Margin;
	// Render items
	for(int LayoutItemIndex = 0; LayoutItemIndex < LayoutItemCount; ++LayoutItemIndex)
	{
		const SStatusLayoutItem &LayoutItem = aLayoutItems[LayoutItemIndex];
		m_CursorX += m_Margin;
		const CStatusItem *pItem = LayoutItem.m_pItem;

		if(LayoutItem.m_ItemWidth > 0.0f)
		{
			if(LayoutItem.m_LabelWidth > 0.0f)
			{
				LabelRender(Localize(pItem->m_aDisplayName));
				m_CursorX += LayoutItem.m_LabelWidth;
			}
			pItem->m_RenderItem();
		}

		m_CursorX += LayoutItem.m_ItemWidth;
		if(LayoutItem.m_IsSpace)
			m_CursorX += SpaceWidth;

		m_CursorX += SpaceBetweenItems;
	}
}
