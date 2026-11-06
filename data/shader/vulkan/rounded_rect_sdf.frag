#version 450
#extension GL_ARB_separate_shader_objects : enable

#ifdef TW_ROUNDED_SDF_TEXTURED
layout(set = 0, binding = 0) uniform sampler2D gTextureSampler;
#endif

// 纹理变体的管线布局有两个描述符集：set 0 放图片采样器，参数块跟着落到 set 1；
// 无纹理变体只有一个 set，参数块就在 set 0。set 号写错时参数块读到的是未绑定
// 内存（全 0），片元会把「启用纹理」当成关闭，照片整块透明——这正是照片卡只剩
// 外框的原因。
#ifdef TW_ROUNDED_SDF_TEXTURED
layout(std140, set = 1, binding = 1)
#else
layout(std140, set = 0, binding = 1)
#endif
uniform SRoundedRectSdf
{
	vec4 gRoundedRectSdfData[17];
} gSdf;

layout(location = 0) noperspective in vec2 texCoord;
layout(location = 0) out vec4 FragClr;

// 圆角形状由两个参数描述：连续曲率强度 Smoothing（0 = 圆弧），
// 以及外扩 Expand（整块形状按距离场向外偏移，等价于同一条曲线的等距外扩）。
// 阴影 / 描边 / 填充都从这一份距离场派生，曲率变化时层与层才不会错位。
// 圆锥曲线角（有理二次 Bézier）：P0=(E,0)、P1=(E,E)、P2=(0,E)，权重 W。
// W = 0.7071 时正好是圆弧；W 越大角越平。零等值线就是这条曲线本身。
vec2 ConicCornerPoint(float T, float E, float W)
{
	float B0 = (1.0 - T) * (1.0 - T);
	float B1 = 2.0 * W * T * (1.0 - T);
	float B2 = T * T;
	float Den = B0 + B1 + B2;
	return vec2((B0 + B1) * E, (B1 + B2) * E) / Den;
}

vec2 ConicCornerDerivative(float T, float E, float W)
{
	float B0 = (1.0 - T) * (1.0 - T);
	float B1 = 2.0 * W * T * (1.0 - T);
	float B2 = T * T;
	float D0 = -2.0 * (1.0 - T);
	float D1 = 2.0 * W * (1.0 - 2.0 * T);
	float D2 = 2.0 * T;
	float Den = B0 + B1 + B2;
	float DDen = D0 + D1 + D2;
	vec2 Num = vec2((B0 + B1) * E, (B1 + B2) * E);
	vec2 DNum = vec2((D0 + D1) * E, (D1 + D2) * E);
	return (DNum * Den - Num * DDen) / (Den * Den);
}

// 到圆锥曲线角的近似带符号距离：负值在形状内部（离角点更近的一侧）。
float ConicCornerDistance(vec2 P, float E, float W)
{
	float Best = 1e9;
	float BestT = 0.5;
	for(int i = 0; i <= 4; i++)
	{
		float T = float(i) / 4.0;
		float Dist = length(ConicCornerPoint(T, E, W) - P);
		if(Dist < Best)
		{
			Best = Dist;
			BestT = T;
		}
	}
	float T = BestT;
	for(int i = 0; i < 4; i++)
	{
		vec2 Q = ConicCornerPoint(T, E, W);
		vec2 D = ConicCornerDerivative(T, E, W);
		T = clamp(T - dot(Q - P, D) / max(dot(D, D), 0.000001), 0.0, 1.0);
	}
	vec2 Q = ConicCornerPoint(T, E, W);
	float Dist = length(Q - P);
	// Corner 坐标离角点越近越靠内：比曲线更近角点 = 形状内部 = 负值。
	return dot(P - Q, P) > 0.0 ? Dist : -Dist;
}

float RoundedRectSdf(vec2 Point, vec2 HalfSize, float Radius, float Smoothing, float CurveType)
{
	// 连续曲率：圆角沿边铺得更长（最多 1.4 倍），曲率从直边处的 0 开始爬升，
	// 角顶因此比同尺寸圆弧略紧一点，但不会留下曲率突变的硬接缝。
	float Amount = clamp(Smoothing, 0.0, 1.0);
	float EffectiveRadius = min(Radius * (1.0 + 1.3 * Amount), min(HalfSize.x, HalfSize.y));
	vec2 DistanceToInner = abs(Point) - HalfSize + EffectiveRadius;
	vec2 Corner = max(DistanceToInner, vec2(0.0));
	float Flat = min(max(DistanceToInner.x, DistanceToInner.y), 0.0);
	// 只有两个分量都为正才是圆角区；边上和内部直接用圆的距离，
	// 否则 p 范数在两个分量都为 0 时会被归一化除成极大值，整块内部被误判成外部。
	if(Amount <= 0.001 || Corner.x <= 0.0 || Corner.y <= 0.0)
		return length(Corner) + Flat - EffectiveRadius;
	// 曲线类型：0 = 超椭圆（p 范数），1 = 圆锥曲线角（有理二次 Bézier）。
	if(CurveType > 0.5)
		return ConicCornerDistance(Corner, EffectiveRadius, mix(0.70710678, 1.2, Amount));
	float Exponent = mix(2.0, 4.0, Amount);
	float PowX = pow(Corner.x, Exponent);
	float PowY = pow(Corner.y, Exponent);
	float Sum = max(PowX + PowY, 0.0000001);
	// 角区里 Flat 恒为 0，形状值就是 p 范数减半径：曲率在直边处为 0，向角顶连续升高。
	float Norm = pow(Sum, 1.0 / Exponent);
	float ShapeValue = Norm - EffectiveRadius;
	// p 范数的梯度不是单位长度，要把「整份形状值」除以梯度模长才换成近似距离，
	// 零等值线因此完全不动。只缩放范数、把半径留在缩放外面，角顶有效半径会被拉小，
	// 画出来就是凹进去的缺口。
	float Scale = pow(Sum, 1.0 / Exponent - 1.0);
	float GradientX = PowX / max(Corner.x, 0.0000001) * Scale;
	float GradientY = PowY / max(Corner.y, 0.0000001) * Scale;
	float GradientLength = max(length(vec2(GradientX, GradientY)), 0.0001);
	return ShapeValue / GradientLength;
}
float CornerRadius(vec2 Point, vec4 CornerRadii)
{
	if(Point.y < 0.0)
		return Point.x < 0.0 ? CornerRadii.x : CornerRadii.y;
	return Point.x < 0.0 ? CornerRadii.w : CornerRadii.z;
}

float SdfFeather(float DistanceValue, float PixelSize)
{
	return max(PixelSize, length(vec2(dFdx(DistanceValue), dFdy(DistanceValue))));
}

float Coverage(float DistanceValue, float PixelSize)
{
	float Feather = SdfFeather(DistanceValue, PixelSize);
	return 1.0 - smoothstep(-Feather * 0.5, Feather * 0.5, DistanceValue);
}

// 渐变语义与 CPU 端 SQmColorGradient::Position/Sample 保持一致。
float GradientAmount(vec2 UV, vec4 Params0, vec4 Params1)
{
	vec2 Offset = UV - Params0.yz;
	float Along = dot(Offset, Params1.xy);
	float Across = dot(Offset, vec2(-Params1.y, Params1.x));
	float Extent = abs(Params1.x) + abs(Params1.y);
	float Range = max(Params0.w, 0.0001);
	float Amount = 0.0;
	int Type = int(Params0.x + 0.5);
	if(Type == 0)
		Amount = 0.5 + Along / max(Range * Extent, 0.0001);
	else if(Type == 1)
		Amount = length(Offset) * 2.0 / Range;
	else if(Type == 2)
	{
		if(length(Offset) > 0.00001)
		{
			Amount = atan(Across, Along) / (2.0 * 3.14159265359);
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

vec4 GradientColor(vec2 UV, vec4 SolidColor)
{
	vec4 Params0 = gSdf.gRoundedRectSdfData[12];
	vec4 Params1 = gSdf.gRoundedRectSdfData[13];
	vec4 Params2 = gSdf.gRoundedRectSdfData[14];
	if(Params2.x < 0.5)
		return SolidColor;
	int Count = clamp(int(Params1.w + 0.5), 1, 7);
	if(Count <= 1)
		return gSdf.gRoundedRectSdfData[5];
	float Amount = GradientAmount(UV, Params0, Params1);
	float Scaled = Amount * float(Count - 1);
	int Index = min(int(floor(Scaled)), Count - 2);
	float T = Scaled - float(Index);
	vec4 From = gSdf.gRoundedRectSdfData[5 + Index];
	vec4 To = gSdf.gRoundedRectSdfData[5 + Index + 1];
	return mix(From, To, T);
}

void main()
{
	vec4 Rect = gSdf.gRoundedRectSdfData[0];
	vec4 BorderColor = gSdf.gRoundedRectSdfData[2];
	vec4 CornerRadii = gSdf.gRoundedRectSdfData[3];
	vec4 Params = gSdf.gRoundedRectSdfData[4];
	float CornerSmoothing = clamp(gSdf.gRoundedRectSdfData[14].w, 0.0, 1.0);
	// 圆角曲线类型：0 = 超椭圆，1 = 圆锥曲线角（有理二次 Bézier）
	float CornerCurveType = gSdf.gRoundedRectSdfData[4].w;
	// 形状外扩：四角形状按基础矩形算完再整体外扩，叠层之间因此保持等距。
	float ShapeExpand = clamp(gSdf.gRoundedRectSdfData[16].w, 0.0, 64.0);
	vec2 HalfSize = Rect.zw * 0.5;
	vec2 Point = (texCoord - vec2(0.5)) * (Rect.zw + vec2(Params.z * 2.0));
	float BorderWidth = clamp(Params.x, 0.0, min(HalfSize.x, HalfSize.y));
	// 渐变坐标必须和几何回退一致：都以边框内缩后的矩形为 0..1 范围。
	vec2 InnerHalfSize = HalfSize - vec2(BorderWidth);
	vec2 GradientExtent = max(InnerHalfSize, vec2(0.0));
	vec2 GradientUV = clamp((Point + GradientExtent) / max(GradientExtent * 2.0, vec2(0.0001)), vec2(0.0), vec2(1.0));
	vec4 FillColor = GradientColor(GradientUV, gSdf.gRoundedRectSdfData[1]);
	vec2 BaseHalfSize = max(HalfSize - vec2(ShapeExpand), vec2(0.5));
	float Radius = clamp(CornerRadius(Point, CornerRadii), 0.0, min(BaseHalfSize.x, BaseHalfSize.y));
	float OuterDistance = RoundedRectSdf(Point, BaseHalfSize, Radius, CornerSmoothing, CornerCurveType) - ShapeExpand;
	float OuterCoverage = Coverage(OuterDistance, Params.y);
	// 内缘是同一形状向内偏移一个边框宽度：换成连续曲率后「半径减边框」不再是等距内缩，
	// 会让环带宽度不均、和外层错位，所以直接从同一份距离场加偏移。
	float InnerDistance = OuterDistance + BorderWidth;
	float InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? Coverage(InnerDistance, Params.y) : 0.0;
	// 填充覆盖率：有边框时以环带内缘为界，无边框时整块形状都是填充。
	float FillCoverage = BorderWidth > 0.0 ? InnerCoverage : OuterCoverage;
#ifdef TW_ROUNDED_SDF_TEXTURED
	// 圆角容器放图：纹理取代纯色填充，并共用同一个圆角覆盖度，四角因此被真实
	// 切掉。放大/平移后图片铺满矩形也不再露出方角，因为方角本身已经不画了。
	// UV 是「逻辑点 → 图片 UV」的仿射，缩放与平移由调用方折算进 Params[15]/[16]；
	// Params[16].y 是整张照片的透明度（卡片淡出时照片和边框一起消失）。
	vec4 TexParams0 = gSdf.gRoundedRectSdfData[15];
	vec4 TexParams1 = gSdf.gRoundedRectSdfData[16];
	if(TexParams0.x > 0.5)
	{
		// 图片矩形之外保持透明：图片没铺满容器时，边缘和普通 quad 一样是硬边。
		vec2 TexUV = Point * TexParams0.yz + vec2(TexParams0.w, TexParams1.x);
		vec4 TexColor = vec4(0.0);
		if(all(greaterThanEqual(TexUV, vec2(0.0))) && all(lessThanEqual(TexUV, vec2(1.0))))
			TexColor = texture(gTextureSampler, TexUV);
		if(TexParams0.x > 1.5)
		{
			// 距离场只存轮廓，颜色由调用方提供；按屏幕像素恢复覆盖率，缩放不重建纹理。
			vec2 UVWidth = max(fwidth(TexUV), vec2(0.000001));
			float ScreenRange = max(0.5 * dot(vec2(TexParams1.z), vec2(1.0) / UVWidth), 1.0);
			float IconCoverage = clamp((TexColor.r - 0.5) * ScreenRange + 0.5, 0.0, 1.0);
			TexColor = vec4(FillColor.rgb, FillColor.a * IconCoverage);
		}
		TexColor.a *= clamp(TexParams1.y, 0.0, 1.0);
		FillColor = TexColor;
		FillCoverage = OuterCoverage;
	}
#endif
	if(BorderWidth <= 0.0)
	{
		FragClr = vec4(FillColor.rgb, FillColor.a * FillCoverage);
		return;
	}
	float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);
	// GradientParams2.z > 0 时按带宽做指数羽化：边框从内缘到外缘逐渐透明，
	// 才有多层辉光叠加的"晕"感。等于 0 时保持原有的实心边框。
	float BorderFalloff = gSdf.gRoundedRectSdfData[14].z;
	if(BorderFalloff > 0.0 && BorderWidth > 0.0)
	{
		// 环带内缘 InnerDistance 约 0，向外缘增至 +BorderWidth；
		// BandT 因此从内缘 0 到外缘 1，按幂次衰减成向外淡出的光晕。
		float BandT = clamp(InnerDistance / max(BorderWidth, 0.0001), 0.0, 1.0);
		BorderCoverage *= pow(max(1.0 - BandT, 0.0), BorderFalloff);
	}
	// GradientParams2.y 是"只描边"开关：渐变只服务于边框，填充整体丢弃。
	// 彩虹光环这类描边效果必须走这条路，否则渐变会把整张卡片染成彩色。
	bool BorderOnly = gSdf.gRoundedRectSdfData[14].y > 0.5;
	if(BorderOnly)
		InnerCoverage = 0.0;
	float FillAlphaMask = BorderOnly ? 0.0 : 1.0;
	// Params.w 作为"边框取渐变"开关（默认 0 保持既有单色边框行为）：
	// 描边类效果（彩虹光环）需要边框本身沿角度取色，而单色 BorderColor 做不到。
	vec4 BorderTint = BorderColor;
	if(Params.w > 0.5)
	{
		// 渐变只提供色相，描边强弱仍由调用方给的 BorderColor.a 决定；
		// 直接沿用渐变的 alpha 会让三层光晕强度全变成 1。
		// 角度渐变直接用相对几何中心的 Point：它是等比例逻辑单位，atan 得到真实
		// 角度。用 0..1 归一化 UV 会把长条卡片的角度压扁成横向色带（4:1 时压 4 倍）。
		vec2 PolarUV = vec2(Point.x / max(Rect.z, 0.0001) + 0.5, Point.y / max(Rect.z, 0.0001) + 0.5);
		vec4 GradientBorder = GradientColor(PolarUV, BorderColor);
		GradientBorder.a = BorderColor.a;
		BorderTint = GradientBorder;
	}
	float BorderAlpha = BorderTint.a * BorderCoverage;
	float FillAlpha = FillColor.a * FillCoverage * FillAlphaMask;
	float OutputAlpha = FillAlpha + BorderAlpha;
	vec3 Premultiplied = FillColor.rgb * FillAlpha + BorderTint.rgb * BorderAlpha;
	FragClr = OutputAlpha > 0.0 ? vec4(Premultiplied / OutputAlpha, OutputAlpha) : vec4(0.0);
}
