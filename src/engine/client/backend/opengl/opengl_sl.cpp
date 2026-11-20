#include "opengl_sl.h"

#include <base/detect.h>
#include <base/log.h>

#if defined(BACKEND_AS_OPENGL_ES) || !defined(CONF_BACKEND_OPENGL_ES)

#include <engine/client/backend/glsl_shader_compiler.h>
#include <engine/graphics.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#ifndef BACKEND_AS_OPENGL_ES
#include <GL/glew.h>
#else
#if defined(CONF_PLATFORM_IOS)
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
#include <GLES3/gl3.h>
#endif
#endif

bool CGLSL::LoadShader(CGLSLCompiler *pCompiler, IStorage *pStorage, const char *pFile, int Type)
{
	if(IsLoaded())
		return true;

	CLineReader LineReader;
	std::vector<std::string> vLines;
	if(!LineReader.OpenFile(pStorage->OpenFile(pFile, IOFLAG_READ, IStorage::TYPE_ALL)))
	{
		log_error("gfx/opengl/shader", "Failed to read shader file '%s'.", pFile);
		return false;
	}

	EBackendType BackendType = pCompiler->m_IsOpenGLES ? BACKEND_TYPE_OPENGL_ES : BACKEND_TYPE_OPENGL;
	bool IsNewOpenGL = (BackendType == BACKEND_TYPE_OPENGL ? (pCompiler->m_OpenGLVersionMajor >= 4 || (pCompiler->m_OpenGLVersionMajor == 3 && pCompiler->m_OpenGLVersionMinor == 3)) : pCompiler->m_OpenGLVersionMajor >= 3);
	std::string GLShaderStringPostfix = std::string(" core\r\n");
	if(BackendType == BACKEND_TYPE_OPENGL_ES)
	{
		GLShaderStringPostfix = std::string(" es\r\n");
	}
	//add compiler specific values
	if(IsNewOpenGL)
	{
		vLines.push_back(std::string("#version ") + std::string(std::to_string(pCompiler->m_OpenGLVersionMajor)) + std::string(std::to_string(pCompiler->m_OpenGLVersionMinor)) + std::string(std::to_string(pCompiler->m_OpenGLVersionPatch)) + GLShaderStringPostfix);
	}
	else
	{
		if(pCompiler->m_OpenGLVersionMajor == 3)
		{
			if(pCompiler->m_OpenGLVersionMinor == 0)
			{
				vLines.emplace_back("#version 130 \r\n");
			}
			if(pCompiler->m_OpenGLVersionMinor == 1)
			{
				vLines.emplace_back("#version 140 \r\n");
			}
			if(pCompiler->m_OpenGLVersionMinor == 2)
			{
				vLines.emplace_back("#version 150 \r\n");
			}
		}
		else if(pCompiler->m_OpenGLVersionMajor == 2)
		{
			if(pCompiler->m_OpenGLVersionMinor == 0)
			{
				vLines.emplace_back("#version 110 \r\n");
			}
			if(pCompiler->m_OpenGLVersionMinor == 1)
			{
				vLines.emplace_back("#version 120 \r\n");
			}
		}
	}

	if(BackendType == BACKEND_TYPE_OPENGL_ES)
	{
		if(Type == GL_FRAGMENT_SHADER)
		{
			vLines.emplace_back("precision highp float; \r\n");
			vLines.emplace_back("precision highp sampler2D; \r\n");
			vLines.emplace_back("precision highp sampler3D; \r\n");
			vLines.emplace_back("precision highp samplerCube; \r\n");
			vLines.emplace_back("precision highp samplerCubeShadow; \r\n");
			vLines.emplace_back("precision highp sampler2DShadow; \r\n");
			vLines.emplace_back("precision highp sampler2DArray; \r\n");
			vLines.emplace_back("precision highp sampler2DArrayShadow; \r\n");
		}
	}

	for(const CGLSLCompiler::SGLSLCompilerDefine &Define : pCompiler->m_vDefines)
	{
		vLines.push_back(std::string("#define ") + Define.m_DefineName + std::string(" ") + Define.m_DefineValue + std::string("\r\n"));
	}

	if(Type == GL_FRAGMENT_SHADER && !IsNewOpenGL && pCompiler->m_OpenGLVersionMajor <= 3 && pCompiler->m_HasTextureArray)
	{
		vLines.emplace_back("#extension GL_EXT_texture_array : enable\r\n");
	}

	while(const char *pReadLine = LineReader.Get())
	{
		std::string Line;
		pCompiler->ParseLine(Line, pReadLine, Type == GL_FRAGMENT_SHADER ? GLSL_SHADER_COMPILER_TYPE_FRAGMENT : GLSL_SHADER_COMPILER_TYPE_VERTEX);
		Line.append("\r\n");
		vLines.push_back(Line);
	}

	std::vector<const char *> vpShaderCode;
	vpShaderCode.reserve(vLines.size());
	for(const auto &Line : vLines)
	{
		vpShaderCode.push_back(Line.c_str());
	}

	// 编译诊断也可能分配失败，成功前由栈上的候选持有 GL 对象。
	struct SShaderCandidate
	{
		TWGLuint m_Id;
		~SShaderCandidate()
		{
			if(m_Id != 0)
				glDeleteShader(m_Id);
		}
	} Candidate{glCreateShader(Type)};
	const TWGLuint ShaderId = Candidate.m_Id;
	if(ShaderId == 0)
	{
		log_error("gfx/opengl/shader", "Failed to create a shader for file '%s'.", pFile);
		return false;
	}

	glShaderSource(ShaderId, vpShaderCode.size(), vpShaderCode.data(), nullptr);
	glCompileShader(ShaderId);

	TWGLint CompilationStatus = GL_FALSE;
	glGetShaderiv(ShaderId, GL_COMPILE_STATUS, &CompilationStatus);

	if(CompilationStatus != GL_TRUE)
	{
		TWGLint LogLength = 0;
		glGetShaderiv(ShaderId, GL_INFO_LOG_LENGTH, &LogLength);
		if(LogLength > 0)
		{
			std::string Log(LogLength, '\0');
			glGetShaderInfoLog(ShaderId, Log.size(), nullptr, &Log.front());
			if(Log.size() >= 2 && Log[Log.size() - 2] == '\n')
			{
				Log[Log.size() - 2] = '\0';
			}
			log_error("gfx/opengl/shader", "Failed to compile shader file '%s'. The compiler returned:\n%s", pFile, Log.c_str());
		}
		else
		{
			log_error("gfx/opengl/shader", "Failed to compile shader file '%s'. The compiler did not return an error.", pFile);
		}
		return false;
	}

	m_ShaderId = std::exchange(Candidate.m_Id, 0);
	return true;
}

void CGLSL::DeleteShader()
{
	if(IsLoaded())
		glDeleteShader(std::exchange(m_ShaderId, 0));
}

bool CGLSL::IsLoaded() const
{
	return m_ShaderId != 0;
}

TWGLuint CGLSL::GetShaderId() const
{
	return m_ShaderId;
}

CGLSL::~CGLSL()
{
	DeleteShader();
}

#endif
