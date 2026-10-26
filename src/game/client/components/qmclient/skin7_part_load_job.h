#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_PART_LOAD_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_PART_LOAD_JOB_H

#include <base/color.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <utility>

class CQmSkinOutline;
namespace QmChatAvatar
{
	struct SSource;
}

class CQmSkin7PartLoadJob : public IJob
{
public:
	struct SResult
	{
		CImageInfo m_OriginalImage;
		CImageInfo m_GrayscaleImage;
		ColorRGBA m_BloodColor = ColorRGBA(0, 0, 0);
		std::shared_ptr<CQmSkinOutline> m_QmSkinOutline;
		std::shared_ptr<const QmChatAvatar::SSource> m_pChatAvatarOriginal;
		std::shared_ptr<const QmChatAvatar::SSource> m_pChatAvatarColorable;
		int m_PartType = 0;
		int m_Flags = 0;
		char m_aName[24] = "";
		bool m_Success = false;

		SResult() = default;
		SResult(SResult &&) noexcept = default;
		SResult &operator=(SResult &&) noexcept = default;
		~SResult()
		{
			m_OriginalImage.Free();
			m_GrayscaleImage.Free();
		}

		void ClearPayload()
		{
			m_OriginalImage.Free();
			m_GrayscaleImage.Free();
			m_QmSkinOutline.reset();
			m_pChatAvatarOriginal.reset();
			m_pChatAvatarColorable.reset();
			m_Success = false;
		}
	};

	// 素材生成仍由皮肤组件提供；只在后台准备CPU数据，不访问图形接口。
	using TPrepareVisuals = void (*)(SResult &);

private:
	const std::string m_Path;
	const std::string m_PartName;
	IStorage *const m_pStorage;
	const int m_StorageType;
	const int m_PartType;
	const int m_Flags;
	const TPrepareVisuals m_pfnPrepareVisuals;
	mutable CLock m_Mutex;
	SResult m_Result;
	bool m_Completed = false;
	bool m_ResultConsumed = false;

protected:
	void Run() override REQUIRES(!m_Mutex)
	{
		if(State() == STATE_ABORTED)
			return;

		SResult Result;
		str_copy(Result.m_aName, m_PartName.c_str(), sizeof(Result.m_aName));
		Result.m_PartType = m_PartType;
		Result.m_Flags = m_Flags;
		try
		{
			struct SFileData
			{
				void *m_pData = nullptr;
				~SFileData() { free(m_pData); }
			} FileData;
			unsigned FileSize = 0;
			if(!m_pStorage->ReadFile(m_Path.c_str(), m_StorageType, &FileData.m_pData, &FileSize))
				log_error("skins7", "Failed to read skin part file '%s'", m_Path.c_str());
			else if(State() == STATE_ABORTED)
				return;
			else if(!CImageLoader::LoadPng(FileData.m_pData, FileSize, m_Path.c_str(), Result.m_OriginalImage))
				log_error("skins7", "Failed to decode skin part PNG '%s'", m_Path.c_str());
			else
			{
				free(FileData.m_pData);
				FileData.m_pData = nullptr;
				if(State() == STATE_ABORTED)
					return;
				if(Result.m_OriginalImage.m_Format != CImageInfo::FORMAT_RGBA)
					log_error("skins7", "Skin part '%s' must be RGBA format", m_Path.c_str());
				else
				{
					Result.m_GrayscaleImage = Result.m_OriginalImage.DeepCopy();
					if(Result.m_GrayscaleImage.m_pData == nullptr)
						log_error("skins7", "Failed to copy skin part '%s'", m_Path.c_str());
					else
					{
						ConvertToGrayscale(Result.m_GrayscaleImage);
						m_pfnPrepareVisuals(Result);
						Result.m_Success = true;
					}
				}
			}
		}
		catch(...)
		{
			// 作业池结束状态不能代替部件完成标记；记录失败也不能再次打断发布。
			try
			{
				log_error("skins7", "Failed to prepare skin part '%s'", m_Path.c_str());
			}
			catch(...)
			{
			}
		}
		if(!Result.m_Success)
			Result.ClearPayload();
		CLockScope Lock(m_Mutex);
		m_Result = std::move(Result);
		m_Completed = true;
	}

public:
	CQmSkin7PartLoadJob(const char *pPath, const char *pPartName, IStorage *pStorage, int StorageType, int PartType, int Flags, TPrepareVisuals pfnPrepareVisuals) :
		m_Path(pPath),
		m_PartName(pPartName),
		m_pStorage(pStorage),
		m_StorageType(StorageType),
		m_PartType(PartType),
		m_Flags(Flags),
		m_pfnPrepareVisuals(pfnPrepareVisuals)
	{
		Abortable(true);
	}

	bool IsCompleted() const REQUIRES(!m_Mutex)
	{
		CLockScope Lock(m_Mutex);
		return m_Completed;
	}

	// 回调不得重入本任务；只有完整发布成功才释放原 CPU 结果。
	template<typename TPublish>
	bool PublishResult(TPublish &&Publish) REQUIRES(!m_Mutex)
	{
		CLockScope Lock(m_Mutex);
		if(!m_Completed || m_ResultConsumed)
			return false;
		try
		{
			if(m_Result.m_Success && !std::forward<TPublish>(Publish)(static_cast<const SResult &>(m_Result)))
				return false;
		}
		catch(...)
		{
			return false;
		}
		m_Result = SResult();
		m_ResultConsumed = true;
		return true;
	}

	SResult GetResult() REQUIRES(!m_Mutex)
	{
		CLockScope Lock(m_Mutex);
		m_ResultConsumed = m_Completed;
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}
};

// 空候选表示名称校验后跳过；分配或接收失败保留当前描述，下一帧重试。
// 跟踪节点先准备，原生引擎确认接收后只推进无分配的游标。
template<typename TParts, typename TAllocator, typename TFactory>
bool QmTryQueueNextSkin7Part(const TParts &Parts, size_t &NextPart,
	std::deque<std::shared_ptr<CQmSkin7PartLoadJob>, TAllocator> &Pending, IEngine &Engine, TFactory &&Factory)
{
	if(NextPart >= Parts.size())
		return false;
	bool Tracked = false;
	try
	{
		auto pJob = std::forward<TFactory>(Factory)(Parts[NextPart]);
		if(pJob)
		{
			Pending.push_back(pJob);
			Tracked = true;
			if(!Engine.TryAddJob(pJob))
			{
				Pending.pop_back();
				return false;
			}
		}
		++NextPart;
		return true;
	}
	catch(...)
	{
		if(Tracked)
			Pending.pop_back();
		return false;
	}
}

#endif
