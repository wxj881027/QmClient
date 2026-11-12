#include "music_world_lyrics.h"

#include <game/client/render.h>

namespace QmMusicLyrics
{
	void CWorldLyricsRenderer::Reset(ITextRender &TextRender)
	{
		for(auto &aRows : m_aaRows)
			for(auto &Row : aRows)
			{
				TextRender.DeleteTextContainer(Row.m_Container);
				Row.m_Text.clear();
				Row.m_Initialized = false;
			}
	}

	void CWorldLyricsRenderer::Render(ITextRender &TextRender, IGraphics &Graphics, const CRenderTools &RenderTools, const CWorldLyricsState &State, vec2 Camera, float Zoom, double Now)
	{
		float X0, Y0, X1, Y1;
		Graphics.GetScreen(&X0, &Y0, &X1, &Y1);
		const unsigned PreviousFlags = TextRender.GetRenderFlags();
		const ColorRGBA PreviousColor = TextRender.GetTextColor();
		const ColorRGBA PreviousOutline = TextRender.GetTextOutlineColor();
		TextRender.SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE);
		TextRender.TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		TextRender.TextOutlineColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		const SWorldLyric *apLines[] = {&State.Previous(), &State.Current()};
		for(size_t Slot = 0; Slot < m_aaRows.size(); ++Slot)
		{
			const auto &Line = *apLines[Slot];
			const auto Visual = WorldLyricVisual(Line, Now);
			float Height = 0.0f;
			float Width = 0.0f;
			for(size_t Index = 0; Index < m_aaRows[Slot].size(); ++Index)
			{
				auto &Cache = m_aaRows[Slot][Index];
				if(!Line.m_Active || Index >= static_cast<size_t>(Line.m_RowCount))
				{
					if(Cache.m_Initialized)
					{
						TextRender.DeleteTextContainer(Cache.m_Container);
						Cache.m_Text.clear();
						Cache.m_Initialized = false;
					}
					continue;
				}
				if(!Cache.m_Initialized || Cache.m_Text != Line.m_aRows[Index] || Cache.m_AtlasRevision != TextRender.GlyphAtlasRevision())
				{
					TextRender.DeleteTextContainer(Cache.m_Container);
					// 使用固定烘焙密度，缩放镜头只改变投影，不逐帧重建文字。
					Graphics.MapScreenToGameInterface(Camera.x, Camera.y, 1.0f);
					CTextCursor Cursor;
					TextRender.SetCursor(&Cursor, 0.0f, 0.0f, Index == 0 ? 22.0f : 16.0f, TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END);
					Cursor.m_LineWidth = 420.0f;
					Cursor.m_MaxLines = 2;
					Cache.m_Text = Line.m_aRows[Index];
					TextRender.CreateTextContainer(Cache.m_Container, &Cursor, Cache.m_Text.c_str());
					const auto Box = TextRender.GetBoundingBoxTextContainer(Cache.m_Container);
					Cache.m_Size = vec2(Box.m_W, Box.m_H);
					Cache.m_AtlasRevision = TextRender.GlyphAtlasRevision();
					Cache.m_Initialized = true;
				}
				Height += Cache.m_Size.y + 4.0f;
				Width = std::max(Width, Cache.m_Size.x);
			}
			if(!Line.m_Active || Visual.m_Alpha <= 0.0f)
				continue;
			Graphics.MapScreenToGameInterface(Camera.x, Camera.y, Zoom);
			float Left, Top, Right, Bottom;
			Graphics.GetScreen(&Left, &Top, &Right, &Bottom);
			const float TextY = Visual.m_Position.y - Height * 0.5f;
			if(Visual.m_Position.x + Width * 0.5f + 8.0f < Left || Visual.m_Position.x - Width * 0.5f - 8.0f > Right || TextY + Height + 8.0f < Top || TextY - 8.0f > Bottom)
				continue;
			SQmTextEffectRenderStyle Style;
			Style.m_Effects = QM_TEXT_EFFECT_GLOW;
			Style.m_GlowRange = 2.0f;
			Style.m_GlowColor = ColorRGBA(0.4f, 0.75f, 1.0f, 0.14f);
			Style.m_TextColor = ColorRGBA(0.94f, 0.98f, 1.0f, Visual.m_Alpha * 0.9f);
			float Y = TextY;
			for(int Index = 0; Index < Line.m_RowCount; ++Index)
			{
				const auto &Cache = m_aaRows[Slot][Index];
				RenderTools.RenderTextContainerWithEffects(Cache.m_Container, Style, Visual.m_Position.x - Cache.m_Size.x * 0.5f, Y);
				Y += Cache.m_Size.y + 4.0f;
			}
		}
		TextRender.TextColor(PreviousColor);
		TextRender.TextOutlineColor(PreviousOutline);
		TextRender.SetRenderFlags(PreviousFlags);
		Graphics.MapScreen(X0, Y0, X1, Y1);
	}
}
