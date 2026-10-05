#include "screenshot_manager.h"

#include <base/str.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/client/gpu_upload_limiter.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
	void BlendImagePixel(CImageInfo &Image, int X, int Y, ColorRGBA Color)
	{
		if(X < 0 || Y < 0 || X >= (int)Image.m_Width || Y >= (int)Image.m_Height)
			return;
		const ColorRGBA Base = Image.PixelColor((size_t)X, (size_t)Y);
		const float Alpha = std::clamp(Color.a, 0.0f, 1.0f);
		Image.SetPixelColor((size_t)X, (size_t)Y, ColorRGBA(Base.r * (1.0f - Alpha) + Color.r * Alpha, Base.g * (1.0f - Alpha) + Color.g * Alpha, Base.b * (1.0f - Alpha) + Color.b * Alpha, Base.a * (1.0f - Alpha) + Color.a * Alpha));
	}

	// 水印导出需要把用户输入的 UTF-8 文字栅格化到图片中。字体数据只在一次导出期间持有，
	// 不把字体资源和截图缩略图缓存绑定在一起。
	struct SWatermarkFont
	{
		FT_Library m_Library = nullptr;
		FT_Face m_Face = nullptr;
		std::vector<unsigned char> m_vData;

		~SWatermarkFont()
		{
			if(m_Face != nullptr)
				FT_Done_Face(m_Face);
			if(m_Library != nullptr)
				FT_Done_FreeType(m_Library);
		}

		bool Load(IStorage *pStorage)
		{
			if(pStorage == nullptr || FT_Init_FreeType(&m_Library) != 0)
				return false;

			// NotoSansSC-VF 已随字体减重移除：水印中文改用随包霞鹜新晰黑，
			// DejaVu 仍作拉丁兜底。
			for(const char *pPath : {"fonts/霞鹜新晰黑.ttf", "fonts/DejaVuSans.ttf"})
			{
				IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL);
				if(File == nullptr)
					continue;
				void *pData = nullptr;
				unsigned DataSize = 0;
				const bool ReadOk = io_read_all(File, &pData, &DataSize);
				io_close(File);
				if(!ReadOk || pData == nullptr || DataSize == 0 || (uint64_t)DataSize > (uint64_t)std::numeric_limits<FT_Long>::max())
				{
					free(pData);
					continue;
				}

				m_vData.assign(static_cast<unsigned char *>(pData), static_cast<unsigned char *>(pData) + DataSize);
				free(pData);
				if(FT_New_Memory_Face(m_Library, m_vData.data(), static_cast<FT_Long>(m_vData.size()), 0, &m_Face) == 0)
					return true;
				m_vData.clear();
			}

			return false;
		}

		bool SetPixelSize(int PixelSize)
		{
			return m_Face != nullptr && PixelSize > 0 && FT_Set_Pixel_Sizes(m_Face, 0, static_cast<FT_UInt>(PixelSize)) == 0;
		}

		bool LoadGlyph(int Codepoint)
		{
			if(m_Face == nullptr || FT_Load_Char(m_Face, static_cast<FT_ULong>(Codepoint), FT_LOAD_RENDER | FT_LOAD_NO_BITMAP) == 0)
				return m_Face != nullptr;
			if(Codepoint == '?')
				return false;
			return FT_Load_Char(m_Face, static_cast<FT_ULong>('?'), FT_LOAD_RENDER | FT_LOAD_NO_BITMAP) == 0;
		}

		int TextWidth(const std::string &Text) const
		{
			if(m_Face == nullptr)
				return 0;
			int Width = 0;
			const char *pCursor = Text.c_str();
			while(*pCursor != '\0')
			{
				const int Codepoint = str_utf8_decode(&pCursor);
				if(Codepoint <= 0)
					break;
				if(!const_cast<SWatermarkFont *>(this)->LoadGlyph(Codepoint))
					continue;
				Width += std::max(0, static_cast<int>(m_Face->glyph->advance.x >> 6));
			}
			return Width;
		}

		bool Draw(CImageInfo &Image, const std::string &Text, int X, int BaselineY, ColorRGBA Color)
		{
			if(m_Face == nullptr)
				return false;
			const int StartX = X;
			const char *pCursor = Text.c_str();
			while(*pCursor != '\0')
			{
				const int Codepoint = str_utf8_decode(&pCursor);
				if(Codepoint <= 0)
					break;
				if(!LoadGlyph(Codepoint))
					continue;

				const FT_Bitmap &Bitmap = m_Face->glyph->bitmap;
				const int Pitch = Bitmap.pitch;
				for(unsigned Row = 0; Row < Bitmap.rows; ++Row)
				{
					const unsigned SourceRow = Pitch >= 0 ? Row : Bitmap.rows - 1 - Row;
					const uint8_t *pSource = Bitmap.buffer + SourceRow * std::abs(Pitch);
					for(unsigned Column = 0; Column < Bitmap.width; ++Column)
					{
						const float Alpha = pSource[Column] / 255.0f;
						if(Alpha <= 0.0f)
							continue;
						BlendImagePixel(Image, X + m_Face->glyph->bitmap_left + static_cast<int>(Column), BaselineY - m_Face->glyph->bitmap_top + static_cast<int>(Row), ColorRGBA(Color.r, Color.g, Color.b, Color.a * Alpha));
					}
				}
				X += std::max(0, static_cast<int>(m_Face->glyph->advance.x >> 6));
			}
			return X >= StartX;
		}
	};

	// 找不到随包字体时保留轻量 5x7 回退字模，确保旧资源环境仍能导出时间和基础拉丁文本。
	static const uint8_t s_aDigits[10][7] = {
		{0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
		{0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
		{0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
		{0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
		{0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
		{0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
		{0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
		{0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
		{0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
		{0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
	};

	static const uint8_t s_aLetters[26][7] = {
		{0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
		{0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
		{0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
		{0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
		{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
		{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
		{0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
		{0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
		{0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
		{0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e},
		{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
		{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
		{0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
		{0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11},
		{0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
		{0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
		{0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
		{0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
		{0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
		{0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
		{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
		{0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
		{0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11},
		{0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
		{0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
		{0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
	};

	static const uint8_t s_aPunctuation[7][7] = {
		{0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}, // :
		{0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}, // -
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}, // .
		{0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}, // /
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
		{0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // |
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}, // _
	};

	const uint8_t *Glyph(char Character)
	{
		if(Character >= '0' && Character <= '9')
			return s_aDigits[Character - '0'];
		if(Character >= 'A' && Character <= 'Z')
			return s_aLetters[Character - 'A'];
		switch(Character)
		{
		case ':': return s_aPunctuation[0];
		case '-': return s_aPunctuation[1];
		case '.': return s_aPunctuation[2];
		case '/': return s_aPunctuation[3];
		case ' ': return s_aPunctuation[4];
		case '|': return s_aPunctuation[5];
		case '_': return s_aPunctuation[6];
		default: return s_aPunctuation[4];
		}
	}
}

void CQmScreenshotManager::Refresh(IStorage *pStorage, const char *pFolder, int StorageType)
{
	m_vEntries.clear();
	if(pStorage == nullptr || pFolder == nullptr || pFolder[0] == '\0')
		return;
	SScanContext Context{&m_vEntries, pFolder};
	pStorage->ListDirectoryInfo(StorageType, pFolder, ScanCallback, &Context);
	std::sort(m_vEntries.begin(), m_vEntries.end(), [](const SEntry &Left, const SEntry &Right) {
		if(Left.m_Modified != Right.m_Modified)
			return Left.m_Modified > Right.m_Modified;
		return Left.m_RelativePath < Right.m_RelativePath;
	});
}

std::string CQmScreenshotManager::ThumbnailKey(const char *pPath, int StorageType)
{
	return std::to_string(StorageType) + ":" + (pPath != nullptr ? pPath : "");
}

const CQmScreenshotManager::SThumbnail *CQmScreenshotManager::LoadThumbnail(const char *pPath, int StorageType)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return nullptr;

	const std::string Key = ThumbnailKey(pPath, StorageType);
	auto It = m_vThumbnails.find(Key);
	if(It == m_vThumbnails.end())
	{
		SThumbnailEntry Entry;
		Entry.m_Path = pPath;
		Entry.m_StorageType = StorageType;
		Entry.m_Thumbnail.m_Loading = true;
		Entry.m_LastUsedFrame = m_ThumbnailFrame;
		It = m_vThumbnails.emplace(Key, std::move(Entry)).first;
	}

	SThumbnailEntry &Entry = It->second;
	if(!Entry.m_Requested)
	{
		Entry.m_Requested = true;
		m_vThumbnailRequests.push_back(Key);
	}
	Entry.m_LastUsedFrame = m_ThumbnailFrame;
	return &Entry.m_Thumbnail;
}

void CQmScreenshotManager::PumpThumbnails(IGraphics *pGraphics, IStorage *pStorage, IEngine *pEngine, CGpuUploadLimiter *pLimiter)
{
	// 请求表记录的是「本帧可见」的条目：先回收上一帧的结果，再按需补足后台任务。
	if(pGraphics != nullptr)
	{
		int UploadsLeft = QM_SCREENSHOT_THUMBNAIL_MAX_UPLOADS_PER_FRAME;
		for(auto &[Key, Entry] : m_vThumbnails)
		{
			(void)Key;
			if(Entry.m_pJob == nullptr || Entry.m_pJob->State() != IJob::STATE_DONE)
				continue;
			if(UploadsLeft <= 0 || (pLimiter != nullptr && !pLimiter->CanUpload()))
				continue;

			CImageInfo *pImage = Entry.m_pJob->Image();
			if(pImage != nullptr && pImage->m_Width > 0 && pImage->m_Height > 0)
			{
				Entry.m_Thumbnail.m_Width = (int)pImage->m_Width;
				Entry.m_Thumbnail.m_Height = (int)pImage->m_Height;
				Entry.m_Thumbnail.m_Texture = pGraphics->LoadTextureRawMove(*pImage, 0, Entry.m_Path.c_str());
				Entry.m_Thumbnail.m_LoadFailed = !Entry.m_Thumbnail.m_Texture.IsValid();
				if(pLimiter != nullptr)
					pLimiter->OnUploaded();
				--UploadsLeft;
			}
			else
			{
				Entry.m_Thumbnail.m_LoadFailed = true;
			}
			Entry.m_Thumbnail.m_Loading = false;
			Entry.m_pJob.reset();
		}
	}

	int RunningJobs = 0;
	for(const auto &[Key, Entry] : m_vThumbnails)
	{
		(void)Key;
		// 已完成但还没上传的条目只占内存，不占用后台解码槽位。
		if(Entry.m_pJob != nullptr && Entry.m_pJob->State() != IJob::STATE_DONE)
			++RunningJobs;
	}

	int PendingRequests = 0;
	for(const std::string &Key : m_vThumbnailRequests)
	{
		const auto It = m_vThumbnails.find(Key);
		if(It != m_vThumbnails.end() && It->second.m_pJob == nullptr && It->second.m_Thumbnail.m_Loading)
			++PendingRequests;
	}

	if(pEngine != nullptr && pStorage != nullptr)
	{
		int JobsToStart = QmScreenshotImageJobsToStart(RunningJobs, PendingRequests, QM_SCREENSHOT_THUMBNAIL_MAX_CONCURRENT_JOBS);
		for(const std::string &Key : m_vThumbnailRequests)
		{
			if(JobsToStart <= 0)
				break;
			const auto It = m_vThumbnails.find(Key);
			if(It == m_vThumbnails.end())
				continue;

			SThumbnailEntry &Entry = It->second;
			if(Entry.m_pJob != nullptr || !Entry.m_Thumbnail.m_Loading)
				continue;

			Entry.m_pJob = std::make_shared<CQmScreenshotImageJob>(pStorage, Entry.m_Path, Entry.m_StorageType, QM_SCREENSHOT_THUMBNAIL_MAX_EDGE);
			pEngine->AddJob(Entry.m_pJob);
			--JobsToStart;
		}
	}

	// 常驻纹理按最久未使用淘汰；本帧可见和仍在加载的条目保留，避免滚动时反复重解码。
	if(pGraphics != nullptr)
	{
		int ResidentEntries = 0;
		for(const auto &[Key, Entry] : m_vThumbnails)
		{
			(void)Key;
			if(Entry.m_pJob == nullptr)
				++ResidentEntries;
		}

		const int EvictionsNeeded = QmScreenshotImageEvictionCount(ResidentEntries, QM_SCREENSHOT_THUMBNAIL_MAX_RESIDENT);
		if(EvictionsNeeded > 0)
		{
			std::vector<std::string> vEvictable;
			vEvictable.reserve(m_vThumbnails.size());
			for(const auto &[Key, Entry] : m_vThumbnails)
				if(Entry.m_pJob == nullptr && !Entry.m_Requested)
					vEvictable.push_back(Key);
			std::sort(vEvictable.begin(), vEvictable.end(), [this](const std::string &Left, const std::string &Right) {
				return m_vThumbnails.at(Left).m_LastUsedFrame < m_vThumbnails.at(Right).m_LastUsedFrame;
			});

			const int EvictCount = std::min(EvictionsNeeded, (int)vEvictable.size());
			for(int Index = 0; Index < EvictCount; ++Index)
			{
				const auto It = m_vThumbnails.find(vEvictable[Index]);
				if(It == m_vThumbnails.end())
					continue;
				if(It->second.m_Thumbnail.m_Texture.IsValid())
					pGraphics->UnloadTexture(&It->second.m_Thumbnail.m_Texture);
				m_vThumbnails.erase(It);
			}
		}
	}

	for(const std::string &Key : m_vThumbnailRequests)
	{
		const auto It = m_vThumbnails.find(Key);
		if(It != m_vThumbnails.end())
			It->second.m_Requested = false;
	}
	m_vThumbnailRequests.clear();
	++m_ThumbnailFrame;
}

void CQmScreenshotManager::ClearThumbnails(IGraphics *pGraphics)
{
	if(pGraphics != nullptr)
	{
		for(auto &[Key, Entry] : m_vThumbnails)
		{
			(void)Key;
			if(Entry.m_Thumbnail.m_Texture.IsValid())
				pGraphics->UnloadTexture(&Entry.m_Thumbnail.m_Texture);
		}
	}
	// 正在运行的任务不再持有结果引用，解码结束后随任务对象自行释放。
	m_vThumbnails.clear();
	m_vThumbnailRequests.clear();
}

int CQmScreenshotManager::ScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	(void)StorageType;
	if(pInfo == nullptr || IsDir || pUser == nullptr ||
		(str_endswith_nocase(pInfo->m_pName, ".png") == nullptr && str_endswith_nocase(pInfo->m_pName, ".webp") == nullptr))
		return 0;
	SScanContext *pContext = static_cast<SScanContext *>(pUser);
	SEntry Entry;
	Entry.m_RelativePath = pContext->m_Folder + "/" + pInfo->m_pName;
	Entry.m_Modified = pInfo->m_TimeModified;
	Entry.m_StorageType = StorageType;
	pContext->m_pEntries->push_back(std::move(Entry));
	return 0;
}

bool CQmScreenshotManager::LoadImage(IStorage *pStorage, const char *pPath, int StorageType, CImageInfo &Image)
{
	if(pStorage == nullptr || pPath == nullptr)
		return false;
	if(str_endswith_nocase(pPath, ".png") != nullptr)
	{
		int PngliteIncompatible = 0;
		return CImageLoader::LoadPng(pStorage->OpenFile(pPath, IOFLAG_READ, StorageType), pPath, Image, PngliteIncompatible);
	}
	if(str_endswith_nocase(pPath, ".webp") != nullptr)
		return CImageLoader::LoadWebP(pStorage->OpenFile(pPath, IOFLAG_READ, StorageType), pPath, Image);
	return false;
}

bool CQmScreenshotManager::EnsureRgba(CImageInfo &Image)
{
	if(Image.m_Format == CImageInfo::FORMAT_RGBA)
		return true;
	if(Image.m_Format != CImageInfo::FORMAT_RGB || Image.m_Width == 0 || Image.m_Height == 0)
		return false;

	CImageInfo Rgba;
	Rgba.m_Width = Image.m_Width;
	Rgba.m_Height = Image.m_Height;
	Rgba.m_Format = CImageInfo::FORMAT_RGBA;
	size_t RgbaDataSize = 0;
	if(!Rgba.DataSize(RgbaDataSize))
		return false;
	Rgba.m_pData = static_cast<uint8_t *>(std::malloc(RgbaDataSize));
	if(Rgba.m_pData == nullptr)
		return false;
	for(size_t Y = 0; Y < Image.m_Height; ++Y)
		for(size_t X = 0; X < Image.m_Width; ++X)
			Rgba.SetPixelColor(X, Y, Image.PixelColor(X, Y));
	Image.Free();
	Image = std::move(Rgba);
	return true;
}

void CQmScreenshotManager::BlendPixel(CImageInfo &Image, int X, int Y, ColorRGBA Color)
{
	if(X < 0 || Y < 0 || X >= (int)Image.m_Width || Y >= (int)Image.m_Height)
		return;
	const ColorRGBA Base = Image.PixelColor((size_t)X, (size_t)Y);
	const float Alpha = std::clamp(Color.a, 0.0f, 1.0f);
	Image.SetPixelColor((size_t)X, (size_t)Y, ColorRGBA(Base.r * (1.0f - Alpha) + Color.r * Alpha, Base.g * (1.0f - Alpha) + Color.g * Alpha, Base.b * (1.0f - Alpha) + Color.b * Alpha, Base.a * (1.0f - Alpha) + Color.a * Alpha));
}

int CQmScreenshotManager::TextWidth(const std::string &Text, int Scale)
{
	return (int)Text.size() * 6 * Scale;
}

void CQmScreenshotManager::DrawText(CImageInfo &Image, const std::string &Text, int X, int Y, int Scale, ColorRGBA Color)
{
	for(char RawCharacter : Text)
	{
		const char Character = (char)std::toupper((unsigned char)RawCharacter);
		const uint8_t *pGlyph = Glyph(Character);
		for(int Row = 0; Row < 7; ++Row)
			for(int Column = 0; Column < 5; ++Column)
				if(pGlyph[Row] & (1 << (4 - Column)))
					for(int Dy = 0; Dy < Scale; ++Dy)
						for(int Dx = 0; Dx < Scale; ++Dx)
							BlendPixel(Image, X + Column * Scale + Dx, Y + Row * Scale + Dy, Color);
		X += 6 * Scale;
	}
}

std::string CQmScreenshotManager::BuildWatermarkText(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const SWatermarkOptions &Options, const char *pMapName) const
{
	char aTimestamp[64] = "";
	const char *pTimestamp = nullptr;
	if(Options.m_ShowTimestamp)
	{
		time_t Timestamp = std::time(nullptr);
		time_t Created = 0;
		time_t Modified = 0;
		if(pStorage != nullptr && pSourcePath != nullptr && pSourcePath[0] != '\0' && pStorage->RetrieveTimes(pSourcePath, SourceStorageType, &Created, &Modified) && Modified > 0)
			Timestamp = Modified;
		str_timestamp_ex(Timestamp, aTimestamp, sizeof(aTimestamp), FORMAT_SPACE);
		pTimestamp = aTimestamp;
	}
	return ComposeWatermarkText(pTimestamp, Options.m_ShowMapName ? pMapName : nullptr, Options.m_CustomText);
}

bool CQmScreenshotManager::ApplyWatermark(IStorage *pStorage, const char *pSourcePath, int SourceStorageType, const char *pTargetPath, const SWatermarkOptions &Options, const char *pMapName) const
{
	if(pStorage == nullptr || pSourcePath == nullptr || pTargetPath == nullptr || pTargetPath[0] == '\0')
		return false;
	CImageInfo Image;
	if(!LoadImage(pStorage, pSourcePath, SourceStorageType, Image) || !EnsureRgba(Image))
	{
		Image.Free();
		return false;
	}

	const std::string Text = BuildWatermarkText(pStorage, pSourcePath, SourceStorageType, Options, pMapName);
	if(!Text.empty())
	{
		const int Scale = std::clamp((int)Image.m_Width / 640, 1, 4);
		const int FontSize = std::clamp((int)Image.m_Width / 50, 14, 32);
		SWatermarkFont Font;
		const bool UseFont = Font.Load(pStorage) && Font.SetPixelSize(FontSize);
		const int Margin = UseFont ? std::max(8, FontSize / 2) : 12 * Scale;
		const int TextW = UseFont ? Font.TextWidth(Text) : TextWidth(Text, Scale);
		const int TextH = UseFont ? FontSize : 7 * Scale;
		const int BandH = std::min((int)Image.m_Height, TextH + 2 * Margin);
		const bool Top = Options.m_Position == EWatermarkPosition::TOP_LEFT || Options.m_Position == EWatermarkPosition::TOP_RIGHT;
		const bool Right = Options.m_Position == EWatermarkPosition::BOTTOM_RIGHT || Options.m_Position == EWatermarkPosition::TOP_RIGHT;
		const int BandY = Top ? 0 : (int)Image.m_Height - BandH;
		for(int Y = BandY; Y < BandY + BandH; ++Y)
			for(int X = 0; X < (int)Image.m_Width; ++X)
				BlendPixel(Image, X, Y, ColorRGBA(0.0f, 0.0f, 0.0f, 0.62f));
		const int TextX = Right ? (int)Image.m_Width - TextW - Margin : Margin;
		if(UseFont)
			Font.Draw(Image, Text, std::max(Margin, TextX), BandY + Margin + FontSize, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		else
			DrawText(Image, Text, std::max(Margin, TextX), BandY + Margin, Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}

	const bool Saved = CImageLoader::SavePng(pStorage->OpenFile(pTargetPath, IOFLAG_WRITE, IStorage::TYPE_SAVE), pTargetPath, Image);
	Image.Free();
	return Saved;
}
