// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmIslandSurface.h"

#include "UiSurface.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace qm_island
{
	namespace
	{
		// 轮廓环兜底的采样上限：环宽 2px 左右时 192 段已经看不出折线。
		constexpr int MAX_OUTLINE_RING_SEGMENTS = 192;

		// 环绕倒计时条的几何兜底：无 SDF 时用内外半径环带近似圆环（使用 Thickness）。
		// 与 HUD 的 DrawMediaIslandArcGeometry 同语义（-90° 起顺时针、按进度覆盖）。
		void DrawRingFallback(IGraphics *pGraphics, vec2 Center, float Radius, float Thickness, float Progress, ColorRGBA Color)
		{
			if(pGraphics == nullptr || Radius <= 0.0f || Thickness <= 0.0f || Color.a <= 0.0f)
				return;

			constexpr float Pi = 3.14159265359f;
			constexpr int MaxSegments = 64;
			const float SafeProgress = std::clamp(Progress, 0.0f, 1.0f);
			if(SafeProgress <= 0.0f)
				return;

			const float OuterRadius = Radius + Thickness * 0.5f;
			const float InnerRadius = std::max(0.0f, Radius - Thickness * 0.5f);
			const int NumSegments = std::max(1, (int)std::ceil((float)MaxSegments * SafeProgress));
			const float Sweep = 2.0f * Pi * SafeProgress;
			std::array<IGraphics::CFreeformItem, MaxSegments> aSegments;
			for(int i = 0; i < NumSegments; ++i)
			{
				const float Angle0 = Sweep * (float)i / (float)NumSegments;
				const float Angle1 = Sweep * (float)(i + 1) / (float)NumSegments;
				const vec2 Direction0(std::sin(Angle0), -std::cos(Angle0));
				const vec2 Direction1(std::sin(Angle1), -std::cos(Angle1));
				aSegments[i] = IGraphics::CFreeformItem(
					Center + Direction0 * OuterRadius,
					Center + Direction1 * OuterRadius,
					Center + Direction1 * InnerRadius,
					Center + Direction0 * InnerRadius);
			}

			pGraphics->TextureClear();
			pGraphics->QuadsBegin();
			pGraphics->SetColor(Color);
			pGraphics->QuadsDrawFreeform(aSegments.data(), NumSegments);
			pGraphics->QuadsEnd();
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
