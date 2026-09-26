#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_STATUSBAR_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_STATUSBAR_H

#include <base/str.h>
#include <base/time.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/components/player_points.h>

#include <algorithm>
#include <map>
#include <string>

namespace tclient_statusbar
{
	inline bool IsValidPlayerId(int PlayerId)
	{
		return PlayerId >= 0 && PlayerId < MAX_CLIENTS;
	}

	inline bool FormatPlayerPoints(char *pBuf, int BufSize, EPointsStatus Status, int Points)
	{
		if(BufSize <= 0)
			return false;

		switch(Status)
		{
		case EPointsStatus::READY:
			str_format(pBuf, BufSize, "%d", Points);
			return true;
		case EPointsStatus::NOT_REQUESTED:
		case EPointsStatus::FETCHING:
			str_copy(pBuf, "...", BufSize);
			return true;
		case EPointsStatus::FAILED:
			str_copy(pBuf, "?", BufSize);
			return true;
		}
		return false;
	}
}

enum
{
	STATUSBAR_MAX_SIZE = 128,
	STATUSBAR_TYPE_LETTERS = 4
};

class CStatusItem
{
public:
	std::function<void()> m_RenderItem;
	std::function<float()> m_GetWidth;
	char m_aName[32];
	char m_aDisplayName[32];
	char m_aDesc[128];
	char m_aLetters[STATUSBAR_TYPE_LETTERS] = {};
	bool m_ShowLabel = true;
	CStatusItem(std::function<void()> Render, std::function<float()> Width, const char *pLetters, const char *pName, const char *pDisplayName, const char *pDesc, bool ShowLabel = true);
};

class CStatusBar : public CComponent
{
public:
	CStatusBar() = default;
	CStatusBar(const CStatusBar &) = delete;
	CStatusBar &operator=(const CStatusBar &) = delete;

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnInit() override;
	void OnWindowResize() override { m_TextWidths.clear(); }

	CStatusItem m_Angle = CStatusItem([this] { AngleRender(); }, [this] { return AngleWidth(); },
		"a", "Angle", "", "Displays your current angle in degrees");
	CStatusItem m_Ping = CStatusItem([this] { PingRender(); }, [this] { return PingWidth(); },
		"p", "Ping", "", "Displays your ping to the current server");
	CStatusItem m_Prediction = CStatusItem([this] { PredictionRender(); }, [this] { return PredictionWidth(); },
		"d", "Prediction", "Prediction", "Displays current prediction value");
	CStatusItem m_Position = CStatusItem([this] { PositionRender(); }, [this] { return PositionWidth(); },
		"c", "Position", "Position", "Displays current position");
	CStatusItem m_LocalTime = CStatusItem([this] { LocalTimeRender(); }, [this] { return LocalTimeWidth(); },
		"l", "Local Time", "", "Displays your local time", false);
	CStatusItem m_RaceTime = CStatusItem([this] { RaceTimeRender(); }, [this] { return RaceTimeWidth(); },
		"r", "Race Time", "", "Display your race time", false);
	CStatusItem m_FPS = CStatusItem([this] { FPSRender(); }, [this] { return FPSWidth(); },
		"f", "FPS", "", "Displays your frames per second");
	CStatusItem m_Velocity = CStatusItem([this] { VelocityRender(); }, [this] { return VelocityWidth(); },
		"v", "Velocity", "", "Displays X and Y velocity");
	CStatusItem m_Zoom = CStatusItem([this] { ZoomRender(); }, [this] { return ZoomWidth(); },
		"z", "Zoom", "", "Displays current zoom value");
	CStatusItem m_Score = CStatusItem([this] { ScoreRender(); }, [this] { return ScoreWidth(); },
		"s", "Points", "Points", "Displays the DDNet Points of the current player");
	CStatusItem m_Downstream = CStatusItem([this] { DownstreamRender(); }, [this] { return DownstreamWidth(); },
		"u", "Snapshot Latency", "Latency", "Displays server snapshot latency");
	CStatusItem m_Rtt = CStatusItem([this] { RttRender(); }, [this] { return RttWidth(); },
		"t", "Round-trip time", "RTT", "Displays the game connection round-trip time");
	CStatusItem m_Upstream = CStatusItem([this] { UpstreamRender(); }, [this] { return UpstreamWidth(); },
		"n", "Prediction Lead", "Prediction lead", "Displays the client's prediction lead");
	CStatusItem m_Jitter = CStatusItem([this] { JitterRender(); }, [this] { return JitterWidth(); },
		"j", "Latency Jitter", "Prediction jitter", "Displays prediction timing jitter");
	CStatusItem m_SnapshotGap = CStatusItem([this] { SnapshotGapRender(); }, [this] { return SnapshotGapWidth(); },
		"g", "Snapshot Age", "Snapshot age", "Displays the time since the last complete snapshot");
	CStatusItem m_PacketLoss = CStatusItem([this] { PacketLossRender(); }, [this] { return PacketLossWidth(); },
		"k", "Vital Resend Queue", "Vital resend queue", "Displays the number of unacknowledged vital chunks");
	CStatusItem m_DownRate = CStatusItem([this] { DownRateRender(); }, [this] { return DownRateWidth(); },
		"i", "Receive Rate", "Process UDP RX (est.)", "Displays estimated process UDP receive rate");
	CStatusItem m_UpRate = CStatusItem([this] { UpRateRender(); }, [this] { return UpRateWidth(); },
		"o", "Send Rate", "Process UDP TX (est.)", "Displays estimated process UDP send rate");
	CStatusItem m_ConnectionGrade = CStatusItem([this] { ConnectionGradeRender(); }, [this] { return ConnectionGradeWidth(); },
		"q", "Connection Quality", "Connection quality", "Displays connection quality grade");
	CStatusItem m_Cpu = CStatusItem([this] { CpuRender(); }, [this] { return CpuWidth(); },
		"x", "DDNet / Total CPU", "DDNet/total CPU", "Displays DDNet process CPU usage / total system CPU usage");
	CStatusItem m_Memory = CStatusItem([this] { MemoryRender(); }, [this] { return MemoryWidth(); },
		"y", "DDNet Memory", "DDNet memory", "Displays DDNet process memory usage");
	CStatusItem m_Space = CStatusItem([this] { SpaceRender(); }, [this] { return SpaceWidth(); },
		" _", "Space", " ", "Gap between statusbar items", false);

	std::vector<CStatusItem> m_vStatusItemTypes = {m_Angle, m_Ping, m_Prediction, m_Position, m_LocalTime, m_RaceTime, m_FPS, m_Velocity, m_Zoom, m_Score, m_Downstream, m_Rtt, m_Upstream, m_Jitter, m_SnapshotGap, m_PacketLoss, m_DownRate, m_UpRate, m_ConnectionGrade, m_Cpu, m_Memory, m_Space};
	std::vector<CStatusItem *> m_StatusBarItems = {&m_LocalTime, &m_FPS, &m_Space, &m_Angle, &m_Space, &m_Ping};

	void UpdateStatusBarSize();
	void ApplyStatusBarScheme(const char *pScheme);
	void UpdateStatusBarScheme(char *pScheme);

	bool m_PingActive = false;

private:
	std::map<std::string, float, std::less<>> m_TextWidths;
	float m_MetricsFontSize = 0.0f;
	vec2 m_MetricsScreenScale = vec2(0.0f, 0.0f);
	unsigned m_MetricsRenderFlags = 0;
	int m_MetricsFontPreset = 0;
	float CachedTextWidth(const char *pText);

	float m_FrameTimeAverage = 0.0f;
	int m_PlayerId = 0;
	float m_FontSize = 12.0f;
	float m_CursorX, m_CursorY, m_BarX = 0.0f, m_BarY;
	float m_Width, m_Height;
	float m_BarHeight, m_Margin;
	char m_aFormattedPoints[32] = {};
	bool m_HasFormattedPoints = false;

	int m_CurrentRaceTime = 0;
	int CalculateRaceTime();
	float GetDurationWidth(int Duration);
	int GetDigitsIndex(int Value, int Max);
	float AngleWidth();
	void AngleRender();

	float PingWidth();
	void PingRender();

	float PredictionWidth();
	void PredictionRender();

	float PositionWidth();
	void PositionRender();

	float LocalTimeWidth();
	void LocalTimeRender();

	float RaceTimeWidth();
	void RaceTimeRender();

	float FPSWidth();
	void FPSRender();

	float VelocityWidth();
	void VelocityRender();

	float ZoomWidth();
	void ZoomRender();

	float ScoreWidth();
	void ScoreRender();
	void UpdateFormattedPoints();

	float DownstreamWidth();
	void DownstreamRender();
	float RttWidth();
	void RttRender();

	float UpstreamWidth();
	void UpstreamRender();

	float JitterWidth();
	void JitterRender();

	float SnapshotGapWidth();
	void SnapshotGapRender();

	float PacketLossWidth();
	void PacketLossRender();

	float DownRateWidth();
	void DownRateRender();

	float UpRateWidth();
	void UpRateRender();

	float ConnectionGradeWidth();
	void ConnectionGradeRender();

	float CpuWidth();
	void CpuRender();

	float MemoryWidth();
	void MemoryRender();

	float SpaceWidth();
	void SpaceRender();

	void LabelRender(const char *pLabel);
	float LabelWidth(const char *pLabel);

	char m_aAppliedStatusBarScheme[STATUSBAR_MAX_SIZE + 1] = {};
};

#endif
