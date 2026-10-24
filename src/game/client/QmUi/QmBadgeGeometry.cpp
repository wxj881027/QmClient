#include "QmBadgeGeometry.h"

#include "QmBadgeGeometryData.h"

#include <game/client/ui_rect.h>

#include <iterator>

SQmBadgeGeometryMesh QmBadgeGeometryMesh(EQmBadgeGeometry Kind)
{
	switch(Kind)
	{
	case EQmBadgeGeometry::DEVELOPER: return {g_aQmBadgeDeveloper, std::size(g_aQmBadgeDeveloper), {91, 24}};
	case EQmBadgeGeometry::LOGO: return {g_aQmBadgeLogo, std::size(g_aQmBadgeLogo), {16, 16}};
	case EQmBadgeGeometry::MOON_LEFT: return {g_aQmBadgeMoonLeft, std::size(g_aQmBadgeMoonLeft), {40, 24}};
	case EQmBadgeGeometry::MOON_MIDDLE: return {g_aQmBadgeMoonMiddle, std::size(g_aQmBadgeMoonMiddle), {45, 24}};
	case EQmBadgeGeometry::MOON_RIGHT: return {g_aQmBadgeMoonRight, std::size(g_aQmBadgeMoonRight), {32, 24}};
	case EQmBadgeGeometry::SUN_LEFT: return {g_aQmBadgeSunLeft, std::size(g_aQmBadgeSunLeft), {40, 24}};
	case EQmBadgeGeometry::SUN_MIDDLE: return {g_aQmBadgeSunMiddle, std::size(g_aQmBadgeSunMiddle), {45, 24}};
	case EQmBadgeGeometry::SUN_RIGHT: return {g_aQmBadgeSunRight, std::size(g_aQmBadgeSunRight), {32, 24}};
	}
	return {};
}

SQmBadgeClippedQuads QmClipBadgeGeometry(const IGraphics::CFreeformItem &Quad, float Right)
{
	const vec2 aInput[] = {{Quad.m_X0, Quad.m_Y0}, {Quad.m_X1, Quad.m_Y1}, {Quad.m_X3, Quad.m_Y3}, {Quad.m_X2, Quad.m_Y2}};
	std::array<vec2, 6> aPoints;
	int Count = 0;
	vec2 Previous = aInput[3];
	for(const vec2 Current : aInput)
	{
		if((Previous.x < Right) != (Current.x < Right))
			aPoints[Count++] = mix(Previous, Current, (Right - Previous.x) / (Current.x - Previous.x));
		if(Current.x < Right)
			aPoints[Count++] = Current;
		Previous = Current;
	}
	SQmBadgeClippedQuads Result;
	for(int Index = 1; Index + 1 < Count; ++Index)
		Result.m_aQuads[Result.m_Count++] = IGraphics::CFreeformItem(aPoints[0], aPoints[Index], aPoints[Index + 1], aPoints[Index + 1]);
	return Result;
}

void QmDrawBadgeCapsuleGeometry(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Background, ColorRGBA Border)
{
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	pGraphics->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Border, IGraphics::CORNER_ALL, Rect.h * 0.5f);
	CUIRect Inside;
	Rect.Margin(std::min(Rect.w, Rect.h) / 24.0f, &Inside);
	pGraphics->DrawRect(Inside.x, Inside.y, Inside.w, Inside.h, Background, IGraphics::CORNER_ALL, Inside.h * 0.5f);
}

void CQmBadgeGeometry::Shutdown(IGraphics *pGraphics) const
{
	if(m_Container >= 0 && m_GraphicsVersion == pGraphics->GraphicsResourcesResetVersion())
		pGraphics->DeleteQuadContainer(m_Container);
	m_Container = -1;
	m_vRanges.clear();
}

void CQmBadgeGeometry::Init(IGraphics *pGraphics) const
{
	Shutdown(pGraphics);
	m_GraphicsVersion = pGraphics->GraphicsResourcesResetVersion();
	const auto Mesh = QmBadgeGeometryMesh(m_Kind);
	if(Mesh.m_Count == 0)
		return;
	m_Container = pGraphics->CreateQuadContainer(false);
	if(m_Container < 0)
		return;
	pGraphics->SetColor(1, 1, 1, 1);
	for(size_t Index = 0; Index < Mesh.m_Count; ++Index)
	{
		const auto &Quad = Mesh.m_pQuads[Index];
		const ColorRGBA Color = ColorRGBA(Quad.m_Color).WithAlpha(Quad.m_Alpha);
		// 旧后端的容器绘制覆盖顶点色，按连续配色分段后两种后端使用相同颜色。
		if(m_vRanges.empty() || m_vRanges.back().m_Color != Color)
			m_vRanges.push_back({static_cast<int>(Index), 0, Color});
		++m_vRanges.back().m_Count;
		auto Item = Quad.m_Quad;
		pGraphics->QuadContainerAddQuads(m_Container, &Item, 1);
	}
	pGraphics->QuadContainerUpload(m_Container);
	pGraphics->QuadContainerChangeAutomaticUpload(m_Container, true);
	pGraphics->SetColor(1, 1, 1, 1);
}

void CQmBadgeGeometry::Render(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Tint, float DesignClipWidth) const
{
	if(Rect.w <= 0.0f || Rect.h <= 0.0f || Tint.a <= 0.0f || DesignClipWidth == 0.0f)
		return;
	if(m_GraphicsVersion != pGraphics->GraphicsResourcesResetVersion())
		Init(pGraphics);
	if(m_Container < 0)
		return;
	const auto Mesh = QmBadgeGeometryMesh(m_Kind);
	pGraphics->TextureClear();
	pGraphics->QuadsSetRotation(0.0f);
	const vec2 Scale(Rect.w / Mesh.m_Size.x, Rect.h / Mesh.m_Size.y);
	const bool Clip = DesignClipWidth > 0.0f && DesignClipWidth < Mesh.m_Size.x;
	if(Clip)
		pGraphics->QuadsBegin();
	for(const auto &Range : m_vRanges)
	{
		pGraphics->SetColor(ColorRGBA(Range.m_Color.r * Tint.r, Range.m_Color.g * Tint.g, Range.m_Color.b * Tint.b, Range.m_Color.a * Tint.a));
		if(!Clip)
		{
			pGraphics->RenderQuadContainerEx(m_Container, Range.m_First, Range.m_Count, Rect.x, Rect.y, Scale.x, Scale.y);
			continue;
		}
		// 极短头衔只裁掉左侧图案，不把小行星压扁；使用固定栈缓冲，不按帧分配。
		std::array<IGraphics::CFreeformItem, 48> aBatch;
		int BatchCount = 0;
		for(int Index = Range.m_First; Index < Range.m_First + Range.m_Count; ++Index)
		{
			const auto Parts = QmClipBadgeGeometry(Mesh.m_pQuads[Index].m_Quad, DesignClipWidth);
			for(int Part = 0; Part < Parts.m_Count; ++Part)
			{
				const auto &Q = Parts.m_aQuads[Part];
				aBatch[BatchCount++] = IGraphics::CFreeformItem(Rect.x + Q.m_X0 * Scale.x, Rect.y + Q.m_Y0 * Scale.y,
					Rect.x + Q.m_X1 * Scale.x, Rect.y + Q.m_Y1 * Scale.y, Rect.x + Q.m_X2 * Scale.x, Rect.y + Q.m_Y2 * Scale.y,
					Rect.x + Q.m_X3 * Scale.x, Rect.y + Q.m_Y3 * Scale.y);
				if(BatchCount == static_cast<int>(aBatch.size()))
				{
					pGraphics->QuadsDrawFreeform(aBatch.data(), BatchCount);
					BatchCount = 0;
				}
			}
		}
		if(BatchCount > 0)
			pGraphics->QuadsDrawFreeform(aBatch.data(), BatchCount);
	}
	if(Clip)
		pGraphics->QuadsEnd();
	pGraphics->SetColor(1, 1, 1, 1);
}
