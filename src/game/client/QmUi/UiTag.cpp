#include "UiTag.h"

#include "UiSurface.h"

#include <engine/textrender.h>

#include <game/client/ui.h>

#include <algorithm>

namespace ui_tag
{
	float MeasureWidth(ITextRender *pTextRender, const char *pText, float UiScale)
	{
		const float Scale = std::max(UiScale, 0.01f);
		if(pTextRender == nullptr || pText == nullptr)
			return 2.0f * PADDING_X * Scale;
		const float TextWidth = pTextRender->TextWidth(FONT_SIZE * Scale, pText, -1, -1.0f);
		return std::max(TextWidth, 0.0f) + 2.0f * PADDING_X * Scale;
	}

	CUIRect FitRect(const CUIRect &Slot, float Width, float UiScale)
	{
		const float Scale = std::max(UiScale, 0.01f);
		CUIRect Rect = Slot;
		Rect.w = std::clamp(Width, 2.0f * PADDING_X * Scale, std::max(Slot.w, 2.0f * PADDING_X * Scale));
		Rect.h = std::min(HEIGHT * Scale, std::max(Slot.h, 0.0f));
		Rect.y += (Slot.h - Rect.h) * 0.5f;
		return Rect;
	}

	void DrawInRect(CUi *pUi, const CUIRect &Rect, const char *pText, EColorMode Mode, const ColorRGBA &Base, const char *pKey, float Alpha, float UiScale)
	{
		if(pUi == nullptr || pText == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return;
		const float Scale = std::max(UiScale, 0.01f);
		const SColors Colors = Resolve(Mode, Base, pKey, Alpha);
		const ColorRGBA PrevColor = pUi->TextRender()->GetTextColor();
		// 只画填充：胶囊没有描边（描边会让它读成一张小卡片，抢列表卡片的层级）。
		DrawRoundedSurface(pUi, Rect, Colors.m_Fill, ColorRGBA(), std::min(RADIUS * Scale, Rect.h * 0.5f));
		SLabelProperties Props;
		Props.m_MaxWidth = (int)std::max(Rect.w - 2.0f * PADDING_X * Scale, 0.0f);
		Props.m_DisallowNewline = true;
		Props.m_EllipsisAtEnd = true;
		pUi->TextRender()->TextColor(Colors.m_Text);
		pUi->DoLabel(&Rect, pText, FONT_SIZE * Scale, TEXTALIGN_MC, Props);
		pUi->TextRender()->TextColor(PrevColor);
	}

	CUIRect Draw(CUi *pUi, const CUIRect &Slot, const char *pText, EColorMode Mode, const ColorRGBA &Base, const char *pKey, float Alpha, float UiScale)
	{
		const float Width = MeasureWidth(pUi != nullptr ? pUi->TextRender() : nullptr, pText, UiScale);
		const CUIRect Rect = FitRect(Slot, Width, UiScale);
		DrawInRect(pUi, Rect, pText, Mode, Base, pKey, Alpha, UiScale);
		return Rect;
	}
} // namespace ui_tag
