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

fragment float4 qmclient_procedural_ring_fragment(SMetalVertexOut Input [[stage_in]], constant float4 &RingParams [[buffer(1)]])
{
	const float InnerRadius = RingParams.x;
	const float OuterRadius = RingParams.y;
	const float Sweep = max(RingParams.w - RingParams.z, 0.0);
	const float2 Point = Input.m_TexCoord - float2(0.5);
	const float Radius = length(Point);
	const float RadialDistance = max(InnerRadius - Radius, Radius - OuterRadius);
	const float RadialFeather = max(fwidth(Radius), 0.0005);
	const float RadialCoverage = 1.0 - smoothstep(-RadialFeather * 0.5, RadialFeather * 0.5, RadialDistance);
	const float Angle = atan2(Point.y, Point.x);
	float RelativeAngle = fmod(Angle - RingParams.z, 6.28318530718);
	if(RelativeAngle < 0.0)
		RelativeAngle += 6.28318530718;
	const float AngularFeather = max(fwidth(Angle), 0.0015);
	const float AngularCoverage = Sweep >= 6.2830 ? 1.0 : smoothstep(0.0, AngularFeather, RelativeAngle) * smoothstep(0.0, AngularFeather, Sweep - RelativeAngle);
	return float4(Input.m_Color.rgb, Input.m_Color.a * RadialCoverage * AngularCoverage);
}

// 圆角形状由曲线档位 CurveType、连续曲率强度 Smoothing（0 = 圆弧）和外扩 Expand
// （整块形状按距离场向外偏移，等价于同一条曲线的等距外扩）描述。
//
// 曲线档位：0 = 原圆弧（旧外观），1 = 超椭圆（p 范数），2 = 五次贝塞尔，
// 3 = 圆锥贝塞尔（有理二次）。超椭圆与五次贝塞尔的端点曲率都是 0，是真正的
// 连续曲率；圆锥贝塞尔的端点曲率恒为 1/(2W^2 E)，只能把接缝跳变压小，做不到 0。
// 圆锥曲线角（有理二次 Bézier）：P0=(E,0)、P1=(E,E)、P2=(0,E)，权重 W。
// W = 0.7071 时正好是圆弧；W 越大角越平。零等值线就是这条曲线本身。
float2 ConicCornerPoint(float T, float E, float W)
{
	const float B0 = (1.0 - T) * (1.0 - T);
	const float B1 = 2.0 * W * T * (1.0 - T);
	const float B2 = T * T;
	const float Den = B0 + B1 + B2;
	return float2((B0 + B1) * E, (B1 + B2) * E) / Den;
}

float2 ConicCornerDerivative(float T, float E, float W)
{
	const float B0 = (1.0 - T) * (1.0 - T);
	const float B1 = 2.0 * W * T * (1.0 - T);
	const float B2 = T * T;
	const float D0 = -2.0 * (1.0 - T);
	const float D1 = 2.0 * W * (1.0 - 2.0 * T);
	const float D2 = 2.0 * T;
	const float Den = B0 + B1 + B2;
	const float DDen = D0 + D1 + D2;
	const float2 Num = float2((B0 + B1) * E, (B1 + B2) * E);
	const float2 DNum = float2((D0 + D1) * E, (D1 + D2) * E);
	return (DNum * Den - Num * DDen) / (Den * Den);
}

float ConicCornerDistance(float2 P, float E, float W)
{
	float Best = 1e9;
	float BestT = 0.5;
	for(int i = 0; i <= 4; i++)
	{
		const float T = float(i) / 4.0;
		const float Dist = length(ConicCornerPoint(T, E, W) - P);
		if(Dist < Best)
		{
			Best = Dist;
			BestT = T;
		}
	}
	float T = BestT;
	for(int i = 0; i < 4; i++)
	{
		const float2 Q = ConicCornerPoint(T, E, W);
		const float2 D = ConicCornerDerivative(T, E, W);
		T = clamp(T - dot(Q - P, D) / max(dot(D, D), 0.000001), 0.0, 1.0);
	}
	const float2 Q = ConicCornerPoint(T, E, W);
	const float Dist = length(Q - P);
	// Corner 坐标离角点越近越靠内：比曲线更近角点 = 形状内部 = 负值。
	return dot(P - Q, P) > 0.0 ? Dist : -Dist;
}

// 生长上限：普通控件封在短边的 25%，已经是胶囊的元素（半径达到短边 9/10）放宽到
// 短边的 40%。这里用半边长表示，所以系数是 0.5 / 0.8。上限只限制生长，
// 不回收调用方本来就给的更大半径。
float RoundedCornerCapRatio(float Radius, float MinHalf)
{
	return Radius >= MinHalf * (9.0 / 10.0) ? 0.8 : 0.5;
}

// 连续曲率的代价是曲率峰值升高：90° 转弯、端点曲率归零、圆角长度不变时，峰值
// 必然高于圆弧（超椭圆满强度能到 2.5 倍）。所以圆角沿边拉长来补偿，拉长量受有效
// 半径上限约束，曲线强度按「实际拿到多少生长」反解——扩不动时（胶囊、短边很近的
// 小控件）自动退回圆弧，不会被强行压弯。
// x = 实际生效的圆角半径（含沿边拉长），y = 实际生效的曲线强度。
float2 RoundedCornerShape(float2 HalfSize, float Radius, float Smoothing, float CurveType, float SeamAmount)
{
	// 原圆弧档就是旧外观：不扩展、也不换曲线。
	const float Amount = CurveType > 0.5 ? clamp(Smoothing, 0.0, 1.0) : 0.0;
	const float MinHalf = max(min(HalfSize.x, HalfSize.y), 0.0001);
	const float ClampedRadius = clamp(Radius, 0.0, MinHalf);
	if(ClampedRadius <= 0.0 || Amount <= 0.0)
		return float2(ClampedRadius, 0.0);
	const float MaxRadius = max(MinHalf * RoundedCornerCapRatio(ClampedRadius, MinHalf), ClampedRadius);
	const float EffectiveRadius = clamp(ClampedRadius * (1.0 + 1.3 * Amount), ClampedRadius, MaxRadius);
	float EffectiveAmount = clamp((EffectiveRadius / ClampedRadius - 1.0) / 1.3, 0.0, 1.0);
	// 胶囊没有生长空间，但半圆端头与直边的接缝可以单独软化：给一个远小于半径生长的
	// 曲线强度，曲率在几个像素内从 0 缓升到 1/R，形状几乎不变，只是肩部不再突然拐弯。
	// 只有「真的有一段直边」的胶囊才谈得上接缝：正圆（滑块圆点、单选按钮）四个角
	// 本来就是连续的，不该被压成方圆，所以再要求长边明显长于短边。
	const bool Capsule = ClampedRadius >= MinHalf * (9.0 / 10.0) && max(HalfSize.x, HalfSize.y) > MinHalf * 1.2;
	if(Capsule)
		EffectiveAmount = max(EffectiveAmount, min(Amount, clamp(SeamAmount, 0.0, 1.0)));
	return float2(EffectiveRadius, EffectiveAmount);
}

// 五次贝塞尔圆角：控制点 P0=(1,0)、P1=(1,y1)、P2=(x2,y2)、P3=(y2,x2)、
// P4=(y1,1)、P5=(0,1)。前两个控制点都落在 x=1 上，端点切向沿直边、曲率恒为 0，
// 这就是「真连续曲率」的来源。两组取值之间按强度插值：0 取曲率匹配的三次圆弧
// 升阶结果（几乎就是圆弧），1 取拟合圆锥 W=1.2 手感的那组（外形偏差 0.6%）。
// 一次返回点与切向量：牛顿迭代每轮两样都要，分开写会把伯恩斯坦求和做两遍。
float4 QuinticCorner(float T, float E, float Amount)
{
	const float Y1 = mix(0.3291504, 0.14, Amount);
	const float X2 = mix(0.8645747, 1.0, Amount);
	const float Y2 = mix(0.6291504, 0.80, Amount);
	const float U = 1.0 - T;
	const float U2 = U * U;
	const float T2 = T * T;
	const float B0 = U2 * U2 * U;
	const float B1 = 5.0 * T * U2 * U2;
	const float B2 = 10.0 * T2 * U2 * U;
	const float B3 = 10.0 * T2 * T * U2;
	const float B4 = 5.0 * T2 * T2 * U;
	const float B5 = T2 * T2 * T;
	const float2 Point = B0 * float2(1.0, 0.0) + B1 * float2(1.0, Y1) + B2 * float2(X2, Y2) + B3 * float2(Y2, X2) + B4 * float2(Y1, 1.0) + B5 * float2(0.0, 1.0);
	const float D0 = 5.0 * U2 * U2;
	const float D1 = 20.0 * T * U2 * U;
	const float D2 = 30.0 * T2 * U2;
	const float D3 = 20.0 * T2 * T * U;
	const float D4 = 5.0 * T2 * T2;
	const float2 Derivative = D0 * float2(0.0, Y1) + D1 * (float2(X2, Y2) - float2(1.0, Y1)) + D2 * (float2(Y2, X2) - float2(X2, Y2)) + D3 * (float2(Y1, 1.0) - float2(Y2, X2)) + D4 * (float2(0.0, 1.0) - float2(Y1, 1.0));
	return float4(Point.x, Point.y, Derivative.x, Derivative.y) * E;
}

float QuinticCornerDistance(float2 P, float E, float Amount)
{
	float Best = 1e9;
	float BestT = 0.5;
	for(int i = 0; i <= 5; i++)
	{
		const float T = float(i) / 5.0;
		const float Dist = length(QuinticCorner(T, E, Amount).xy - P);
		if(Dist < Best)
		{
			Best = Dist;
			BestT = T;
		}
	}
	float T = BestT;
	for(int i = 0; i < 4; i++)
	{
		const float4 Node = QuinticCorner(T, E, Amount);
		T = clamp(T - dot(Node.xy - P, Node.zw) / max(dot(Node.zw, Node.zw), 0.000001), 0.0, 1.0);
	}
	const float2 Q = QuinticCorner(T, E, Amount).xy;
	const float Dist = length(Q - P);
	// Corner 坐标离角点越近越靠内：比曲线更近角点 = 形状内部 = 负值。
	return dot(P - Q, P) > 0.0 ? Dist : -Dist;
}

float RoundedRectDistance(float2 Point, float2 HalfSize, float Radius, float Smoothing, float CurveType, float SeamAmount)
{
	const float2 Shape = RoundedCornerShape(HalfSize, Radius, Smoothing, CurveType, SeamAmount);
	const float EffectiveRadius = Shape.x;
	const float Amount = Shape.y;
	const float2 DistanceToInner = abs(Point) - HalfSize + EffectiveRadius;
	const float2 Corner = max(DistanceToInner, float2(0.0));
	const float Flat = min(max(DistanceToInner.x, DistanceToInner.y), 0.0);
	// 只有两个分量都为正才是圆角区；边上和内部直接用圆的距离。
	if(Amount <= 0.001 || Corner.x <= 0.0 || Corner.y <= 0.0)
		return length(Corner) + Flat - EffectiveRadius;
	// 曲线档位：1 = 超椭圆（p 范数），2 = 五次贝塞尔，3 = 圆锥贝塞尔。
	const int Type = int(CurveType + 0.5);
	if(Type == 2)
		return QuinticCornerDistance(Corner, EffectiveRadius, Amount);
	if(Type == 3)
		return ConicCornerDistance(Corner, EffectiveRadius, mix(0.70710678, 1.2, Amount));
	const float Exponent = mix(2.0, 4.0, Amount);
	const float PowX = pow(Corner.x, Exponent);
	const float PowY = pow(Corner.y, Exponent);
	const float Sum = max(PowX + PowY, 0.0000001);
	// 角区里 Flat 恒为 0，形状值就是 p 范数减半径。
	const float Norm = pow(Sum, 1.0 / Exponent);
	const float ShapeValue = Norm - EffectiveRadius;
	// p 范数的梯度不是单位长度，要把「整份形状值」除以梯度模长才换成近似距离。
	const float Scale = pow(Sum, 1.0 / Exponent - 1.0);
	const float GradientX = PowX / max(Corner.x, 0.0000001) * Scale;
	const float GradientY = PowY / max(Corner.y, 0.0000001) * Scale;
	const float GradientLength = max(length(float2(GradientX, GradientY)), 0.0001);
	return ShapeValue / GradientLength;
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

// 渐变语义与 CPU 端 SQmColorGradient::Position/Sample 保持一致。
float RoundedRectGradientAmount(float2 UV, float4 Params0, float4 Params1)
{
	const float2 Offset = UV - Params0.yz;
	const float Along = dot(Offset, Params1.xy);
	const float Across = dot(Offset, float2(-Params1.y, Params1.x));
	const float Extent = abs(Params1.x) + abs(Params1.y);
	const float Range = max(Params0.w, 0.0001);
	float Amount = 0.0;
	const int Type = int(Params0.x + 0.5);
	if(Type == 0)
		Amount = 0.5 + Along / max(Range * Extent, 0.0001);
	else if(Type == 1)
		Amount = length(Offset) * 2.0 / Range;
	else if(Type == 2)
	{
		if(length(Offset) > 0.00001)
		{
			Amount = atan2(Across, Along) / (2.0 * 3.14159265359);
			Amount -= floor(Amount);
			Amount /= Range;
		}
	}
	else if(Type == 3)
		Amount = abs(Along) * 2.0 / max(Range * Extent, 0.0001);
	else
		Amount = (abs(Along) + abs(Across)) * 2.0 / Range;
	Amount = clamp(Amount, 0.0, 1.0);
	return Params1.z > 0.5 ? 1.0 - Amount : Amount;
}

float4 RoundedRectGradientColor(float2 UV, float4 SolidColor, constant float4 *Params)
{
	if(Params[14].x < 0.5)
		return SolidColor;
	const int Count = clamp(int(Params[13].w + 0.5), 1, 7);
	if(Count <= 1)
		return Params[5];
	const float Amount = RoundedRectGradientAmount(UV, Params[12], Params[13]);
	const float Scaled = Amount * float(Count - 1);
	const int Index = min(int(floor(Scaled)), Count - 2);
	const float T = Scaled - float(Index);
	return mix(Params[5 + Index], Params[5 + Index + 1], T);
}

fragment float4 qmclient_rounded_rect_sdf_fragment(SMetalVertexOut Input [[stage_in]], constant float4 *Params [[buffer(1)]])
{
	const float4 Rect = Params[0];
	float4 FillColor = Params[1];
	const float4 BorderColor = Params[2];
	const float4 CornerRadii = Params[3];
	const float4 RenderParams = Params[4];
	const float CornerSmoothing = clamp(Params[14].w, 0.0, 1.0);
	// 圆角曲线档位：0 = 原圆弧，1 = 超椭圆，2 = 五次贝塞尔，3 = 圆锥贝塞尔。
	// 档位在 Params[17].x、胶囊接缝软化在 Params[17].y；RenderParams.w 仍然是
	// 「边框取渐变」开关，两者不共用分量。
	const float CurveType = Params[17].x;
	const float CornerSeam = clamp(Params[17].y, 0.0, 1.0);
	// 形状外扩：四角形状按基础矩形算完再整体外扩，叠层之间因此保持等距。
	const float ShapeExpand = clamp(Params[16].w, 0.0, 64.0);
	const float2 HalfSize = Rect.zw * 0.5;
	const float2 Point = (Input.m_TexCoord - float2(0.5)) * (Rect.zw + float2(RenderParams.z * 2.0));
	const float BorderWidth = clamp(RenderParams.x, 0.0, min(HalfSize.x, HalfSize.y));
	// 渐变坐标必须和几何回退一致：都以边框内缩后的矩形为 0..1 范围。
	const float2 InnerHalfSize = HalfSize - float2(BorderWidth);
	const float2 GradientExtent = max(InnerHalfSize, float2(0.0));
	const float2 GradientUV = clamp((Point + GradientExtent) / max(GradientExtent * 2.0, float2(0.0001)), float2(0.0), float2(1.0));
	FillColor = RoundedRectGradientColor(GradientUV, FillColor, Params);
	const float2 BaseHalfSize = max(HalfSize - float2(ShapeExpand), float2(0.5));
	const float Radius = clamp(RoundedRectCornerRadius(Point, CornerRadii), 0.0, min(BaseHalfSize.x, BaseHalfSize.y));
	const float OuterDistance = RoundedRectDistance(Point, BaseHalfSize, Radius, CornerSmoothing, CurveType, CornerSeam) - ShapeExpand;
	const float OuterCoverage = RoundedRectCoverage(OuterDistance, RenderParams.y);
	// 内缘是同一形状向内偏移一个边框宽度：连续曲率下「半径减边框」不是等距内缩。
	const float InnerDistance = OuterDistance + BorderWidth;
	// 与 rounded_rect_sdf.frag 保持一致：只描边时这里要被清零，不能是 const。
	float InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? RoundedRectCoverage(InnerDistance, RenderParams.y) : 0.0;
	if(BorderWidth <= 0.0)
		return float4(FillColor.rgb, FillColor.a * OuterCoverage) * Input.m_Color;
	float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);
	// GradientParams2.z > 0 时按带宽指数羽化，边框从内缘向外缘淡出成光晕。
	const float BorderFalloff = Params[14].z;
	if(BorderFalloff > 0.0 && BorderWidth > 0.0)
	{
		const float BandT = clamp(InnerDistance / max(BorderWidth, 0.0001), 0.0, 1.0);
		BorderCoverage *= pow(max(1.0 - BandT, 0.0), BorderFalloff);
	}
	// GradientParams2.y 是"只描边"开关：渐变只服务于边框，填充整体丢弃。
	const bool BorderOnly = Params[14].y > 0.5;
	if(BorderOnly)
		InnerCoverage = 0.0;
	const float FillAlphaMask = BorderOnly ? 0.0 : 1.0;
	// RenderParams.w 是"边框取渐变"开关（默认 0 保持既有单色边框行为）。
	float4 BorderTint = BorderColor;
	if(RenderParams.w > 0.5)
	{
		// 渐变只提供色相，描边强弱仍由调用方给的 BorderColor.a 决定。
		// 角度渐变用相对几何中心的 Point（等比例逻辑单位），避免归一化 UV
		// 把长条卡片的角度压扁成横向色带。
		const float2 PolarUV = float2(Point.x / max(Rect.z, 0.0001) + 0.5, Point.y / max(Rect.z, 0.0001) + 0.5);
		float4 GradientBorder = RoundedRectGradientColor(PolarUV, BorderColor, Params);
		GradientBorder.a = BorderColor.a;
		BorderTint = GradientBorder;
	}
	const float BorderAlpha = BorderTint.a * BorderCoverage;
	const float FillAlpha = FillColor.a * InnerCoverage * FillAlphaMask;
	const float OutputAlpha = FillAlpha + BorderAlpha;
	const float3 Premultiplied = FillColor.rgb * FillAlpha + BorderTint.rgb * BorderAlpha;
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
