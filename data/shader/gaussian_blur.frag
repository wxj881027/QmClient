uniform sampler2D gTextureSampler;
uniform vec2 gTexelOffset;
uniform int gRadius;
uniform int gMode;
uniform int gPass;
uniform float gWeights[11];

noperspective in vec2 texCoord;
out vec4 FragClr;

const int GAUSSIAN_BLUR_MAX_RADIUS = 10;

void main()
{
	vec2 Texel = gTexelOffset;
	if(gMode == 1)
	{
		float Step = gPass == 0 ? 1.5 : 2.5;
		vec2 Offset = Texel * Step;
		FragClr = (texture(gTextureSampler, texCoord + vec2(Offset.x, Offset.y)) +
			texture(gTextureSampler, texCoord + vec2(-Offset.x, Offset.y)) +
			texture(gTextureSampler, texCoord + vec2(Offset.x, -Offset.y)) +
			texture(gTextureSampler, texCoord - Offset)) * 0.25;
		return;
	}
	if(gMode == 2)
	{
		if(gPass == 0)
		{
			vec2 Offset = Texel * 0.5;
			vec4 Result = texture(gTextureSampler, texCoord) * 4.0;
			Result += texture(gTextureSampler, texCoord + vec2(-Offset.x, -Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(Offset.x, -Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(-Offset.x, Offset.y));
			Result += texture(gTextureSampler, texCoord + vec2(Offset.x, Offset.y));
			FragClr = Result / 8.0;
		}
		else
		{
			vec2 Offset = Texel * 0.5;
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
	vec4 Result = texture(gTextureSampler, texCoord) * gWeights[0];
	for(int Offset = 1; Offset <= GAUSSIAN_BLUR_MAX_RADIUS; ++Offset)
	{
		if(Offset > gRadius)
			break;
		vec2 SampleOffset = gTexelOffset * float(Offset);
		Result += texture(gTextureSampler, texCoord + SampleOffset) * gWeights[Offset];
		Result += texture(gTextureSampler, texCoord - SampleOffset) * gWeights[Offset];
	}
	FragClr = Result;
}
