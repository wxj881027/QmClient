#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform sampler2D gTextureSampler;
layout(push_constant) uniform SGaussianBlurPushConstants
{
	vec2 gTexelOffset;
	int gRadius;
	int gMode;
	float gWeights[11];
	int gPass;
} gBlur;

layout(location = 0) noperspective in vec2 texCoord;
layout(location = 0) out vec4 FragClr;

const int GAUSSIAN_BLUR_MAX_RADIUS = 10;

void main()
{
	if(gBlur.gMode == 1)
	{
		float Step = gBlur.gPass == 0 ? 1.5 : 2.5;
		vec2 Offset = gBlur.gTexelOffset * Step;
		FragClr = (texture(gTextureSampler, texCoord + vec2(Offset.x, Offset.y)) +
			texture(gTextureSampler, texCoord + vec2(-Offset.x, Offset.y)) +
			texture(gTextureSampler, texCoord + vec2(Offset.x, -Offset.y)) +
			texture(gTextureSampler, texCoord - Offset)) * 0.25;
		return;
	}
	if(gBlur.gMode == 2)
	{
		if(gBlur.gPass == 0)
		{
			vec2 Offset = gBlur.gTexelOffset * 0.5;
			vec4 Result = texture(gTextureSampler, texCoord) * 4.0;
			Result += texture(gTextureSampler, texCoord + vec2(-Offset.x, -Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(Offset.x, -Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(-Offset.x, Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(Offset.x, Offset.y));
			FragClr = Result / 8.0;
		}
		else
		{
			vec2 Offset = gBlur.gTexelOffset * 0.5;
			vec4 Result = texture(gTextureSampler, texCoord + vec2(-2.0 * Offset.x, 0.0));
			Result += texture(gTextureSampler, texCoord + vec2(2.0 * Offset.x, 0.0));
			Result += texture(gTextureSampler, texCoord + vec2(0.0, -2.0 * Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(0.0, 2.0 * Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(-Offset.x, -Offset.y)) * 2.0;
			Result += texture(gTextureSampler, texCoord + vec2(Offset.x, -Offset.y)) * 2.0;
			Result += texture(gTextureSampler, texCoord + vec2(-Offset.x, Offset.y)) * 2.0;
			Result += texture(gTextureSampler, texCoord + vec2(Offset.x, Offset.y)) * 2.0;
			FragClr = Result / 12.0;
		}
		return;
	}
	vec4 Result = texture(gTextureSampler, texCoord) * gBlur.gWeights[0];
	for(int Offset = 1; Offset <= GAUSSIAN_BLUR_MAX_RADIUS; ++Offset)
	{
		if(Offset > gBlur.gRadius)
			break;
		vec2 SampleOffset = gBlur.gTexelOffset * float(Offset);
		Result += texture(gTextureSampler, texCoord + SampleOffset) * gBlur.gWeights[Offset];
		Result += texture(gTextureSampler, texCoord - SampleOffset) * gBlur.gWeights[Offset];
	}
	FragClr = Result;
}
