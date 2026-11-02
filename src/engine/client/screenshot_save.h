#ifndef ENGINE_CLIENT_SCREENSHOT_SAVE_H
#define ENGINE_CLIENT_SCREENSHOT_SAVE_H

#include <base/log.h>
#include <base/log_color.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace screenshot_save
{
	class COwnedImage
	{
		CImageInfo m_Image;

	public:
		explicit COwnedImage(CImageInfo &&Image) :
			m_Image(std::move(Image)) {}
		~COwnedImage() { m_Image.Free(); }
		COwnedImage(const COwnedImage &) = delete;
		COwnedImage &operator=(const COwnedImage &) = delete;
		COwnedImage(COwnedImage &&Other) noexcept :
			m_Image(std::move(Other.m_Image)) {}
		COwnedImage &operator=(COwnedImage &&Other) noexcept
		{
			if(this != &Other)
			{
				m_Image.Free();
				m_Image = std::move(Other.m_Image);
			}
			return *this;
		}
		CImageInfo &Image() { return m_Image; }
	};

	inline bool Save(IStorage *pStorage, const char *pName, CImageInfo &Image,
		const IGraphics::FScreenshotProcessor &pfnProcessor,
		char *pOutWholePath, size_t OutWholePathSize)
	{
		static constexpr LOG_COLOR SCREENSHOT_LOG_COLOR = LOG_COLOR{255, 153, 76};
		if(pStorage == nullptr || pName == nullptr || pName[0] == '\0')
			return false;
		std::string Comment;
		if(pfnProcessor && !pfnProcessor(Image, Comment))
		{
			log_error_color(SCREENSHOT_LOG_COLOR, "client", "Failed to process screenshot '%s'", pName);
			return false;
		}
		char aWholePath[IO_MAX_PATH_LENGTH] = {};
		// io_open 不会代建目录：自定义截图目录（qm_screenshot_directory）或第一次
		// 保存到新位置时，整条保存会直接失败。先按最终路径把上级目录补出来，
		// 存档相对路径与绝对路径都适用。
		{
			char aTargetPath[IO_MAX_PATH_LENGTH];
			pStorage->GetCompletePath(IStorage::TYPE_SAVE_OR_ABSOLUTE, pName, aTargetPath, sizeof(aTargetPath));
			fs_makedir_rec_for(aTargetPath);
		}
		// 无扩展名信息时默认 png；WebP 走无损编码，避免合影出现色带。
		IOHANDLE File = pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE, aWholePath, sizeof(aWholePath));
		const bool Saved = str_endswith_nocase(pName, ".webp") != nullptr ?
					   CImageLoader::SaveWebP(File, pName, Image) :
					   CImageLoader::SavePng(File, pName, Image, Comment.c_str());
		if(!Saved)
		{
			log_error_color(SCREENSHOT_LOG_COLOR, "client", "Failed to save screenshot to '%s'", aWholePath);
			return false;
		}
		log_info_color(SCREENSHOT_LOG_COLOR, "client", "Saved screenshot to '%s'", aWholePath);
		if(pOutWholePath != nullptr && OutWholePathSize > 0)
			str_copy(pOutWholePath, aWholePath, OutWholePathSize);
		return true;
	}

	class CSaveJob : public IJob
	{
		IStorage *m_pStorage;
		std::function<void()> m_pfnFailure;
		char m_aName[IO_MAX_PATH_LENGTH];
		COwnedImage m_Image;
		IGraphics::FScreenshotProcessor m_pfnProcessor;
		IGraphics::FScreenshotCallback m_pfnCompletion;

		void ReportFailure() noexcept
		{
			try
			{
				if(m_pfnFailure)
					m_pfnFailure();
			}
			catch(...)
			{
				log_error("client", "Could not report screenshot failure for '%s'", m_aName);
			}
		}

	protected:
		void Run() override
		{
			static constexpr LOG_COLOR SCREENSHOT_LOG_COLOR = LOG_COLOR{255, 153, 76};
			try
			{
				char aWholePath[IO_MAX_PATH_LENGTH] = {};
				if(Save(m_pStorage, m_aName, m_Image.Image(), m_pfnProcessor, aWholePath, sizeof(aWholePath)))
				{
					if(m_pfnCompletion)
						m_pfnCompletion(std::move(m_Image.Image()), aWholePath);
					return;
				}
			}
			catch(...)
			{
				log_error_color(SCREENSHOT_LOG_COLOR, "client", "Exception while saving screenshot '%s'", m_aName);
			}
			ReportFailure();
		}

	public:
		// Storage与失败回调的owner须活到任务结束；客户端先关任务池再释放这些接口。
		CSaveJob(IStorage *pStorage, const char *pName, CImageInfo &&Image, IGraphics::FScreenshotProcessor pfnProcessor, std::function<void()> pfnFailure, IGraphics::FScreenshotCallback pfnCompletion = nullptr) :
			m_pStorage(pStorage), m_pfnFailure(std::move(pfnFailure)), m_Image(std::move(Image)), m_pfnProcessor(std::move(pfnProcessor)), m_pfnCompletion(std::move(pfnCompletion))
		{
			str_copy(m_aName, pName);
		}
	};

	// 仅在GPU读回完成后接管图像；预览失败不阻止保存，提交失败不重放已有副作用。
	template<typename TCreateJob, typename TSubmit, typename TReportFailure>
	bool Finish(CImageInfo &Image, IGraphics::FScreenshotCallback &Preview, IGraphics::FScreenshotProcessor &Processor, TCreateJob &&CreateJob, TSubmit &&Submit, TReportFailure &&ReportFailure, const char *pPreviewPath = "") noexcept
	{
		COwnedImage Captured(std::move(Image));
		auto PreviewCallback = std::move(Preview);
		Preview = nullptr;
		auto SaveProcessor = std::move(Processor);
		Processor = nullptr;
		if(PreviewCallback)
		{
			try
			{
				COwnedImage Copy(Captured.Image().DeepCopy());
				if(Copy.Image().m_pData != nullptr)
					PreviewCallback(std::move(Copy.Image()), pPreviewPath);
			}
			catch(...)
			{
				log_warn("graphics", "Could not show screenshot preview; saving continues");
			}
		}
		try
		{
			auto pJob = CreateJob(std::move(Captured.Image()), std::move(SaveProcessor));
			if(pJob && Submit(pJob))
				return true;
		}
		catch(...)
		{
			log_error("graphics", "Could not submit screenshot save job");
		}
		try
		{
			ReportFailure();
		}
		catch(...)
		{
			log_error("graphics", "Could not report screenshot submission failure");
		}
		return false;
	}
	// 任务只排队；图形 owner 的 Swap 在渲染线程领取，释放 owner 后不再调用 UI。
	class CCompletionQueue
	{
		struct SCompletion
		{
			COwnedImage m_Image;
			char m_aPath[IO_MAX_PATH_LENGTH] = {};
			IGraphics::FScreenshotCallback m_Callback;
			SCompletion(CImageInfo &&Image, const char *pPath, IGraphics::FScreenshotCallback Callback) :
				m_Image(std::move(Image)), m_Callback(std::move(Callback)) { str_copy(m_aPath, pPath); }
		};
		std::mutex m_Mutex;
		std::vector<SCompletion> m_vPending;

	public:
		void Enqueue(CImageInfo &&Image, const char *pPath, IGraphics::FScreenshotCallback Callback)
		{
			SCompletion Completion(std::move(Image), pPath, std::move(Callback));
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_vPending.push_back(std::move(Completion));
		}
		void Flush()
		{
			std::vector<SCompletion> vReady;
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				vReady.swap(m_vPending);
			}
			for(auto &Completion : vReady)
			{
				try
				{
					if(Completion.m_Callback)
						Completion.m_Callback(std::move(Completion.m_Image.Image()), Completion.m_aPath);
				}
				catch(...)
				{
					log_warn("graphics", "Could not complete screenshot preview");
				}
			}
		}
	};

	inline bool SaveAndNotify(IStorage *pStorage, const char *pName, CImageInfo &&Image,
		const IGraphics::FScreenshotProcessor &Processor, const IGraphics::FScreenshotCallback &Callback)
	{
		COwnedImage Owned(std::move(Image));
		char aWholePath[IO_MAX_PATH_LENGTH] = {};
		if(!Save(pStorage, pName, Owned.Image(), Processor, aWholePath, sizeof(aWholePath)))
			return false;
		if(Callback)
			Callback(std::move(Owned.Image()), aWholePath);
		return true;
	}
}

#endif
