// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmWikiMedia.h"

#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/image.h>
#include <engine/storage.h>

#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
	IStorage *s_pStorage = nullptr;
	IGraphics *s_pGraphics = nullptr;
	std::unordered_map<std::string, SWikiMedia> s_Media;

	// Wiki 图片都是说明用的短动图，给解码结果设硬上限：单张静态图、动图逐帧副本、拼好的图集
	// 各自不超过上限，超了直接当缺图处理，避免一张大图在绘制帧内吃掉上 GB 内存。
	constexpr size_t WIKI_MEDIA_MAX_STATIC_BYTES = 64u * 1024u * 1024u;
	constexpr size_t WIKI_MEDIA_MAX_FRAME_BYTES = 64u * 1024u * 1024u;
	constexpr size_t WIKI_MEDIA_MAX_ATLAS_BYTES = 96u * 1024u * 1024u;
	constexpr int WIKI_MEDIA_MAX_CANVAS = 2048;
	constexpr int WIKI_MEDIA_MAX_FRAMES = 48;

	bool EndsWithNoCase(const char *pText, const char *pSuffix)
	{
		const size_t TextLength = str_length(pText);
		const size_t SuffixLength = str_length(pSuffix);
		if(SuffixLength > TextLength)
			return false;
		return str_comp_nocase(pText + TextLength - SuffixLength, pSuffix) == 0;
	}

	bool BuildPath(const char *pRelative, char *pBuffer, int BufferSize)
	{
		if(pRelative == nullptr || pRelative[0] == '\0')
			return false;
		// 只允许 media 目录内的相对路径：拒绝绝对路径、盘符、反斜杠和 ..（导入的 Markdown 可能带这些写法）。
		const size_t Length = str_length(pRelative);
		if(pRelative[0] == '/' || pRelative[0] == '\\' || (Length > 1 && pRelative[1] == ':') ||
			str_find(pRelative, "..") != nullptr || str_find(pRelative, "\\") != nullptr)
			return false;
		str_format(pBuffer, BufferSize, "wiki/media/%s", pRelative);
		return true;
	}

	bool LoadImageTexture(const char *pRelative, SWikiMedia *pMedia)
	{
		char aFull[IO_MAX_PATH_LENGTH];
		if(!BuildPath(pRelative, aFull, sizeof(aFull)))
			return false;
		IOHANDLE File = s_pStorage->OpenFile(aFull, IOFLAG_READ, IStorage::TYPE_ALL);
		if(File == nullptr)
			return false;
		void *pData = nullptr;
		unsigned DataSize = 0;
		const bool ReadOk = io_read_all(File, &pData, &DataSize);
		io_close(File);
		if(!ReadOk || pData == nullptr)
			return false;

		CImageInfo Image;
		bool Decoded = false;
		// 显式传解码上限：默认是 SIZE_MAX，一张畸形大图会尝试几 GB 分配。
		if(EndsWithNoCase(pRelative, ".webp"))
			Decoded = CImageLoader::LoadWebP(pData, DataSize, pRelative, Image, WIKI_MEDIA_MAX_STATIC_BYTES);
		else
			Decoded = CImageLoader::LoadPng(pData, DataSize, pRelative, Image, WIKI_MEDIA_MAX_STATIC_BYTES);
		free(pData);
		if(!Decoded)
			return false;

		pMedia->m_Width = Image.m_Width;
		pMedia->m_Height = Image.m_Height;
		pMedia->m_Texture = s_pGraphics->LoadTextureRawMove(Image, 0);
		pMedia->m_Valid = pMedia->m_Width > 0 && pMedia->m_Height > 0;
		return pMedia->m_Valid;
	}

	// 元数据格式：cols=6 rows=4 frames=24 fps=12.5，空格或换行分隔。
	void ParseSheetMeta(const char *pText, SWikiMedia *pMedia)
	{
		const char *pCursor = pText;
		while(*pCursor != '\0')
		{
			while(*pCursor == ' ' || *pCursor == '\t' || *pCursor == '\r' || *pCursor == '\n')
				++pCursor;
			if(*pCursor == '\0')
				break;
			const char *pTokenEnd = pCursor;
			while(*pTokenEnd != '\0' && *pTokenEnd != ' ' && *pTokenEnd != '\t' && *pTokenEnd != '\r' && *pTokenEnd != '\n')
				++pTokenEnd;
			const std::string Token(pCursor, pTokenEnd - pCursor);
			const size_t Equals = Token.find('=');
			if(Equals != std::string::npos)
			{
				const std::string Key = Token.substr(0, Equals);
				const std::string Value = Token.substr(Equals + 1);
				// 手写/损坏的元数据也要夹在合理范围，否则行×列会溢出、帧号取模会出错。
				if(Key == "cols")
					pMedia->m_Cols = std::clamp(std::atoi(Value.c_str()), 1, 64);
				else if(Key == "rows")
					pMedia->m_Rows = std::clamp(std::atoi(Value.c_str()), 1, 64);
				else if(Key == "frames")
					pMedia->m_Frames = std::clamp(std::atoi(Value.c_str()), 1, 64 * 64);
				else if(Key == "fps")
					pMedia->m_Fps = std::clamp(static_cast<float>(std::atof(Value.c_str())), 0.0f, 60.0f);
			}
			pCursor = pTokenEnd;
		}
		pMedia->m_Cols = std::max(1, pMedia->m_Cols);
		pMedia->m_Rows = std::max(1, pMedia->m_Rows);
		pMedia->m_Frames = std::clamp(pMedia->m_Frames, 1, pMedia->m_Cols * pMedia->m_Rows);
	}

	bool LoadSheet(const char *pStem, SWikiMedia *pMedia)
	{
		char aImage[IO_MAX_PATH_LENGTH];
		char aMeta[IO_MAX_PATH_LENGTH];
		str_format(aImage, sizeof(aImage), "%s.sheet.png", pStem);
		str_format(aMeta, sizeof(aMeta), "%s.sheet.txt", pStem);
		if(!LoadImageTexture(aImage, pMedia))
			return false;

		char aFull[IO_MAX_PATH_LENGTH];
		if(BuildPath(aMeta, aFull, sizeof(aFull)))
		{
			IOHANDLE File = s_pStorage->OpenFile(aFull, IOFLAG_READ, IStorage::TYPE_ALL);
			if(File != nullptr)
			{
				char *pText = io_read_all_str(File);
				io_close(File);
				if(pText != nullptr)
				{
					ParseSheetMeta(pText, pMedia);
					free(pText);
				}
			}
		}
		// 没有元数据时按单帧静态图处理，至少不会画错。
		if(pMedia->m_Frames <= 1)
		{
			pMedia->m_Cols = 1;
			pMedia->m_Rows = 1;
			pMedia->m_Frames = 1;
			pMedia->m_Fps = 0.0f;
		}
		return true;
	}

	// ---- 动图 WebP ----
	// 客户端没有 libwebpdemux，但动图 WebP 就是 RIFF 容器里的多帧 VP8/VP8L，
	// 自己拆 ANMF 后逐帧交给已经链接的 libwebp 解码即可，不需要新增依赖。
	struct SWebPFrame
	{
		int m_X = 0;
		int m_Y = 0;
		int m_Width = 0;
		int m_Height = 0;
		int m_DurationMs = 0;
		bool m_Blend = true;
		bool m_DisposeBackground = false;
		const uint8_t *m_pData = nullptr;
		size_t m_Size = 0;
	};

	uint32_t ReadLE24(const uint8_t *pData)
	{
		return static_cast<uint32_t>(pData[0]) | (static_cast<uint32_t>(pData[1]) << 8) | (static_cast<uint32_t>(pData[2]) << 16);
	}

	uint32_t ReadLE32(const uint8_t *pData)
	{
		return static_cast<uint32_t>(pData[0]) | (static_cast<uint32_t>(pData[1]) << 8) |
		       (static_cast<uint32_t>(pData[2]) << 16) | (static_cast<uint32_t>(pData[3]) << 24);
	}

	bool ParseAnimatedWebP(const uint8_t *pData, size_t Size, int *pCanvasWidth, int *pCanvasHeight, std::vector<SWebPFrame> *pFrames)
	{
		if(Size < 16 || std::memcmp(pData, "RIFF", 4) != 0 || std::memcmp(pData + 8, "WEBP", 4) != 0)
			return false;
		int CanvasWidth = 0;
		int CanvasHeight = 0;
		size_t Offset = 12;
		while(Offset + 8 <= Size)
		{
			const uint8_t *pChunk = pData + Offset;
			const uint32_t ChunkSize = ReadLE32(pChunk + 4);
			const uint8_t *pPayload = pChunk + 8;
			if(Offset + 8 + ChunkSize > Size)
				break;
			if(std::memcmp(pChunk, "VP8X", 4) == 0 && ChunkSize >= 10)
			{
				CanvasWidth = static_cast<int>(ReadLE24(pPayload + 4)) + 1;
				CanvasHeight = static_cast<int>(ReadLE24(pPayload + 7)) + 1;
			}
			else if(std::memcmp(pChunk, "ANMF", 4) == 0 && ChunkSize >= 16)
			{
				SWebPFrame Frame;
				Frame.m_X = static_cast<int>(ReadLE24(pPayload + 0)) * 2;
				Frame.m_Y = static_cast<int>(ReadLE24(pPayload + 3)) * 2;
				Frame.m_Width = static_cast<int>(ReadLE24(pPayload + 6)) + 1;
				Frame.m_Height = static_cast<int>(ReadLE24(pPayload + 9)) + 1;
				Frame.m_DurationMs = static_cast<int>(ReadLE24(pPayload + 12));
				const uint8_t Flags = pPayload[15];
				Frame.m_Blend = (Flags & 0x02) == 0;
				Frame.m_DisposeBackground = (Flags & 0x01) != 0;
				// ANMF 里嵌着 ALPH / VP8 / VP8L 子块；ALPH 先不支持（视频转出来的动图没有 alpha）。
				size_t SubOffset = 16;
				while(SubOffset + 8 <= ChunkSize)
				{
					const uint8_t *pSub = pPayload + SubOffset;
					const uint32_t SubSize = ReadLE32(pSub + 4);
					if(SubOffset + 8 + SubSize > ChunkSize)
						break;
					if(std::memcmp(pSub, "VP8 ", 4) == 0 || std::memcmp(pSub, "VP8L", 4) == 0)
					{
						Frame.m_pData = pSub + 8;
						Frame.m_Size = SubSize;
						break;
					}
					SubOffset += 8 + SubSize + (SubSize & 1u);
				}
				if(Frame.m_pData != nullptr && Frame.m_Width > 0 && Frame.m_Height > 0)
					pFrames->push_back(Frame);
			}
			Offset += 8 + ChunkSize + (ChunkSize & 1u);
		}
		if(CanvasWidth <= 0 || CanvasHeight <= 0 || pFrames->empty())
			return false;
		*pCanvasWidth = CanvasWidth;
		*pCanvasHeight = CanvasHeight;
		return true;
	}

	bool LoadAnimatedWebP(const uint8_t *pData, size_t Size, SWikiMedia *pMedia)
	{
		std::vector<SWebPFrame> vFrames;
		int CanvasWidth = 0;
		int CanvasHeight = 0;
		if(!ParseAnimatedWebP(pData, Size, &CanvasWidth, &CanvasHeight, &vFrames))
			return false;
		if(vFrames.size() < 2 || vFrames.size() > static_cast<size_t>(WIKI_MEDIA_MAX_FRAMES))
			return false;
		if(CanvasWidth <= 0 || CanvasHeight <= 0 || CanvasWidth > WIKI_MEDIA_MAX_CANVAS || CanvasHeight > WIKI_MEDIA_MAX_CANVAS)
			return false;
		// 逐帧副本 + 图集都要分配，先按画布尺寸算出峰值，超预算就当缺图。
		const size_t CanvasBytes = static_cast<size_t>(CanvasWidth) * CanvasHeight * 4;
		if(CanvasBytes * vFrames.size() > WIKI_MEDIA_MAX_FRAME_BYTES)
		{
			dbg_msg("wiki/media", "animated webp rejected: %dx%d, %d frames", CanvasWidth, CanvasHeight, static_cast<int>(vFrames.size()));
			return false;
		}

		std::vector<uint8_t> Canvas(CanvasBytes, 0);
		std::vector<std::vector<uint8_t>> vFramePixels;
		vFramePixels.reserve(vFrames.size());
		int TotalMs = 0;
		for(const SWebPFrame &Frame : vFrames)
		{
			int FrameWidth = 0;
			int FrameHeight = 0;
			uint8_t *pRGBA = WebPDecodeRGBA(Frame.m_pData, Frame.m_Size, &FrameWidth, &FrameHeight);
			if(pRGBA == nullptr)
				break;
			const int CopyWidth = std::min({Frame.m_Width, FrameWidth, CanvasWidth - Frame.m_X});
			const int CopyHeight = std::min({Frame.m_Height, FrameHeight, CanvasHeight - Frame.m_Y});
			for(int Y = 0; Y < CopyHeight; ++Y)
			{
				for(int X = 0; X < CopyWidth; ++X)
				{
					const uint8_t *pSrc = pRGBA + (static_cast<size_t>(Y) * FrameWidth + X) * 4;
					uint8_t *pDst = Canvas.data() + (static_cast<size_t>(Y + Frame.m_Y) * CanvasWidth + (X + Frame.m_X)) * 4;
					if(!Frame.m_Blend)
					{
						std::memcpy(pDst, pSrc, 4);
						continue;
					}
					// 直通 alpha 的 source-over 混合。
					const int SrcAlpha = pSrc[3];
					if(SrcAlpha == 255)
					{
						std::memcpy(pDst, pSrc, 4);
						continue;
					}
					if(SrcAlpha == 0)
						continue;
					const int DstAlpha = pDst[3];
					const int OutAlpha = SrcAlpha + DstAlpha * (255 - SrcAlpha) / 255;
					if(OutAlpha <= 0)
						continue;
					for(int Channel = 0; Channel < 3; ++Channel)
					{
						const int Src = pSrc[Channel] * SrcAlpha;
						const int Dst = pDst[Channel] * DstAlpha * (255 - SrcAlpha) / 255;
						pDst[Channel] = static_cast<uint8_t>(std::clamp((Src + Dst) / OutAlpha, 0, 255));
					}
					pDst[3] = static_cast<uint8_t>(std::clamp(OutAlpha, 0, 255));
				}
			}
			WebPFree(pRGBA);
			vFramePixels.push_back(Canvas);
			if(Frame.m_DisposeBackground && Frame.m_X < CanvasWidth)
			{
				const int ClearWidth = std::max(0, std::min(Frame.m_Width, CanvasWidth - Frame.m_X));
				for(int Y = Frame.m_Y; Y < Frame.m_Y + Frame.m_Height && Y < CanvasHeight; ++Y)
					std::memset(Canvas.data() + static_cast<size_t>(Y) * CanvasWidth * 4 + static_cast<size_t>(Frame.m_X) * 4, 0, static_cast<size_t>(ClearWidth) * 4);
			}
			TotalMs += std::max(10, Frame.m_DurationMs);
		}
		if(vFramePixels.size() < 2)
			return false;

		// 拼成和图集一致的网格，播放逻辑与雪花图完全相同。
		const int Count = static_cast<int>(vFramePixels.size());
		const int Cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(Count))));
		const int Rows = (Count + Cols - 1) / Cols;
		const int AtlasWidth = Cols * CanvasWidth;
		const int AtlasHeight = Rows * CanvasHeight;
		const size_t AtlasBytes = static_cast<size_t>(AtlasWidth) * AtlasHeight * 4;
		if(AtlasBytes > WIKI_MEDIA_MAX_ATLAS_BYTES)
		{
			dbg_msg("wiki/media", "animated webp atlas rejected: %dx%d", AtlasWidth, AtlasHeight);
			return false;
		}
		uint8_t *pAtlas = static_cast<uint8_t *>(malloc(AtlasBytes));
		if(pAtlas == nullptr)
			return false;
		std::memset(pAtlas, 0, AtlasBytes);
		for(int Index = 0; Index < Count; ++Index)
		{
			const int CellX = (Index % Cols) * CanvasWidth;
			const int CellY = (Index / Cols) * CanvasHeight;
			const std::vector<uint8_t> &Pixels = vFramePixels[Index];
			for(int Y = 0; Y < CanvasHeight; ++Y)
				std::memcpy(pAtlas + (static_cast<size_t>(Y + CellY) * AtlasWidth + CellX) * 4, Pixels.data() + static_cast<size_t>(Y) * CanvasWidth * 4, static_cast<size_t>(CanvasWidth) * 4);
		}

		CImageInfo Image;
		Image.m_Width = AtlasWidth;
		Image.m_Height = AtlasHeight;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = pAtlas;
		pMedia->m_Width = AtlasWidth;
		pMedia->m_Height = AtlasHeight;
		pMedia->m_Cols = Cols;
		pMedia->m_Rows = Rows;
		pMedia->m_Frames = Count;
		const float AverageMs = static_cast<float>(TotalMs) / Count;
		pMedia->m_Fps = AverageMs > 0.0f ? 1000.0f / AverageMs : 0.0f;
		pMedia->m_Texture = s_pGraphics->LoadTextureRawMove(Image, 0);
		pMedia->m_Valid = true;
		return true;
	}

	bool ReadRelativeBytes(const char *pRelative, std::vector<uint8_t> *pBytes)
	{
		char aFull[IO_MAX_PATH_LENGTH];
		if(!BuildPath(pRelative, aFull, sizeof(aFull)))
			return false;
		IOHANDLE File = s_pStorage->OpenFile(aFull, IOFLAG_READ, IStorage::TYPE_ALL);
		if(File == nullptr)
			return false;
		void *pData = nullptr;
		unsigned DataSize = 0;
		const bool ReadOk = io_read_all(File, &pData, &DataSize);
		io_close(File);
		if(!ReadOk || pData == nullptr)
			return false;
		pBytes->assign(static_cast<uint8_t *>(pData), static_cast<uint8_t *>(pData) + DataSize);
		free(pData);
		return true;
	}

	bool LoadMedia(const char *pRelative, SWikiMedia *pMedia)
	{
		// 动图 WebP 优先：解成图集后与雪花图走同一套播放逻辑。
		if(EndsWithNoCase(pRelative, ".webp"))
		{
			std::vector<uint8_t> vBytes;
			if(ReadRelativeBytes(pRelative, &vBytes) && LoadAnimatedWebP(vBytes.data(), vBytes.size(), pMedia))
				return true;
		}
		// 1) 直接给图片文件。
		if(EndsWithNoCase(pRelative, ".png") || EndsWithNoCase(pRelative, ".webp"))
		{
			// 作者也可能直接写雪花图名，此时仍优先找同名元数据。
			if(EndsWithNoCase(pRelative, ".sheet.png"))
			{
				std::string Stem(pRelative);
				Stem.resize(Stem.size() - str_length(".sheet.png"));
				return LoadSheet(Stem.c_str(), pMedia);
			}
			if(LoadImageTexture(pRelative, pMedia))
				return true;
		}
		// 2) mp4/avif/gif/webp 动图：去掉扩展名后找同名的雪花图。
		std::string Stem(pRelative);
		const size_t Dot = Stem.rfind('.');
		if(Dot != std::string::npos)
			Stem.resize(Dot);
		return LoadSheet(Stem.c_str(), pMedia);
	}
} // namespace

void qm_wiki_media::Init(IStorage *pStorage, IGraphics *pGraphics)
{
	if(s_pStorage == pStorage && s_pGraphics == pGraphics)
		return;
	Clear();
	s_pStorage = pStorage;
	s_pGraphics = pGraphics;
}

void qm_wiki_media::Clear()
{
	s_Media.clear();
}

const SWikiMedia *qm_wiki_media::Get(const char *pPath)
{
	if(pPath == nullptr || pPath[0] == '\0' || s_pStorage == nullptr || s_pGraphics == nullptr)
		return nullptr;
	auto It = s_Media.find(pPath);
	if(It != s_Media.end())
		return It->second.m_Valid ? &It->second : nullptr;

	SWikiMedia Media;
	LoadMedia(pPath, &Media);
	auto Inserted = s_Media.emplace(pPath, Media);
	return Inserted.first->second.m_Valid ? &Inserted.first->second : nullptr;
}

bool qm_wiki_media::Exists(const char *pPath)
{
	return Get(pPath) != nullptr;
}

float qm_wiki_media::Aspect(const SWikiMedia &Media)
{
	if(Media.m_Height <= 0)
		return 1.0f;
	const float FrameWidth = static_cast<float>(Media.m_Width) / std::max(1, Media.m_Cols);
	const float FrameHeight = static_cast<float>(Media.m_Height) / std::max(1, Media.m_Rows);
	return FrameHeight > 0.0f ? FrameWidth / FrameHeight : 1.0f;
}

void qm_wiki_media::Draw(const SWikiMedia &Media, const CUIRect &Rect, ColorRGBA Color)
{
	if(!Media.m_Valid || s_pGraphics == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	int Frame = 0;
	if(Media.m_Fps > 0.0f && Media.m_Frames > 1)
	{
		const double Seconds = static_cast<double>(time_get_nanoseconds().count()) / 1.0e9;
		// 先对帧数取模再转 int：运行时间乘以 fps 会立刻超出 int 范围（转换是未定义行为）。
		const double Frames = static_cast<double>(Media.m_Frames);
		const double Position = std::fmod(Seconds * static_cast<double>(Media.m_Fps), Frames);
		Frame = static_cast<int>(std::max(0.0, Position)) % Media.m_Frames;
	}
	const int Col = Frame % Media.m_Cols;
	const int Row = Frame / Media.m_Cols;
	s_pGraphics->TextureSet(Media.m_Texture);
	s_pGraphics->QuadsBegin();
	s_pGraphics->SetColor(Color);
	if(Media.m_Cols > 1 || Media.m_Rows > 1)
	{
		const float U0 = static_cast<float>(Col) / Media.m_Cols;
		const float V0 = static_cast<float>(Row) / Media.m_Rows;
		s_pGraphics->QuadsSetSubset(U0, V0, U0 + 1.0f / Media.m_Cols, V0 + 1.0f / Media.m_Rows);
	}
	else
	{
		s_pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	}
	const IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	s_pGraphics->QuadsDrawTL(&Quad, 1);
	s_pGraphics->QuadsEnd();
	s_pGraphics->TextureClear();
	s_pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
