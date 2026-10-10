#ifndef GAME_CLIENT_COMPONENTS_QM_TOOLTIP_TEXT_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QM_TOOLTIP_TEXT_LAYOUT_H

#include <base/str.h>

#include <engine/textrender.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

// 测量与生成使用同一逻辑坐标；动画只变换已生成的字形顶点。
class CQmTooltipRenderFlags
{
	ITextRender &m_TextRender;
	unsigned m_Previous;
	ColorRGBA m_PreviousColor;
	EFontPreset m_PreviousPreset;

public:
	explicit CQmTooltipRenderFlags(ITextRender &TextRender) :
		m_TextRender(TextRender), m_Previous(TextRender.GetRenderFlags()), m_PreviousColor(TextRender.GetTextColor()), m_PreviousPreset(TextRender.GetFontPreset())
	{
		// 不继承图标、名牌和特殊 bearing 状态，所有共享 Tooltip 使用普通文字。
		TextRender.SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender.SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_ONE_TIME_USE);
		// 容器顶点使用白色，最终配置色不受调用者临时文字色或透明度影响。
		TextRender.TextColor(ColorRGBA(1, 1, 1, 1));
	}
	~CQmTooltipRenderFlags()
	{
		m_TextRender.SetRenderFlags(m_Previous);
		m_TextRender.SetFontPreset(m_PreviousPreset);
		m_TextRender.TextColor(m_PreviousColor);
	}
};

struct SQmTooltipTextLayout
{
	float m_FontSize = 0.0f;
	float m_LineWidth = 0.0f;
	float m_LineHeight = 0.0f;
	float m_Padding = 0.0f;
	float m_TextOffsetX = 0.0f;
	float m_TextOffsetY = 0.0f;
	vec2 m_Size{};
	int m_LineCount = 0;
	int m_VisibleLines = 0;
	bool m_Truncated = false;
};

inline SQmTooltipTextLayout QmTooltipMeasureText(ITextRender &TextRender, const char *pText, float FontSize, float WidthLimit, vec2 AvailableSize, float Padding)
{
	SQmTooltipTextLayout Layout;
	Layout.m_FontSize = std::max(1.0f, FontSize);
	// 留出字形描边；极小视口仍保留有效内容区，不制造负宽度。
	Layout.m_Padding = std::min(std::max(Padding, Layout.m_FontSize * 0.15f), std::max(0.0f, std::min(AvailableSize.x, AvailableSize.y) * 0.25f));
	Layout.m_LineWidth = std::min(std::max(1.0f, WidthLimit), std::max(0.0f, AvailableSize.x - 2.0f * Layout.m_Padding));
	if(Layout.m_LineWidth <= 0.0f || AvailableSize.y <= 0.0f)
		return Layout;
	CQmTooltipRenderFlags Flags(TextRender);
	CTextCursor Cursor;
	Cursor.m_Flags = 0;
	Cursor.m_FontSize = Layout.m_FontSize;
	Cursor.m_LineWidth = Layout.m_LineWidth;
	Cursor.m_CalculateVisualBoundingBox = true;
	TextRender.TextEx(&Cursor, pText, -1);
	// 对齐后的行高仅用于容量，不作为下一次字号输入再次取整。
	Layout.m_LineHeight = std::max(0.0001f, Cursor.m_AlignedFontSize);
	Layout.m_LineCount = Cursor.m_LineCount;
	// 保留逻辑断行宽度，只平移实际填充范围，左右描边余量由 Padding 统一负责。
	Layout.m_TextOffsetX = Cursor.m_HasVisualBoundingBox ? -Cursor.m_VisualLeft : 0.0f;
	const float VisualWidth = Cursor.m_HasVisualBoundingBox ? std::max(0.0f, Cursor.m_VisualRight - Cursor.m_VisualLeft) : 0.0f;
	Layout.m_TextOffsetY = Cursor.m_HasVisualBoundingBox ? std::max(0.0f, -Cursor.m_VisualTop) : 0.0f;
	const float BottomOverhang = Cursor.m_HasVisualBoundingBox ? std::max(0.0f, Cursor.m_VisualBottom - Cursor.Height()) : 0.0f;
	const float ContentHeight = std::max(0.0f, AvailableSize.y - 2.0f * Layout.m_Padding - Layout.m_TextOffsetY - BottomOverhang);
	// 比较离散行数，避免完整一行因浮点高度误差被省略。
	const int Capacity = std::max(0, static_cast<int>(std::floor(ContentHeight / Layout.m_LineHeight + 0.0001f)));
	Layout.m_VisibleLines = std::min(Layout.m_LineCount, Capacity);
	Layout.m_Truncated = Layout.m_LineCount > Capacity || Cursor.m_CharCount < str_length(pText) || Cursor.m_LongestLineWidth > Layout.m_LineWidth + 0.0001f;
	const int Lines = std::min(Layout.m_LineCount, Layout.m_VisibleLines);
	Layout.m_Size = vec2(
		std::min(AvailableSize.x, (Layout.m_Truncated ? std::max(Layout.m_LineWidth, VisualWidth) : VisualWidth) + 2.0f * Layout.m_Padding),
		std::min(AvailableSize.y, Lines * Layout.m_LineHeight + Layout.m_TextOffsetY + BottomOverhang + 2.0f * Layout.m_Padding));
	return Layout;
}

inline CTextCursor QmTooltipTextCursor(const CUIRect &Content, float FontSize, float WrapWidth, int MaxLines)
{
	CTextCursor Cursor;
	Cursor.SetPosition(Content.TopLeft());
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = std::max(1.0f, WrapWidth);
	Cursor.m_MaxLines = MaxLines;
	Cursor.m_CalculateVisualBoundingBox = true;
	return Cursor;
}

inline CTextCursor QmTooltipCreateText(ITextRender &TextRender, const SQmTooltipTextLayout &Layout, const char *pText, vec2 Position, STextContainerIndex &Index, bool Ellipsis = false)
{
	CQmTooltipRenderFlags Flags(TextRender);
	CTextCursor Cursor = QmTooltipTextCursor({Position.x, Position.y, 0, 0}, Layout.m_FontSize, Layout.m_LineWidth, Layout.m_Truncated ? std::max(1, Layout.m_VisibleLines - 1) : 0);
	if(Ellipsis)
	{
		// 仅省略标记在不足一个字宽的视口中缩小，正文逻辑字号保持固定。
		const float MarkerWidth = TextRender.TextWidth(Layout.m_FontSize, pText, -1, std::max(Layout.m_LineWidth, Layout.m_FontSize * 4.0f), TEXTFLAG_DISALLOW_NEWLINE);
		if(MarkerWidth > Layout.m_LineWidth)
			Cursor.m_FontSize = std::max(1.0f, std::floor(Layout.m_FontSize * Layout.m_LineWidth / MarkerWidth));
		Cursor.m_Flags |= TEXTFLAG_DISALLOW_NEWLINE | TEXTFLAG_STOP_AT_END;
		Cursor.m_MaxLines = 1;
	}
	else if(Layout.m_Truncated)
		Cursor.m_MaxLines = std::max(1, Layout.m_VisibleLines - 1);
	if(Layout.m_VisibleLines > 0 && (Ellipsis || !Layout.m_Truncated || Layout.m_VisibleLines > 1))
		TextRender.CreateTextContainer(Index, &Cursor, pText);
	return Cursor;
}

// 宽高共同约束统一比例，尺寸过渡中完整目标布局也必须落在当前气泡内。
inline float QmTooltipTextScale(const CUIRect &LayoutRect, const CUIRect &RenderRect)
{
	if(LayoutRect.w <= 0.0f || LayoutRect.h <= 0.0f || RenderRect.w <= 0.0f || RenderRect.h <= 0.0f)
		return 0.0f;
	return std::min(RenderRect.w / LayoutRect.w, RenderRect.h / LayoutRect.h);
}

// 逆向映射投影，使固定逻辑顶点与气泡围绕同一中心缩放。
inline CUIRect QmTooltipTextProjection(const CUIRect &Projection, vec2 Center, float Scale)
{
	if(Scale <= 0.0f || !std::isfinite(Scale))
		return Projection;
	return {Center.x + (Projection.x - Center.x) / Scale, Center.y + (Projection.y - Center.y) / Scale,
		Projection.w / Scale, Projection.h / Scale};
}

// 字形使用目标布局，移动和缩放后的气泡中心只影响投影，不重新断行。
inline CUIRect QmTooltipTextProjection(const CUIRect &Projection, const CUIRect &LayoutRect, const CUIRect &RenderRect)
{
	const float Scale = QmTooltipTextScale(LayoutRect, RenderRect);
	if(Scale <= 0.0f || !std::isfinite(Scale))
		return Projection;
	CUIRect Result = QmTooltipTextProjection(Projection, LayoutRect.Center(), Scale);
	Result.x += (LayoutRect.Center().x - RenderRect.Center().x) / Scale;
	Result.y += (LayoutRect.Center().y - RenderRect.Center().y) / Scale;
	return Result;
}

#endif
