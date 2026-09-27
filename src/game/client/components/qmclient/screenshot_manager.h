#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_MANAGER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_MANAGER_H

#include <base/types.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/storage.h>

#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

// 截图页只负责选择文件，水印处理放在这个独立模块中，便于其它卡片复用。
class CQmScreenshotManager
{
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

	struct SThumbnail
	{
		IGraphics::CTextureHandle m_Texture;
		int m_Width = 0;
		int m_Height = 0;
		bool m_LoadFailed = false;
	};

	void Refresh(IStorage *pStorage, const char *pFolder = "screenshots", int StorageType = IStorage::TYPE_ALL);
	const std::vector<SEntry> &Entries() const { return m_vEntries; }
	const SThumbnail *LoadThumbnail(IStorage *pStorage, IGraphics *pGraphics, const char *pPath, int StorageType);
	void ClearThumbnails(IGraphics *pGraphics);
	std::string BuildWatermarkText(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const SWatermarkOptions &Options, const char *pMapName) const;

	// 从指定存储路径读取图片，合成水印后写入用户保存目录。源文件不会被覆盖。
	bool ApplyWatermark(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const char *pTargetPath, const SWatermarkOptions &Options, const char *pMapName) const;

private:
	struct SScanContext
	{
		std::vector<SEntry> *m_pEntries;
		std::string m_Folder;
	};

	static int ScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);
	static bool LoadImage(IStorage *pStorage, const char *pPath, int StorageType, CImageInfo &Image);
	static bool EnsureRgba(CImageInfo &Image);
	static void BlendPixel(CImageInfo &Image, int X, int Y, ColorRGBA Color);
	static void DrawText(CImageInfo &Image, const std::string &Text, int X, int Y, int Scale, ColorRGBA Color);
	static int TextWidth(const std::string &Text, int Scale);
	static std::string ThumbnailKey(const char *pPath, int StorageType);

	std::vector<SEntry> m_vEntries;
	std::unordered_map<std::string, SThumbnail> m_vThumbnails;
};

#endif
