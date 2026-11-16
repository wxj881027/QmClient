#include "hud_map_progress.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/localization.h>

namespace QmHudMapProgress
{
	float MeasureMinimumWidth(ITextRender &TextRender, float Scale)
	{
		const float FontSize = 4.4f * Scale;
		return MinimumWidth(TextRender.TextWidth(FontSize, Localize("Map")), TextRender.TextWidth(FontSize, "100.0%"), Scale);
	}

	void RenderIslandStrip(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Row, float Scale, bool HasProgress, float Progress, const ColorRGBA &FillColor, float Alpha)
	{
		if(Alpha <= 0.01f)
			return;
		const float FontSize = 4.4f * Scale;
		const char *pLabel = Localize("Map");
		// 百分比始终预留满进度的宽度，数字位数变化不推动横条。
		const SStripLayout Layout = StripLayout(Row, TextRender.TextWidth(FontSize, pLabel), TextRender.TextWidth(FontSize, "100.0%"), Scale);
		if(!Layout.m_Visible)
			return;
		Graphics.DrawRect(Layout.m_Track.x, Layout.m_Track.y, Layout.m_Track.w, Layout.m_Track.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f * Alpha), IGraphics::CORNER_ALL, Layout.m_Track.h * 0.5f);
		if(HasProgress && Progress > 0.0f)
		{
			CUIRect Fill = Layout.m_Track;
			Fill.w *= std::clamp(Progress, 0.0f, 1.0f);
			Graphics.DrawRect(Fill.x, Fill.y, Fill.w, Fill.h, FillColor.WithAlpha(0.95f * Alpha), IGraphics::CORNER_ALL, std::min(Fill.h, Fill.w) * 0.5f);
		}
		char aValue[16];
		if(HasProgress)
			str_format(aValue, sizeof(aValue), "%.1f%%", std::clamp(Progress, 0.0f, 1.0f) * 100.0f);
		else
			str_copy(aValue, "--");
		const ColorRGBA PreviousColor = TextRender.GetTextColor();
		TextRender.TextColor(0.78f, 0.82f, 0.88f, Alpha);
		TextRender.Text(Layout.m_Label.x, Layout.m_Label.y, FontSize, pLabel);
		TextRender.TextColor(0.95f, 0.97f, 1.0f, Alpha);
		const float ValueWidth = TextRender.TextWidth(FontSize, aValue);
		TextRender.Text(Layout.m_Value.x + Layout.m_Value.w - ValueWidth, Layout.m_Value.y, FontSize, aValue);
		TextRender.TextColor(PreviousColor);
	}
}
