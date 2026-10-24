#ifndef ENGINE_CLIENT_SCREENSHOT_SAVE_H
#define ENGINE_CLIENT_SCREENSHOT_SAVE_H

// 截图落盘：后处理（水印/元数据）+ 按扩展名选编码器 + 写盘。
//
// 单独抽出来是因为它有两个调用方：普通截图的后台保存任务，以及合影超采样静帧的
// 同步保存。两条路径必须共用同一套规则——尤其是「回调拿到的路径」必须就是
// 真正写盘的那个文件：游戏内照片卡的「查看截图」会把该路径交给看图子进程，
// 名字对不上或文件还没写完就会报「无法打开截图文件」。

#include <base/log.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/storage.h>

namespace screenshot_save
{
	// pName 是截图目标名（相对存档目录或绝对路径，通常已由 media_paths 解析）。
	// 成功时把真正写盘的完整路径写入 pOutWholePath，供调用方交给界面/子进程。
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
		char aWholePath[IO_MAX_PATH_LENGTH];
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

	// 保存成功之后才触发回调，回调拿到的就是写盘的完整路径。
	// 这个顺序是界面契约的一部分：照片卡的「查看截图」会立刻把该路径交给看图子进程，
	// 先回调后落盘（或回传请求名）都会让子进程打开失败。
	inline bool SaveAndNotify(IStorage *pStorage, const char *pName, CImageInfo &&Image,
		const IGraphics::FScreenshotProcessor &pfnProcessor, const IGraphics::FScreenshotCallback &pfnCallback)
	{
		char aWholePath[IO_MAX_PATH_LENGTH];
		if(!Save(pStorage, pName, Image, pfnProcessor, aWholePath, sizeof(aWholePath)))
			return false;
		if(pfnCallback)
			pfnCallback(std::move(Image), aWholePath);
		return true;
	}
}

#endif
