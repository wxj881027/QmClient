// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmIslandSurface.h"

#include "UiSurface.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace qm_island
{
	namespace
	{
		// 环绕倒计时条的几何兜底：无 SDF 时用粗线段近似圆环。
		// 与 HUD 的 DrawMediaIslandArcGeometry 同语义（-90° 起顺时针、按进度覆盖），
		// 但这里按周长离散成线段，因此不需要 HUD 那份圆形专用几何。
		void DrawRingFallback(IGraphics *pGraphics, vec2 Center, float Radius, float Thickness, float Progress, ColorRGBA Color)
		{
			if(pGraphics == nullptr || Radius <= 0.0f || Thickness <= 0.0f || Color.a <= 0.0f)
				return;

			constexpr float Pi = 3.14159265359f;
			const float SafeProgress = std::clamp(Progress, 0.0f, 1.0f);
			if(SafeProgress <= 0.0f)
				return;

			// 段数随半径增长，避免大环上出现可见折线；上限控制顶点开销。
			const int Segments = std::clamp((int)(Radius * 0.6f), 16, 192);
			const int DrawnSegments = std::max(1, (int)std::ceil((float)Segments * SafeProgress));

			std::vector<IGraphics::CLineItem> vLines;
			vLines.reserve((size_t)DrawnSegments);
			for(int i = 0; i < DrawnSegments; ++i)
			{
				const float T0 = (float)i / (float)Segments;
				const float T1 = (float)(i + 1) / (float)Segments;
				// 从正上方开始顺时针：内部角度以 -90° 为起点。
				const float A0 = -Pi * 0.5f + T0 * 2.0f * Pi * SafeProgress;
				const float A1 = -Pi * 0.5f + T1 * 2.0f * Pi * SafeProgress;
				vLines.emplace_back(
					Center.x + std::cos(A0) * Radius,
					Center.y + std::sin(A0) * Radius,
					Center.x + std::cos(A1) * Radius,
					Center.y + std::sin(A1) * Radius);
			}

			pGraphics->TextureClear();
			pGraphics->LinesBegin();
			pGraphics->SetColor(Color);
			pGraphics->LinesDraw(vLines.data(), vLines.size());
			pGraphics->LinesEnd();
		}

		// IGraphics 只有一个重载可直调（接受 SRoundedSurfaceParams），
		// 其余重载要 CUi / IUiContext；这里按 IGraphics 构造参数。
		void DrawIslandRect(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Color, float Radius, int Corners)
		{
			SRoundedSurfaceParams Params;
			Params.m_Radius = std::max(0.0f, Radius);
			Params.m_Corners = Corners;
			DrawRoundedSurface(pGraphics, Rect, Color, ColorRGBA(), Params);
		}

		// 无 SDF 时的整块几何兜底：主体胶囊 + 各附加项 + 环绕环。
		void RenderGeometryFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)
		{
			if(pGraphics == nullptr || State.m_MainRect.w <= 0.0f || State.m_MainRect.h <= 0.0f)
				return;

			DrawIslandRect(pGraphics, State.m_MainRect, State.m_BackgroundColor, State.m_MainRadius, State.m_MainCorners);

			for(int i = 0; i < State.m_ItemCount; ++i)
			{
				const SHudMediaIslandSdfItem &Item = State.m_Items[i];
				if(Item.m_Radii.x <= 0.0f || Item.m_Radii.y <= 0.0f)
					continue;

				const CUIRect ItemRect = {Item.m_Center.x - Item.m_Radii.x, Item.m_Center.y - Item.m_Radii.y, Item.m_Radii.x * 2.0f, Item.m_Radii.y * 2.0f};
				const float ItemRadius = std::max(0.0f, std::min(Item.m_Radii.x, Item.m_Radii.y));
				DrawIslandRect(pGraphics, ItemRect, State.m_BackgroundColor, ItemRadius, IGraphics::CORNER_ALL);

				if(Item.m_ContentAlpha > 0.001f)
				{
					const float RingRadius = State.m_RingRadius * Item.m_ContentScale;
					const float RingThickness = State.m_RingThickness * Item.m_ContentScale;
					// 整圈底色 18%，进度弧覆盖在上面 —— 与 HUD 卫星环同一层次。
					DrawRingFallback(pGraphics, Item.m_Center, RingRadius, RingThickness, 1.0f, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * 0.18f * Item.m_ContentAlpha));
					DrawRingFallback(pGraphics, Item.m_Center, RingRadius, RingThickness, Item.m_CountdownProgress, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * Item.m_ContentAlpha));
				}
			}

			if(State.m_HasRightCapsule && State.m_RightCapsule.m_Rect.w > 0.0f && State.m_RightCapsule.m_Rect.h > 0.0f)
				DrawIslandRect(pGraphics, State.m_RightCapsule.m_Rect, State.m_BackgroundColor, State.m_RightCapsule.m_Radius, IGraphics::CORNER_ALL);
		}
	} // namespace

	bool Render(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &SdfState, IGraphics::CRenderTargetHandle Backdrop)
	{
		if(pGraphics == nullptr)
			return false;

		IGraphics::SMediaIslandSdfParams GpuSdfParams;
		if(!QmHudMediaIslandBuildGpuSdfParams(SdfState, GpuSdfParams))
			return false;

		if(pGraphics->HasMediaIslandSdf())
		{
			pGraphics->RenderMediaIslandSdf(GpuSdfParams, Backdrop);
			return true;
		}

		RenderGeometryFallback(pGraphics, SdfState);
		return true;
	}
} // namespace qm_island
