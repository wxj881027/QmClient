#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_MEDIA_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_MEDIA_CONTROLS_H

#include <game/client/ui.h>

class CSystemMediaControls;
class CTooltips;

inline float QmScoreboardMediaMaxWidth(float ScreenWidth, bool HasMedia)
{
	return std::max(0.0f, ScreenWidth - (HasMedia ? 76.0f : 10.0f));
}

inline CUIRect QmScoreboardMediaRail(const CUIRect &Scoreboard, const CUIRect &Screen)
{
	const float Top = std::clamp(Scoreboard.y + std::max(0.0f, (Scoreboard.h - 220.0f) * 0.5f), Screen.y + 5.0f, std::max(Screen.y + 5.0f, Screen.y + Screen.h - 225.0f));
	return {Scoreboard.x + Scoreboard.w + 5.0f, Top, 28.0f, 220.0f};
}

inline float QmScoreboardMediaVolumeAt(float MouseY, const CUIRect &Track)
{
	return Track.h > 0.0f ? std::clamp(1.0f - (MouseY - Track.y) / Track.h, 0.0f, 1.0f) : 0.0f;
}

class CQmScoreboardMediaControls
{
	CButtonContainer m_aButtons[3];
	char m_VolumeId = 0;
	uint64_t m_Generation = 0;
	float m_LocalVolume = 0.0f;
	int64_t m_LastRequest = 0;

public:
	void Cancel(CUi &Ui);
	void Render(CUi &Ui, ITextRender &TextRender, CTooltips &Tooltips, CSystemMediaControls &Media, const CUIRect &Scoreboard, bool Interactive, float Alpha, ColorRGBA Background);
};

#endif
