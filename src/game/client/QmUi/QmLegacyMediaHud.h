#ifndef GAME_CLIENT_QMUI_QMLEGACYMEDIAHUD_H
#define GAME_CLIENT_QMUI_QMLEGACYMEDIAHUD_H

#include <base/str.h>

#include <game/client/components/system_media_controls.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cstdint>

class CUi;
class ITextRender;

struct SQmLegacyMediaHudContent
{
	bool m_Visible;
	bool m_ShowMetadata;
	bool m_ShowArtist;
	bool m_ShowLyrics;
};

inline SQmLegacyMediaHudContent QmLegacyMediaHudContent(bool HasTitle, bool HasArtist, bool HasCurrentLyric, bool LyricsActive)
{
	const bool ShowMetadata = HasTitle || HasArtist;
	const bool ShowLyrics = HasCurrentLyric || LyricsActive;
	return {ShowMetadata || ShowLyrics, ShowMetadata, HasTitle && HasArtist, ShowLyrics};
}

struct SQmLegacyMediaHudLayout
{
	CUIRect m_Panel{};
	CUIRect m_Cover{};
	CUIRect m_Title{};
	CUIRect m_Artist{};
	CUIRect m_Progress{};
	CUIRect m_Lyrics{};
	float m_Scale = 1.0f;
};

inline SQmLegacyMediaHudLayout QmLegacyMediaHudLayout(float AnchorX, float CenterY, float ScreenWidth, float ScreenHeight, const SQmLegacyMediaHudContent &Content)
{
	SQmLegacyMediaHudLayout Layout;
	if(!Content.m_Visible || ScreenWidth <= 0.0f || ScreenHeight <= 0.0f)
		return Layout;

	constexpr float Width = 100.0f;
	const float MetadataHeight = Content.m_ShowMetadata ? 22.0f : 0.0f;
	const float LyricsHeight = Content.m_ShowLyrics ? 11.0f : 0.0f;
	const float Height = MetadataHeight + LyricsHeight;
	Layout.m_Scale = std::min({1.0f, ScreenWidth / Width, ScreenHeight / Height});
	const float Scale = Layout.m_Scale;
	const float X = std::clamp(AnchorX, 0.0f, std::max(0.0f, ScreenWidth - Width * Scale));
	// 歌词向下扩展，避免每次出现歌词都把封面和歌曲信息向上推。
	const float AnchorHeight = Content.m_ShowMetadata ? MetadataHeight : LyricsHeight;
	const float Y = std::clamp(CenterY - AnchorHeight * Scale * 0.5f, 0.0f, std::max(0.0f, ScreenHeight - Height * Scale));
	const auto Rect = [X, Y, Scale](float OffsetX, float OffsetY, float W, float H) {
		return CUIRect{X + OffsetX * Scale, Y + OffsetY * Scale, W * Scale, H * Scale};
	};
	Layout.m_Panel = Rect(0.0f, 0.0f, Width, Height);
	if(Content.m_ShowMetadata)
	{
		Layout.m_Cover = Rect(3.0f, 3.0f, 16.0f, 16.0f);
		Layout.m_Title = Content.m_ShowArtist ? Rect(22.0f, 2.5f, 75.0f, 7.0f) : Rect(22.0f, 5.0f, 75.0f, 9.0f);
		if(Content.m_ShowArtist)
			Layout.m_Artist = Rect(22.0f, 10.0f, 75.0f, 5.5f);
		Layout.m_Progress = Rect(22.0f, 18.5f, 75.0f, 1.0f);
	}
	if(Content.m_ShowLyrics)
		Layout.m_Lyrics = Rect(3.0f, MetadataHeight + 1.0f, 94.0f, 9.0f);
	return Layout;
}

class CQmLegacyMediaHudLyricState
{
	char m_aSource[128] = {};
	char m_aTitle[128] = {};
	char m_aArtist[128] = {};
	char m_aLyrics[256] = {};
	int64_t m_StartTick = 0;
	int64_t m_LastTick = 0;
	bool m_Initialized = false;

public:
	void Reset()
	{
		if(m_Initialized)
			*this = {};
	}

	void Update(const CSystemMediaControls::SState &Media, const char *pLyrics, int64_t Now)
	{
		if(!m_Initialized || Now < m_LastTick || str_comp(m_aSource, Media.m_aSourceAppId) != 0 ||
			str_comp(m_aTitle, Media.m_aTitle) != 0 || str_comp(m_aArtist, Media.m_aArtist) != 0 || str_comp(m_aLyrics, pLyrics) != 0)
		{
			str_copy(m_aSource, Media.m_aSourceAppId, sizeof(m_aSource));
			str_copy(m_aTitle, Media.m_aTitle, sizeof(m_aTitle));
			str_copy(m_aArtist, Media.m_aArtist, sizeof(m_aArtist));
			str_copy(m_aLyrics, pLyrics, sizeof(m_aLyrics));
			m_StartTick = Now;
			m_Initialized = true;
		}
		m_LastTick = Now;
	}

	float ElapsedSeconds(int64_t Now, int64_t TickFrequency) const
	{
		return m_Initialized && Now >= m_StartTick && TickFrequency > 0 ? (Now - m_StartTick) / static_cast<float>(TickFrequency) : 0.0f;
	}
};

void QmRenderLegacyMediaHud(CUi &Ui, IGraphics &Graphics, ITextRender &TextRender, const CSystemMediaControls::SState &Media,
	const SQmLegacyMediaHudContent &Content, const SQmLegacyMediaHudLayout &Layout, const char *pLyrics, ColorRGBA LyricsColor, float LyricsElapsedSeconds, int Corners);

#endif
