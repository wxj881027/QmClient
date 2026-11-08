/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui_rect.h"

#include "ui.h"

#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/UiSurface.h>

#include <algorithm>
#include <cmath>

IGraphics *CUIRect::ms_pGraphics = nullptr;
CUi *CUIRect::ms_pUi = nullptr;

void CUIRect::DrawRectBackdrop(int Corners, float Rounding) const
{
	if(ms_pUi != nullptr && ms_pUi->GaussianBlurScopeActive())
		ms_pUi->RenderGaussianBlur(*this, ms_pUi->GaussianBlurScopeAlpha(), Corners, Rounding);
}

namespace
{
	float CurrentPixelSize(IGraphics *pGraphics)
	{
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const int ScreenWidth = pGraphics->ScreenWidth();
		return ScreenWidth > 0 ? std::max(std::abs(ScreenX1 - ScreenX0) / ScreenWidth, 0.0001f) : 0.0001f;
	}
}

CUIRect CUIRect::Intersection(const CUIRect &Other) const
{
	CUIRect Result;
	Result.x = std::max(x, Other.x);
	Result.y = std::max(y, Other.y);
	Result.w = std::max(0.0f, std::min(x + w, Other.x + Other.w) - Result.x);
	Result.h = std::max(0.0f, std::min(y + h, Other.y + Other.h) - Result.y);
	return Result;
}

void CUIRect::HSplitMid(CUIRect *pTop, CUIRect *pBottom, float Spacing) const
{
	CUIRect r = *this;
	const float Cut = r.h / 2;
	const float HalfSpacing = Spacing / 2;

	if(pTop)
	{
		pTop->x = r.x;
		pTop->y = r.y;
		pTop->w = r.w;
		pTop->h = Cut - HalfSpacing;
	}

	if(pBottom)
	{
		pBottom->x = r.x;
		pBottom->y = r.y + Cut + HalfSpacing;
		pBottom->w = r.w;
		pBottom->h = Cut - HalfSpacing;
	}
}

void CUIRect::HSplitTop(float Cut, CUIRect *pTop, CUIRect *pBottom) const
{
	CUIRect r = *this;

	if(pTop)
	{
		pTop->x = r.x;
		pTop->y = r.y;
		pTop->w = r.w;
		pTop->h = Cut;
	}

	if(pBottom)
	{
		pBottom->x = r.x;
		pBottom->y = r.y + Cut;
		pBottom->w = r.w;
		pBottom->h = r.h - Cut;
	}
}

void CUIRect::HSplitBottom(float Cut, CUIRect *pTop, CUIRect *pBottom) const
{
	CUIRect r = *this;

	if(pTop)
	{
		pTop->x = r.x;
		pTop->y = r.y;
		pTop->w = r.w;
		pTop->h = r.h - Cut;
	}

	if(pBottom)
	{
		pBottom->x = r.x;
		pBottom->y = r.y + r.h - Cut;
		pBottom->w = r.w;
		pBottom->h = Cut;
	}
}

void CUIRect::VSplitMid(CUIRect *pLeft, CUIRect *pRight, float Spacing) const
{
	CUIRect r = *this;
	const float Cut = r.w / 2;
	const float HalfSpacing = Spacing / 2;

	if(pLeft)
	{
		pLeft->x = r.x;
		pLeft->y = r.y;
		pLeft->w = Cut - HalfSpacing;
		pLeft->h = r.h;
	}

	if(pRight)
	{
		pRight->x = r.x + Cut + HalfSpacing;
		pRight->y = r.y;
		pRight->w = Cut - HalfSpacing;
		pRight->h = r.h;
	}
}

void CUIRect::VSplitLeft(float Cut, CUIRect *pLeft, CUIRect *pRight) const
{
	CUIRect r = *this;

	if(pLeft)
	{
		pLeft->x = r.x;
		pLeft->y = r.y;
		pLeft->w = Cut;
		pLeft->h = r.h;
	}

	if(pRight)
	{
		pRight->x = r.x + Cut;
		pRight->y = r.y;
		pRight->w = r.w - Cut;
		pRight->h = r.h;
	}
}

void CUIRect::VSplitRight(float Cut, CUIRect *pLeft, CUIRect *pRight) const
{
	CUIRect r = *this;

	if(pLeft)
	{
		pLeft->x = r.x;
		pLeft->y = r.y;
		pLeft->w = r.w - Cut;
		pLeft->h = r.h;
	}

	if(pRight)
	{
		pRight->x = r.x + r.w - Cut;
		pRight->y = r.y;
		pRight->w = Cut;
		pRight->h = r.h;
	}
}

void CUIRect::Margin(vec2 Cut, CUIRect *pOtherRect) const
{
	CUIRect r = *this;

	pOtherRect->x = r.x + Cut.x;
	pOtherRect->y = r.y + Cut.y;
	pOtherRect->w = r.w - 2 * Cut.x;
	pOtherRect->h = r.h - 2 * Cut.y;
}

void CUIRect::Margin(float Cut, CUIRect *pOtherRect) const
{
	Margin(vec2(Cut, Cut), pOtherRect);
}

void CUIRect::VMargin(float Cut, CUIRect *pOtherRect) const
{
	Margin(vec2(Cut, 0.0f), pOtherRect);
}

void CUIRect::HMargin(float Cut, CUIRect *pOtherRect) const
{
	Margin(vec2(0.0f, Cut), pOtherRect);
}

bool CUIRect::Inside(vec2 Point) const
{
	return Point.x >= x && Point.x < x + w && Point.y >= y && Point.y < y + h;
}

void CUIRect::Draw(ColorRGBA Color, int Corners, float Rounding) const
{
	if(ms_pUi != nullptr && ms_pUi->DrawingSuppressed())
		return;
	if(Color.a > 0.0f && Color.a < 1.0f)
		DrawRectBackdrop(Corners, Rounding);
	if(ms_pGraphics->HasRoundedRectSdf() && Rounding > 0.0f && Corners != IGraphics::CORNER_NONE)
	{
		const float PixelSize = CurrentPixelSize(ms_pGraphics);
		SRoundedSurfaceParams Params;
		Params.m_Radius = Rounding;
		Params.m_PixelSize = PixelSize;
		Params.m_Corners = Corners;
		if(DrawRoundedSurface(ms_pGraphics, *this, Color, ColorRGBA(), Params))
			return;
	}
	ms_pGraphics->DrawRect(x, y, w, h, Color, Corners, Rounding);
}

void CUIRect::Draw4(ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, int Corners, float Rounding) const
{
	if(ms_pUi != nullptr && ms_pUi->DrawingSuppressed())
		return;
	const float MaximumAlpha = std::max({ColorTopLeft.a, ColorTopRight.a, ColorBottomLeft.a, ColorBottomRight.a});
	if(MaximumAlpha > 0.0f && (ColorTopLeft.a < 1.0f || ColorTopRight.a < 1.0f || ColorBottomLeft.a < 1.0f || ColorBottomRight.a < 1.0f))
		DrawRectBackdrop(Corners, Rounding);
	ms_pGraphics->DrawRect4(x, y, w, h, ColorTopLeft, ColorTopRight, ColorBottomLeft, ColorBottomRight, Corners, Rounding);
}

void CUIRect::DrawOutline(ColorRGBA Color) const
{
	if(ms_pUi != nullptr && ms_pUi->DrawingSuppressed())
		return;
	const IGraphics::CLineItem aArray[] = {
		IGraphics::CLineItem(x, y, x + w, y),
		IGraphics::CLineItem(x + w, y, x + w, y + h),
		IGraphics::CLineItem(x + w, y + h, x, y + h),
		IGraphics::CLineItem(x, y + h, x, y)};
	ms_pGraphics->TextureClear();
	ms_pGraphics->LinesBegin();
	ms_pGraphics->SetColor(Color);
	ms_pGraphics->LinesDraw(aArray, std::size(aArray));
	ms_pGraphics->LinesEnd();
}
