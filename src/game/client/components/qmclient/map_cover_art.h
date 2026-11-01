// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_COVER_ART_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_COVER_ART_H

#include <engine/image.h>

#include <cstddef>
#include <cstdint>

// 地图库条目的封面背景：把官方地图缩略图裁成目标矩形，压暗后在尾部做参差渐隐。
// 这里只做纯计算与解码，不接触图形接口，因此可以在 worker 线程执行并直接单元测试。
namespace QmMapCover
{
	// ddnet.org 官方地图缩略图地址前缀；只有该来源的图片会被下载和缓存。
	inline constexpr const char *THUMBNAIL_URL_PREFIX = "https://ddnet.org/ranks/maps/";
	// 缓存目录与文件名前缀：缓存文件名沿用 URL 末段，避开 Windows 设备名。
	inline constexpr const char *CACHE_FOLDER = "qmclient/mapcovers";
	inline constexpr const char *CACHE_FILE_PREFIX = "cover_";
	// 合成的上限：尺寸再大也只会让背景更糊，不值得占纹理内存。
	inline constexpr int MAX_WIDTH = 1280;
	inline constexpr int MAX_HEIGHT = 256;
	// 单张缩略图解码上限，防止异常响应把内存吃满。
	inline constexpr size_t MAX_DECODED_BYTES = 8 * 1024 * 1024;

	// 只接受 ddnet.org 官方地图缩略图：https://ddnet.org/ranks/maps/<文件名>.png
	bool ValidThumbnailUrl(const char *pUrl);
	// 生成缓存文件名（相对存储根）：qmclient/mapcovers/cover_<文件>.png。
	bool ThumbnailCacheFileName(const char *pUrl, char *pOut, size_t OutSize);

	// 尾部渐隐遮罩的形状参数。Start/End 是归一化 x 坐标，尾部在它们之间渐隐到 0。
	struct SDissolve
	{
		float m_Start = 0.36f;
		float m_End = 0.80f;
		// 参差强度；0 时边界平直，1 时噪声完全错切边界。
		float m_Ragged = 1.0f;
		// 输出像素单位的圆角半径，让条目背景和列表选中底色贴合。
		float m_CornerRadius = 4.0f;
		uint32_t m_Seed = 0;
	};

	// 压暗系数：列表文字压在封面上，保持可读性比保留原图亮度更重要。
	struct SArt
	{
		SDissolve m_Dissolve;
		float m_Shade = 0.40f;
	};

	// 单个输出像素的遮罩覆盖率（0..1），已包含左侧圆角与尾部参差。
	float DissolveAlpha(const SDissolve &Style, int X, int Y, int Width, int Height);
	// 确定性噪声：同一 (X, Y, Seed) 永远得到同一个值，便于测试与逐帧稳定。
	float ValueNoise(float X, float Y, uint32_t Seed);

	// 把 Source 按「填满目标矩形、居中裁切」缩放，压暗并按 Art 应用遮罩。
	// 输出总是 FORMAT_RGBA，尺寸为 OutWidth×OutHeight。失败时不修改 Out。
	bool Compose(const CImageInfo &Source, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out);
	// 在工作线程里先解码 PNG，再走 Compose。pContextName 只用于解码日志。
	bool DecodeAndCompose(const void *pPngData, size_t DataSize, const char *pContextName, int OutWidth, int OutHeight, const SArt &Art, CImageInfo &Out);

	// 同一张地图的行条目与详情面板共用同一个种子，让尾部走向保持一致。
	uint32_t SeedForUrl(const char *pUrl);
} // namespace QmMapCover

#endif
