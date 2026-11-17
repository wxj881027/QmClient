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
	int m_LyricsRowCount = 1;
};

inline SQmLegacyMediaHudContent QmLegacyMediaHudContent(bool HasTitle, bool HasArtist, bool HasCurrentLyric, bool LyricsActive, int LyricsRowCount = 1)
{
	const bool ShowMetadata = HasTitle || HasArtist;
	const bool ShowLyrics = HasCurrentLyric || LyricsActive;
	return {ShowMetadata || ShowLyrics, ShowMetadata, HasTitle && HasArtist, ShowLyrics, std::clamp(LyricsRowCount, 1, 2)};
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
	const float LyricsHeight = Content.m_ShowLyrics ? 11.0f * Content.m_LyricsRowCount : 0.0f;
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
		Layout.m_Lyrics = Rect(3.0f, MetadataHeight + 1.0f, 94.0f, LyricsHeight - 2.0f);
	return Layout;
}

void QmRenderLegacyMediaHud(CUi &Ui, IGraphics &Graphics, ITextRender &TextRender, const CSystemMediaControls::SState &Media,
	const SQmLegacyMediaHudContent &Content, const SQmLegacyMediaHudLayout &Layout, const char *pLyrics, ColorRGBA LyricsColor, float LyricsProgress, int Corners, const char *pTranslation = "");

#endif
