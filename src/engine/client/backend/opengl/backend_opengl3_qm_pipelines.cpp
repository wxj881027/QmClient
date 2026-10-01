#include "backend_opengl3.h"

#include <base/detect.h>
#include <base/log.h>

#if defined(BACKEND_AS_OPENGL_ES) || !defined(CONF_BACKEND_OPENGL_ES)

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

#include <engine/client/backend/glsl_shader_compiler.h>
#include <engine/client/backend/opengl/opengl_qm_sl_program.h>
#include <engine/client/backend/opengl/opengl_sl.h>
#ifndef BACKEND_NO_SDL
#include <engine/client/backend_sdl.h>
#endif

void CCommandProcessorFragment_OpenGL3_3::AllocateQmPrograms()
{
	m_pMediaIslandSdfProgram = new CGLSLMediaIslandSdfProgram;
	m_MediaIslandSdfProgramValid = false;
	m_pRoundedRectSdfProgram = new CGLSLRoundedRectSdfProgram;
	m_RoundedRectSdfProgramValid = false;
	m_pTexturedMsdfProgram = new CGLSLTexturedMsdfProgram;
	m_TexturedMsdfProgramValid = false;
	m_pGaussianBlurProgram = new CGLSLGaussianBlurProgram;
	m_GaussianBlurProgramValid = false;
}

void CCommandProcessorFragment_OpenGL3_3::LoadQmPrograms(const SCommand_Init *pCommand, CGLSLCompiler &ShaderCompiler, int ShaderMajor, int ShaderMinor, int ShaderPatch)
{
	{
		CGLSL VertexShader;
		CGLSL FragmentShader;
		ShaderCompiler.AddDefine("TW_MODERN_GL", "");
		VertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/media_island_sdf.vert", GL_VERTEX_SHADER);
		FragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/media_island_sdf.frag", GL_FRAGMENT_SHADER);
		ShaderCompiler.ClearDefines();

		m_pMediaIslandSdfProgram->CreateProgram();
		const bool VertexAdded = m_pMediaIslandSdfProgram->AddShader(&VertexShader);
		const bool FragmentAdded = m_pMediaIslandSdfProgram->AddShader(&FragmentShader);
		const bool Linked = VertexAdded && FragmentAdded && m_pMediaIslandSdfProgram->LinkProgram();
		if(Linked)
		{
			UseProgram(m_pMediaIslandSdfProgram);
			m_pMediaIslandSdfProgram->m_LocPos = m_pMediaIslandSdfProgram->GetUniformLoc("gPos");
			m_pMediaIslandSdfProgram->m_LocTextureSampler = m_pMediaIslandSdfProgram->GetUniformLoc("gBackdropSampler");
			m_pMediaIslandSdfProgram->m_LocData = m_pMediaIslandSdfProgram->GetUniformLoc("gMediaIslandSdfData[0]");
			m_MediaIslandSdfProgramValid = m_pMediaIslandSdfProgram->m_LocPos >= 0 && m_pMediaIslandSdfProgram->m_LocTextureSampler >= 0 && m_pMediaIslandSdfProgram->m_LocData >= 0;
			if(m_MediaIslandSdfProgramValid)
				m_pMediaIslandSdfProgram->SetUniform(m_pMediaIslandSdfProgram->m_LocTextureSampler, 0);
		}
		pCommand->m_pCapabilities->m_MediaIslandSdf = m_MediaIslandSdfProgramValid;
	}
	{
		CGLSL VertexShader;
		CGLSL FragmentShader;
		ShaderCompiler.AddDefine("TW_MODERN_GL", "");
		VertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/rounded_rect_sdf.vert", GL_VERTEX_SHADER);
		FragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/rounded_rect_sdf.frag", GL_FRAGMENT_SHADER);
		ShaderCompiler.ClearDefines();

		m_pRoundedRectSdfProgram->CreateProgram();
		const bool VertexAdded = m_pRoundedRectSdfProgram->AddShader(&VertexShader);
		const bool FragmentAdded = m_pRoundedRectSdfProgram->AddShader(&FragmentShader);
		const bool Linked = VertexAdded && FragmentAdded && m_pRoundedRectSdfProgram->LinkProgram();
		if(Linked)
		{
			UseProgram(m_pRoundedRectSdfProgram);
			m_pRoundedRectSdfProgram->m_LocPos = m_pRoundedRectSdfProgram->GetUniformLoc("gPos");
			m_pRoundedRectSdfProgram->m_LocData = m_pRoundedRectSdfProgram->GetUniformLoc("gRoundedRectSdfData[0]");
			m_RoundedRectSdfProgramValid = m_pRoundedRectSdfProgram->m_LocPos >= 0 && m_pRoundedRectSdfProgram->m_LocData >= 0;
		}
		pCommand->m_pCapabilities->m_RoundedRectSdf = m_RoundedRectSdfProgramValid;
	}
	{
		CGLSL VertexShader;
		CGLSL FragmentShader;
		// QmClient: 图标 MSDF 使用现代 GLSL 输入/输出和导数函数；显式声明
		// 现代路径，避免 OpenGL 初始化时误走兼容 shader 转换分支。
		ShaderCompiler.AddDefine("TW_MODERN_GL", "");
		VertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/textured_msdf.vert", GL_VERTEX_SHADER);
		FragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/textured_msdf.frag", GL_FRAGMENT_SHADER);
		ShaderCompiler.ClearDefines();

		m_pTexturedMsdfProgram->CreateProgram();
		const bool VertexAdded = m_pTexturedMsdfProgram->AddShader(&VertexShader);
		const bool FragmentAdded = m_pTexturedMsdfProgram->AddShader(&FragmentShader);
		const bool Linked = VertexAdded && FragmentAdded && m_pTexturedMsdfProgram->LinkProgram();
		if(Linked)
		{
			UseProgram(m_pTexturedMsdfProgram);
			m_pTexturedMsdfProgram->m_LocPos = m_pTexturedMsdfProgram->GetUniformLoc("gPos");
			m_pTexturedMsdfProgram->m_LocTextureSampler = m_pTexturedMsdfProgram->GetUniformLoc("gTextureSampler");
			m_pTexturedMsdfProgram->m_LocParams = m_pTexturedMsdfProgram->GetUniformLoc("gMsdfParams");
			m_pTexturedMsdfProgram->m_LocSecondaryColor = m_pTexturedMsdfProgram->GetUniformLoc("gMsdfSecondaryColor");
			m_TexturedMsdfProgramValid = m_pTexturedMsdfProgram->m_LocPos >= 0 && m_pTexturedMsdfProgram->m_LocTextureSampler >= 0 && m_pTexturedMsdfProgram->m_LocParams >= 0 && m_pTexturedMsdfProgram->m_LocSecondaryColor >= 0;
			if(m_TexturedMsdfProgramValid)
				m_pTexturedMsdfProgram->SetUniform(m_pTexturedMsdfProgram->m_LocTextureSampler, 0);
		}
		log_info("gfx/opengl", "Textured MSDF program: vertex=%d fragment=%d linked=%d uniforms=%d/%d/%d/%d valid=%d context=%d.%d.%d",
			VertexAdded,
			FragmentAdded,
			Linked,
			m_pTexturedMsdfProgram->m_LocPos,
			m_pTexturedMsdfProgram->m_LocTextureSampler,
			m_pTexturedMsdfProgram->m_LocParams,
			m_pTexturedMsdfProgram->m_LocSecondaryColor,
			m_TexturedMsdfProgramValid,
			ShaderMajor,
			ShaderMinor,
			ShaderPatch);
		pCommand->m_pCapabilities->m_TexturedMsdf.store(m_TexturedMsdfProgramValid, std::memory_order_release);
	}
	{
		CGLSL VertexShader;
		CGLSL FragmentShader;
		VertexShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/gaussian_blur.vert", GL_VERTEX_SHADER);
		FragmentShader.LoadShader(&ShaderCompiler, pCommand->m_pStorage, "shader/gaussian_blur.frag", GL_FRAGMENT_SHADER);

		m_pGaussianBlurProgram->CreateProgram();
		const bool VertexAdded = m_pGaussianBlurProgram->AddShader(&VertexShader);
		const bool FragmentAdded = m_pGaussianBlurProgram->AddShader(&FragmentShader);
		const bool Linked = VertexAdded && FragmentAdded && m_pGaussianBlurProgram->LinkProgram();
		if(Linked)
		{
			UseProgram(m_pGaussianBlurProgram);
			m_pGaussianBlurProgram->m_LocTextureSampler = m_pGaussianBlurProgram->GetUniformLoc("gTextureSampler");
			m_pGaussianBlurProgram->m_LocTexelOffset = m_pGaussianBlurProgram->GetUniformLoc("gTexelOffset");
			m_pGaussianBlurProgram->m_LocRadius = m_pGaussianBlurProgram->GetUniformLoc("gRadius");
			m_pGaussianBlurProgram->m_LocMode = m_pGaussianBlurProgram->GetUniformLoc("gMode");
			m_pGaussianBlurProgram->m_LocPass = m_pGaussianBlurProgram->GetUniformLoc("gPass");
			m_pGaussianBlurProgram->m_LocWeights = m_pGaussianBlurProgram->GetUniformLoc("gWeights[0]");
			m_GaussianBlurProgramValid = m_pGaussianBlurProgram->m_LocTextureSampler >= 0 && m_pGaussianBlurProgram->m_LocTexelOffset >= 0 && m_pGaussianBlurProgram->m_LocRadius >= 0 && m_pGaussianBlurProgram->m_LocMode >= 0 && m_pGaussianBlurProgram->m_LocPass >= 0 && m_pGaussianBlurProgram->m_LocWeights >= 0;
			if(m_GaussianBlurProgramValid)
				m_pGaussianBlurProgram->SetUniform(m_pGaussianBlurProgram->m_LocTextureSampler, 0);
		}
		pCommand->m_pCapabilities->m_RenderTargetGaussianBlur = pCommand->m_pCapabilities->m_RenderTargets && m_GaussianBlurProgramValid;
	}
	GLint BackbufferSamples = 0;
	GLint RedBits = 0;
	GLint GreenBits = 0;
	GLint BlueBits = 0;
	GLint AlphaBits = 0;
	glGetIntegerv(GL_SAMPLES, &BackbufferSamples);
	glGetIntegerv(GL_RED_BITS, &RedBits);
	glGetIntegerv(GL_GREEN_BITS, &GreenBits);
	glGetIntegerv(GL_BLUE_BITS, &BlueBits);
	glGetIntegerv(GL_ALPHA_BITS, &AlphaBits);
	m_BackbufferCaptureMultisampleResolveSupported = RedBits == 8 && GreenBits == 8 && BlueBits == 8 && AlphaBits == 8;
	pCommand->m_pCapabilities->m_BackbufferCapture = pCommand->m_pCapabilities->m_RenderTargets && (BackbufferSamples == 0 || m_BackbufferCaptureMultisampleResolveSupported);
}

void CCommandProcessorFragment_OpenGL3_3::UnloadQmPrograms()
{
	m_pMediaIslandSdfProgram->DeleteProgram();
	m_pRoundedRectSdfProgram->DeleteProgram();
	m_pTexturedMsdfProgram->DeleteProgram();
	m_pGaussianBlurProgram->DeleteProgram();
}

void CCommandProcessorFragment_OpenGL3_3::FreeQmPrograms()
{
	delete m_pMediaIslandSdfProgram;
	m_pMediaIslandSdfProgram = nullptr;
	m_MediaIslandSdfProgramValid = false;
	delete m_pRoundedRectSdfProgram;
	m_pRoundedRectSdfProgram = nullptr;
	m_RoundedRectSdfProgramValid = false;
	delete m_pTexturedMsdfProgram;
	m_pTexturedMsdfProgram = nullptr;
	m_TexturedMsdfProgramValid = false;
	delete m_pGaussianBlurProgram;
	m_pGaussianBlurProgram = nullptr;
	m_GaussianBlurProgramValid = false;
}

void CCommandProcessorFragment_OpenGL3_3::BindRenderTargetTexture(TWGLuint Texture)
{
	// 离屏纹理只有单层，必须清除上一笔绘制的 mipmap 采样器，并绑定到着色器使用的 0 号单元。
	glActiveTexture(GL_TEXTURE0);
	glBindSampler(0, 0);
	glBindTexture(GL_TEXTURE_2D, Texture);
}

void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderMediaIslandSdf(const CCommandBuffer::SCommand_RenderMediaIslandSdf *pCommand)
{
	if(!m_MediaIslandSdfProgramValid || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0 || m_pMediaIslandSdfProgram == nullptr)
		return;

	UseProgram(m_pMediaIslandSdfProgram);
	SetState(pCommand->m_State, m_pMediaIslandSdfProgram);
	if(pCommand->m_BackdropTargetId >= 0 && (size_t)pCommand->m_BackdropTargetId < m_vRenderTargets.size())
	{
		const SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_BackdropTargetId];
		if(Target.m_Texture != 0)
			BindRenderTargetTexture(Target.m_Texture);
	}
	m_pMediaIslandSdfProgram->SetUniformVec4(m_pMediaIslandSdfProgram->m_LocData, IGraphics::SMediaIslandSdfParams::DATA_COUNT, (const float *)pCommand->m_Params.m_aData.data());

	UploadStreamBufferData(pCommand->m_PrimType, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex), pCommand->m_PrimCount);
	glBindVertexArray(m_aPrimitiveDrawVertexId[m_LastStreamBuffer]);

	switch(pCommand->m_PrimType)
	{
	case EPrimitiveType::LINES:
		glDrawArrays(GL_LINES, 0, pCommand->m_PrimCount * 2);
		break;
	case EPrimitiveType::TRIANGLES:
		glDrawArrays(GL_TRIANGLES, 0, pCommand->m_PrimCount * 3);
		break;
	case EPrimitiveType::QUADS:
		if(m_aLastIndexBufferBound[m_LastStreamBuffer] != m_QuadDrawIndexBufferId)
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadDrawIndexBufferId);
			m_aLastIndexBufferBound[m_LastStreamBuffer] = m_QuadDrawIndexBufferId;
		}
		glDrawElements(GL_TRIANGLES, pCommand->m_PrimCount * 6, GL_UNSIGNED_INT, 0);
		break;
	default:
		dbg_assert_failed("Invalid media island SDF primitive type: %d", (int)pCommand->m_PrimType);
		break;
	}
	m_LastStreamBuffer = (m_LastStreamBuffer + 1 >= MAX_STREAM_BUFFER_COUNT ? 0 : m_LastStreamBuffer + 1);
}

void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderRoundedRectSdf(const CCommandBuffer::SCommand_RenderRoundedRectSdf *pCommand)
{
	if(!m_RoundedRectSdfProgramValid || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0 || m_pRoundedRectSdfProgram == nullptr)
		return;

	UseProgram(m_pRoundedRectSdfProgram);
	SetState(pCommand->m_State, m_pRoundedRectSdfProgram);
	m_pRoundedRectSdfProgram->SetUniformVec4(m_pRoundedRectSdfProgram->m_LocData, 5, (const float *)&pCommand->m_Params);
	UploadStreamBufferData(pCommand->m_PrimType, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex), pCommand->m_PrimCount);
	glBindVertexArray(m_aPrimitiveDrawVertexId[m_LastStreamBuffer]);
	if(m_aLastIndexBufferBound[m_LastStreamBuffer] != m_QuadDrawIndexBufferId)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadDrawIndexBufferId);
		m_aLastIndexBufferBound[m_LastStreamBuffer] = m_QuadDrawIndexBufferId;
	}
	glDrawElements(GL_TRIANGLES, pCommand->m_PrimCount * 6, GL_UNSIGNED_INT, 0);
	m_LastStreamBuffer = (m_LastStreamBuffer + 1 >= MAX_STREAM_BUFFER_COUNT ? 0 : m_LastStreamBuffer + 1);
}

void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderTexturedMsdf(const CCommandBuffer::SCommand_RenderTexturedMsdf *pCommand)
{
	if(!m_TexturedMsdfProgramValid || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0 || m_pTexturedMsdfProgram == nullptr)
		return;

	UseProgram(m_pTexturedMsdfProgram);
	SetState(pCommand->m_State, m_pTexturedMsdfProgram);
	m_pTexturedMsdfProgram->SetUniformVec4(m_pTexturedMsdfProgram->m_LocParams, 1, (const float *)&pCommand->m_MsdfParams);
	m_pTexturedMsdfProgram->SetUniformVec4(m_pTexturedMsdfProgram->m_LocSecondaryColor, 1, (const float *)&pCommand->m_MsdfSecondaryColor);
	UploadStreamBufferData(pCommand->m_PrimType, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex), pCommand->m_PrimCount);
	glBindVertexArray(m_aPrimitiveDrawVertexId[m_LastStreamBuffer]);
	if(m_aLastIndexBufferBound[m_LastStreamBuffer] != m_QuadDrawIndexBufferId)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadDrawIndexBufferId);
		m_aLastIndexBufferBound[m_LastStreamBuffer] = m_QuadDrawIndexBufferId;
	}
	glDrawElements(GL_TRIANGLES, pCommand->m_PrimCount * 6, GL_UNSIGNED_INT, 0);
	m_LastStreamBuffer = (m_LastStreamBuffer + 1 >= MAX_STREAM_BUFFER_COUNT ? 0 : m_LastStreamBuffer + 1);
}

void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderTarget_Draw(const CCommandBuffer::SCommand_RenderTarget_Draw *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
		return;
	const SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Texture == 0 || pCommand->m_W <= 0.0f || pCommand->m_H <= 0.0f || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0)
		return;

	UseProgram(m_pPrimitiveProgramTextured);
	SetState(pCommand->m_State, m_pPrimitiveProgramTextured);
	BindRenderTargetTexture(Target.m_Texture);

	UploadStreamBufferData(EPrimitiveType::QUADS, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex), pCommand->m_PrimCount);
	glBindVertexArray(m_aPrimitiveDrawVertexId[m_LastStreamBuffer]);
	if(m_aLastIndexBufferBound[m_LastStreamBuffer] != m_QuadDrawIndexBufferId)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadDrawIndexBufferId);
		m_aLastIndexBufferBound[m_LastStreamBuffer] = m_QuadDrawIndexBufferId;
	}
	glDrawElements(GL_TRIANGLES, pCommand->m_PrimCount * 6, GL_UNSIGNED_INT, 0);
	m_LastStreamBuffer = (m_LastStreamBuffer + 1 >= MAX_STREAM_BUFFER_COUNT ? 0 : m_LastStreamBuffer + 1);
}

void CCommandProcessorFragment_OpenGL3_3::DestroyBackbufferCaptureResolveTarget()
{
	if(m_BackbufferCaptureResolveFramebuffer != 0)
		glDeleteFramebuffers(1, &m_BackbufferCaptureResolveFramebuffer);
	if(m_BackbufferCaptureResolveTexture != 0)
		glDeleteTextures(1, &m_BackbufferCaptureResolveTexture);
	m_BackbufferCaptureResolveFramebuffer = 0;
	m_BackbufferCaptureResolveTexture = 0;
	m_BackbufferCaptureResolveWidth = 0;
	m_BackbufferCaptureResolveHeight = 0;
}

bool CCommandProcessorFragment_OpenGL3_3::EnsureBackbufferCaptureResolveTarget(int Width, int Height)
{
	if(m_BackbufferCaptureResolveFramebuffer != 0 && m_BackbufferCaptureResolveTexture != 0 &&
		m_BackbufferCaptureResolveWidth == Width && m_BackbufferCaptureResolveHeight == Height)
		return true;

	DestroyBackbufferCaptureResolveTarget();
	glGenTextures(1, &m_BackbufferCaptureResolveTexture);
	glBindTexture(GL_TEXTURE_2D, m_BackbufferCaptureResolveTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenFramebuffers(1, &m_BackbufferCaptureResolveFramebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_BackbufferCaptureResolveFramebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_BackbufferCaptureResolveTexture, 0);
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		dbg_msg("opengl", "backbuffer capture resolve framebuffer incomplete");
		DestroyBackbufferCaptureResolveTarget();
		return false;
	}

	m_BackbufferCaptureResolveWidth = Width;
	m_BackbufferCaptureResolveHeight = Height;
	return true;
}

void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderTarget_CaptureBackbuffer(const CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer *pCommand)
{
	if(m_RenderTargetActive || pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || m_CanvasWidth == 0 || m_CanvasHeight == 0)
		return;
	const SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Framebuffer == 0 || Target.m_Width <= 0 || Target.m_Height <= 0)
		return;

	GLint PreviousReadFramebuffer = 0;
	GLint PreviousDrawFramebuffer = 0;
	GLint PreviousTexture = 0;
	GLint PreviousReadBuffer = GL_BACK;
	GLint aSourceViewport[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &PreviousReadFramebuffer);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &PreviousDrawFramebuffer);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &PreviousTexture);
	glGetIntegerv(GL_READ_BUFFER, &PreviousReadBuffer);
	glGetIntegerv(GL_VIEWPORT, aSourceViewport);
	if(PreviousDrawFramebuffer == (GLint)Target.m_Framebuffer)
		return;
	const int SourceX = aSourceViewport[0];
	const int SourceY = aSourceViewport[1];
	const int SourceWidth = aSourceViewport[2];
	const int SourceHeight = aSourceViewport[3];
	if(SourceWidth <= 0 || SourceHeight <= 0)
		return;

	const GLboolean ScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	if(ScissorEnabled)
		glDisable(GL_SCISSOR_TEST);

	auto RestoreState = [&] {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, PreviousReadFramebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, PreviousDrawFramebuffer);
		glReadBuffer(PreviousReadBuffer);
		glBindTexture(GL_TEXTURE_2D, PreviousTexture);
		if(ScissorEnabled)
			glEnable(GL_SCISSOR_TEST);
	};

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, PreviousDrawFramebuffer);
	GLint Samples = 0;
	glGetIntegerv(GL_SAMPLES, &Samples);
	if(Samples > 0 && !m_BackbufferCaptureMultisampleResolveSupported)
	{
		RestoreState();
		return;
	}
	const bool RequiresSeparateResolve = SourceWidth != Target.m_Width || SourceHeight != Target.m_Height;
	if(RequiresSeparateResolve)
	{
		if(!EnsureBackbufferCaptureResolveTarget(SourceWidth, SourceHeight))
		{
			RestoreState();
			return;
		}
		if(Samples > 0)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, PreviousReadFramebuffer);
			if(PreviousReadFramebuffer == 0)
				glReadBuffer(GL_BACK);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_BackbufferCaptureResolveFramebuffer);
			glBlitFramebuffer(SourceX, SourceY, SourceX + SourceWidth, SourceY + SourceHeight, 0, 0, SourceWidth, SourceHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, PreviousReadFramebuffer);
			if(PreviousReadFramebuffer == 0)
				glReadBuffer(GL_BACK);
			glBindTexture(GL_TEXTURE_2D, m_BackbufferCaptureResolveTexture);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SourceX, SourceY, SourceWidth, SourceHeight);
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_BackbufferCaptureResolveFramebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Target.m_Framebuffer);
		glBlitFramebuffer(0, 0, SourceWidth, SourceHeight, 0, 0, Target.m_Width, Target.m_Height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}
	else
	{
		if(Samples == 0 && PreviousReadFramebuffer == 0)
		{
			// Copy directly from the window back buffer. Some drivers expose a
			// stale/empty read framebuffer after a swap, while CopyTexSubImage2D
			// reliably addresses the presented back buffer.
			glBindFramebuffer(GL_READ_FRAMEBUFFER, PreviousReadFramebuffer);
			glReadBuffer(GL_BACK);
			glBindTexture(GL_TEXTURE_2D, Target.m_Texture);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SourceX, SourceY, SourceWidth, SourceHeight);
		}
		else
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, PreviousReadFramebuffer);
			if(PreviousReadFramebuffer == 0)
				glReadBuffer(GL_BACK);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Target.m_Framebuffer);
			glBlitFramebuffer(SourceX, SourceY, SourceX + SourceWidth, SourceY + SourceHeight, 0, 0, Target.m_Width, Target.m_Height, GL_COLOR_BUFFER_BIT, Samples > 0 ? GL_NEAREST : GL_LINEAR);
		}
	}

	RestoreState();
}

void CCommandProcessorFragment_OpenGL3_3::Cmd_RenderTarget_GaussianBlurPass(const CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass *pCommand)
{
	if(!m_GaussianBlurProgramValid || !m_RenderTargetActive || m_ActiveRenderTargetId < 0 || pCommand->m_SourceTargetId < 0 ||
		(size_t)m_ActiveRenderTargetId >= m_vRenderTargets.size() || (size_t)pCommand->m_SourceTargetId >= m_vRenderTargets.size() ||
		pCommand->m_SourceTargetId == m_ActiveRenderTargetId || pCommand->m_Radius < 1 || pCommand->m_Radius > IGraphics::GAUSSIAN_BLUR_MAX_RADIUS)
		return;
	const SOpenGLRenderTarget &Source = m_vRenderTargets[pCommand->m_SourceTargetId];
	const SOpenGLRenderTarget &Destination = m_vRenderTargets[m_ActiveRenderTargetId];
	const bool DualKawase = pCommand->m_Mode == IGraphics::EBlurMode::DUAL;
	if(Source.m_Texture == 0 || Destination.m_Texture == 0 || Source.m_Width <= 0 || Source.m_Height <= 0 || Destination.m_Width <= 0 || Destination.m_Height <= 0 ||
		(!DualKawase && (Source.m_Width != Destination.m_Width || Source.m_Height != Destination.m_Height)) ||
		(DualKawase && (pCommand->m_Upsample ? (Source.m_Width >= Destination.m_Width || Source.m_Height >= Destination.m_Height) : (Source.m_Width <= Destination.m_Width || Source.m_Height <= Destination.m_Height))))
		return;

	UseProgram(m_pGaussianBlurProgram);
	glDisable(GL_BLEND);
	m_LastBlendMode = EBlendMode::NONE;
	BindRenderTargetTexture(Source.m_Texture);
	const bool Gaussian = pCommand->m_Mode == IGraphics::EBlurMode::GAUSSIAN;
	const float aTexelOffset[2] = {
		Gaussian && !pCommand->m_Horizontal ? 0.0f : 1.0f / Source.m_Width,
		Gaussian && pCommand->m_Horizontal ? 0.0f : 1.0f / Source.m_Height,
	};
	m_pGaussianBlurProgram->SetUniformVec2(m_pGaussianBlurProgram->m_LocTexelOffset, 1, aTexelOffset);
	m_pGaussianBlurProgram->SetUniform(m_pGaussianBlurProgram->m_LocRadius, pCommand->m_Radius);
	m_pGaussianBlurProgram->SetUniform(m_pGaussianBlurProgram->m_LocMode, static_cast<int>(pCommand->m_Mode));
	m_pGaussianBlurProgram->SetUniform(m_pGaussianBlurProgram->m_LocPass, pCommand->m_Pass);
	m_pGaussianBlurProgram->SetUniform(m_pGaussianBlurProgram->m_LocWeights, (int)pCommand->m_aWeights.size(), pCommand->m_aWeights.data());

	CCommandBuffer::SVertex aVertices[4]{};
	aVertices[0].m_Pos = vec2(-1.0f, -1.0f);
	aVertices[0].m_Tex = vec2(0.0f, 0.0f);
	aVertices[1].m_Pos = vec2(1.0f, -1.0f);
	aVertices[1].m_Tex = vec2(1.0f, 0.0f);
	aVertices[2].m_Pos = vec2(1.0f, 1.0f);
	aVertices[2].m_Tex = vec2(1.0f, 1.0f);
	aVertices[3].m_Pos = vec2(-1.0f, 1.0f);
	aVertices[3].m_Tex = vec2(0.0f, 1.0f);
	UploadStreamBufferData(EPrimitiveType::QUADS, aVertices, sizeof(CCommandBuffer::SVertex), 1);
	glBindVertexArray(m_aPrimitiveDrawVertexId[m_LastStreamBuffer]);
	if(m_aLastIndexBufferBound[m_LastStreamBuffer] != m_QuadDrawIndexBufferId)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadDrawIndexBufferId);
		m_aLastIndexBufferBound[m_LastStreamBuffer] = m_QuadDrawIndexBufferId;
	}
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	m_LastStreamBuffer = (m_LastStreamBuffer + 1 >= MAX_STREAM_BUFFER_COUNT ? 0 : m_LastStreamBuffer + 1);
}

#endif
