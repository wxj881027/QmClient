// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmIslandSurface.h"

#include "UiSurface.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace qm_island
{
	namespace
	{
		// 轮廓环兜底的采样上限：环宽 2px 左右时 192 段已经看不出折线。
		constexpr int MAX_OUTLINE_RING_SEGMENTS = 192;

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

		// 按矩形等距外扩：外扩 d 的圆角矩形就是原轮廓的等距偏移（直边外移 d、圆角半径 +d）。
		CUIRect InflateIslandRect(const CUIRect &Rect, float Amount)
		{
			return {Rect.x - Amount, Rect.y - Amount, Rect.w + Amount * 2.0f, Rect.h + Amount * 2.0f};
		}

		// 一轮轮廓环采样：把内外沿对应点连成四边形，按周长进度截断。
		void DrawOutlineRingPass(IGraphics *pGraphics, const CUIRect &InnerRect, float InnerRadius, const CUIRect &OuterRect, float OuterRadius, int Segments, float Progress, ColorRGBA Color)
		{
			Progress = std::clamp(Progress, 0.0f, 1.0f);
			if(pGraphics == nullptr || Segments <= 0 || Progress <= 0.0f || Color.a <= 0.0f)
				return;

			const int DrawnSegments = std::max(1, (int)std::ceil((float)Segments * Progress));
			std::array<IGraphics::CFreeformItem, MAX_OUTLINE_RING_SEGMENTS> aSegments;
			for(int i = 0; i < DrawnSegments; ++i)
			{
				const float Fraction0 = Progress * (float)i / (float)DrawnSegments;
				const float Fraction1 = Progress * (float)(i + 1) / (float)DrawnSegments;
				aSegments[i] = IGraphics::CFreeformItem(
					RoundedRectPerimeterPoint(OuterRect, OuterRadius, Fraction0),
					RoundedRectPerimeterPoint(OuterRect, OuterRadius, Fraction1),
					RoundedRectPerimeterPoint(InnerRect, InnerRadius, Fraction1),
					RoundedRectPerimeterPoint(InnerRect, InnerRadius, Fraction0));
			}

			pGraphics->TextureClear();
			pGraphics->QuadsBegin();
			pGraphics->SetColor(Color);
			pGraphics->QuadsDrawFreeform(aSegments.data(), DrawnSegments);
			pGraphics->QuadsEnd();
		}

		// 轮廓环兜底：贴着主体外轮廓绕一圈。参数语义与 SDF 路径一致
		// （厚度 / 中心线相对轮廓外扩 / 颜色与进度取第 0 个 item），只是没有逐像素抗锯齿。
		void DrawOutlineRingFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)
		{
			if(pGraphics == nullptr || State.m_OutlineRingThickness <= 0.0f || State.m_ItemCount <= 0 || State.m_MainRect.w <= 0.0f || State.m_MainRect.h <= 0.0f)
				return;

			const SHudMediaIslandSdfItem &Item = State.m_Items[0];
			if(Item.m_ContentAlpha <= 0.001f)
				return;

			const float Thickness = State.m_OutlineRingThickness;
			const float CenterOffset = std::max(0.0f, State.m_OutlineRingOffset);
			const float MaxInnerOffset = std::max(0.0f, std::min(State.m_MainRect.w, State.m_MainRect.h) * 0.5f - 1.0f);
			const float InnerOffset = std::clamp(CenterOffset - Thickness * 0.5f, 0.0f, MaxInnerOffset);
			const float OuterOffset = std::max(InnerOffset, CenterOffset + Thickness * 0.5f);
			const CUIRect InnerRect = InflateIslandRect(State.m_MainRect, InnerOffset);
			const CUIRect OuterRect = InflateIslandRect(State.m_MainRect, OuterOffset);
			const float InnerRadius = std::max(0.0f, State.m_MainRadius + InnerOffset);
			const float OuterRadius = std::max(0.0f, State.m_MainRadius + OuterOffset);
			const float Perimeter = RoundedRectPerimeterLength(OuterRect, OuterRadius);
			if(Perimeter <= 0.0f)
				return;

			const int Segments = std::clamp((int)(Perimeter * 0.5f), 24, MAX_OUTLINE_RING_SEGMENTS);
			// 整圈底色 18%，进度弧覆盖在上面 —— 与圆形环同一层次。
			DrawOutlineRingPass(pGraphics, InnerRect, InnerRadius, OuterRect, OuterRadius, Segments, 1.0f, Item.m_RingColor.WithMultipliedAlpha(0.18f * Item.m_ContentAlpha));
			DrawOutlineRingPass(pGraphics, InnerRect, InnerRadius, OuterRect, OuterRadius, Segments, Item.m_CountdownProgress, Item.m_RingColor.WithMultipliedAlpha(Item.m_ContentAlpha));
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

				// 轮廓环模式下环不属于任何单个 item，统一在循环外画。
				if(Item.m_ContentAlpha > 0.001f && State.m_OutlineRingThickness <= 0.0f)
				{
					const float RingRadius = State.m_RingRadius * Item.m_ContentScale;
					const float RingThickness = State.m_RingThickness * Item.m_ContentScale;
					// 整圈底色 18%，进度弧覆盖在上面 —— 与 HUD 卫星环同一层次。
					DrawRingFallback(pGraphics, Item.m_Center, RingRadius, RingThickness, 1.0f, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * 0.18f * Item.m_ContentAlpha));
					DrawRingFallback(pGraphics, Item.m_Center, RingRadius, RingThickness, Item.m_CountdownProgress, Item.m_RingColor.WithAlpha(Item.m_RingColor.a * Item.m_ContentAlpha));
				}
			}

			if(State.m_OutlineRingThickness > 0.0f)
				DrawOutlineRingFallback(pGraphics, State);

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
