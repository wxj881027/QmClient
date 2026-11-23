uniform sampler2D gTextSampler;
uniform sampler2D gTextOutlineSampler;

uniform vec4 gVertColor;
uniform vec4 gVertOutlineColor;
uniform float gMsdfPxRange;
uniform float gMsdfEffectStrength;

noperspective in vec2 texCoord;
noperspective in vec4 outVertColor;

out vec4 FragClr;
void main()
{
	vec3 Sample = texture(gTextSampler, texCoord).rgb;
	float Fill = Sample.r;
	float Outline;
	float Glow = 0.0;
	float Shadow = 0.0;
	if(gMsdfPxRange > 0.0)
	{
		float Median = max(min(Sample.r, Sample.g), min(max(Sample.r, Sample.g), Sample.b));
		vec2 TextureSize = vec2(textureSize(gTextSampler, 0));
		vec2 PixelUv = fwidth(texCoord);
		vec2 TexelsPerPixel = PixelUv * TextureSize;
		float Range = gMsdfPxRange / max(max(TexelsPerPixel.x, TexelsPerPixel.y), 0.001);
		float Distance = (Median - 0.5) * Range;
		Fill = clamp(Distance + 0.5, 0.0, 1.0);
		Outline = clamp(Distance + 0.5 + min(1.0, Range * 0.25), 0.0, 1.0);
		if(gMsdfEffectStrength > 0.0)
		{
			Glow = smoothstep(-min(2.25, Range * 0.40), 0.25, Distance) * gMsdfEffectStrength * 0.38;
			vec2 Shift = min(PixelUv * vec2(0.9, 1.6), vec2(gMsdfPxRange * 0.24) / TextureSize);
			vec3 ShadowSample = texture(gTextSampler, texCoord - Shift).rgb;
			float ShadowMedian = max(min(ShadowSample.r, ShadowSample.g), min(max(ShadowSample.r, ShadowSample.g), ShadowSample.b));
			float ShadowDistance = (ShadowMedian - 0.5) * Range;
			Shadow = smoothstep(-min(1.8, Range * 0.35), 0.8, ShadowDistance) * gMsdfEffectStrength * 0.70;
		}
	}
	else
		Outline = texture(gTextOutlineSampler, texCoord).r;
	vec4 textColor = gVertColor * outVertColor * vec4(1.0, 1.0, 1.0, Fill);
	vec4 textOutlineTex = gVertOutlineColor * vec4(1.0, 1.0, 1.0, Outline);

	// ratio between the two textures
	float OutlineBlend = (1.0 - textColor.a);	

	// since the outline is always black, or even if it has decent colors, it can be just added to the actual color
	// without losing any or too much color
	
	// lerp isn't commutative, so add the color the fragment looses by lerping
	// this reduces the chance of false color calculation if the text is transparent
	
	// first get the right color
	vec4 textOutlineFrag = vec4(textOutlineTex.rgb * textOutlineTex.a, textOutlineTex.a) * OutlineBlend;
	vec3 textFrag = (textColor.rgb * textColor.a);
	vec3 finalFragColor = textOutlineFrag.rgb + textFrag;
	
	float RealAlpha = textOutlineFrag.a + textColor.a;
	if(Glow > 0.0 || Shadow > 0.0)
	{
		float GlowAlpha = Glow * gVertColor.a * outVertColor.a * (1.0 - RealAlpha);
		finalFragColor += textColor.rgb * GlowAlpha;
		RealAlpha += GlowAlpha;
		float ShadowAlpha = Shadow * gVertOutlineColor.a * (1.0 - RealAlpha);
		finalFragColor += gVertOutlineColor.rgb * ShadowAlpha;
		RealAlpha += ShadowAlpha;
	}
	
	// simply add the color we will loose through blending
	if(RealAlpha > 0.0)
		FragClr = vec4(finalFragColor / RealAlpha, RealAlpha);
	else
		FragClr = vec4(0.0, 0.0, 0.0, 0.0);
}
