#ifndef ENGINE_CLIENT_BACKEND_OPENGL_RENDER_TARGET_CREATE_H
#define ENGINE_CLIENT_BACKEND_OPENGL_RENDER_TARGET_CREATE_H

#include <engine/client/graphics_defines.h>

#include <cstddef>
#include <vector>

namespace opengl_render_target
{
	struct STarget
	{
		TWGLuint m_Framebuffer = 0;
		TWGLuint m_Texture = 0;
		int m_Width = 0;
		int m_Height = 0;
	};

	struct SBindings
	{
		TWGLuint m_DrawFramebuffer = 0;
		TWGLuint m_ReadFramebuffer = 0;
		TWGLuint m_Texture = 0;
	};

	// 只替换设备边界；操作不得抛异常，创建返回 0 表示未取得资源。
	class IOperations
	{
	public:
		virtual ~IOperations() = default;
		virtual SBindings GetBindings() noexcept = 0;
		virtual TWGLuint CreateTexture(int Width, int Height) noexcept = 0;
		virtual TWGLuint CreateFramebuffer(TWGLuint Texture) noexcept = 0;
		virtual bool IsFramebufferComplete() noexcept = 0;
		virtual void RestoreBindings(const SBindings &Bindings) noexcept = 0;
		virtual void DeleteFramebuffer(TWGLuint Framebuffer) noexcept = 0;
		virtual void DeleteTexture(TWGLuint Texture) noexcept = 0;
	};

	template<typename TAllocator>
	bool Create(std::vector<STarget, TAllocator> &vTargets, int Id, int Width, int Height, IOperations &Operations) noexcept
	{
		if(Id < 0 || Width <= 0 || Height <= 0)
			return false;
		const size_t OldSize = vTargets.size();
		const size_t Index = static_cast<size_t>(Id);
		try
		{
			if(Index >= OldSize)
				vTargets.resize(Index + 1);
		}
		catch(...)
		{
			return false;
		}

		const STarget Previous = vTargets[Index];
		const SBindings Saved = Operations.GetBindings();
		// 不能替换仍在绘制或读取的同一目标，避免删除当前 pass 所依赖的 FBO。
		if(Previous.m_Framebuffer != 0 && (Saved.m_DrawFramebuffer == Previous.m_Framebuffer || Saved.m_ReadFramebuffer == Previous.m_Framebuffer))
			return false;

		STarget Candidate;
		const auto Fail = [&]() noexcept {
			Operations.RestoreBindings(Saved);
			if(Candidate.m_Framebuffer != 0)
				Operations.DeleteFramebuffer(Candidate.m_Framebuffer);
			if(Candidate.m_Texture != 0)
				Operations.DeleteTexture(Candidate.m_Texture);
			// 新增项都是空的标量状态；失败只恢复 size，保留可复用容量。
			if(vTargets.size() > OldSize)
				vTargets.resize(OldSize);
			return false;
		};
		Candidate.m_Texture = Operations.CreateTexture(Width, Height);
		if(Candidate.m_Texture == 0)
			return Fail();
		Candidate.m_Framebuffer = Operations.CreateFramebuffer(Candidate.m_Texture);
		if(Candidate.m_Framebuffer == 0 || !Operations.IsFramebufferComplete())
			return Fail();

		Candidate.m_Width = Width;
		Candidate.m_Height = Height;
		SBindings Restored = Saved;
		// 已绑定的旧纹理被同一逻辑目标替换后，绑定新纹理，不能恢复随后要删除的 ID。
		if(Previous.m_Texture != 0 && Saved.m_Texture == Previous.m_Texture)
			Restored.m_Texture = Candidate.m_Texture;
		Operations.RestoreBindings(Restored);
		vTargets[Index] = Candidate;
		if(Previous.m_Framebuffer != 0)
			Operations.DeleteFramebuffer(Previous.m_Framebuffer);
		if(Previous.m_Texture != 0)
			Operations.DeleteTexture(Previous.m_Texture);
		return true;
	}
}

#endif
