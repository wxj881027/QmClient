#include <engine/client/backend/glsl_shader_compiler.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>
#include <utility>
#include <vector>

namespace
{
	class CGlslShaderSourceTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void Write(const std::string &Source)
		{
			IOHANDLE File = m_pStorage->OpenFile("shader_source.frag", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Source.data(), static_cast<unsigned>(Source.size())), Source.size());
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(CGlslShaderSourceTest, NativeStoredShaderPreservesLongLineForBoundedStackConversion)
{
	std::string Source, Expected;
	for(int Index = 0; Index < 20000; ++Index)
	{
		Source += "texture(s, uv); ";
		Expected += "texture2D(s, uv); ";
	}
	Write(Source);
	ASSERT_FALSE(HasFatalFailure());
	CLineReader Reader;
	ASSERT_TRUE(Reader.OpenFile(m_pStorage->OpenFile("shader_source.frag", IOFLAG_READ, IStorage::TYPE_ALL)));
	const char *pLine = Reader.Get();
	ASSERT_NE(pLine, nullptr);
	ASSERT_EQ(std::string(pLine), Source);
	CGLSLCompiler Compiler(2, 1, 0, false, 0.0f);
	std::string Output;
	Compiler.ParseLine(Output, pLine, GLSL_SHADER_COMPILER_TYPE_FRAGMENT);
	EXPECT_EQ(Output, Expected);
	EXPECT_EQ(Reader.Get(), nullptr);
}

TEST_F(CGlslShaderSourceTest, NativeLineReaderKeepsBlankLinesAndIndependentConversion)
{
	Write("vec4 Color = texture(s, uv);\r\n\r\ntexture(t, coord);\n");
	ASSERT_FALSE(HasFatalFailure());
	CLineReader Reader;
	ASSERT_TRUE(Reader.OpenFile(m_pStorage->OpenFile("shader_source.frag", IOFLAG_READ, IStorage::TYPE_ALL)));
	CGLSLCompiler Compiler(3, 2, 0, false, 0.0f);
	std::vector<std::string> vLines;
	while(const char *pLine = Reader.Get())
	{
		std::string Line;
		Compiler.ParseLine(Line, pLine, GLSL_SHADER_COMPILER_TYPE_FRAGMENT);
		vLines.push_back(std::move(Line));
	}
	EXPECT_EQ(vLines, (std::vector<std::string>{"vec4 Color = texture2D(s, uv);", "", "texture2D(t, coord);"}));
}
