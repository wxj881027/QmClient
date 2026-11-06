#include <engine/client/backend/glsl_shader_compiler.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	std::string ParseLegacy(const char *pSource, EGLSLShaderCompilerType Type = GLSL_SHADER_COMPILER_TYPE_FRAGMENT)
	{
		CGLSLCompiler Compiler(2, 1, 0, false, 0.0f);
		std::string Output;
		Compiler.ParseLine(Output, pSource, Type);
		return Output;
	}
}

TEST(GlslLegacyCompiler, RewritesSeveralTextureCallsOnSameLine)
{
	EXPECT_EQ(ParseLegacy("vec4 Color = texture(s, a) + texture(t, b);"), "vec4 Color = texture2D(s, a) + texture2D(t, b);");
}

TEST(GlslLegacyCompiler, RewritesTextureCallsInsideOtherArguments)
{
	EXPECT_EQ(ParseLegacy("texture(s, vec2(texture(t, uv).x, 0.0))"), "texture2D(s, vec2(texture2D(t, uv).x, 0.0))");
}

TEST(GlslLegacyCompiler, TextureReplacementUsesSelectedTargetForEveryCall)
{
	struct SCase
	{
		int m_Mode;
		const char *m_pExpected;
	};
	for(const SCase &Case : {
		    SCase{CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_3D, "texture3D(s, a) + texture3D(t, b)"},
		    SCase{CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_2D_ARRAY, "texture2DArray(s, a) + texture2DArray(t, b)"}})
	{
		SCOPED_TRACE(Case.m_Mode);
		CGLSLCompiler Compiler(3, 2, 0, false, 0.0f);
		Compiler.SetTextureReplaceType(Case.m_Mode);
		std::string Output;
		Compiler.ParseLine(Output, "texture(s, a) + texture(t, b)", GLSL_SHADER_COMPILER_TYPE_FRAGMENT);
		EXPECT_EQ(Output, Case.m_pExpected);
	}
}

TEST(GlslLegacyCompiler, PreservesExistingOutputPrefix)
{
	CGLSLCompiler Compiler(2, 1, 0, false, 0.0f);
	std::string Output = "prefix: ";
	Compiler.ParseLine(Output, "texture(s, uv)", GLSL_SHADER_COMPILER_TYPE_FRAGMENT);
	EXPECT_EQ(Output, "prefix: texture2D(s, uv)");
}

TEST(GlslLegacyCompiler, FragmentColorAfterTextureStillUsesLegacyOutputName)
{
	EXPECT_EQ(ParseLegacy("texture(s, uv); FragClr = vec4(1.0);"), "texture2D(s, uv); gl_FragColor = vec4(1.0);");
}

TEST(GlslLegacyCompiler, VertexLayoutStillBecomesAttribute)
{
	EXPECT_EQ(ParseLegacy("layout(location = 0) in vec2 Pos;", GLSL_SHADER_COMPILER_TYPE_VERTEX), "attribute vec2 Pos;");
}

TEST(GlslLegacyCompiler, InterpolationDeclarationStillBecomesVarying)
{
	EXPECT_EQ(ParseLegacy("noperspective in vec2 TexCoord;"), " varying vec2 TexCoord;");
}

TEST(GlslLegacyCompiler, FragmentOutputDeclarationIsRemoved)
{
	EXPECT_EQ(ParseLegacy("out vec4 FragClr;"), "");
	EXPECT_EQ(ParseLegacy("out vec2 TexCoord;", GLSL_SHADER_COMPILER_TYPE_VERTEX), "varying vec2 TexCoord;");
}

TEST(GlslLegacyCompiler, ModernDesktopInputRemainsUnchanged)
{
	CGLSLCompiler Compiler(3, 3, 0, false, 0.0f);
	const std::string Source = "FragClr = texture(s, a) + texture(t, b);";
	std::string Output;
	Compiler.ParseLine(Output, Source.c_str(), GLSL_SHADER_COMPILER_TYPE_FRAGMENT);
	EXPECT_EQ(Output, Source);
}

TEST(GlslLegacyCompiler, EmptyLineDoesNotAlterExistingLegacyOutput)
{
	CGLSLCompiler Compiler(2, 1, 0, false, 0.0f);
	std::string Output = "prefix";
	Compiler.ParseLine(Output, "", GLSL_SHADER_COMPILER_TYPE_VERTEX);
	EXPECT_EQ(Output, "prefix");
}

TEST(GlslLegacyCompiler, LongSingleLineConvertsEveryCallWithoutRecursiveSuffixes)
{
	std::string Source, Expected;
	for(int Index = 0; Index < 20000; ++Index)
	{
		Source += "texture(s, uv); ";
		Expected += "texture2D(s, uv); ";
	}
	EXPECT_EQ(ParseLegacy(Source.c_str()), Expected);
}

TEST(GlslLegacyCompiler, RepeatedLinesAndTargetChangesDoNotReusePreviousScanState)
{
	CGLSLCompiler Compiler(3, 0, 0, false, 0.0f);
	std::string First, Second, Third;
	Compiler.ParseLine(First, "texture(s, uv)", GLSL_SHADER_COMPILER_TYPE_FRAGMENT);
	Compiler.ParseLine(Second, "vec4 Color;", GLSL_SHADER_COMPILER_TYPE_VERTEX);
	Compiler.SetTextureReplaceType(CGLSLCompiler::GLSL_COMPILER_TEXTURE_REPLACE_TYPE_3D);
	Compiler.ParseLine(Third, "texture(v, uv)", GLSL_SHADER_COMPILER_TYPE_FRAGMENT);
	EXPECT_EQ(First, "texture2D(s, uv)");
	EXPECT_EQ(Second, "vec4 Color;");
	EXPECT_EQ(Third, "texture3D(v, uv)");
}
