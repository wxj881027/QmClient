#include <metal_stdlib>
#include "metal_types.h"

using namespace metal;

struct SMetalVertex
{
	float2 m_Position [[attribute(0)]];
	float2 m_TexCoord [[attribute(1)]];
	float4 m_Color [[attribute(2)]];
};

struct SMetalTex3DVertex
{
	float2 m_Position [[attribute(0)]];
	float4 m_Color [[attribute(1)]];
	float3 m_TexCoord [[attribute(2)]];
};

struct SMetalVertexOut
{
	float4 m_Position [[position]];
	float2 m_TexCoord;
	float4 m_Color;
};

struct SMetalTex3DVertexOut
{
	float4 m_Position [[position]];
	float3 m_TexCoord;
	float4 m_Color;
};

struct SMetalTileVertex
{
	float2 m_Position [[attribute(0)]];
	uchar4 m_TexCoord [[attribute(1)]];
};

struct SMetalTilePlainVertex
{
	float2 m_Position [[attribute(0)]];
};

struct SMetalQuadVertex
{
	float4 m_PositionCenter [[attribute(0)]];
	float4 m_Color [[attribute(1)]];
	float2 m_TexCoord [[attribute(2)]];
};

struct SMetalQuadPlainVertex
{
	float4 m_PositionCenter [[attribute(0)]];
	float4 m_Color [[attribute(1)]];
};

vertex SMetalVertexOut qmclient_vertex(SMetalVertex Vertex [[stage_in]], constant SMetalUniforms &Uniforms [[buffer(1)]])
{
	SMetalVertexOut Out;
	Out.m_Position = Uniforms.m_MVP * float4(Vertex.m_Position, 0.0, 1.0);
	Out.m_TexCoord = Vertex.m_TexCoord;
	Out.m_Color = float4(Vertex.m_Color) * Uniforms.m_Color;
	return Out;
}

fragment float4 qmclient_fragment(SMetalVertexOut Input [[stage_in]])
{
	return Input.m_Color;
}

fragment float4 qmclient_textured_fragment(SMetalVertexOut Input [[stage_in]], texture2d<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]])
{
	return Texture.sample(Sampler, Input.m_TexCoord) * Input.m_Color;
}

float QmClientMedian(float3 Value)
{
	return max(min(Value.r, Value.g), min(max(Value.r, Value.g), Value.b));
}

struct QmClientMsdfParams
{
	float4 m_Params;
	float4 m_SecondaryColor;
};

fragment float4 qmclient_textured_msdf_fragment(SMetalVertexOut Input [[stage_in]], texture2d<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]], constant QmClientMsdfParams &Msdf [[buffer(1)]])
{
	const float4 MsdfParams = Msdf.m_Params;
	if(MsdfParams.x < 0.0)
	{
		const float InnerRadius = -MsdfParams.x;
		const float OuterRadius = MsdfParams.y;
		const float Sweep = max(MsdfParams.w - MsdfParams.z, 0.0);
		const float2 Point = Input.m_TexCoord - float2(0.5);
		const float Radius = length(Point);
		const float RadialDistance = max(InnerRadius - Radius, Radius - OuterRadius);
		const float RadialFeather = max(fwidth(Radius), 0.0005);
		const float RadialCoverage = 1.0 - smoothstep(-RadialFeather * 0.5, RadialFeather * 0.5, RadialDistance);
		const float Angle = atan2(Point.y, Point.x);
		float RelativeAngle = fmod(Angle - MsdfParams.z, 6.28318530718);
		if(RelativeAngle < 0.0)
			RelativeAngle += 6.28318530718;
		const float AngularFeather = max(fwidth(Angle), 0.0015);
		const float AngularCoverage = Sweep >= 6.2830 ? 1.0 : smoothstep(0.0, AngularFeather, RelativeAngle) * smoothstep(0.0, AngularFeather, Sweep - RelativeAngle);
		return float4(Input.m_Color.rgb, Input.m_Color.a * RadialCoverage * AngularCoverage);
	}
	const float4 Sample = Texture.sample(Sampler, Input.m_TexCoord);
		const float TrueSignedDistance = Sample.a - 0.5;
		// w 编码契约见 src/engine/graphics.h 的 qm_msdf_param 命名空间，三个后端必须一致：
		//   w > 0 → 普通 MSDF（w = 描边宽度）；-0.001 < w < 0 → Duotone；w <= -0.001 → Alpha 真 SDF。
		// 注意 Duotone 区间必须严格避开真 SDF 的描边编码，否则带描边的真 SDF 字形会被误判。
		const bool UseTrueSdf = MsdfParams.w <= -0.001;
		const bool UseSecondarySdf = MsdfParams.w < 0.0 && !UseTrueSdf;
		const float SignedDistance = UseTrueSdf ? TrueSignedDistance : QmClientMedian(Sample.rgb) - 0.5;
		const float2 UnitRange = float2(MsdfParams.x) / MsdfParams.yz;
		const float2 ScreenTexSize = 1.0 / fwidth(Input.m_TexCoord);
		const float ScreenPxRange = max(0.5 * dot(UnitRange, ScreenTexSize), 1.0);
		const float RequestedOutline = UseTrueSdf ? max(-MsdfParams.w - 0.001, 0.0) : MsdfParams.w;
		if(RequestedOutline > 0.0)
	{
		// 距离场在当前 quad 上最多只能表示约 0.5 * ScreenPxRange 的外扩。
		// 超出这个范围会把 atlas 背景也推成不透明矩形；限制到留出一个抗锯齿像素的可表示范围。
		const float MaxRepresentableOutline = max(0.0, 0.5 * ScreenPxRange - 0.5);
			const float OutlineWidth = min(RequestedOutline, MaxRepresentableOutline);
		const float FillCoverage = clamp(SignedDistance * ScreenPxRange + 0.5, 0.0, 1.0);
		// 描边直接沿同一 signed distance 外扩，避免邻字形 UV 串采样造成孤立白点。
		// MTSDF alpha is a true single-channel distance, so the outer edge does
		// not inherit MSDF corner-channel interpolation artifacts.
		if(UseTrueSdf)
		{
			const float OuterCoverage = clamp(TrueSignedDistance * ScreenPxRange + OutlineWidth + 0.5, 0.0, 1.0);
			const float OutlineCoverage = max(OuterCoverage - FillCoverage, 0.0);
			return float4(Input.m_Color.rgb, Input.m_Color.a * OutlineCoverage);
		}
		const float OuterCoverage = clamp(SignedDistance * ScreenPxRange + OutlineWidth + 0.5, 0.0, 1.0);
		const float OutlineCoverage = max(OuterCoverage - FillCoverage, 0.0);
		return float4(Input.m_Color.rgb, Input.m_Color.a * OutlineCoverage);
	}
	const float Opacity = clamp(SignedDistance * ScreenPxRange + 0.5, 0.0, 1.0);
	if(UseSecondarySdf)
	{
		// Duotone atlas：RGB 与 Alpha 是同一 px_range 下的两张距离场（primary / secondary），
		// 因此复用 ScreenPxRange 解码 secondary 覆盖，缩放到任意尺寸都保持锐利边缘。
		const float SecondaryCoverage = clamp((Sample.a - 0.5) * ScreenPxRange + 0.5, 0.0, 1.0);
		const float3 SecondaryColor = Msdf.m_SecondaryColor.rgb;
		const float SecondaryAlpha = SecondaryCoverage * Msdf.m_SecondaryColor.a;
		const float Alpha = max(Opacity, SecondaryAlpha);
		const float3 Color = mix(SecondaryColor, Input.m_Color.rgb, Opacity);
		return float4(Color, Input.m_Color.a * Alpha);
	}
	return float4(Input.m_Color.rgb, Input.m_Color.a * Opacity);
}

float RoundedRectDistance(float2 Point, float2 HalfSize, float Radius)
{
	const float2 DistanceToInner = abs(Point) - HalfSize + Radius;
	return length(max(DistanceToInner, float2(0.0))) + min(max(DistanceToInner.x, DistanceToInner.y), 0.0) - Radius;
}

float RoundedRectCornerRadius(float2 Point, float4 CornerRadii)
{
	if(Point.y < 0.0)
		return Point.x < 0.0 ? CornerRadii.x : CornerRadii.y;
	return Point.x < 0.0 ? CornerRadii.w : CornerRadii.z;
}

float RoundedRectCoverage(float DistanceValue, float PixelSize)
{
	const float Feather = max(PixelSize, length(float2(dfdx(DistanceValue), dfdy(DistanceValue))));
	return 1.0 - smoothstep(-Feather * 0.5, Feather * 0.5, DistanceValue);
}

fragment float4 qmclient_rounded_rect_sdf_fragment(SMetalVertexOut Input [[stage_in]], constant float4 *Params [[buffer(1)]])
{
	const float4 Rect = Params[0];
	const float4 FillColor = Params[1];
	const float4 BorderColor = Params[2];
	const float4 CornerRadii = Params[3];
	const float4 RenderParams = Params[4];
	const float2 HalfSize = Rect.zw * 0.5;
	const float2 Point = (Input.m_TexCoord - float2(0.5)) * (Rect.zw + float2(RenderParams.z * 2.0));
	const float Radius = clamp(RoundedRectCornerRadius(Point, CornerRadii), 0.0, min(HalfSize.x, HalfSize.y));
	const float OuterDistance = RoundedRectDistance(Point, HalfSize, Radius);
	const float OuterCoverage = RoundedRectCoverage(OuterDistance, RenderParams.y);
	const float BorderWidth = clamp(RenderParams.x, 0.0, min(HalfSize.x, HalfSize.y));
	const float2 InnerHalfSize = HalfSize - float2(BorderWidth);
	const float4 InnerCornerRadii = max(CornerRadii - float4(BorderWidth), float4(0.0));
	const float InnerRadius = clamp(RoundedRectCornerRadius(Point, InnerCornerRadii), 0.0, min(InnerHalfSize.x, InnerHalfSize.y));
	const float InnerDistance = RoundedRectDistance(Point, InnerHalfSize, InnerRadius);
	const float InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? RoundedRectCoverage(InnerDistance, RenderParams.y) : 0.0;
	if(BorderWidth <= 0.0)
		return float4(FillColor.rgb, FillColor.a * OuterCoverage) * Input.m_Color;
	const float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);
	const float BorderAlpha = BorderColor.a * BorderCoverage;
	const float FillAlpha = FillColor.a * InnerCoverage;
	const float OutputAlpha = FillAlpha + BorderAlpha;
	const float3 Premultiplied = FillColor.rgb * FillAlpha + BorderColor.rgb * BorderAlpha;
	return (OutputAlpha > 0.0 ? float4(Premultiplied / OutputAlpha, OutputAlpha) : float4(0.0)) * Input.m_Color;
}

constant float MEDIA_ISLAND_PI = 3.14159265359;
constant float MEDIA_ISLAND_TAU = 6.28318530718;
constant int MEDIA_ISLAND_MAX_ITEMS = 12;
constant int MEDIA_ISLAND_ITEM_BASE = 8;
constant int MEDIA_ISLAND_ITEM_STRIDE = 3;
constant int MEDIA_ISLAND_BACKDROP_UV = 44;

bool MediaIslandHasCorner(float Flags, int Bit)
{
	return fmod(floor(Flags / float(Bit)), 2.0) > 0.5;
}

float MediaIslandRoundedRectDistance(float2 Point, float4 Rect, float Radius, float Corners, float DisabledRadius)
{
	if(Rect.z <= 0.0 || Rect.w <= 0.0)
		return 1000000.0;
	const float2 HalfSize = Rect.zw * 0.5;
	const float2 Local = Point - (Rect.xy + HalfSize);
	int Corner = 8;
	if(Local.x < 0.0)
		Corner = Local.y < 0.0 ? 1 : 4;
	else if(Local.y < 0.0)
		Corner = 2;
	const float RequestedRadius = MediaIslandHasCorner(Corners, Corner) ? Radius : DisabledRadius;
	const float CornerRadius = clamp(RequestedRadius, 0.0, min(HalfSize.x, HalfSize.y));
	const float2 DistanceToInner = abs(Local) - HalfSize + CornerRadius;
	const float2 Outside = max(DistanceToInner, float2(0.0));
	return length(Outside) + min(max(DistanceToInner.x, DistanceToInner.y), 0.0) - CornerRadius;
}

float MediaIslandBlobExponent(float2 Radii)
{
	const float MaxRadius = max(Radii.x, Radii.y);
	const float RadiusDifference = MaxRadius > 0.0 ? abs(Radii.x - Radii.y) / MaxRadius : 0.0;
	return mix(2.0, 5.0, smoothstep(0.0, 0.1, RadiusDifference));
}

float MediaIslandBlobDistance(float2 Point, float2 Center, float2 Radii)
{
	if(Radii.x <= 0.0 || Radii.y <= 0.0)
		return 1000000.0;
	const float Exponent = MediaIslandBlobExponent(Radii);
	const float2 Normalized = abs((Point - Center) / Radii);
	const float NormalizedDistance = pow(pow(Normalized.x, Exponent) + pow(Normalized.y, Exponent), 1.0 / Exponent);
	return (NormalizedDistance - 1.0) * min(Radii.x, Radii.y);
}

float MediaIslandSmoothUnion(float Left, float Right, float Blend)
{
	if(Blend <= 0.0)
		return min(Left, Right);
	const float H = max(Blend - abs(Left - Right), 0.0) / Blend;
	return min(Left, Right) - H * H * Blend * 0.25;
}

float MediaIslandCoverage(float DistanceValue, float Feather)
{
	Feather = max(Feather, 0.0001);
	const float T = clamp((DistanceValue + Feather) / (Feather * 2.0), 0.0, 1.0);
	return 1.0 - (T * T * (3.0 - 2.0 * T));
}

float MediaIslandArcDistance(float2 Point, float2 Center, float Radius, float HalfThickness, float Progress)
{
	const float2 Relative = Point - Center;
	Progress = clamp(Progress, 0.0, 1.0);
	if(Progress <= 0.0)
		return 1000000.0;
	if(Progress >= 0.9999)
		return abs(length(Relative) - Radius) - HalfThickness;
	float Angle = atan2(Relative.y, Relative.x) + MEDIA_ISLAND_PI * 0.5;
	if(Angle < 0.0)
		Angle += MEDIA_ISLAND_TAU;
	const float EndAngle = Progress * MEDIA_ISLAND_TAU;
	if(Angle <= EndAngle)
		return abs(length(Relative) - Radius) - HalfThickness;
	const float2 StartPoint = Center + float2(0.0, -Radius);
	const float2 EndPoint = Center + float2(sin(EndAngle) * Radius, -cos(EndAngle) * Radius);
	return min(distance(Point, StartPoint), distance(Point, EndPoint)) - HalfThickness;
}

void MediaIslandComposite(thread float3 &PremulColor, thread float &Alpha, float4 Color, float ShapeCoverage)
{
	const float SourceAlpha = clamp(Color.a * ShapeCoverage, 0.0, 1.0);
	PremulColor = Color.rgb * SourceAlpha + PremulColor * (1.0 - SourceAlpha);
	Alpha = SourceAlpha + Alpha * (1.0 - SourceAlpha);
}

fragment float4 qmclient_media_island_sdf_fragment(SMetalVertexOut Input [[stage_in]], texture2d<float> BackdropTexture [[texture(0)]], sampler BackdropSampler [[sampler(0)]], constant float4 *Data [[buffer(1)]])
{
	const float4 OuterRect = Data[0];
	const float4 MainParams = Data[4];
	const float4 Metadata = Data[5];
	const float4 ShadowParams = Data[7];
	const float2 Point = OuterRect.xy + Input.m_TexCoord * OuterRect.zw;
	const float ScreenPixelSize = max(Metadata.w, 0.0001);
	const float Feather = max(ScreenPixelSize * 0.8, max(fwidth(Point.x), fwidth(Point.y)) * 0.9);
	const int ItemCount = clamp(int(Metadata.x + 0.5), 0, MEDIA_ISLAND_MAX_ITEMS);
	const float MainRadius = MainParams.x;
	const float MainDistance = MediaIslandRoundedRectDistance(Point, Data[1], MainRadius, Metadata.y, MainParams.y);
	float SatelliteDistance = 1000000.0;
	float ShapeDistance = MainDistance;
	for(int i = 0; i < ItemCount; ++i)
	{
		const float4 ItemShape = Data[MEDIA_ISLAND_ITEM_BASE + i * MEDIA_ISLAND_ITEM_STRIDE];
		const float4 ItemParams = Data[MEDIA_ISLAND_ITEM_BASE + i * MEDIA_ISLAND_ITEM_STRIDE + 1];
		const float ItemDistance = MediaIslandBlobDistance(Point, ItemShape.xy, ItemShape.zw);
		SatelliteDistance = i == 0 ? ItemDistance : MediaIslandSmoothUnion(SatelliteDistance, ItemDistance, MainRadius * 0.28);
		if(ItemParams.x > 0.0)
			ShapeDistance = min(ShapeDistance, MediaIslandSmoothUnion(MainDistance, ItemDistance, ItemParams.x));
	}
	ShapeDistance = min(ShapeDistance, SatelliteDistance);

	const float4 CapsuleRect = Data[2];
	const float4 CapsuleParams = Data[6];
	if(Metadata.z > 0.5 && CapsuleRect.z > 0.0 && CapsuleRect.w > 0.0)
	{
		const float CapsuleDistance = MediaIslandRoundedRectDistance(Point, CapsuleRect, CapsuleParams.x, 15.0, 0.0);
		ShapeDistance = min(ShapeDistance, CapsuleDistance);
		if(CapsuleParams.y > 0.0)
			ShapeDistance = min(ShapeDistance, MediaIslandSmoothUnion(MainDistance, CapsuleDistance, CapsuleParams.y));
	}

	float3 PremulColor = float3(0.0);
	float Alpha = 0.0;
	const float ShadowSize = max(ShadowParams.x, 0.0);
	if(ShadowSize > 0.0 && ShadowParams.y > 0.0)
	{
		const float OutsideMask = smoothstep(-Feather, Feather, ShapeDistance);
		const float ShadowFalloff = 1.0 - smoothstep(0.0, ShadowSize, max(ShapeDistance, 0.0));
		MediaIslandComposite(PremulColor, Alpha, float4(0.0, 0.0, 0.0, ShadowParams.y), OutsideMask * ShadowFalloff);
	}
	const float ShapeCoverage = MediaIslandCoverage(ShapeDistance, Feather);
	const float4 Background = Data[3];
	const float4 BackdropUv = Data[MEDIA_ISLAND_BACKDROP_UV];
	if(abs(BackdropUv.z) > 0.000001 && abs(BackdropUv.w) > 0.000001)
	{
		const float3 Backdrop = BackdropTexture.sample(BackdropSampler, BackdropUv.xy + Input.m_TexCoord * BackdropUv.zw).rgb;
		MediaIslandComposite(PremulColor, Alpha, float4(mix(Backdrop, Background.rgb, clamp(Background.a, 0.0, 1.0)), 1.0), ShapeCoverage);
	}
	else
		MediaIslandComposite(PremulColor, Alpha, Background, ShapeCoverage);
	for(int i = 0; i < ItemCount; ++i)
	{
		const float4 ItemShape = Data[MEDIA_ISLAND_ITEM_BASE + i * MEDIA_ISLAND_ITEM_STRIDE];
		const float4 ItemParams = Data[MEDIA_ISLAND_ITEM_BASE + i * MEDIA_ISLAND_ITEM_STRIDE + 1];
		if(ItemParams.y > 0.001)
		{
			const float RingRadius = MainParams.z * ItemParams.z;
			const float RingThickness = MainParams.w * ItemParams.z;
			const float2 Relative = Point - ItemShape.xy;
			if(abs(Relative.x) <= RingRadius + RingThickness + Feather && abs(Relative.y) <= RingRadius + RingThickness + Feather)
			{
				const float TrackDistance = abs(length(Relative) - RingRadius) - RingThickness * 0.5;
				float4 ItemColor = Data[MEDIA_ISLAND_ITEM_BASE + i * MEDIA_ISLAND_ITEM_STRIDE + 2];
				float4 TrackColor = ItemColor;
				TrackColor.a *= 0.18 * ItemParams.y;
				MediaIslandComposite(PremulColor, Alpha, TrackColor, MediaIslandCoverage(TrackDistance, Feather));
				ItemColor.a *= ItemParams.y;
				MediaIslandComposite(PremulColor, Alpha, ItemColor, MediaIslandCoverage(MediaIslandArcDistance(Point, ItemShape.xy, RingRadius, RingThickness * 0.5, ItemParams.w), Feather));
			}
		}
	}
	const float InvAlpha = Alpha > 0.0001 ? 1.0 / Alpha : 0.0;
	return float4(clamp(PremulColor * InvAlpha, 0.0, 1.0), clamp(Alpha, 0.0, 1.0)) * Input.m_Color;
}

float GaussianBlurWeight(constant SMetalGaussianBlurUniforms &Uniforms, int Offset)
{
	if(Offset < 4)
		return Uniforms.m_Weights0[Offset];
	if(Offset < 8)
		return Uniforms.m_Weights1[Offset - 4];
	return Uniforms.m_Weights2[Offset - 8];
}

fragment float4 qmclient_gaussian_blur_fragment(SMetalVertexOut Input [[stage_in]], texture2d<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]], constant SMetalGaussianBlurUniforms &Uniforms [[buffer(1)]])
{
	const int Mode = int(Uniforms.m_TexelOffsetRadius.w);
	const int Pass = int(Uniforms.m_Weights2.w);
	const float2 Texel = Uniforms.m_TexelOffsetRadius.xy;
	if(Mode == 1)
	{
		const float Step = Pass == 0 ? 1.5 : 2.5;
		const float2 Offset = Texel * Step;
		return (Texture.sample(Sampler, Input.m_TexCoord + float2(Offset.x, Offset.y)) +
			Texture.sample(Sampler, Input.m_TexCoord + float2(-Offset.x, Offset.y)) +
			Texture.sample(Sampler, Input.m_TexCoord + float2(Offset.x, -Offset.y)) +
			Texture.sample(Sampler, Input.m_TexCoord - Offset)) * 0.25;
	}
	if(Mode == 2)
	{
		if(Pass == 0)
		{
			const float2 Offset = Texel * 0.5;
			float4 Result = Texture.sample(Sampler, Input.m_TexCoord) * 4.0;
			Result += Texture.sample(Sampler, Input.m_TexCoord + float2(-Offset.x, -Offset.y));
			Result += Texture.sample(Sampler, Input.m_TexCoord + float2(Offset.x, -Offset.y));
			Result += Texture.sample(Sampler, Input.m_TexCoord + float2(-Offset.x, Offset.y));
			Result += Texture.sample(Sampler, Input.m_TexCoord + float2(Offset.x, Offset.y));
			return Result / 8.0;
		}
		const float2 Offset = Texel * 0.5;
		float4 Result = Texture.sample(Sampler, Input.m_TexCoord + float2(-2.0 * Offset.x, 0.0));
		Result += Texture.sample(Sampler, Input.m_TexCoord + float2(2.0 * Offset.x, 0.0));
		Result += Texture.sample(Sampler, Input.m_TexCoord + float2(0.0, -2.0 * Offset.y));
		Result += Texture.sample(Sampler, Input.m_TexCoord + float2(0.0, 2.0 * Offset.y));
		Result += Texture.sample(Sampler, Input.m_TexCoord + float2(-Offset.x, -Offset.y)) * 2.0;
		Result += Texture.sample(Sampler, Input.m_TexCoord + float2(Offset.x, -Offset.y)) * 2.0;
		Result += Texture.sample(Sampler, Input.m_TexCoord + float2(-Offset.x, Offset.y)) * 2.0;
		Result += Texture.sample(Sampler, Input.m_TexCoord + float2(Offset.x, Offset.y)) * 2.0;
		return Result / 12.0;
	}
	float4 Result = Texture.sample(Sampler, Input.m_TexCoord) * GaussianBlurWeight(Uniforms, 0);
	const float2 TexelOffset = Uniforms.m_TexelOffsetRadius.xy;
	const int Radius = int(Uniforms.m_TexelOffsetRadius.z);
	for(int Offset = 1; Offset <= 10; ++Offset)
	{
		if(Offset > Radius)
			break;
		const float2 SampleOffset = TexelOffset * float(Offset);
		const float Weight = GaussianBlurWeight(Uniforms, Offset);
		Result += Texture.sample(Sampler, Input.m_TexCoord + SampleOffset) * Weight;
		Result += Texture.sample(Sampler, Input.m_TexCoord - SampleOffset) * Weight;
	}
	return Result;
}

vertex SMetalTex3DVertexOut qmclient_tex_array_vertex(SMetalTex3DVertex Vertex [[stage_in]], constant SMetalUniforms &Uniforms [[buffer(1)]])
{
	SMetalTex3DVertexOut Out;
	Out.m_Position = Uniforms.m_MVP * float4(Vertex.m_Position, 0.0, 1.0);
	Out.m_TexCoord = Vertex.m_TexCoord;
	Out.m_Color = float4(Vertex.m_Color) * Uniforms.m_Color;
	return Out;
}

fragment float4 qmclient_tex_array_fragment(SMetalTex3DVertexOut Input [[stage_in]], texture2d_array<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]])
{
	return Texture.sample(Sampler, Input.m_TexCoord.xy, uint(Input.m_TexCoord.z)) * Input.m_Color;
}

fragment float4 qmclient_text_fragment(SMetalVertexOut Input [[stage_in]], texture2d<float> TextTexture [[texture(0)]], texture2d<float> OutlineTexture [[texture(1)]], sampler Sampler [[sampler(0)]], constant SMetalTextUniforms &Uniforms [[buffer(1)]])
{
	const float2 TexCoord = Input.m_TexCoord / Uniforms.m_Params.x;
	const float TextAlpha = TextTexture.sample(Sampler, TexCoord).r * Input.m_Color.a;
	const float OutlineAlpha = OutlineTexture.sample(Sampler, TexCoord).r * Uniforms.m_OutlineColor.a;
	const float OutlineBlend = 1.0 - TextAlpha;
	const float3 OutlinePremultiplied = Uniforms.m_OutlineColor.rgb * OutlineAlpha * OutlineBlend;
	const float3 TextPremultiplied = Input.m_Color.rgb * TextAlpha;
	const float Alpha = OutlineAlpha * OutlineBlend + TextAlpha;
	return Alpha > 0.0 ? float4((OutlinePremultiplied + TextPremultiplied) / Alpha, Alpha) : float4(0.0);
}

struct SMetalTileVertexOut
{
	float4 m_Position [[position]];
	float4 m_TexCoord [[center_no_perspective]];
};

struct SMetalTileBorderVertexOut
{
	float4 m_Position [[position]];
	float4 m_TexCoord [[centroid_no_perspective]];
};

vertex SMetalTileVertexOut qmclient_tile_vertex(SMetalTileVertex Vertex [[stage_in]], constant SMetalTileUniforms &Uniforms [[buffer(1)]])
{
	SMetalTileVertexOut Out;
	const float2 Position = Vertex.m_Position * Uniforms.m_Transform.zw + Uniforms.m_Transform.xy;
	Out.m_Position = Uniforms.m_MVP * float4(Position, 0.0, 1.0);
	float2 TexScale = Uniforms.m_Transform.zw;
	if(Vertex.m_TexCoord.w > 0)
		TexScale = TexScale.yx;
	Out.m_TexCoord = float4(float2(Vertex.m_TexCoord.xy) * TexScale, float(Vertex.m_TexCoord.z), float(Vertex.m_TexCoord.w));
	return Out;
}

vertex SMetalTileBorderVertexOut qmclient_tile_border_vertex(SMetalTileVertex Vertex [[stage_in]], constant SMetalTileUniforms &Uniforms [[buffer(1)]])
{
	SMetalTileBorderVertexOut Out;
	const float2 Position = Vertex.m_Position * Uniforms.m_Transform.zw + Uniforms.m_Transform.xy;
	Out.m_Position = Uniforms.m_MVP * float4(Position, 0.0, 1.0);
	float2 TexScale = Uniforms.m_Transform.zw;
	if(Vertex.m_TexCoord.w > 0)
		TexScale = TexScale.yx;
	Out.m_TexCoord = float4(float2(Vertex.m_TexCoord.xy) * TexScale, float(Vertex.m_TexCoord.z), float(Vertex.m_TexCoord.w));
	return Out;
}

vertex SMetalTileVertexOut qmclient_tile_plain_vertex(SMetalTilePlainVertex Vertex [[stage_in]], constant SMetalTileUniforms &Uniforms [[buffer(1)]])
{
	SMetalTileVertexOut Out;
	const float2 Position = Vertex.m_Position * Uniforms.m_Transform.zw + Uniforms.m_Transform.xy;
	Out.m_Position = Uniforms.m_MVP * float4(Position, 0.0, 1.0);
	Out.m_TexCoord = float4(0.0);
	return Out;
}

fragment float4 qmclient_tile_fragment(SMetalTileVertexOut Input [[stage_in]], constant SMetalTileUniforms &Uniforms [[buffer(1)]])
{
	return Uniforms.m_Color;
}

fragment float4 qmclient_tile_textured_fragment(SMetalTileVertexOut Input [[stage_in]], texture2d_array<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]], constant SMetalTileUniforms &Uniforms [[buffer(1)]])
{
	return Texture.sample(Sampler, Input.m_TexCoord.xy, uint(Input.m_TexCoord.z)) * Uniforms.m_Color;
}

fragment float4 qmclient_tile_border_textured_fragment(SMetalTileBorderVertexOut Input [[stage_in]], texture2d_array<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]], constant SMetalTileUniforms &Uniforms [[buffer(1)]])
{
	const float2 TexCoord = fract(Input.m_TexCoord.xy);
	const float2 Dx = dfdx(Input.m_TexCoord.xy);
	const float2 Dy = dfdy(Input.m_TexCoord.xy);
	return Texture.sample(Sampler, TexCoord, uint(Input.m_TexCoord.z), gradient2d(Dx, Dy)) * Uniforms.m_Color;
}

struct SMetalQuadVertexOut
{
	float4 m_Position [[position]];
	float4 m_Color;
	float2 m_TexCoord;
};

SMetalQuadVertexOut QuadVertexImpl(SMetalQuadVertex Vertex, device const SMetalQuadUniforms &Uniforms, uint VertexId, bool Grouped)
{
	SMetalQuadVertexOut Out;
	uint QuadIndex = Grouped ? 0 : (VertexId / 4) - uint(Uniforms.m_QuadOffset);
	float2 Position = Vertex.m_PositionCenter.xy;
	const float Rotation = Uniforms.m_aOffsetsRotations[QuadIndex].z;
	if(Rotation != 0.0)
	{
		const float2 Relative = Position - Vertex.m_PositionCenter.zw;
		const float SinRotation = sin(Rotation);
		const float CosRotation = cos(Rotation);
		Position = float2(Relative.x * CosRotation - Relative.y * SinRotation, Relative.x * SinRotation + Relative.y * CosRotation) + Vertex.m_PositionCenter.zw;
	}
	Position += Uniforms.m_aOffsetsRotations[QuadIndex].xy;
	Out.m_Position = Uniforms.m_MVP * float4(Position, 0.0, 1.0);
	Out.m_Color = float4(Vertex.m_Color) * Uniforms.m_aColors[QuadIndex];
	Out.m_TexCoord = Vertex.m_TexCoord;
	return Out;
}

vertex SMetalQuadVertexOut qmclient_quad_vertex_grouped(SMetalQuadVertex Vertex [[stage_in]], device const SMetalQuadUniforms &Uniforms [[buffer(1)]], uint VertexId [[vertex_id]])
{
	return QuadVertexImpl(Vertex, Uniforms, VertexId, true);
}

vertex SMetalQuadVertexOut qmclient_quad_vertex_ungrouped(SMetalQuadVertex Vertex [[stage_in]], device const SMetalQuadUniforms &Uniforms [[buffer(1)]], uint VertexId [[vertex_id]])
{
	return QuadVertexImpl(Vertex, Uniforms, VertexId, false);
}

SMetalQuadVertexOut QuadPlainVertexImpl(SMetalQuadPlainVertex Vertex, device const SMetalQuadUniforms &Uniforms, uint VertexId, bool Grouped)
{
	SMetalQuadVertexOut Out;
	uint QuadIndex = Grouped ? 0 : (VertexId / 4) - uint(Uniforms.m_QuadOffset);
	float2 Position = Vertex.m_PositionCenter.xy;
	const float Rotation = Uniforms.m_aOffsetsRotations[QuadIndex].z;
	if(Rotation != 0.0)
	{
		const float2 Relative = Position - Vertex.m_PositionCenter.zw;
		const float SinRotation = sin(Rotation);
		const float CosRotation = cos(Rotation);
		Position = float2(Relative.x * CosRotation - Relative.y * SinRotation, Relative.x * SinRotation + Relative.y * CosRotation) + Vertex.m_PositionCenter.zw;
	}
	Position += Uniforms.m_aOffsetsRotations[QuadIndex].xy;
	Out.m_Position = Uniforms.m_MVP * float4(Position, 0.0, 1.0);
	Out.m_Color = float4(Vertex.m_Color) * Uniforms.m_aColors[QuadIndex];
	Out.m_TexCoord = float2(0.0);
	return Out;
}

vertex SMetalQuadVertexOut qmclient_quad_plain_vertex_grouped(SMetalQuadPlainVertex Vertex [[stage_in]], device const SMetalQuadUniforms &Uniforms [[buffer(1)]], uint VertexId [[vertex_id]])
{
	return QuadPlainVertexImpl(Vertex, Uniforms, VertexId, true);
}

vertex SMetalQuadVertexOut qmclient_quad_plain_vertex_ungrouped(SMetalQuadPlainVertex Vertex [[stage_in]], device const SMetalQuadUniforms &Uniforms [[buffer(1)]], uint VertexId [[vertex_id]])
{
	return QuadPlainVertexImpl(Vertex, Uniforms, VertexId, false);
}

fragment float4 qmclient_quad_fragment(SMetalQuadVertexOut Input [[stage_in]])
{
	return Input.m_Color;
}

fragment float4 qmclient_quad_textured_fragment(SMetalQuadVertexOut Input [[stage_in]], texture2d<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]])
{
	return Texture.sample(Sampler, Input.m_TexCoord) * Input.m_Color;
}

vertex SMetalVertexOut qmclient_quad_container_ex_vertex(SMetalVertex Vertex [[stage_in]], constant SMetalQuadContainerUniforms &Uniforms [[buffer(1)]])
{
	SMetalVertexOut Out;
	float2 Position = Vertex.m_Position;
	const float Rotation = Uniforms.m_CenterRotation.z;
	if(Rotation != 0.0)
	{
		const float2 Relative = Position - Uniforms.m_CenterRotation.xy;
		const float SinRotation = sin(Rotation);
		const float CosRotation = cos(Rotation);
		Position = float2(Relative.x * CosRotation - Relative.y * SinRotation, Relative.x * SinRotation + Relative.y * CosRotation) + Uniforms.m_CenterRotation.xy;
	}
	Out.m_Position = Uniforms.m_MVP * float4(Position, 0.0, 1.0);
	Out.m_TexCoord = Vertex.m_TexCoord;
	Out.m_Color = float4(Vertex.m_Color);
	return Out;
}

fragment float4 qmclient_quad_container_ex_fragment(SMetalVertexOut Input [[stage_in]], constant SMetalQuadContainerUniforms &Uniforms [[buffer(1)]])
{
	return Input.m_Color * Uniforms.m_VertexColor;
}

fragment float4 qmclient_quad_container_ex_textured_fragment(SMetalVertexOut Input [[stage_in]], texture2d<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]], constant SMetalQuadContainerUniforms &Uniforms [[buffer(1)]])
{
	return Texture.sample(Sampler, Input.m_TexCoord) * Input.m_Color * Uniforms.m_VertexColor;
}

vertex SMetalVertexOut qmclient_sprite_multiple_vertex(SMetalVertex Vertex [[stage_in]], device const SMetalSpriteMultipleUniforms &Uniforms [[buffer(1)]], uint InstanceId [[instance_id]])
{
	SMetalVertexOut Out;
	const float4 RenderInfo = Uniforms.m_aRenderInfo[InstanceId];
	float2 Position = Vertex.m_Position;
	const float Rotation = RenderInfo.w;
	if(Rotation != 0.0)
	{
		const float2 Relative = Position - Uniforms.m_Center.xy;
		const float SinRotation = sin(Rotation);
		const float CosRotation = cos(Rotation);
		Position = float2(Relative.x * CosRotation - Relative.y * SinRotation, Relative.x * SinRotation + Relative.y * CosRotation) + Uniforms.m_Center.xy;
	}
	Position *= RenderInfo.z;
	Position += RenderInfo.xy;
	Out.m_Position = Uniforms.m_MVP * float4(Position, 0.0, 1.0);
	Out.m_TexCoord = Vertex.m_TexCoord;
	Out.m_Color = float4(Vertex.m_Color);
	return Out;
}

fragment float4 qmclient_sprite_multiple_fragment(SMetalVertexOut Input [[stage_in]], device const SMetalSpriteMultipleUniforms &Uniforms [[buffer(1)]])
{
	return Input.m_Color * Uniforms.m_VertexColor;
}

fragment float4 qmclient_sprite_multiple_textured_fragment(SMetalVertexOut Input [[stage_in]], texture2d<float> Texture [[texture(0)]], sampler Sampler [[sampler(0)]], device const SMetalSpriteMultipleUniforms &Uniforms [[buffer(1)]])
{
	return Texture.sample(Sampler, Input.m_TexCoord) * Input.m_Color * Uniforms.m_VertexColor;
}
