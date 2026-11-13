#include "music_world_lyrics.h"

#include <game/client/QmUi/UiTextRenderState.h>
#include <game/client/components/qmclient/qm_text_effect_render.h>

#include <new>
#include <stdexcept>

namespace QmMusicLyrics
{
	void CWorldLyricsRenderer::Reset(ITextRender &TextRender)
	{
		for(auto &aRows : m_aaRows)
			for(auto &Row : aRows)
			{
				Row.m_Initialized = false;
				try
				{
					TextRender.DeleteTextContainer(Row.m_Container);
					Row.m_Text.clear();
				}
				catch(const std::bad_alloc &)
				{
					// 保留持久句柄，后续重置或绘制继续清理。
				}
				catch(const std::length_error &)
				{
				}
			}
	}

	void CWorldLyricsRenderer::Render(ITextRender &TextRender, IGraphics &Graphics, const CWorldLyricsState &State, vec2 Camera, float Zoom, double Now)
	try
	{
		CUiScopedTextRenderState TextState(TextRender);
		float X0, Y0, X1, Y1;
		Graphics.GetScreen(&X0, &Y0, &X1, &Y1);
		struct SRestoreScreen
		{
			IGraphics &m_Graphics;
			CScreenRect m_Screen;
			~SRestoreScreen() { m_Graphics.MapScreen(m_Screen); }
		};
		const SRestoreScreen RestoreScreen{Graphics, CScreenRect(vec2(X0, Y0), vec2(X1, Y1))};
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
					if(Cache.m_Initialized || Cache.m_Container.m_Index >= 0)
					{
						Cache.m_Initialized = false;
						TextRender.DeleteTextContainer(Cache.m_Container);
						Cache.m_Text.clear();
					}
					continue;
				}
				const int VisibleCharacters = WorldLyricVisibleCharacters(Line, Index);
				if(!Cache.m_Initialized || !Cache.m_Container.Valid() || Cache.m_Text != Line.m_aRows[Index] || Cache.m_VisibleCharacters != VisibleCharacters || Cache.m_AtlasRevision != TextRender.GlyphAtlasRevision())
				{
					std::string PreparedText(Line.m_aRows[Index]);
					// 使用固定烘焙密度，缩放镜头只改变投影，不逐帧重建文字。
					Graphics.MapScreenToGameInterface(Camera.x, Camera.y, 1.0f);
					CTextCursor Cursor;
					Cursor.m_FontSize = Index == 0 ? 22.0f : 16.0f;
					Cursor.m_LineWidth = 420.0f;
					// 按完整句子排版，只隐藏尚未出现的字符，避免逐字居中或换行跳动。
					if(VisibleCharacters < Line.m_aCharacterCounts[Index])
					{
						// 文字渲染器的颜色分段使用 UTF-8 字节位置，不能直接传字符数量。
						const int VisibleBytes = static_cast<int>(str_utf8_offset_chars_to_bytes(Line.m_aRows[Index].c_str(), VisibleCharacters));
						Cursor.m_vColorSplits.emplace_back(VisibleBytes, -1, ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
					}
					// 句柄始终由缓存持有，创建失败后也能在下帧先清理再重建。
					Cache.m_Initialized = false;
					TextRender.DeleteTextContainer(Cache.m_Container);
					if(!TextRender.CreateTextContainer(Cache.m_Container, &Cursor, PreparedText.c_str()) || !Cache.m_Container.Valid())
						return;
					const auto Box = TextRender.GetBoundingBoxTextContainer(Cache.m_Container);
					Cache.m_Size = vec2(Box.m_W, Box.m_H);
					Cache.m_Text.swap(PreparedText);
					Cache.m_VisibleCharacters = VisibleCharacters;
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
				QmRenderTextContainerWithEffects(TextRender, Cache.m_Container, Style, Visual.m_Position.x - Cache.m_Size.x * 0.5f, Y);
				Y += Cache.m_Size.y + 4.0f;
			}
		}
	}
	catch(const std::bad_alloc &)
	{
		// 仅跳过当前绘制，缓存保留其资源所有权与重建入口。
	}
	catch(const std::length_error &)
	{
	}
}
