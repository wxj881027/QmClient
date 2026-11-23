#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (std140, set = 0, binding = 1) uniform SRingParams {
	vec4 gRingParams;
} gRing;

layout (location = 0) noperspective in vec2 TexCoord;
layout (location = 1) noperspective in vec4 Tint;
layout (location = 0) out vec4 FragClr;

void main()
{
	const float InnerRadius = gRing.gRingParams.x;
	const float OuterRadius = gRing.gRingParams.y;
	float Sweep = max(gRing.gRingParams.w - gRing.gRingParams.z, 0.0);
	vec2 Point = TexCoord - vec2(0.5);
	float Radius = length(Point);
	float RadialDistance = max(InnerRadius - Radius, Radius - OuterRadius);
	float RadialFeather = max(fwidth(Radius), 0.0005);
	float RadialCoverage = 1.0 - smoothstep(-RadialFeather * 0.5, RadialFeather * 0.5, RadialDistance);
	const float Pi = 3.14159265359;
	float HalfSweep = Sweep * 0.5;
	float MiddleAngle = gRing.gRingParams.z + HalfSweep;
	float Angle = Radius > 0.0 ? atan(Point.y, Point.x) : MiddleAngle;
	float CenteredAngle = mod(Angle - MiddleAngle + Pi, 2.0 * Pi) - Pi;
	float ArcDistance = (abs(CenteredAngle) - HalfSweep) * Radius;
	float ArcFeather = max(max(fwidth(Point.x), fwidth(Point.y)), 0.0005);
	float AngularCoverage = Sweep >= 6.2830 ? 1.0 : 1.0 - smoothstep(-ArcFeather * 0.5, ArcFeather * 0.5, ArcDistance);
	if(Sweep <= 0.0)
		AngularCoverage = 0.0;
	FragClr = vec4(Tint.rgb, Tint.a * RadialCoverage * AngularCoverage);

}
