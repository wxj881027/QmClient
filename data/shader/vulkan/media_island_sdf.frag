#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform sampler2D gBackdropSampler;
layout(std140, set = 1, binding = 1) uniform SMediaIslandSdf
{
	vec4 gMediaIslandSdfData[45];
} gSdf;

layout(location = 0) noperspective in vec2 texCoord;
layout(location = 1) noperspective in vec4 vertColor;
layout(location = 0) out vec4 FragClr;

const float PI = 3.14159265359;
const float TAU = 6.28318530718;
const int MAX_ITEMS = 12;
const int ITEM_BASE = 8;
const int ITEM_STRIDE = 3;
const int BACKDROP_UV = 44;

vec4 Data(int Index)
{
	return gSdf.gMediaIslandSdfData[Index];
}

bool HasCorner(float Flags, int Bit)
{
	return mod(floor(Flags / float(Bit)), 2.0) > 0.5;
}

float RoundedRectSdf(vec2 Point, vec4 Rect, float Radius, float Corners, float DisabledRadius)
{
	if(Rect.z <= 0.0 || Rect.w <= 0.0)
		return 1000000.0;
	vec2 HalfSize = Rect.zw * 0.5;
	vec2 Local = Point - (Rect.xy + HalfSize);
	int Corner = 8;
	if(Local.x < 0.0)
		Corner = Local.y < 0.0 ? 1 : 4;
	else if(Local.y < 0.0)
		Corner = 2;
	float RequestedRadius = HasCorner(Corners, Corner) ? Radius : DisabledRadius;
	float CornerRadius = clamp(RequestedRadius, 0.0, min(HalfSize.x, HalfSize.y));
	vec2 DistanceToInner = abs(Local) - HalfSize + CornerRadius;
	vec2 Outside = max(DistanceToInner, vec2(0.0));
	return length(Outside) + min(max(DistanceToInner.x, DistanceToInner.y), 0.0) - CornerRadius;
}

float BlobExponent(vec2 Radii)
{
	float MaxRadius = max(Radii.x, Radii.y);
	float RadiusDifference = MaxRadius > 0.0 ? abs(Radii.x - Radii.y) / MaxRadius : 0.0;
	return mix(2.0, 5.0, smoothstep(0.0, 0.1, RadiusDifference));
}

float BlobSdf(vec2 Point, vec2 Center, vec2 Radii)
{
	if(Radii.x <= 0.0 || Radii.y <= 0.0)
		return 1000000.0;
	float Exponent = BlobExponent(Radii);
	vec2 Normalized = abs((Point - Center) / Radii);
	float NormalizedDistance = pow(pow(Normalized.x, Exponent) + pow(Normalized.y, Exponent), 1.0 / Exponent);
	return (NormalizedDistance - 1.0) * min(Radii.x, Radii.y);
}

float SmoothUnion(float Left, float Right, float Blend)
{
	if(Blend <= 0.0)
		return min(Left, Right);
	float H = max(Blend - abs(Left - Right), 0.0) / Blend;
	return min(Left, Right) - H * H * Blend * 0.25;
}

float Coverage(float DistanceValue, float Feather)
{
	Feather = max(Feather, 0.0001);
	float T = clamp((DistanceValue + Feather) / (Feather * 2.0), 0.0, 1.0);
	return 1.0 - (T * T * (3.0 - 2.0 * T));
}

float ArcSdf(vec2 Point, vec2 Center, float Radius, float HalfThickness, float Progress)
{
	vec2 Relative = Point - Center;
	Progress = clamp(Progress, 0.0, 1.0);
	if(Progress <= 0.0)
		return 1000000.0;
	if(Progress >= 0.9999)
		return abs(length(Relative) - Radius) - HalfThickness;
	float Angle = atan(Relative.y, Relative.x) + PI * 0.5;
	if(Angle < 0.0)
		Angle += TAU;
	float EndAngle = Progress * TAU;
	if(Angle <= EndAngle)
		return abs(length(Relative) - Radius) - HalfThickness;
	vec2 StartPoint = Center + vec2(0.0, -Radius);
	vec2 EndPoint = Center + vec2(sin(EndAngle) * Radius, -cos(EndAngle) * Radius);
	return min(distance(Point, StartPoint), distance(Point, EndPoint)) - HalfThickness;
}

void Composite(inout vec3 PremulColor, inout float Alpha, vec4 Color, float ShapeCoverage)
{
	float SourceAlpha = clamp(Color.a * ShapeCoverage, 0.0, 1.0);
	PremulColor = Color.rgb * SourceAlpha + PremulColor * (1.0 - SourceAlpha);
	Alpha = SourceAlpha + Alpha * (1.0 - SourceAlpha);
}

void main()
{
	vec4 OuterRect = Data(0);
	vec4 MainParams = Data(4);
	vec4 Metadata = Data(5);
	vec4 ShadowParams = Data(7);
	vec2 Point = OuterRect.xy + texCoord * OuterRect.zw;
	float ScreenPixelSize = max(Metadata.w, 0.0001);
	float Feather = max(ScreenPixelSize * 0.8, max(fwidth(Point.x), fwidth(Point.y)) * 0.9);
	int ItemCount = clamp(int(Metadata.x + 0.5), 0, MAX_ITEMS);
	float MainRadius = MainParams.x;
	float MainDistance = RoundedRectSdf(Point, Data(1), MainRadius, Metadata.y, MainParams.y);
	float SatelliteDistance = 1000000.0;
	float ShapeDistance = MainDistance;
	for(int i = 0; i < ItemCount; ++i)
	{
		vec4 ItemShape = Data(ITEM_BASE + i * ITEM_STRIDE);
		vec4 ItemParams = Data(ITEM_BASE + i * ITEM_STRIDE + 1);
		float ItemDistance = BlobSdf(Point, ItemShape.xy, ItemShape.zw);
		SatelliteDistance = i == 0 ? ItemDistance : SmoothUnion(SatelliteDistance, ItemDistance, MainRadius * 0.28);
		float Smooth = ItemParams.x;
		if(Smooth > 0.0)
			ShapeDistance = min(ShapeDistance, SmoothUnion(MainDistance, ItemDistance, Smooth));
	}
	ShapeDistance = min(ShapeDistance, SatelliteDistance);

	vec4 CapsuleRect = Data(2);
	vec4 CapsuleParams = Data(6);
	if(Metadata.z > 0.5 && CapsuleRect.z > 0.0 && CapsuleRect.w > 0.0)
	{
		float CapsuleDistance = RoundedRectSdf(Point, CapsuleRect, CapsuleParams.x, 15.0, 0.0);
		ShapeDistance = min(ShapeDistance, CapsuleDistance);
		if(CapsuleParams.y > 0.0)
			ShapeDistance = min(ShapeDistance, SmoothUnion(MainDistance, CapsuleDistance, CapsuleParams.y));
	}

	vec3 PremulColor = vec3(0.0);
	float Alpha = 0.0;
	vec4 Background = Data(3);
	// 面板整体不透明度：模糊底图与背景色一起按它淡出，所以透明度 0 时整块板（含外圈阴影）消失。
	// 不能写成 const（clamp 结果不是常量表达式，GLSL 330 不允许 const 非常量初始化）。
	float PanelAlpha = clamp(Background.a, 0.0, 1.0);
	float ShadowSize = max(ShadowParams.x, 0.0);
	if(ShadowSize > 0.0 && ShadowParams.y > 0.0 && PanelAlpha > 0.0)
	{
		float OutsideMask = smoothstep(-Feather, Feather, ShapeDistance);
		float ShadowFalloff = 1.0 - smoothstep(0.0, ShadowSize, max(ShapeDistance, 0.0));
		Composite(PremulColor, Alpha, vec4(0.0, 0.0, 0.0, ShadowParams.y * PanelAlpha), OutsideMask * ShadowFalloff);
	}
	float ShapeCoverage = Coverage(ShapeDistance, Feather);
	vec4 BackdropUv = Data(BACKDROP_UV);
	if(abs(BackdropUv.z) > 0.000001 && abs(BackdropUv.w) > 0.000001)
	{
		vec3 Backdrop = texture(gBackdropSampler, BackdropUv.xy + texCoord * BackdropUv.zw).rgb;
		// 亚克力板：背景色与模糊底图按 PanelAlpha 混合后整体按 PanelAlpha 合成，
		// 即"模糊照旧，板越透越看得见后面的画面"。下方无底图分支直接用 Background，语义一致。
		vec3 ShapeColor = mix(Backdrop, Background.rgb, PanelAlpha);
		Composite(PremulColor, Alpha, vec4(ShapeColor, PanelAlpha), ShapeCoverage);
	}
	else
		Composite(PremulColor, Alpha, Background, ShapeCoverage);
	for(int i = 0; i < ItemCount; ++i)
	{
		vec4 ItemShape = Data(ITEM_BASE + i * ITEM_STRIDE);
		vec4 ItemParams = Data(ITEM_BASE + i * ITEM_STRIDE + 1);
		if(ItemParams.y > 0.001)
		{
			float RingRadius = MainParams.z * ItemParams.z;
			float RingThickness = MainParams.w * ItemParams.z;
			vec2 Relative = Point - ItemShape.xy;
			if(abs(Relative.x) <= RingRadius + RingThickness + Feather && abs(Relative.y) <= RingRadius + RingThickness + Feather)
			{
				float TrackDistance = abs(length(Relative) - RingRadius) - RingThickness * 0.5;
				vec4 ItemColor = Data(ITEM_BASE + i * ITEM_STRIDE + 2);
				vec4 TrackColor = ItemColor;
				TrackColor.a *= 0.18 * ItemParams.y;
				Composite(PremulColor, Alpha, TrackColor, Coverage(TrackDistance, Feather));
				ItemColor.a *= ItemParams.y;
				Composite(PremulColor, Alpha, ItemColor, Coverage(ArcSdf(Point, ItemShape.xy, RingRadius, RingThickness * 0.5, ItemParams.w), Feather));
			}
		}
	}
	float InvAlpha = Alpha > 0.0001 ? 1.0 / Alpha : 0.0;
	FragClr = vec4(clamp(PremulColor * InvAlpha, 0.0, 1.0), clamp(Alpha, 0.0, 1.0)) * vertColor;
}
