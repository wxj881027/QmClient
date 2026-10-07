uniform vec4 gRingParams;

noperspective in vec2 TexCoord;
noperspective in vec4 Tint;

out vec4 FragClr;

void main()
{
	float InnerRadius = gRingParams.x;
	float OuterRadius = gRingParams.y;
	float Sweep = max(gRingParams.w - gRingParams.z, 0.0);
	vec2 Point = TexCoord - vec2(0.5);
	float Radius = length(Point);
	float RadialDistance = max(InnerRadius - Radius, Radius - OuterRadius);
	float RadialFeather = max(fwidth(Radius), 0.0005);
	float RadialCoverage = 1.0 - smoothstep(-RadialFeather * 0.5, RadialFeather * 0.5, RadialDistance);
	float Angle = atan(Point.y, Point.x);
	float RelativeAngle = mod(Angle - gRingParams.z, 6.28318530718);
	if(RelativeAngle < 0.0)
		RelativeAngle += 6.28318530718;
	float AngularFeather = max(fwidth(Angle), 0.0015);
	float AngularCoverage = Sweep >= 6.2830 ? 1.0 : smoothstep(0.0, AngularFeather, RelativeAngle) * smoothstep(0.0, AngularFeather, Sweep - RelativeAngle);
	FragClr = vec4(Tint.rgb, Tint.a * RadialCoverage * AngularCoverage);

}
