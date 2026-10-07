// 单图标标签的共享识别与布局，普通标签与缓存标签使用同一规则。
#ifndef GAME_CLIENT_QM_ICON_LABEL_H
#define GAME_CLIENT_QM_ICON_LABEL_H

#include "qm_icon.h"

#include <engine/textrender.h>

inline EQmIcon QmIconForLabel(EFontPreset Preset, const char *pText, int Length = -1)
{
	if(Preset != EFontPreset::ICON_FONT && Preset != EFontPreset::ICON_FONT_BOLD)
		return EQmIcon::COUNT;
	return CQmIconRegistry::IconFromGlyph(pText, Length);
}

inline CUIRect QmIconLabelRect(const CUIRect &Rect, float Size, int Align)
{
	const float Side = std::max(0.0f, std::min(Size, std::min(Rect.w, Rect.h)));
	CUIRect Result{Rect.x, Rect.y, Side, Side};
	if(Align & TEXTALIGN_CENTER)
		Result.x += (Rect.w - Side) * 0.5f;
	else if(Align & TEXTALIGN_RIGHT)
		Result.x += Rect.w - Side;
	if(Align & TEXTALIGN_MIDDLE)
		Result.y += (Rect.h - Side) * 0.5f;
	else if(Align & TEXTALIGN_BOTTOM)
		Result.y += Rect.h - Side;
	return Result;
}

// 组合按钮最多容纳八个图标，不分配堆内存；任一未知字形整体回退字体。
struct SQmIconLabelGlyphs
{
	std::array<EQmIcon, 8> m_aIcons{};
	int m_Count = 0;
};

inline SQmIconLabelGlyphs QmIconLabelGlyphs(EFontPreset Preset, const char *pText, int Length = -1)
{
	SQmIconLabelGlyphs Result;
	if((Preset != EFontPreset::ICON_FONT && Preset != EFontPreset::ICON_FONT_BOLD) || pText == nullptr)
		return Result;
	if(Length < 0)
		Length = str_length(pText);
	while(Length > 0)
	{
		if(Result.m_Count == static_cast<int>(Result.m_aIcons.size()))
			return {};
		const unsigned char Lead = static_cast<unsigned char>(*pText);
		const int Bytes = Lead < 0x80 ? 1 : (Lead < 0xE0 ? 2 : (Lead < 0xF0 ? 3 : 4));
		if(Bytes > Length)
			return {};
		const EQmIcon Icon = CQmIconRegistry::IconFromGlyph(pText, Bytes);
		if(Icon == EQmIcon::COUNT)
			return {};
		Result.m_aIcons[Result.m_Count++] = Icon;
		pText += Bytes;
		Length -= Bytes;
	}
	return Result;
}

#endif
