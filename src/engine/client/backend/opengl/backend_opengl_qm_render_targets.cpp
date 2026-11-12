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
	Target.m_SampleCount = 1;

	// GL 3.0+/ES3 才有多采样 renderbuffer；调用方只在能力位为真时请求。
	if(pCommand->m_SampleCount > 1)
	{
		glGenFramebuffers(1, &Target.m_MultisampleFramebuffer);
		glGenRenderbuffers(1, &Target.m_MultisampleRenderbuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, Target.m_MultisampleRenderbuffer);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, pCommand->m_SampleCount, GL_RGBA8, Target.m_Width, Target.m_Height);
		glBindFramebuffer(GL_FRAMEBUFFER, Target.m_MultisampleFramebuffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, Target.m_MultisampleRenderbuffer);
		const GLenum MultisampleStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if(MultisampleStatus == GL_FRAMEBUFFER_COMPLETE)
		{
			Target.m_SampleCount = pCommand->m_SampleCount;
		}
		else
		{
			dbg_msg("opengl", "multisample render target %d incomplete: 0x%x; falling back to single sample", pCommand->m_TargetId, MultisampleStatus);
			glDeleteFramebuffers(1, &Target.m_MultisampleFramebuffer);
			glDeleteRenderbuffers(1, &Target.m_MultisampleRenderbuffer);
			Target.m_MultisampleFramebuffer = 0;
			Target.m_MultisampleRenderbuffer = 0;
		}
	}
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_Destroy(const CCommandBuffer::SCommand_RenderTarget_Destroy *pCommand)
{
	if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
		return;
	SOpenGLRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
	if(Target.m_MultisampleFramebuffer != 0)
		glDeleteFramebuffers(1, &Target.m_MultisampleFramebuffer);
	if(Target.m_MultisampleRenderbuffer != 0)
		glDeleteRenderbuffers(1, &Target.m_MultisampleRenderbuffer);
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
	glGetFloatv(GL_COLOR_CLEAR_VALUE, m_aRenderTargetPreviousClearColor);
	m_RenderTargetActive = true;
	m_ActiveRenderTargetId = pCommand->m_TargetId;
	glBindFramebuffer(GL_FRAMEBUFFER, Target.m_SampleCount > 1 && Target.m_MultisampleFramebuffer != 0 ? Target.m_MultisampleFramebuffer : Target.m_Framebuffer);
	glViewport(0, 0, Target.m_Width, Target.m_Height);
	glDisable(GL_SCISSOR_TEST);
	m_LastClipEnable = false;
	glClearColor(pCommand->m_ClearColor.r, pCommand->m_ClearColor.g, pCommand->m_ClearColor.b, pCommand->m_ClearColor.a);
	glClear(GL_COLOR_BUFFER_BIT);
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_End(const CCommandBuffer::SCommand_RenderTarget_End *pCommand)
{
	if(!m_RenderTargetActive)
		return;
	if(m_ActiveRenderTargetId >= 0 && (size_t)m_ActiveRenderTargetId < m_vRenderTargets.size())
	{
		const SOpenGLRenderTarget &Target = m_vRenderTargets[m_ActiveRenderTargetId];
		if(Target.m_SampleCount > 1 && Target.m_MultisampleFramebuffer != 0)
		{
			// 多采样 renderbuffer 不能直接采样；结束前一次性 resolve 到单采样纹理。
			glBindFramebuffer(GL_READ_FRAMEBUFFER, Target.m_MultisampleFramebuffer);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, Target.m_Framebuffer);
			glBlitFramebuffer(0, 0, Target.m_Width, Target.m_Height, 0, 0, Target.m_Width, Target.m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, m_RenderTargetPreviousFramebuffer);
	glViewport(m_aRenderTargetPreviousViewport[0], m_aRenderTargetPreviousViewport[1], m_aRenderTargetPreviousViewport[2], m_aRenderTargetPreviousViewport[3]);
	m_RenderTargetActive = false;
	glClearColor(m_aRenderTargetPreviousClearColor[0], m_aRenderTargetPreviousClearColor[1], m_aRenderTargetPreviousClearColor[2], m_aRenderTargetPreviousClearColor[3]);
	if(pCommand->m_State.m_ClipEnable)
	{
		SetClipRect(pCommand->m_State);
		glEnable(GL_SCISSOR_TEST);
	}
	else
		glDisable(GL_SCISSOR_TEST);
	m_LastClipEnable = pCommand->m_State.m_ClipEnable;
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

void CCommandProcessorFragment_OpenGL::FinishPendingReadbackPbo()
{
	if(!m_ReadbackPboPending)
		return;
	m_ReadbackPboPending = false;
	// 先取走并复位在途状态：结算路径里不再触碰成员，避免嵌套读回时状态错乱。
	const TWGLuint PendingPbo = m_aReadbackPbos[m_ReadbackPboFlip ^ 1];
	auto *pImage = m_pPendingReadbackImage;
	auto *pCompletion = m_pPendingReadbackCompletion;
	m_pPendingReadbackImage = nullptr;
	m_pPendingReadbackCompletion = nullptr;
	if(pImage == nullptr)
		return;

	GLint Alignment = 0;
	GLint PreviousPixelBuffer = 0;
	glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &PreviousPixelBuffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, PendingPbo);
	void *pMapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_ReadbackPboPendingSize, GL_MAP_READ_BIT);
	if(pMapped != nullptr)
	{
		// 读回尺寸在排队时记录在 Image 上；这里按它做垂直翻转拷出（OpenGL 原点在左下）。
		const int w = (int)pImage->m_Width;
		const int h = (int)pImage->m_Height;
		unsigned char *pPixelData = (unsigned char *)malloc((size_t)w * (h + 1) * 4);
		if(pPixelData != nullptr)
		{
			unsigned char *pTempRow = pPixelData + (size_t)w * h * 4;
			mem_copy(pPixelData, pMapped, (size_t)w * h * 4);
			for(int y = 0; y < h / 2; y++)
			{
				mem_copy(pTempRow, pPixelData + y * w * 4, w * 4);
				mem_copy(pPixelData + y * w * 4, pPixelData + ((h - y) - 1) * w * 4, w * 4);
				mem_copy(pPixelData + ((h - y) - 1) * w * 4, pTempRow, w * 4);
			}
			pImage->m_pData = pPixelData;
		}
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, PreviousPixelBuffer);
	glPixelStorei(GL_PACK_ALIGNMENT, Alignment);
	if(pCompletion != nullptr)
		pCompletion->Signal();
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

	// PBO 双缓冲需要缓冲对象与异步 map；不可用（老 GL/ES2）时退回同步读回，行为与旧版一致。
	const bool PboPath = glGenBuffers != nullptr && glMapBufferRange != nullptr;
	if(!PboPath)
	{
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
		if(pCommand->m_pCompletion != nullptr)
			pCommand->m_pCompletion->Signal();
		return;
	}

	// 先结算上一笔在途 DMA（其像素在上一帧排队，此刻早已传完，map 基本不等待），
	// 再把本次读回排进另一个 PBO——DMA 与调用方的下一帧渲染重叠。
	FinishPendingReadbackPbo();

	const size_t DataSize = (size_t)w * h * 4;
	if(m_aReadbackPbos[0] == 0)
		glGenBuffers(2, m_aReadbackPbos);
	const int PboIndex = m_ReadbackPboFlip;
	const TWGLuint Pbo = m_aReadbackPbos[PboIndex];
	// 每个 PBO 各自记录容量：两块交替使用且读回尺寸可能变化（静帧与视频混用），
	// 只扩当前这一块，另一块保持自己的容量，避免向小缓冲排队越界 DMA。
	if(DataSize > m_aReadbackPboSizes[PboIndex])
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, Pbo);
		glBufferData(GL_PIXEL_PACK_BUFFER, DataSize, nullptr, GL_STREAM_READ);
		m_aReadbackPboSizes[PboIndex] = DataSize;
	}

	GLint Alignment = 0;
	GLint PreviousFramebuffer = 0;
	GLint PreviousPixelBuffer = 0;
	glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &PreviousFramebuffer);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &PreviousPixelBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, Target.m_Framebuffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, Pbo);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	// 目标为 nullptr 时 glReadPixels 只启动 DMA 立即返回；像素由 FinishPendingReadbackPbo 拷出。
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, PreviousPixelBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, PreviousFramebuffer);
	glPixelStorei(GL_PACK_ALIGNMENT, Alignment);

	// 尺寸与格式在排队时就记录（结算时只填像素），主线程据此校验。
	pCommand->m_pImage->m_Width = w;
	pCommand->m_pImage->m_Height = h;
	pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
	m_pPendingReadbackImage = pCommand->m_pImage;
	m_pPendingReadbackCompletion = pCommand->m_pCompletion;
	m_ReadbackPboPendingSize = DataSize;
	m_ReadbackPboFlip ^= 1;
	m_ReadbackPboPending = true;
}

void CCommandProcessorFragment_OpenGL::Cmd_RenderTarget_ReadbackFlush(const CCommandBuffer::SCommand_RenderTarget_ReadbackFlush *pCommand)
{
	(void)pCommand;
	FinishPendingReadbackPbo();
}

#endif
