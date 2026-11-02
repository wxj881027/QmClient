#include "QmLegacyMediaHud.h"
#include "QmLyricsHud.h"

#include <engine/textrender.h>

#include <game/client/components/hud_media_island_logic.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>

#include <cmath>

namespace
{
	void RenderMetadata(ITextRender &TextRender, const CUIRect &Row, float FontSize, const char *pText, ColorRGBA Color)
	{
		float VisualTop = 0.0f;
		float VisualBottom = 0.0f;
		STextSizeProperties Props{};
		Props.m_pVisualTop = &VisualTop;
		Props.m_pVisualBottom = &VisualBottom;
		TextRender.TextWidth(FontSize, pText, -1, -1.0f, TEXTFLAG_DISALLOW_NEWLINE, Props);
		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = Row.w;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END | TEXTFLAG_DISALLOW_NEWLINE;
		Cursor.SetPosition(vec2(Row.x, Row.y + Row.h * 0.5f - (VisualTop + VisualBottom) * 0.5f));
		TextRender.TextColor(Color);
		TextRender.TextEx(&Cursor, pText);
	}
}

void QmRenderLegacyMediaHud(CUi &Ui, IGraphics &Graphics, ITextRender &TextRender, const CSystemMediaControls::SState &Media,
	const SQmLegacyMediaHudContent &Content, const SQmLegacyMediaHudLayout &Layout, const char *pLyrics, ColorRGBA LyricsColor, float LyricsElapsedSeconds, int Corners, const char *pTranslation)
{
	if(!Content.m_Visible || Layout.m_Panel.w <= 0.0f || Layout.m_Panel.h <= 0.0f)
		return;
	const float Scale = Layout.m_Scale;
	const CUIRect &Panel = Layout.m_Panel;
	const float Radius = std::min(7.0f * Scale, Panel.h * 0.5f);
	Graphics.DrawRect(Panel.x, Panel.y, Panel.w, Panel.h, ColorRGBA(0.03f, 0.035f, 0.045f, 0.58f), Corners, Radius);
	Graphics.DrawRect4(Panel.x, Panel.y, Panel.w, Panel.h,
		ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f),
		ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f), Corners, Radius);

	const unsigned PreviousFlags = TextRender.GetRenderFlags();
	const EFontPreset PreviousPreset = TextRender.GetFontPreset();
	const ColorRGBA PreviousTextColor = TextRender.GetTextColor();
	const ColorRGBA PreviousOutlineColor = TextRender.GetTextOutlineColor();
	TextRender.SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender.SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	TextRender.TextOutlineColor(0.0f, 0.0f, 0.0f, 0.30f);

	if(Content.m_ShowMetadata)
	{
		const CUIRect &Cover = Layout.m_Cover;
		Graphics.DrawRect(Cover.x - 0.3f * Scale, Cover.y - 0.3f * Scale, Cover.w + 0.6f * Scale, Cover.h + 0.6f * Scale,
			ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_ALL, Cover.h * 0.5f + 0.3f * Scale);
		// 圆形透明纹理由媒体组件统一准备，HUD 不重新裁剪或上传封面。
		if(Media.m_AlbumArtCircular.IsValid() && !Media.m_AlbumArtCircular.IsNullTexture())
		{
			Graphics.WrapClamp();
			Graphics.TextureSet(Media.m_AlbumArtCircular);
			Graphics.QuadsBegin();
			Graphics.SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			const IGraphics::CQuadItem Quad(Cover.x, Cover.y, Cover.w, Cover.h);
			Graphics.QuadsDrawTL(&Quad, 1);
			Graphics.QuadsEnd();
			Graphics.WrapNormal();
			Graphics.TextureClear();
		}
		else
		{
			Graphics.DrawRect(Cover.x, Cover.y, Cover.w, Cover.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_ALL, Cover.h * 0.5f);
			const CUIRect Icon = {Cover.x + Cover.w * 0.25f, Cover.y + Cover.h * 0.25f, Cover.w * 0.5f, Cover.h * 0.5f};
			Ui.DrawQmIcon(Icon, EQmIcon::MUSIC, FontIcons::FONT_ICON_MUSIC, ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f));
		}
		const char *pTitle = Media.m_aTitle[0] != '\0' ? Media.m_aTitle : Media.m_aArtist;
		RenderMetadata(TextRender, Layout.m_Title, 5.8f * Scale, pTitle, ColorRGBA(1.0f, 1.0f, 1.0f, 0.96f));
		if(Content.m_ShowArtist)
			RenderMetadata(TextRender, Layout.m_Artist, 4.5f * Scale, Media.m_aArtist, ColorRGBA(1.0f, 1.0f, 1.0f, 0.64f));

		const CUIRect &Bar = Layout.m_Progress;
		Graphics.DrawRect(Bar.x, Bar.y, Bar.w, Bar.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.13f), IGraphics::CORNER_ALL, Bar.h * 0.5f);
		const float Progress = Media.m_DurationMs > 0 ? std::clamp(static_cast<float>(Media.m_PositionMs) / static_cast<float>(Media.m_DurationMs), 0.0f, 1.0f) : 0.0f;
		if(Progress > 0.0f)
			Graphics.DrawRect(Bar.x, Bar.y, Bar.w * Progress, Bar.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.78f), IGraphics::CORNER_ALL, Bar.h * 0.5f);
	}

	if(Content.m_ShowLyrics)
	{
		const CUIRect &Row = Layout.m_Lyrics;
		if(Content.m_ShowMetadata)
			Graphics.DrawRect(Panel.x + 3.0f * Scale, Row.y - Scale, Panel.w - 6.0f * Scale, 0.3f * Scale,
				ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 0.15f * Scale);
		const char *apRows[] = {pLyrics, pTranslation};
		QmRenderLyricsRows(Graphics, TextRender, Row, apRows, Content.m_LyricsRowCount, 5.2f * Scale, LyricsColor, LyricsElapsedSeconds);
	}

	TextRender.SetFontPreset(PreviousPreset);
	TextRender.TextColor(PreviousTextColor);
	TextRender.TextOutlineColor(PreviousOutlineColor);
	TextRender.SetRenderFlags(PreviousFlags);
}
