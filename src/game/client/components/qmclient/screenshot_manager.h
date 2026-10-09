#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_MANAGER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_MANAGER_H

#include <base/types.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/screenshot_image_job.h>

#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class IEngine;
class CGpuUploadLimiter;
class CQmScreenshotWatermarkJob;

// 取消与最终替换串行：取消先发生时保留旧文件，替换已提交时不撤回成功结果。
class CQmScreenshotExportControl
{
	mutable std::mutex m_Mutex;
	bool m_Canceled = false;

public:
	void Cancel();
	bool Canceled() const;
	bool Commit(const std::string &TempPath, const std::string &TargetPath);
};

// 截图页只负责选择文件，水印处理放在这个独立模块中，便于其它卡片复用。
class CQmScreenshotManager
{
	friend class CQmScreenshotWatermarkJob;
public:
	enum class EWatermarkPosition
	{
		BOTTOM_LEFT,
		BOTTOM_RIGHT,
		TOP_LEFT,
		TOP_RIGHT,
	};

	struct SEntry
	{
		std::string m_RelativePath;
		time_t m_Modified = 0;
		int m_StorageType = IStorage::TYPE_SAVE;
	};

	struct SWatermarkOptions
	{
		bool m_ShowTimestamp = true;
		bool m_ShowMapName = true;
		std::string m_CustomText;
		EWatermarkPosition m_Position = EWatermarkPosition::BOTTOM_LEFT;
	};

	// 预览和导出共用同一套片段拼接规则，避免两条路径显示不同的水印内容。
	static std::string ComposeWatermarkText(const char *pTimestamp, const char *pMapName, const std::string &CustomText)
	{
		std::string Text;
		const auto AppendPart = [&Text](const char *pPart) {
			if(pPart == nullptr || pPart[0] == '\0')
				return;
			if(!Text.empty())
				Text += " | ";
			Text += pPart;
		};
		AppendPart(pTimestamp);
		if(pMapName != nullptr && pMapName[0] != '\0')
		{
			const std::string MapPart = std::string("MAP: ") + pMapName;
			AppendPart(MapPart.c_str());
		}
		AppendPart(CustomText.c_str());
		return Text;
	}

	// 缩略图条目：纹理就绪前 m_Loading 为真；加载失败是终态，不再重试同一路径。
	struct SThumbnail
	{
		IGraphics::CTextureHandle m_Texture;
		int m_Width = 0;
		int m_Height = 0;
		bool m_Loading = false;
		bool m_LoadFailed = false;
	};

	void Refresh(IStorage *pStorage, const char *pFolder = "screenshots", int StorageType = IStorage::TYPE_ALL);
	const std::vector<SEntry> &Entries() const { return m_vEntries; }

	// 只查缓存并按可见性排队，不在渲染线程做任何磁盘或解码工作。
	// 返回的条目在 m_Loading 时纹理尚不可用，调用方保留占位底色即可。
	// 指针在下一次 LoadThumbnail 插入或 PumpThumbnails 淘汰之前有效，只可当帧使用。
	const SThumbnail *LoadThumbnail(const char *pPath, int StorageType);
	// 每帧渲染完可见项后调用一次：回收后台结果、按 GPU 上传预算上传纹理并淘汰超限条目。
	void PumpThumbnails(IGraphics *pGraphics, IStorage *pStorage, IEngine *pEngine, CGpuUploadLimiter *pLimiter);
	void ClearThumbnails(IGraphics *pGraphics);

	// 请求时保存时间、地图及配置的值快照；后台处理不访问菜单或当前连接状态。
	static SWatermarkOptions CurrentWatermarkOptions();
	static IGraphics::FScreenshotProcessor CaptureProcessor(IStorage *pStorage, bool Watermark, const SWatermarkOptions &Options, const char *pMapName, time_t Timestamp);

	std::string BuildWatermarkText(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const SWatermarkOptions &Options) const;

	// 从指定存储路径读取图片，合成水印后写入用户保存目录。源文件不会被覆盖。
	bool ApplyWatermark(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const char *pTargetPath, const SWatermarkOptions &Options) const;
	std::shared_ptr<CQmScreenshotWatermarkJob> CreateWatermarkJob(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const char *pTargetPath, const SWatermarkOptions &Options) const;
	static bool SavePngAtomically(const std::string &TargetPath, const CImageInfo &Image, const char *pComment, CQmScreenshotExportControl *pControl = nullptr);

private:
	struct SCaptureMetadata
	{
		std::string m_Timestamp;
		std::string m_MapName;
		std::string m_Comment;
	};
	const SCaptureMetadata &CaptureMetadata(IStorage *pStorage, const char *pPath, int StorageType) const;
	static bool DrawWatermark(IStorage *pStorage, CImageInfo &Image, const std::string &Text, EWatermarkPosition Position, const std::vector<std::string> &vFontPaths = {});

	struct SScanContext
	{
		std::vector<SEntry> *m_pEntries;
		std::string m_Folder;
	};

	struct SThumbnailEntry
	{
		SThumbnail m_Thumbnail;
		std::shared_ptr<CQmScreenshotImageJob> m_pJob;
		std::string m_Path;
		int m_StorageType = IStorage::TYPE_SAVE;
		uint64_t m_LastUsedFrame = 0;
		bool m_Requested = false;
	};

	static int ScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);
	static bool LoadImage(IStorage *pStorage, const char *pPath, int StorageType, CImageInfo &Image);
	static bool EnsureRgba(CImageInfo &Image);
	static void BlendPixel(CImageInfo &Image, int X, int Y, ColorRGBA Color);
	static void DrawText(CImageInfo &Image, const std::string &Text, int X, int Y, int Scale, ColorRGBA Color);
	static int TextWidth(const std::string &Text, int Scale);
	static std::string ThumbnailKey(const char *pPath, int StorageType);

	std::vector<SEntry> m_vEntries;
	mutable std::unordered_map<std::string, SCaptureMetadata> m_CaptureMetadata;
	std::unordered_map<std::string, SThumbnailEntry> m_vThumbnails;
	// 本帧按可见顺序排队的缩略图键：只在当前可见项上启动后台任务，滚动过快不会堆积陈旧请求。
	std::vector<std::string> m_vThumbnailRequests;
	uint64_t m_ThumbnailFrame = 0;
};

// 后台任务只持有路径和内容快照，不借用菜单、配置或其 Storage。
class CQmScreenshotWatermarkJob : public IJob
{
	std::string m_SourcePath;
	std::string m_TargetPath;
	std::string m_Text;
	std::string m_Comment;
	std::vector<std::string> m_vFontPaths;
	CQmScreenshotManager::EWatermarkPosition m_Position;
	CQmScreenshotExportControl m_Control;
	bool m_Saved = false;
	void Run() override;

public:
	CQmScreenshotWatermarkJob(std::string SourcePath, std::string TargetPath, std::string Text, std::string Comment, std::vector<std::string> vFontPaths, CQmScreenshotManager::EWatermarkPosition Position);
	void Cancel() { m_Control.Cancel(); }
	bool Canceled() const { return m_Control.Canceled(); }
	bool Saved() const { return State() == STATE_DONE && m_Saved; }
};

#endif
