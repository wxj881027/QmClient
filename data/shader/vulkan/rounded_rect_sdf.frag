#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(std140, set = 0, binding = 1) uniform SRoundedRectSdf
{
	vec4 gRoundedRectSdfData[15];
} gSdf;

layout(location = 0) noperspective in vec2 texCoord;
layout(location = 0) out vec4 FragClr;

float RoundedRectSdf(vec2 Point, vec2 HalfSize, float Radius)
{
	vec2 DistanceToInner = abs(Point) - HalfSize + Radius;
	return length(max(DistanceToInner, vec2(0.0))) + min(max(DistanceToInner.x, DistanceToInner.y), 0.0) - Radius;
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
	vec2 HalfSize = Rect.zw * 0.5;
	vec2 Point = (texCoord - vec2(0.5)) * (Rect.zw + vec2(Params.z * 2.0));
	float BorderWidth = clamp(Params.x, 0.0, min(HalfSize.x, HalfSize.y));
	// 渐变坐标必须和几何回退一致：都以边框内缩后的矩形为 0..1 范围。
	vec2 InnerHalfSize = HalfSize - vec2(BorderWidth);
	vec2 GradientExtent = max(InnerHalfSize, vec2(0.0));
	vec2 GradientUV = clamp((Point + GradientExtent) / max(GradientExtent * 2.0, vec2(0.0001)), vec2(0.0), vec2(1.0));
	vec4 FillColor = GradientColor(GradientUV, gSdf.gRoundedRectSdfData[1]);
	float Radius = clamp(CornerRadius(Point, CornerRadii), 0.0, min(HalfSize.x, HalfSize.y));
	float OuterDistance = RoundedRectSdf(Point, HalfSize, Radius);
	float OuterCoverage = Coverage(OuterDistance, Params.y);
	vec4 InnerCornerRadii = max(CornerRadii - vec4(BorderWidth), vec4(0.0));
	float InnerRadius = clamp(CornerRadius(Point, InnerCornerRadii), 0.0, min(InnerHalfSize.x, InnerHalfSize.y));
	float InnerDistance = RoundedRectSdf(Point, InnerHalfSize, InnerRadius);
	float InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? Coverage(InnerDistance, Params.y) : 0.0;
	if(BorderWidth <= 0.0)
	{
		FragClr = vec4(FillColor.rgb, FillColor.a * OuterCoverage);
		return;
	}
	float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);
	float BorderAlpha = BorderColor.a * BorderCoverage;
	float FillAlpha = FillColor.a * InnerCoverage;
	float OutputAlpha = FillAlpha + BorderAlpha;
	vec3 Premultiplied = FillColor.rgb * FillAlpha + BorderColor.rgb * BorderAlpha;
	FragClr = OutputAlpha > 0.0 ? vec4(Premultiplied / OutputAlpha, OutputAlpha) : vec4(0.0);
}
