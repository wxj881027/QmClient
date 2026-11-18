#include <engine/client/backend/glsl_shader_compiler.h>
#include <engine/client/backend/opengl/opengl_sl.h>
#include <engine/client/backend/opengl/opengl_sl_program.h>
#include <engine/storage.h>

#include <GL/glew.h>
#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <utility>

namespace
{
	template<typename T>
	class CScopedGlFunction
	{
		T &m_Slot;
		T m_Previous;

	public:
		CScopedGlFunction(T &Slot, T Replacement) :
			m_Slot(Slot),
			m_Previous(std::exchange(Slot, Replacement)) {}
		~CScopedGlFunction() { m_Slot = m_Previous; }
	};

	class COpenGlShaderLifecycle : public ::testing::Test
	{
		inline static COpenGlShaderLifecycle *s_pActive = nullptr;

		static GLuint GLAPIENTRY CreateShader(GLenum) { return s_pActive->m_ShaderId; }
		static void GLAPIENTRY ShaderSource(GLuint, GLsizei, const GLchar *const *, const GLint *) { ++s_pActive->m_ShaderSourceCalls; }
		static void GLAPIENTRY CompileShader(GLuint) {}
		static void GLAPIENTRY GetShaderStatus(GLuint Shader, GLenum Property, GLint *pValue)
		{
			if(Property == GL_COMPILE_STATUS)
			{
				++s_pActive->m_CompileQueries;
				if(Shader != 0 && s_pActive->m_ReportsCompileStatus)
					*pValue = s_pActive->m_CompileStatus;
			}
			else if(Property == GL_INFO_LOG_LENGTH)
				*pValue = s_pActive->m_ShaderLogLength;
		}
		static void GLAPIENTRY GetShaderLog(GLuint, GLsizei, GLsizei *, GLchar *)
		{
			// 在候选已创建的诊断阶段注入异常，验证异常展开时的资源归还。
			throw std::bad_alloc();
		}
		static void GLAPIENTRY DeleteShader(GLuint Shader)
		{
			++s_pActive->m_ShaderDeletes;
			s_pActive->m_LastDeletedShader = Shader;
		}
		static GLuint GLAPIENTRY CreateProgram() { return s_pActive->m_ProgramId; }
		static void GLAPIENTRY DeleteProgram(GLuint) {}
		static void GLAPIENTRY AttachShader(GLuint, GLuint) { ++s_pActive->m_AttachCalls; }
		static void GLAPIENTRY LinkProgram(GLuint) { ++s_pActive->m_LinkCalls; }
		static void GLAPIENTRY GetProgramStatus(GLuint Program, GLenum Property, GLint *pValue)
		{
			if(Property == GL_LINK_STATUS && Program != 0 && s_pActive->m_ReportsLinkStatus)
				*pValue = GL_TRUE;
			else if(Property == GL_INFO_LOG_LENGTH)
				*pValue = 0;
		}
		static void GLAPIENTRY GetAttachedShaders(GLuint, GLsizei, GLsizei *pCount, GLuint *) { *pCount = 0; }
		static GLint GLAPIENTRY GetUniformLocation(GLuint, const GLchar *)
		{
			++s_pActive->m_UniformQueries;
			return 4;
		}
		static void GLAPIENTRY UseProgram(GLuint) { ++s_pActive->m_UseCalls; }

		CScopedGlFunction<PFNGLCREATESHADERPROC> m_FakeCreateShader{glCreateShader, &CreateShader};
		CScopedGlFunction<PFNGLSHADERSOURCEPROC> m_FakeShaderSource{glShaderSource, &ShaderSource};
		CScopedGlFunction<PFNGLCOMPILESHADERPROC> m_FakeCompileShader{glCompileShader, &CompileShader};
		CScopedGlFunction<PFNGLGETSHADERIVPROC> m_FakeGetShaderStatus{glGetShaderiv, &GetShaderStatus};
		CScopedGlFunction<PFNGLGETSHADERINFOLOGPROC> m_FakeGetShaderLog{glGetShaderInfoLog, &GetShaderLog};
		CScopedGlFunction<PFNGLDELETESHADERPROC> m_FakeDeleteShader{glDeleteShader, &DeleteShader};
		CScopedGlFunction<PFNGLCREATEPROGRAMPROC> m_FakeCreateProgram{glCreateProgram, &CreateProgram};
		CScopedGlFunction<PFNGLDELETEPROGRAMPROC> m_FakeDeleteProgram{glDeleteProgram, &DeleteProgram};
		CScopedGlFunction<PFNGLATTACHSHADERPROC> m_FakeAttachShader{glAttachShader, &AttachShader};
		CScopedGlFunction<PFNGLLINKPROGRAMPROC> m_FakeLinkProgram{glLinkProgram, &LinkProgram};
		CScopedGlFunction<PFNGLGETPROGRAMIVPROC> m_FakeGetProgramStatus{glGetProgramiv, &GetProgramStatus};
		CScopedGlFunction<PFNGLGETATTACHEDSHADERSPROC> m_FakeGetAttachedShaders{glGetAttachedShaders, &GetAttachedShaders};
		CScopedGlFunction<PFNGLGETUNIFORMLOCATIONPROC> m_FakeGetUniformLocation{glGetUniformLocation, &GetUniformLocation};
		CScopedGlFunction<PFNGLUSEPROGRAMPROC> m_FakeUseProgram{glUseProgram, &UseProgram};

	protected:
		GLuint m_ShaderId = 7;
		GLuint m_ProgramId = 11;
		GLuint m_LastDeletedShader = 0;
		GLint m_CompileStatus = GL_TRUE;
		GLint m_ShaderLogLength = 0;
		bool m_ReportsCompileStatus = true;
		bool m_ReportsLinkStatus = true;
		int m_ShaderSourceCalls = 0;
		int m_CompileQueries = 0;
		int m_ShaderDeletes = 0;
		int m_AttachCalls = 0;
		int m_LinkCalls = 0;
		int m_UniformQueries = 0;
		int m_UseCalls = 0;

		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CGLSLCompiler m_Compiler{3, 3, 0, false, 0.0f};

		void SetUp() override
		{
			s_pActive = this;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			IOHANDLE File = m_pStorage->OpenFile("lifecycle.vert", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			const char aSource[] = "void main() { gl_Position = vec4(0.0); }\n";
			EXPECT_EQ(io_write(File, aSource, sizeof(aSource) - 1), sizeof(aSource) - 1);
			EXPECT_EQ(io_close(File), 0);
		}

		void TearDown() override { s_pActive = nullptr; }
		bool LoadVertexShader(CGLSL &Shader)
		{
			return Shader.LoadShader(&m_Compiler, m_pStorage.get(), "lifecycle.vert", GL_VERTEX_SHADER);
		}
	};
}

TEST_F(COpenGlShaderLifecycle, FailedShaderCreationDoesNotSubmitOrQueryShader)
{
	m_ShaderId = 0;
	CGLSL Shader;
	EXPECT_FALSE(LoadVertexShader(Shader));
	EXPECT_FALSE(Shader.IsLoaded());
	EXPECT_EQ(Shader.GetShaderId(), 0u);
	EXPECT_EQ(m_ShaderSourceCalls, 0);
	EXPECT_EQ(m_CompileQueries, 0);
	EXPECT_EQ(m_ShaderDeletes, 0);
}

TEST_F(COpenGlShaderLifecycle, MissingCompileStatusReleasesTheCandidateAndAllowsRetry)
{
	m_ReportsCompileStatus = false;
	CGLSL Shader;
	EXPECT_FALSE(LoadVertexShader(Shader));
	EXPECT_FALSE(Shader.IsLoaded());
	EXPECT_EQ(m_ShaderDeletes, 1);
	EXPECT_EQ(m_LastDeletedShader, m_ShaderId);

	m_ReportsCompileStatus = true;
	++m_ShaderId;
	ASSERT_TRUE(LoadVertexShader(Shader));
	EXPECT_EQ(Shader.GetShaderId(), m_ShaderId);
	Shader.DeleteShader();
	EXPECT_EQ(Shader.GetShaderId(), 0u);
	EXPECT_EQ(m_ShaderDeletes, 2);
	EXPECT_EQ(m_LastDeletedShader, m_ShaderId);
}

TEST_F(COpenGlShaderLifecycle, DiagnosticExceptionReleasesTheUnpublishedShader)
{
	m_CompileStatus = GL_FALSE;
	m_ShaderLogLength = 16;
	CGLSL Shader;
	EXPECT_THROW(LoadVertexShader(Shader), std::bad_alloc);
	EXPECT_FALSE(Shader.IsLoaded());
	EXPECT_EQ(Shader.GetShaderId(), 0u);
	EXPECT_EQ(m_ShaderDeletes, 1);
	EXPECT_EQ(m_LastDeletedShader, m_ShaderId);
}

TEST_F(COpenGlShaderLifecycle, FailedProgramCreationRejectsAttachLinkAndUniformLookup)
{
	m_ProgramId = 0;
	CGLSL Shader;
	ASSERT_TRUE(LoadVertexShader(Shader));
	CGLSLProgram Program;
	Program.CreateProgram();
	EXPECT_FALSE(Program.AddShader(&Shader));
	EXPECT_FALSE(Program.LinkProgram());
	EXPECT_EQ(Program.GetUniformLoc("gPos"), -1);
	EXPECT_EQ(m_AttachCalls, 0);
	EXPECT_EQ(m_LinkCalls, 0);
	EXPECT_EQ(m_UniformQueries, 0);
}

TEST_F(COpenGlShaderLifecycle, MissingLinkStatusKeepsTheProgramUnavailable)
{
	m_ReportsLinkStatus = false;
	CGLSLProgram Program;
	Program.CreateProgram();
	EXPECT_FALSE(Program.LinkProgram());
	Program.UseProgram();
	EXPECT_EQ(Program.GetUniformLoc("gPos"), -1);
	EXPECT_EQ(m_LinkCalls, 1);
	EXPECT_EQ(m_UseCalls, 0);
	EXPECT_EQ(m_UniformQueries, 0);
}

TEST_F(COpenGlShaderLifecycle, SuccessfulLinkAllowsUseAndUniformLookup)
{
	CGLSLProgram Program;
	Program.CreateProgram();
	ASSERT_TRUE(Program.LinkProgram());
	Program.UseProgram();
	EXPECT_EQ(Program.GetUniformLoc("gPos"), 4);
	EXPECT_EQ(m_UseCalls, 1);
	EXPECT_EQ(m_UniformQueries, 1);
}
