#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (set = 0, binding = 0) uniform sampler2D gTextureSampler;
layout (std140, set = 1, binding = 1) uniform SMsdfParams {
	vec4 gMsdfParams;
	vec4 gMsdfSecondaryColor;
} gMsdf;

layout (location = 0) noperspective in vec2 TexCoord;
layout (location = 1) noperspective in vec4 Tint;
layout (location = 0) out vec4 FragClr;

float Median(vec3 Value)
{
	return max(min(Value.r, Value.g), min(max(Value.r, Value.g), Value.b));
}

void main()
{
	if(gMsdf.gMsdfParams.x < 0.0)
	{
		const float InnerRadius = -gMsdf.gMsdfParams.x;
		const float OuterRadius = gMsdf.gMsdfParams.y;
		float Sweep = max(gMsdf.gMsdfParams.w - gMsdf.gMsdfParams.z, 0.0);
		vec2 Point = TexCoord - vec2(0.5);
		float Radius = length(Point);
		float RadialDistance = max(InnerRadius - Radius, Radius - OuterRadius);
		float RadialFeather = max(fwidth(Radius), 0.0005);
		float RadialCoverage = 1.0 - smoothstep(-RadialFeather * 0.5, RadialFeather * 0.5, RadialDistance);
		float Angle = atan(Point.y, Point.x);
		float RelativeAngle = mod(Angle - gMsdf.gMsdfParams.z, 6.28318530718);
		if(RelativeAngle < 0.0)
			RelativeAngle += 6.28318530718;
		float AngularFeather = max(fwidth(Angle), 0.0015);
		float AngularCoverage = Sweep >= 6.2830 ? 1.0 : smoothstep(0.0, AngularFeather, RelativeAngle) * smoothstep(0.0, AngularFeather, Sweep - RelativeAngle);
		FragClr = vec4(Tint.rgb, Tint.a * RadialCoverage * AngularCoverage);
		return;
	}
	const vec4 Sample = texture(gTextureSampler, TexCoord);
	float TrueSignedDistance = Sample.a - 0.5;
	// w 编码契约见 src/engine/graphics.h 的 qm_msdf_param 命名空间，三个后端必须一致：
	//   w > 0 → 普通 MSDF（w = 描边宽度）；-0.001 < w < 0 → Duotone；w <= -0.001 → Alpha 真 SDF。
	// 注意 Duotone 区间必须严格避开真 SDF 的描边编码，否则带描边的真 SDF 字形会被误判。
	const bool UseTrueSdf = gMsdf.gMsdfParams.w <= -0.001;
	const bool UseSecondarySdf = gMsdf.gMsdfParams.w < 0.0 && !UseTrueSdf;
	float SignedDistance = UseTrueSdf ? TrueSignedDistance : Median(Sample.rgb) - 0.5;
	vec2 UnitRange = vec2(gMsdf.gMsdfParams.x) / gMsdf.gMsdfParams.yz;
	vec2 ScreenTexSize = vec2(1.0) / fwidth(TexCoord);
	float ScreenPxRange = max(0.5 * dot(UnitRange, ScreenTexSize), 1.0);
	float RequestedOutline = UseTrueSdf ? max(-gMsdf.gMsdfParams.w - 0.001, 0.0) : gMsdf.gMsdfParams.w;
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
			FragClr = vec4(Tint.rgb, Tint.a * OutlineCoverage);
			return;
		}
		const float OuterCoverage = clamp(SignedDistance * ScreenPxRange + OutlineWidth + 0.5, 0.0, 1.0);
		const float OutlineCoverage = max(OuterCoverage - FillCoverage, 0.0);
		FragClr = vec4(Tint.rgb, Tint.a * OutlineCoverage);
		return;
	}
	float Opacity = clamp(SignedDistance * ScreenPxRange + 0.5, 0.0, 1.0);
	if(UseSecondarySdf)
	{
		// Duotone atlas：RGB 与 Alpha 是同一 px_range 下的两张距离场（primary / secondary），
		// 因此复用 ScreenPxRange 解码 secondary 覆盖，缩放到任意尺寸都保持锐利边缘。
		const float SecondaryCoverage = clamp((Sample.a - 0.5) * ScreenPxRange + 0.5, 0.0, 1.0);
		const vec3 SecondaryColor = gMsdf.gMsdfSecondaryColor.rgb;
		const float SecondaryAlpha = SecondaryCoverage * gMsdf.gMsdfSecondaryColor.a;
		const float Alpha = max(Opacity, SecondaryAlpha);
		const vec3 Color = mix(SecondaryColor, Tint.rgb, Opacity);
		FragClr = vec4(Color, Tint.a * Alpha);
	}
	else
		FragClr = vec4(Tint.rgb, Tint.a * Opacity);
}
