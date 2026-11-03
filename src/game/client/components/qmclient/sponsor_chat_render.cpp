#include "sponsor_chat_render.h"

#include <engine/graphics.h>

void QmSponsorChatAddPlatinumSplits(CTextCursor &Cursor, const char *pText, float Alpha)
{
	int Count = 0;
	for(const char *pCurrent = pText; *pCurrent;)
	{
		const int Codepoint = str_utf8_decode(&pCurrent);
		if(Codepoint <= 0)
			break;
		if(Codepoint != '\n')
			++Count;
	}
	if(Count == 0)
		return;
	const int Start = Cursor.m_CharCount;
	int Character = 0;
	for(const char *pCurrent = pText; *pCurrent;)
	{
		const char *pNext = pCurrent;
		const int Codepoint = str_utf8_decode(&pNext);
		if(Codepoint <= 0)
			break;
		if(Codepoint != '\n')
		{
			const float Position = Count > 1 ? float(Character) / float(Count - 1) : 0.64f;
			Cursor.m_vColorSplits.emplace_back(Start + int(pCurrent - pText), int(pNext - pCurrent), QmSponsorChatPlatinumColor(Position, Alpha));
			++Character;
		}
		pCurrent = pNext;
	}
}

void QmRenderSponsorChatText(IGraphics *pGraphics, ITextRender *pTextRender, STextContainerIndex Index, EQmSponsorChatStyle Style, float Alpha, float X, float Y, float FontSize, const SQmSponsorChatSweep &SweepState, double TimeSeconds)
{
	if(!Index.Valid() || Alpha <= 0.0f)
		return;
	const ColorRGBA Outline = Style == EQmSponsorChatStyle::NONE ? pTextRender->DefaultTextOutlineColor() :
								       ColorRGBA(0.0f, 0.0f, 0.0f, 0.82f);
	if(Style == EQmSponsorChatStyle::FLUID)
	{
		struct SFluidPaint
		{
			CQmSponsorChatFluid m_Fluid;
			float m_FontSize;
		};
		const SFluidPaint Paint{CQmSponsorChatFluid(TimeSeconds), std::max(1.0f, FontSize)};
		const auto Sample = [](vec2 Position, const void *pContext) {
			const auto &Context = *static_cast<const SFluidPaint *>(pContext);
			return Context.m_Fluid.Sample(Position / Context.m_FontSize);
		};
		pTextRender->RenderTextContainer(Index, ColorRGBA(1, 1, 1, 0), Outline.WithMultipliedAlpha(Alpha), X, Y);
		pTextRender->RenderTextContainerFill(Index, Sample, &Paint, 4, 4, ColorRGBA(1, 1, 1, Alpha), X, Y);
		return;
	}
	pTextRender->RenderTextContainer(Index, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha), Outline.WithMultipliedAlpha(Alpha), X, Y);
	if(Style == EQmSponsorChatStyle::PLATINUM && SweepState.m_Line >= 0)
	{
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const float PixelWidth = (ScreenX1 - ScreenX0) / std::max(1, pGraphics->ScreenWidth());
		STextSweepParams Sweep;
		Sweep.m_Line = SweepState.m_Line;
		Sweep.m_Progress = SweepState.m_Progress;
		Sweep.m_HalfWidth = std::max(FontSize * 0.75f, PixelWidth * 2.0f);
		Sweep.m_Slant = 0.28f;
		Sweep.m_Color = ColorRGBA(1.0f, 1.0f, 0.97f, 0.94f * Alpha);
		pTextRender->RenderTextContainerSweep(Index, Sweep, X, Y);
	}
}
