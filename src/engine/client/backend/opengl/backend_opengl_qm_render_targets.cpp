#include "backend_opengl.h"

#include <base/detect.h>

#include <engine/graphics.h>

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
#define GL_TEXTURE_2D_ARRAY_EXT GL_TEXTURE_2D_ARRAY
// GLES doesn't support GL_QUADS, but the code is also never executed
#define GL_QUADS GL_TRIANGLES
#ifndef CONF_BACKEND_OPENGL_ES3
#include <GLES/gl.h>
#define glOrtho glOrthof
#else
#define BACKEND_GL_MODERN_API 1
#endif
#endif

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_Create(const CCommandBuffer::SCommand_RenderTarget_Create *pCommand)
{
	if(pCommand->m_TargetId < 0 || pCommand->m_Width <= 0 || pCommand->m_Height <= 0)
		return;
	if((size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
		m_vRenderTargets.resize((size_t)pCommand->m_TargetId + 1);

	SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Framebuffer != 0 || Target.m_Texture != 0)
	{
		CCommandBuffer::SCommand_RenderTarget_Destroy DestroyCmd;
		DestroyCmd.m_TargetId = pCommand->m_TargetId;
		Cmd_RenderTarget_Destroy(&DestroyCmd);
	}

	glGenTextures(1, &Target.m_Texture);
	glBindTexture(GL_TEXTURE_2D, Target.m_Texture);
#if defined(BACKEND_AS_OPENGL_ES)
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pCommand->m_Width, pCommand->m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pCommand->m_Width, pCommand->m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#endif
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenFramebuffers(1, &Target.m_Framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, Target.m_Framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Target.m_Texture, 0);
	const GLenum Status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if(Status != GL_FRAMEBUFFER_COMPLETE)
	{
		dbg_msg("opengl", "render target %d incomplete: 0x%x", pCommand->m_TargetId, Status);
		CCommandBuffer::SCommand_RenderTarget_Destroy DestroyCmd;
		DestroyCmd.m_TargetId = pCommand->m_TargetId;
		Cmd_RenderTarget_Destroy(&DestroyCmd);
		return;
	}

	Target.m_Width = pCommand->m_Width;
	Target.m_Height = pCommand->m_Height;
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_Destroy(const CCommandBuffer::SCommand_RenderTarget_Destroy *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
		return;
	SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Framebuffer != 0)
		glDeleteFramebuffers(1, &Target.m_Framebuffer);
	if(Target.m_Texture != 0)
		glDeleteTextures(1, &Target.m_Texture);
	Target = SOpenGLRenderTarget{};
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_Begin(const CCommandBuffer::SCommand_RenderTarget_Begin *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
		return;
	const SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Framebuffer == 0)
		return;
	if(m_RenderTargetActive)
	{
		dbg_msg("opengl", "nested render target begin ignored");
		return;
	}

	// render target 会临时改写绘制目标和视口，结束时必须恢复，否则后续 UI 坐标会错位。
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_RenderTargetPreviousFramebuffer);
	glGetIntegerv(GL_VIEWPORT, m_aRenderTargetPreviousViewport);
	m_RenderTargetActive = true;
	m_ActiveRenderTargetId = pCommand->m_TargetId;
	glBindFramebuffer(GL_FRAMEBUFFER, Target.m_Framebuffer);
	glViewport(0, 0, Target.m_Width, Target.m_Height);
	SetState(pCommand->m_State);
	glClearColor(pCommand->m_ClearColor.r, pCommand->m_ClearColor.g, pCommand->m_ClearColor.b, pCommand->m_ClearColor.a);
	glClear(GL_COLOR_BUFFER_BIT);
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_End(const CCommandBuffer::SCommand_RenderTarget_End *pCommand)
{
	if(!m_RenderTargetActive)
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, m_RenderTargetPreviousFramebuffer);
	glViewport(m_aRenderTargetPreviousViewport[0], m_aRenderTargetPreviousViewport[1], m_aRenderTargetPreviousViewport[2], m_aRenderTargetPreviousViewport[3]);
	m_RenderTargetActive = false;
	SetState(pCommand->m_State);
	m_ActiveRenderTargetId = -1;
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_Draw(const CCommandBuffer::SCommand_RenderTarget_Draw *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
		return;
	const SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Texture == 0 || pCommand->m_W <= 0.0f || pCommand->m_H <= 0.0f || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0)
		return;

	SetState(pCommand->m_State);
	// Render-target textures are allocated without mipmaps. Clear any sampler
	// inherited from the previous draw before sampling the target.
	if(IsNewApi())
	{
		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, 0);
	}

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, Target.m_Texture);

#ifndef BACKEND_GL_MODERN_API
	glVertexPointer(2, GL_FLOAT, sizeof(CCommandBuffer::SVertex), (char *)pCommand->m_pVertices);
	glTexCoordPointer(2, GL_FLOAT, sizeof(CCommandBuffer::SVertex), (char *)pCommand->m_pVertices + sizeof(float) * 2);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(CCommandBuffer::SVertex), (char *)pCommand->m_pVertices + sizeof(float) * 4);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glDrawArrays(GL_QUADS, 0, pCommand->m_PrimCount * 4);

	// 离屏纹理回绘也必须清理数组状态，不能泄漏给后续地图绘制。
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
#endif
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_Readback(const CCommandBuffer::SCommand_RenderTarget_Readback *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || pCommand->m_pImage == nullptr)
		return;

	const SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_Framebuffer == 0 || Target.m_Width <= 0 || Target.m_Height <= 0)
		return;

	const int w = Target.m_Width;
	const int h = Target.m_Height;

	// we allocate one more row to use when we are flipping the texture
	unsigned char *pPixelData = (unsigned char *)malloc((size_t)w * (h + 1) * 4);
	unsigned char *pTempRow = pPixelData + w * h * 4;

	GLint Alignment;
	GLint PreviousFramebuffer;
	glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &PreviousFramebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, Target.m_Framebuffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pPixelData);
	glPixelStorei(GL_PACK_ALIGNMENT, Alignment);
	glBindFramebuffer(GL_FRAMEBUFFER, PreviousFramebuffer);

	// flip the pixel because opengl works from bottom left corner
	for(int y = 0; y < h / 2; y++)
	{
		mem_copy(pTempRow, pPixelData + y * w * 4, w * 4);
		mem_copy(pPixelData + y * w * 4, pPixelData + (h - y - 1) * w * 4, w * 4);
		mem_copy(pPixelData + (h - y - 1) * w * 4, pTempRow, w * 4);
	}

	pCommand->m_pImage->m_Width = w;
	pCommand->m_pImage->m_Height = h;
	pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
	pCommand->m_pImage->m_pData = pPixelData;
}

#endif
