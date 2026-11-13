// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_COVER_ART_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_COVER_ART_H

#include <engine/image.h>

#include <cstddef>
#include <cstdint>

class CPngReadAllocator;

// 地图库条目的封面图：把官方地图缩略图缩成条目左侧的小图，并在右侧接一段
// 参差的拖尾渐隐到没有。这里只做纯计算与解码，不接触图形接口，因此可以在
// worker 线程执行并直接单元测试。
//
// 为什么不是整行背景：条目只有二三十像素高，整行铺满等于把缩略图横着裁成
// 一条极窄的横条，内容无法辨认；缩成正常比例的小图反而能看出是哪张地图。
namespace QmMapCover
{
	enum class EPrepareResult
	{
		SUCCESS,
		INVALID,
		RESOURCE_ERROR,
	};

	// 仅在同步合成期间借用；成功返回的内存须能由 CImageInfo::Free 释放。
	struct SOutputAllocation
	{
		void *(*m_pfnAllocate)(size_t Size, void *pUser) noexcept = nullptr;
		void *m_pUser = nullptr;
	};

	// ddnet.org 官方地图缩略图地址前缀；只有该来源的图片会被下载和缓存。
	inline constexpr const char *THUMBNAIL_URL_PREFIX = "https://ddnet.org/ranks/maps/";
	// 缓存目录与文件名前缀：缓存文件名沿用 URL 末段，避开 Windows 设备名。
	inline constexpr const char *CACHE_FOLDER = "qmclient/mapcovers";
	inline constexpr const char *CACHE_FILE_PREFIX = "cover_";
	// 输出尺寸上限与单张缩略图解码上限。
	inline constexpr int MAX_WIDTH = 512;
	inline constexpr int MAX_HEIGHT = 256;
	inline constexpr size_t MAX_DECODED_BYTES = 8 * 1024 * 1024;

	// 只接受 ddnet.org 官方地图缩略图：https://ddnet.org/ranks/maps/<文件名>.png
	bool ValidThumbnailUrl(const char *pUrl);
	// 生成缓存文件名（相对存储根）：qmclient/mapcovers/cover_<文件>.png。
	bool ThumbnailCacheFileName(const char *pUrl, char *pOut, size_t OutSize);

	// 条目封面图的形状参数。
	struct SArt
	{
		// 输出宽度里给完整缩略图的比例，其余宽度是右侧拖尾。
		float m_ImageFraction = 0.69f;
		// 压暗系数：条目文字压不到这里，但太亮会抢列表的注意力。
		float m_Shade = 0.84f;
		// 拖尾参差强度；0 为平直渐隐。
		float m_Ragged = 1.0f;
		// 输出像素单位的圆角半径（只作用于缩略图左侧两角）。
		float m_CornerRadius = 3.0f;
		uint32_t m_Seed = 0;
	};

	// 确定性噪声：同一 (X, Y, Seed) 永远得到同一个值，便于测试与逐帧稳定。
	float ValueNoise(float X, float Y, uint32_t Seed);

	// 拖尾区单个像素的覆盖率（0..1）。X 必须 >= ImageWidth 且 < Width。
	// 三层不同长度、不同相位的噪声错切叠加，形成层次不齐的尾巴。
	float TailAlpha(const SArt &Art, int X, int Y, int Width, int Height, int ImageWidth);

	// 把 Source 缩成「缩略图 + 拖尾」：左侧 ImageFraction 宽度是完整缩略图
	// （按目标高度等比缩放并居中裁切），右侧采样缩略图靠右的列拉出参差拖尾。
	// 输出总是 FORMAT_RGBA，尺寸为 OutWidth×OutHeight。失败时不修改 Out。
	bool Compose(const CImageInfo &Source, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out);
	EPrepareResult ComposeChecked(const CImageInfo &Source, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out, const SOutputAllocation *pAllocation = nullptr);
	// 在工作线程里先解码 PNG，再走 Compose。pContextName 只用于解码日志。
	bool DecodeAndCompose(const void *pPngData, size_t DataSize, const char *pContextName, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out);
	EPrepareResult DecodeAndComposeChecked(const void *pPngData, size_t DataSize, const char *pContextName, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out,
		CPngReadAllocator *pPngAllocator = nullptr, const SOutputAllocation *pOutputAllocation = nullptr);

	// 同一张地图的行条目与详情条共用同一个种子，让拖尾走向保持一致。
	uint32_t SeedForUrl(const char *pUrl);
} // namespace QmMapCover

#endif
