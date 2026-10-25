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
#include <string>
#include <utility>

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
		CImageInfo &Image() { return m_Image; }
	};

	class CSaveJob : public IJob
	{
		IStorage *m_pStorage;
		std::function<void()> m_pfnFailure;
		char m_aName[IO_MAX_PATH_LENGTH];
		COwnedImage m_Image;
		IGraphics::FScreenshotProcessor m_pfnProcessor;

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
				std::string Comment;
				if(m_pfnProcessor && !m_pfnProcessor(m_Image.Image(), Comment))
				{
					log_error_color(SCREENSHOT_LOG_COLOR, "client", "Failed to process screenshot '%s'", m_aName);
					ReportFailure();
					return;
				}
				char aWholePath[IO_MAX_PATH_LENGTH] = {};
				if(CImageLoader::SavePng(m_pStorage->OpenFile(m_aName, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE, aWholePath, sizeof(aWholePath)), m_aName, m_Image.Image(), Comment.c_str()))
				{
					log_info_color(SCREENSHOT_LOG_COLOR, "client", "Saved screenshot to '%s'", aWholePath);
					return;
				}
				log_error_color(SCREENSHOT_LOG_COLOR, "client", "Failed to save screenshot to '%s'", aWholePath);
			}
			catch(...)
			{
				log_error_color(SCREENSHOT_LOG_COLOR, "client", "Exception while saving screenshot '%s'", m_aName);
			}
			ReportFailure();
		}

	public:
		// Storage与失败回调的owner须活到任务结束；客户端先关任务池再释放这些接口。
		CSaveJob(IStorage *pStorage, const char *pName, CImageInfo &&Image, IGraphics::FScreenshotProcessor pfnProcessor, std::function<void()> pfnFailure) :
			m_pStorage(pStorage), m_pfnFailure(std::move(pfnFailure)), m_Image(std::move(Image)), m_pfnProcessor(std::move(pfnProcessor))
		{
			str_copy(m_aName, pName);
		}
	};

	// 仅在GPU读回完成后接管图像；预览失败不阻止保存，提交失败不重放已有副作用。
	template<typename TCreateJob, typename TSubmit, typename TReportFailure>
	bool Finish(CImageInfo &Image, IGraphics::FScreenshotCallback &Preview, IGraphics::FScreenshotProcessor &Processor, TCreateJob &&CreateJob, TSubmit &&Submit, TReportFailure &&ReportFailure) noexcept
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
					PreviewCallback(std::move(Copy.Image()));
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
}

#endif
