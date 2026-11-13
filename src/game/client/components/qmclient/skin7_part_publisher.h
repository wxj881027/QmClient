#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_PART_PUBLISHER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_PART_PUBLISHER_H

#include "skin7_part_load_job.h"

#include <engine/client/gpu_upload_limiter.h>
#include <engine/graphics.h>

#include <type_traits>
#include <vector>

// 仅由主线程使用；未提交句柄显式退回图形接口，不在析构或工作线程卸载。
class CQmSkin7PartPublisher
{
	IGraphics::CTextureHandle m_aUncommitted[2];

public:
	CQmSkin7PartPublisher() = default;
	CQmSkin7PartPublisher(const CQmSkin7PartPublisher &) = delete;
	CQmSkin7PartPublisher &operator=(const CQmSkin7PartPublisher &) = delete;

	bool TryRelease(IGraphics &Graphics) noexcept
	{
		bool Released = true;
		for(auto &Handle : m_aUncommitted)
		{
			if(!Handle.IsValid() || Handle.IsNullTexture())
			{
				Handle.Invalidate();
				continue;
			}
			try
			{
				Graphics.UnloadTexture(&Handle);
			}
			catch(...)
			{
			}
			Released = Released && !Handle.IsValid();
		}
		return Released;
	}

	bool TryPublishAccessory(const CImageInfo &Image, IGraphics::CTextureHandle &Target,
		IGraphics &Graphics, CGpuUploadLimiter &Limiter, const char *pName) noexcept
	{
		if(!TryRelease(Graphics))
			return false;
		try
		{
			if(Image.m_pData)
			{
				if(!Limiter.CanUpload())
					return false;
				Limiter.OnUploaded();
				m_aUncommitted[0] = Graphics.LoadTextureRaw(Image, 0, pName);
				if(!m_aUncommitted[0].IsValid() || m_aUncommitted[0].IsNullTexture() || !Graphics.IsTextureHandleAllocated(m_aUncommitted[0]))
				{
					TryRelease(Graphics);
					return false;
				}
			}
			// 候选可用后才卸载旧图；缺失附件仍沿用原来的清空语义。
			if(Target.IsValid() && !Target.IsNullTexture())
				Graphics.UnloadTexture(&Target);
			if(Target.IsValid() && !Target.IsNullTexture())
			{
				TryRelease(Graphics);
				return false;
			}
			Target = m_aUncommitted[0];
			m_aUncommitted[0].Invalidate();
			return true;
		}
		catch(...)
		{
			TryRelease(Graphics);
			return false;
		}
	}

	template<typename TPart, typename TAllocator>
	bool TryPublish(const CQmSkin7PartLoadJob::SResult &Result, std::vector<TPart, TAllocator> &Parts,
		IGraphics &Graphics, CGpuUploadLimiter &Limiter) noexcept
	{
		static_assert(std::is_nothrow_copy_constructible_v<TPart>);
		if(!TryRelease(Graphics) || !Limiter.CanUpload(2) || !Result.m_Success)
			return false;
		try
		{
			if(Parts.size() == Parts.max_size())
				return false;
			// 先准备容器容量，上传失败仍保留任务中的两份 CPU 图像。
			if(Parts.size() == Parts.capacity())
			{
				const auto Growth = Parts.size() > (Parts.max_size() - 1) / 2 ? Parts.max_size() : Parts.size() * 2 + 1;
				Parts.reserve(Growth);
			}
			for(int i = 0; i < 2; ++i)
			{
				// 失败尝试也计入本帧预算，避免异常输入反复提交。
				Limiter.OnUploaded();
				m_aUncommitted[i] = Graphics.LoadTextureRaw(i == 0 ? Result.m_OriginalImage : Result.m_GrayscaleImage, 0, Result.m_aName);
				if(!m_aUncommitted[i].IsValid() || m_aUncommitted[i].IsNullTexture() || !Graphics.IsTextureHandleAllocated(m_aUncommitted[i]))
				{
					TryRelease(Graphics);
					return false;
				}
			}
			TPart Part;
			Part.m_Type = Result.m_PartType;
			Part.m_Flags = Result.m_Flags;
			str_copy(Part.m_aName, Result.m_aName, sizeof(Part.m_aName));
			Part.m_BloodColor = Result.m_BloodColor;
			Part.m_QmSkinOutline = Result.m_QmSkinOutline;
			Part.m_pChatAvatarOriginal = Result.m_pChatAvatarOriginal;
			Part.m_pChatAvatarColorable = Result.m_pChatAvatarColorable;
			Part.m_OriginalTexture = m_aUncommitted[0];
			Part.m_ColorableTexture = m_aUncommitted[1];
			Parts.emplace_back(Part);
			for(auto &Handle : m_aUncommitted)
				Handle.Invalidate();
			return true;
		}
		catch(...)
		{
			TryRelease(Graphics);
			return false;
		}
	}
};

#endif
