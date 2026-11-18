/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiSurface.h"

#include "QmColorGradient.h"
#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/ui.h>

#include <algorithm>

namespace
{
	// 圆角曲线走配置：0 原圆弧（旧外观），1 超椭圆，2 五次贝塞尔，3 圆锥贝塞尔。
	// 同一元素的所有绘制分支必须取同一个值，否则填充、边框、照片会各自圆角不同。
	float CornerCurveType()
	{
		// 档位直接透传给着色器与几何采样器；有效半径与曲线强度由两者各自的
		// ResolveRoundedCornerShape 用同一套公式反解，不在这里预先算。
		return (float)g_Config.m_QmUiCornerCurve;
	}

	float CornerSmoothing()
	{
		return QmEffectiveCornerSmoothing();
	}

	// 胶囊端头：0 = 不启用，非 0 = 直边肩部外鼓。着色器只按是否大于 0 判断。
	float CapsuleSeam()
	{
		return QmEffectiveCapsuleSeam();
	}

	// 无 SDF 时直接提交小数坐标网格，避免 DrawRect 再次吸附到物理像素。
	void DrawSubpixelRoundedSurface(IGraphics *pGraphics, SRoundedSurfacePlan Plan, const ColorRGBA &Fill, const ColorRGBA &Border, int Corners)
	{
		const int Curve = g_Config.m_QmUiCornerCurve;
		const float Smoothing = QmEffectiveCornerSmoothing();
		const float Seam = QmEffectiveCapsuleSeam();
		const auto aBorder = ResolveRoundedSurfaceBorderMesh(Plan, Corners, Curve, Smoothing, Seam);
		const vec2 Center(Plan.m_Rect.x + Plan.m_Rect.w * 0.5f, Plan.m_Rect.y + Plan.m_Rect.h * 0.5f);
		std::array<IGraphics::CFreeformItem, ROUNDED_SURFACE_OUTLINE_POINTS> aFill;
		for(size_t i = 0; i < aFill.size(); ++i)
			aFill[i] = IGraphics::CFreeformItem(Center, vec2(aBorder[i].m_X2, aBorder[i].m_Y2), Center, vec2(aBorder[i].m_X3, aBorder[i].m_Y3));
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Fill);
		pGraphics->QuadsDrawFreeform(aFill.data(), aFill.size());
		if(Plan.m_BorderWidth > 0.0f)
		{
			pGraphics->SetColor(Border);
			pGraphics->QuadsDrawFreeform(aBorder.data(), aBorder.size());
		}

		// 外缘一物理像素羽化；填充和边框各占自己的区域，不叠加半透明底色。
		const ColorRGBA Edge = Plan.m_BorderWidth > 0.0f ? Border : Fill;
		Plan.m_Rect.x -= Plan.m_PixelSize;
		Plan.m_Rect.y -= Plan.m_PixelSize;
		Plan.m_Rect.w += 2.0f * Plan.m_PixelSize;
		Plan.m_Rect.h += 2.0f * Plan.m_PixelSize;
		Plan.m_Radius += Plan.m_PixelSize;
		Plan.m_BorderWidth = Plan.m_PixelSize;
		const auto aFringe = ResolveRoundedSurfaceBorderMesh(Plan, Corners, Curve, Smoothing, Seam);
		const IGraphics::CColorVertex aColors[] = {
			{0, Edge.WithAlpha(0.0f)}, {1, Edge.WithAlpha(0.0f)}, {2, Edge}, {3, Edge}};
		pGraphics->SetColorVertex(aColors, std::size(aColors));
		pGraphics->QuadsDrawFreeform(aFringe.data(), aFringe.size());
		pGraphics->QuadsEnd();
	}

}

bool DrawRoundedSurface(IGraphics *pGraphics, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const SRoundedSurfaceParams &Params)
{
	if(pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return false;

	SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, pGraphics->HasRoundedRectSdf());
	if(!Plan.m_UseSdf)
	{
		// 几何回退只能画圆：外扩按「半径加外扩量」近似，和 SDF 分支保持同一外缘。
		Plan.m_Radius += Plan.m_Expand;
	}
	if(Plan.m_UseSdf)
	{
		IGraphics::SRoundedRectSdfParams SdfParams;
		SdfParams.m_Rect = vec4(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h);
		SdfParams.m_FillColor = vec4(Fill.r, Fill.g, Fill.b, Fill.a);
		const ColorRGBA ResolvedBorder = Plan.m_BorderWidth > 0.0f ? Border : Fill;
		SdfParams.m_BorderColor = vec4(ResolvedBorder.r, ResolvedBorder.g, ResolvedBorder.b, ResolvedBorder.a);
		SdfParams.m_CornerRadii = Plan.m_CornerRadii;
		SdfParams.m_Params = vec4(Plan.m_BorderWidth, Plan.m_PixelSize, Plan.m_PixelSize * 2.0f, 0.0f);
		// 单色表面同样支持「只描边」与带宽羽化：光晕不需要渐变，只靠这两项把环带
		// 铺成向外淡出的晕。字段语义与渐变路径完全一致，调用点不另开一条绘制链。
		SdfParams.m_GradientParams2.y = Params.m_BorderOnly ? 1.0f : 0.0f;
		SdfParams.m_GradientParams2.z = std::max(0.0f, Params.m_BorderFalloff);
		SdfParams.m_GradientParams2.w = CornerSmoothing();
		SdfParams.m_ShapeParams = vec4(CornerCurveType(), CapsuleSeam(), 0.0f, 0.0f);
		SdfParams.m_TextureParams1.w = Plan.m_Expand;
		pGraphics->RenderRoundedRectSdf(SdfParams);
		return true;
	}

	// 无 SDF 时所有表面统一走曲线轮廓：填充扇、边框环带和外缘羽化共用同一条轮廓。
	// 对齐像素已经在 ResolveRoundedSurfacePlan 里完成，这里只负责提交小数坐标网格。
	DrawSubpixelRoundedSurface(pGraphics, Plan, Fill, Border, ResolveRoundedSurfaceCorners(Params.m_Radius, Params.m_Corners));
	return true;
}

bool DrawRoundedImage(IGraphics *pGraphics, const CUIRect &Rect, const SRoundedImageParams &Image, const ColorRGBA &Border, const SRoundedSurfaceParams &Params)
{
	if(pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f || Image.m_Alpha <= 0.0f)
		return false;
	// 纹理圆角是设备能力：不支持时由调用方回退到预烘焙圆角的贴图。
	if(!pGraphics->HasRoundedRectSdf() || !pGraphics->HasRoundedRectSdfTexture() || !Image.m_Texture.IsValid())
		return false;
	const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
	if(!Plan.m_UseSdf)
		return false;

	IGraphics::SRoundedRectSdfParams SdfParams;
	SdfParams.m_Rect = vec4(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h);
	SdfParams.m_FillColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	// 没有边框时只留一个 alpha 为 0 的颜色：SDF 的边框项会被覆盖度乘成 0，
	// 这样既不用写死颜色字面量，也保持边框与填充共用同一份参数。
	ColorRGBA ResolvedBorder = Border;
	if(Plan.m_BorderWidth <= 0.0f)
		ResolvedBorder.a = 0.0f;
	ResolvedBorder.a *= Image.m_Alpha;
	SdfParams.m_BorderColor = vec4(ResolvedBorder.r, ResolvedBorder.g, ResolvedBorder.b, ResolvedBorder.a);
	SdfParams.m_CornerRadii = Plan.m_CornerRadii;
	SdfParams.m_Params = vec4(Plan.m_BorderWidth, Plan.m_PixelSize, Plan.m_PixelSize * 2.0f, 0.0f);
	ResolveRoundedImageUV(Plan.m_Rect.w, Plan.m_Rect.h, Image.m_UVScale, Image.m_UVOffset, SdfParams.m_TextureParams0, SdfParams.m_TextureParams1);
	// 照片整体透明度（卡片淡出）由片元乘在采到的 alpha 上：填充没有别的着色入口。
	SdfParams.m_TextureParams1.y = std::clamp(Image.m_Alpha, 0.0f, 1.0f);
	SdfParams.m_TextureParams1.w = Plan.m_Expand;
	SdfParams.m_GradientParams2.w = CornerSmoothing();
	SdfParams.m_ShapeParams = vec4(CornerCurveType(), CapsuleSeam(), 0.0f, 0.0f);
	// 图片取样器必须由命令携带：RenderRoundedRectSdf 沿用调用方 TextureSet 的绑定。
	pGraphics->TextureSet(Image.m_Texture);
	pGraphics->RenderRoundedRectSdf(SdfParams);
	pGraphics->TextureClear();
	return true;
}

bool DrawRoundedImage(CUi *pUi, const CUIRect &Rect, const SRoundedImageParams &Image, const ColorRGBA &Border, const float Radius, const float BorderWidth)
{
	if(pUi == nullptr || pUi->DrawingSuppressed())
		return false;
	SRoundedSurfaceParams Params;
	Params.m_Radius = Radius;
	Params.m_BorderWidth = BorderWidth;
	Params.m_PixelSize = pUi->PixelSize();
	Params.m_Corners = IGraphics::CORNER_ALL;
	return DrawRoundedImage(pUi->Graphics(), Rect, Image, Border, Params);
}

bool DrawRoundedSurface(CUi *pUi, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const float Radius, const float BorderWidth, const int Corners, const bool AlignToPixel)
{
	if(pUi == nullptr || pUi->DrawingSuppressed())
		return false;
	SRoundedSurfaceParams Params;
	Params.m_Radius = Radius;
	Params.m_BorderWidth = BorderWidth;
	Params.m_PixelSize = pUi->PixelSize();
	Params.m_Corners = Corners;
	Params.m_AlignToPixel = AlignToPixel;
	return DrawRoundedSurface(pUi->Graphics(), Rect, Fill, Border, Params);
}

bool DrawRoundedSurface(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const float Radius, const float BorderWidth, const int Corners, const bool AlignToPixel)
{
	return DrawRoundedSurface(Ctx.m_pUi, Rect, Fill, Border, Radius, BorderWidth, Corners, AlignToPixel);
}

bool DrawRoundedSurface(CUi *pUi, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const SRoundedSurfaceParams &Params)
{
	if(pUi == nullptr || pUi->DrawingSuppressed())
		return false;
	SRoundedSurfaceParams Resolved = Params;
	if(Resolved.m_PixelSize <= 0.0f)
		Resolved.m_PixelSize = pUi->PixelSize();
	return DrawRoundedSurface(pUi->Graphics(), Rect, Fill, Border, Resolved);
}

bool DrawRoundedSurface(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Fill, const ColorRGBA &Border, const SRoundedSurfaceParams &Params)
{
	return DrawRoundedSurface(Ctx.m_pUi, Rect, Fill, Border, Params);
}

bool DrawRoundedSurfaceGlow(IGraphics *pGraphics, const CUIRect &Rect, const ColorRGBA &Color, const SRoundedGlowParams &Params)
{
	if(pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f || Params.m_Spread <= 0.0f || Color.a <= 0.004f)
		return false;

	if(pGraphics->HasRoundedRectSdf())
	{
		// 一条连续的衰减带：形状按距离场向外偏移 m_Spread，带宽同为 m_Spread，
		// 于是零等值线落回外壳边缘，整个环带都在外壳之外，从边缘向外淡出。
		SRoundedSurfaceParams SurfaceParams;
		SurfaceParams.m_Radius = Params.m_Radius;
		SurfaceParams.m_ShapeExpand = Params.m_Spread;
		SurfaceParams.m_BorderWidth = Params.m_Spread;
		SurfaceParams.m_BorderOnly = true;
		SurfaceParams.m_BorderFalloff = std::max(Params.m_Falloff, 0.05f);
		SurfaceParams.m_Corners = Params.m_Corners;
		SurfaceParams.m_PixelSize = Params.m_PixelSize > 0.0f ? Params.m_PixelSize : 1.0f;
		return DrawRoundedSurface(pGraphics, Rect, ui_token::color::COLOR_TRANSPARENT, Color, SurfaceParams);
	}

	// 几何回退没有带宽内的连续羽化，只能用几道等距、逐层变淡的窄环近似「向外扩散」。
	// 环宽压到一像素：带宽稍宽一点就会读成实心光圈，而不是晕。层序从外到内，
	// 越靠内越亮，和 SDF 分支边缘最强、向外渐弱的方向一致。
	bool Any = false;
	constexpr int GLOW_FALLBACK_LAYERS = 3;
	const float PixelSize = std::max(Params.m_PixelSize > 0.0f ? Params.m_PixelSize : 1.0f, 0.0001f);
	for(int i = GLOW_FALLBACK_LAYERS; i >= 1; --i)
	{
		const float Offset = Params.m_Spread * static_cast<float>(i) / static_cast<float>(GLOW_FALLBACK_LAYERS);
		const CUIRect LayerRect{Rect.x - Offset, Rect.y - Offset, Rect.w + Offset * 2.0f, Rect.h + Offset * 2.0f};
		SRoundedSurfaceParams RingParams;
		RingParams.m_Radius = Params.m_Radius + Offset;
		RingParams.m_BorderWidth = PixelSize;
		RingParams.m_Corners = Params.m_Corners;
		RingParams.m_PixelSize = PixelSize;
		const float LayerAlpha = Color.a * (1.0f - 0.4f * static_cast<float>(i - 1));
		if(LayerAlpha <= 0.004f)
			continue;
		Any = DrawRoundedSurface(pGraphics, LayerRect, ui_token::color::COLOR_TRANSPARENT, Color.WithAlpha(LayerAlpha), RingParams) || Any;
	}
	return Any;
}

bool DrawRoundedSurfaceGlow(CUi *pUi, const CUIRect &Rect, const ColorRGBA &Color, const SRoundedGlowParams &Params)
{
	if(pUi == nullptr || pUi->DrawingSuppressed())
		return false;
	SRoundedGlowParams Resolved = Params;
	if(Resolved.m_PixelSize <= 0.0f)
		Resolved.m_PixelSize = pUi->PixelSize();
	return DrawRoundedSurfaceGlow(pUi->Graphics(), Rect, Color, Resolved);
}

bool DrawRoundedSurfaceGlow(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Color, const SRoundedGlowParams &Params)
{
	return DrawRoundedSurfaceGlow(Ctx.m_pUi, Rect, Color, Params);
}

// 固定网格一次提交顶点；预览和候选栏共享五种渐变，不生成每帧纹理。
bool DrawRoundedGradientSurface(IGraphics *pGraphics, const CUIRect &Rect, const SQmColorGradient &Gradient,
	float Alpha, const ColorRGBA &Border, const SRoundedSurfaceParams &Params)
{
	if(pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f)
		return false;
	if(Gradient.m_NumColors <= 1)
	{
		ColorRGBA Fill = Gradient.m_aColors[0];
		Fill.a *= Alpha;
		return DrawRoundedSurface(pGraphics, Rect, Fill, Border, Params);
	}
	SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, pGraphics->HasRoundedRectSdf());
	if(!Plan.m_UseSdf)
		Plan.m_Radius += Plan.m_Expand;
	// 渐变填充在 SDF 片元里按同一套语义采样，避免 32x16 网格在圆角处露出多边形边缘。
	if(Plan.m_UseSdf)
	{
		IGraphics::SRoundedRectSdfParams SdfParams;
		SdfParams.m_Rect = vec4(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h);
		const ColorRGBA First = Gradient.m_aColors[0];
		SdfParams.m_FillColor = vec4(First.r, First.g, First.b, First.a * Alpha);
		const ColorRGBA ResolvedBorder = Plan.m_BorderWidth > 0.0f ? Border : First;
		SdfParams.m_BorderColor = vec4(ResolvedBorder.r, ResolvedBorder.g, ResolvedBorder.b, ResolvedBorder.a);
		SdfParams.m_CornerRadii = Plan.m_CornerRadii;
		// 第四分量是"边框取渐变"开关；默认 0 保持其它调用点的单色边框。
		SdfParams.m_Params = vec4(Plan.m_BorderWidth, Plan.m_PixelSize, Plan.m_PixelSize * 2.0f, Params.m_BorderUsesGradient ? 1.0f : 0.0f);
		const int Count = std::clamp(Gradient.m_NumColors, 1, (int)IGraphics::ROUNDED_RECT_SDF_GRADIENT_MAX_COLORS);
		for(int i = 0; i < Count; ++i)
		{
			const ColorRGBA Color = Gradient.m_aColors[i];
			SdfParams.m_aGradientColors[i] = vec4(Color.r, Color.g, Color.b, Color.a * Alpha);
		}
		SdfParams.m_GradientParams0 = vec4((float)Gradient.m_Type, Gradient.m_Center.x, Gradient.m_Center.y, Gradient.m_Range);
		SdfParams.m_GradientParams1 = vec4(Gradient.m_Direction.x, Gradient.m_Direction.y, Gradient.m_Reverse ? 1.0f : 0.0f, (float)Count);
		SdfParams.m_GradientParams2.x = 1.0f;
		SdfParams.m_GradientParams2.y = Params.m_BorderOnly ? 1.0f : 0.0f;
		SdfParams.m_GradientParams2.z = std::max(0.0f, Params.m_BorderFalloff);
		SdfParams.m_GradientParams2.w = CornerSmoothing();
		SdfParams.m_ShapeParams = vec4(CornerCurveType(), CapsuleSeam(), 0.0f, 0.0f);
		SdfParams.m_TextureParams1.w = Plan.m_Expand;
		pGraphics->RenderRoundedRectSdf(SdfParams);
		return true;
	}
	CUIRect Inner = Plan.m_Rect;
	if(Plan.m_BorderWidth > 0.0f)
		Inner.Margin(Plan.m_BorderWidth, &Inner);
	const int Corners = ResolveRoundedSurfaceCorners(Params.m_Radius, Params.m_Corners);
	const int Curve = g_Config.m_QmUiCornerCurve;
	const float Smoothing = QmEffectiveCornerSmoothing();
	const float Seam = QmEffectiveCapsuleSeam();
	SRoundedSurfacePlan FillPlan = Plan;
	if(Plan.m_BorderWidth > 0.0f)
	{
		FillPlan.m_Rect = Inner;
		FillPlan.m_Radius = std::max(0.0f, Plan.m_Radius - Plan.m_BorderWidth);
	}
	const auto aFill = ResolveRoundedSurfaceOutline(FillPlan, Corners, Curve, Smoothing, Seam);
	const auto aOuter = ResolveRoundedSurfaceOutline(Plan, Corners, Curve, Smoothing, Seam);
	const auto aInner = ResolveRoundedSurfaceOutline(FillPlan, Corners, Curve, Smoothing, Seam);
	SRoundedSurfacePlan FringePlan = Plan;
	FringePlan.m_Rect.x -= Plan.m_PixelSize;
	FringePlan.m_Rect.y -= Plan.m_PixelSize;
	FringePlan.m_Rect.w += 2.0f * Plan.m_PixelSize;
	FringePlan.m_Rect.h += 2.0f * Plan.m_PixelSize;
	FringePlan.m_Radius += Plan.m_PixelSize;
	const auto aFringe = ResolveRoundedSurfaceOutline(FringePlan, Corners, Curve, Smoothing, Seam);
	const vec2 Center = FillPlan.m_Rect.Center();
	const auto DrawQuad = [&](vec2 TopLeft, vec2 TopRight, vec2 BottomLeft, vec2 BottomRight, bool FadeTop = false, bool FadeBottom = false) {
		const std::array<vec2, 4> aPoints = {TopLeft, TopRight, BottomLeft, BottomRight};
		std::array<IGraphics::CColorVertex, 4> aColors;
		for(int i = 0; i < 4; ++i)
		{
			ColorRGBA Color = Gradient.Sample((aPoints[i] - FillPlan.m_Rect.TopLeft()) / FillPlan.m_Rect.Size());
			Color.a *= Alpha;
			if((FadeTop && i < 2) || (FadeBottom && i >= 2))
				Color.a = 0.0f;
			aColors[i] = IGraphics::CColorVertex(i, Color);
		}
		pGraphics->SetColorVertex(aColors.data(), aColors.size());
		const IGraphics::CFreeformItem Quad(TopLeft, TopRight, BottomLeft, BottomRight);
		pGraphics->QuadsDrawFreeform(&Quad, 1);
	};
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	for(size_t i = 0; i < aFill.size(); ++i)
	{
		const size_t Next = (i + 1) % aFill.size();
		DrawQuad(Center, Center, aFill[i], aFill[Next]);
	}
	if(Plan.m_BorderWidth > 0.0f)
	{
		pGraphics->SetColor(Border);
		std::array<IGraphics::CFreeformItem, ROUNDED_SURFACE_OUTLINE_POINTS> aBorder{};
		for(size_t i = 0; i < aBorder.size(); ++i)
		{
			const size_t Next = (i + 1) % aBorder.size();
			aBorder[i] = IGraphics::CFreeformItem(aOuter[i], aOuter[Next], aInner[i], aInner[Next]);
		}
		pGraphics->QuadsDrawFreeform(aBorder.data(), aBorder.size());
	}
	// 羽化沿用内缘的颜色和透明度，避免无描边的渐变在淡出时留下首色外框。
	const auto SampleEdge = [&](vec2 Point) {
		if(Plan.m_BorderWidth > 0.0f)
			return Border;
		if(!(Plan.m_Rect.w > 0.0f && Plan.m_Rect.h > 0.0f))
			return ui_token::color::COLOR_TRANSPARENT;
		return Gradient.Sample((Point - Plan.m_Rect.TopLeft()) / Plan.m_Rect.Size()).WithMultipliedAlpha(Alpha);
	};
	ColorRGBA Edge = SampleEdge(aOuter.front());
	for(size_t i = 0; i < aOuter.size(); ++i)
	{
		const size_t Next = (i + 1) % aOuter.size();
		const ColorRGBA NextEdge = SampleEdge(aOuter[Next]);
		const IGraphics::CColorVertex aColors[] = {
			{0, Edge.WithAlpha(0.0f)}, {1, NextEdge.WithAlpha(0.0f)}, {2, Edge}, {3, NextEdge}};
		pGraphics->SetColorVertex(aColors, std::size(aColors));
		const IGraphics::CFreeformItem Quad(aFringe[i], aFringe[Next], aOuter[i], aOuter[Next]);
		pGraphics->QuadsDrawFreeform(&Quad, 1);
		Edge = NextEdge;
	}
	pGraphics->QuadsEnd();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	return true;
}
