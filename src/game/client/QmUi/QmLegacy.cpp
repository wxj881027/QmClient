/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmLegacy.h"

CUIRect CUiV2LegacyAdapter::ToCUIRect(const SUiLayoutBox &Box)
{
	CUIRect Rect;
	Rect.x = Box.m_X;
	Rect.y = Box.m_Y;
	Rect.w = Box.m_W;
	Rect.h = Box.m_H;
	return Rect;
}

SUiLayoutBox CUiV2LegacyAdapter::FromCUIRect(const CUIRect &Rect)
{
	SUiLayoutBox Box;
	Box.m_X = Rect.x;
	Box.m_Y = Rect.y;
	Box.m_W = Rect.w;
	Box.m_H = Rect.h;
	return Box;
}
