#include "QmAuthorTee.h"

#include <game/client/components/qmclient/qm_sponsor_authors.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <algorithm>

void QmAuthorTee::Render(CUi *pUi, CRenderTools *pRenderTools, const CAnimState *pAnim, const CTeeRenderInfo &SkinInfo, int Emote, bool Cute, vec2 Position, const CUIRect &Bounds, const QmSponsorAuthors::STeePresentation &Presentation)
{
	if(pUi->DrawingSuppressed() || Bounds.w <= 0.0f || Bounds.h <= 0.0f || SkinInfo.m_Size <= 0.0f)
		return;
	// 所有通道复用同一眼睛朝向，色差只来自故障偏移。
	vec2 Direction(1.0f, 0.0f);
	if(Cute)
	{
		Direction = (pUi->MousePos() - Position) / SkinInfo.m_Size;
		const float Length = length(Direction);
		if(Length > 1.0f)
			Direction /= Length;
		if(Length < 0.4f)
			Emote = 2;
	}
	if(!Presentation.m_Glitch)
	{
		pRenderTools->RenderTee(pAnim, &SkinInfo, Emote, Direction, Position);
		return;
	}
	pUi->ClipEnable(&Bounds);
	CTeeRenderInfo Tinted = SkinInfo;
	const auto DrawTinted = [&](float OffsetX, ColorRGBA Tint, float Alpha) {
		Tinted.m_ColorBody = SkinInfo.m_ColorBody.Multiply(Tint);
		Tinted.m_ColorFeet = SkinInfo.m_ColorFeet.Multiply(Tint);
		pRenderTools->RenderTee(pAnim, &Tinted, Emote, Direction, Position + vec2(OffsetX * SkinInfo.m_Size, 0.0f), Alpha);
	};
	// 先画青色与洋红色重影，再画切开的原色皮肤，保留轮廓边缘的色差。
	DrawTinted(-Presentation.m_ChromaOffset, ColorRGBA(0.08f, 1.0f, 1.0f, 1.0f), Presentation.m_ChromaAlpha);
	DrawTinted(Presentation.m_ChromaOffset, ColorRGBA(1.0f, 0.08f, 0.75f, 1.0f), Presentation.m_ChromaAlpha);
	for(size_t Index = 0; Index < Presentation.m_aSliceOffsets.size(); ++Index)
	{
		const float Top = Bounds.y + Bounds.h * Index / Presentation.m_aSliceOffsets.size();
		const float Bottom = Bounds.y + Bounds.h * (Index + 1) / Presentation.m_aSliceOffsets.size();
		const CUIRect Slice{Bounds.x, Top, Bounds.w, Bottom - Top};
		pUi->ClipEnable(&Slice);
		pRenderTools->RenderTee(pAnim, &SkinInfo, Emote, Direction, Position + vec2(Presentation.m_aSliceOffsets[Index] * SkinInfo.m_Size, 0.0f));
		pUi->ClipDisable();
	}
	// 扫描线只重画皮肤自身的像素，不在透明背景或名字上铺色块。
	const CUIRect Scanline{Bounds.x, Bounds.y + Bounds.h * Presentation.m_ScanlineY, Bounds.w, Bounds.h * Presentation.m_ScanlineHeight};
	pUi->ClipEnable(&Scanline);
	const size_t ScanSlice = std::min(static_cast<size_t>(Presentation.m_ScanlineY * Presentation.m_aSliceOffsets.size()), Presentation.m_aSliceOffsets.size() - 1);
	DrawTinted(Presentation.m_aSliceOffsets[ScanSlice], ColorRGBA(0.35f, 1.0f, 1.0f, 1.0f), Presentation.m_ScanlineAlpha);
	pUi->ClipDisable();
	pUi->ClipDisable();
}
