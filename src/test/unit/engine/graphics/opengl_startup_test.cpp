#include <engine/client/backend/opengl/backend_opengl3.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
	class COpenGLStartupAccess : public CCommandProcessorFragment_OpenGL3_3
	{
	public:
		using CCommandProcessorFragment_OpenGL2::AttachProgramStages;
		using CCommandProcessorFragment_OpenGL2::CreateTileProbePixels;
		using CCommandProcessorFragment_OpenGL3_3::QuadBatchCapacity;
	};

	struct SShader
	{
		bool m_Loaded = true;
		unsigned int m_Id = 0;

		bool IsLoaded() const { return m_Loaded; }
		unsigned int GetShaderId() const { return m_Id; }
	};

	struct SProgram
	{
		unsigned int m_Id = 1;
		int m_CreateCalls = 0;
		std::vector<unsigned int> m_vAttached;

		void CreateProgram() { ++m_CreateCalls; }
		unsigned int GetProgramId() const { return m_Id; }
		bool AddShader(const SShader *pShader)
		{
			m_vAttached.push_back(pShader->GetShaderId());
			return true;
		}
	};
} // namespace

TEST(OpenGLStartup, TileProbeReportsAllocationFailure)
{
	const auto pPixels = COpenGLStartupAccess::CreateTileProbePixels([](size_t) -> void * { return nullptr; });
	EXPECT_EQ(pPixels, nullptr);
}

TEST(OpenGLStartup, TileProbeInitializesEveryUploadedLayer)
{
	constexpr size_t LayerBytes = 64 * 64 * 4;
	const auto pPixels = COpenGLStartupAccess::CreateTileProbePixels([](size_t Bytes) -> void * {
		EXPECT_EQ(Bytes, 256 * LayerBytes);
		void *pData = std::malloc(Bytes);
		if(pData != nullptr)
			std::memset(pData, 0xA5, Bytes);
		return pData;
	});
	ASSERT_NE(pPixels, nullptr);
	EXPECT_EQ(std::memcmp(pPixels.get(), "\x19\x19\x19\xFF", 4), 0);
	EXPECT_EQ(std::memcmp(pPixels.get() + LayerBytes, "\x19\x19\x4B\xFF", 4), 0);
	EXPECT_EQ(std::memcmp(pPixels.get() + 125 * LayerBytes - 4, "\xE1\xE1\xE1\xFF", 4), 0);
	EXPECT_TRUE(std::all_of(pPixels.get() + 125 * LayerBytes, pPixels.get() + 256 * LayerBytes, [](uint8_t Byte) { return Byte == 0; }));
}

TEST(OpenGLStartup, MissingShaderStagePreventsProgramCreation)
{
	SShader Vertex{true, 27};
	SShader Fragment{false, 31};
	SProgram Program;
	EXPECT_FALSE(COpenGLStartupAccess::AttachProgramStages(Program, Vertex, Fragment));
	EXPECT_EQ(Program.m_CreateCalls, 0);
	EXPECT_TRUE(Program.m_vAttached.empty());
}

TEST(OpenGLStartup, ZeroShaderHandlePreventsProgramCreation)
{
	SShader Vertex{true, 0};
	SShader Fragment{true, 31};
	SProgram Program;
	EXPECT_FALSE(COpenGLStartupAccess::AttachProgramStages(Program, Vertex, Fragment));
	EXPECT_EQ(Program.m_CreateCalls, 0);
}

TEST(OpenGLStartup, ZeroProgramHandlePreventsShaderAttachment)
{
	SShader Vertex{true, 27};
	SShader Fragment{true, 31};
	SProgram Program;
	Program.m_Id = 0;
	EXPECT_FALSE(COpenGLStartupAccess::AttachProgramStages(Program, Vertex, Fragment));
	EXPECT_EQ(Program.m_CreateCalls, 1);
	EXPECT_TRUE(Program.m_vAttached.empty());
}

TEST(OpenGLStartup, CompleteShaderPairAttachesBothStages)
{
	SShader Vertex{true, 27};
	SShader Fragment{true, 31};
	SProgram Program;
	EXPECT_TRUE(COpenGLStartupAccess::AttachProgramStages(Program, Vertex, Fragment));
	EXPECT_EQ(Program.m_CreateCalls, 1);
	EXPECT_EQ(Program.m_vAttached, (std::vector<unsigned int>{27, 31}));
}

TEST(OpenGLStartup, InsufficientVertexUniformsAllowNoQuadBatch)
{
	EXPECT_EQ(COpenGLStartupAccess::QuadBatchCapacity(0), 0);
	EXPECT_EQ(COpenGLStartupAccess::QuadBatchCapacity(31), 0);
	EXPECT_EQ(COpenGLStartupAccess::QuadBatchCapacity(32), 1);
}

TEST(OpenGLStartup, QuadBatchCapacityRespectsUniformAndShaderLimits)
{
	EXPECT_EQ(COpenGLStartupAccess::QuadBatchCapacity(1024), 83);
	EXPECT_EQ(COpenGLStartupAccess::QuadBatchCapacity(4096), 256);
}
