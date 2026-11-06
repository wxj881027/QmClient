// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_ASSETS_PREVIEW_SCALE_H
#define GAME_CLIENT_COMPONENTS_ASSETS_PREVIEW_SCALE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

inline constexpr size_t ASSET_PREVIEW_UPLOAD_MAX_BYTES_PER_FRAME = 1 * 1024 * 1024;
inline constexpr int ASSET_PREVIEW_MIN_TEXTURE_SIZE = 512;
inline constexpr int LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE = 4096;
// 解码前按 RGBA 最坏情况限制源图，允许完整 4K，避免先分配大图再缩小。
inline constexpr size_t ASSET_PREVIEW_MAX_DECODED_BYTES = size_t(LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE) * LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE * 4u;
inline constexpr int WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE = 2048;
inline constexpr int LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE = 64 * 1024 * 1024;

// Tile size for the entity preview grid: the largest square that fits both axes
// given the available rect and the column/row count. Pure so it is unit-testable.
inline float ComputeEntityPreviewTileSize(float RectW, float RectH, int Cols, int Rows)
{
	return std::min(RectW / (float)Cols, RectH / (float)Rows);
}

struct SPreviewTargetSize
{
	int m_Width;
	int m_Height;
	bool m_Resized;
};

struct SWorkshopPreviewDecodeSourcePlan
{
	bool m_UseInstallSource;
	bool m_UseThumbCache;
};

inline SPreviewTargetSize ComputePreviewTargetSize(int Width, int Height, int MaxTextureSize)
{
	if(Width <= 0 || Height <= 0 || MaxTextureSize <= 0)
		return {Width, Height, false};

	if(Width <= MaxTextureSize && Height <= MaxTextureSize)
		return {Width, Height, false};

	if(Width > Height)
	{
		const int ScaledHeight = std::max(1, (int)((float)Height * MaxTextureSize / Width));
		return {MaxTextureSize, ScaledHeight, true};
	}

	const int ScaledWidth = std::max(1, (int)((float)Width * MaxTextureSize / Height));
	return {ScaledWidth, MaxTextureSize, true};
}

inline size_t PreviewTextureSizeBytesEstimate(int TextureSize)
{
	if(TextureSize <= 0)
		return 0;
	return (size_t)TextureSize * (size_t)TextureSize * 4u;
}

inline int PreviewBudgetNextTier(int TextureSize, int MinTextureSize)
{
	if(TextureSize <= MinTextureSize)
		return MinTextureSize;
	return std::max(MinTextureSize, TextureSize / 2);
}

inline int PreviewUploadBudgetTextureSize(int MaxTextureSize, int MinTextureSize, size_t MaxUploadBytes)
{
	if(MaxTextureSize <= 0)
		return std::max(MinTextureSize, 0);
	if(MinTextureSize <= 0)
		MinTextureSize = MaxTextureSize;
	if(MaxUploadBytes == 0)
		return std::clamp(MaxTextureSize, MinTextureSize, MaxTextureSize);

	int TextureSize = std::clamp(MaxTextureSize, MinTextureSize, MaxTextureSize);
	while(TextureSize > MinTextureSize && PreviewTextureSizeBytesEstimate(TextureSize) > MaxUploadBytes)
	{
		const int NextTextureSize = PreviewBudgetNextTier(TextureSize, MinTextureSize);
		if(NextTextureSize == TextureSize)
			break;
		TextureSize = NextTextureSize;
	}
	return std::clamp(TextureSize, MinTextureSize, MaxTextureSize);
}

template<typename TByteCount = size_t>
inline TByteCount PreviewBudgetBytes(size_t OverrideMb, int Percent, float GpuBudgetMb)
{
	static_assert(std::is_integral_v<TByteCount> && std::is_unsigned_v<TByteCount> && !std::is_same_v<TByteCount, bool>);
	constexpr uint64_t BytesPerMb = 1024ull * 1024ull;
	constexpr TByteCount MaxBytes = std::numeric_limits<TByteCount>::max();
	constexpr uint64_t MaxWholeMb = MaxBytes / BytesPerMb;
	if(OverrideMb > 0)
	{
		if(OverrideMb > MaxWholeMb)
			return MaxBytes;
		return static_cast<TByteCount>(OverrideMb * BytesPerMb);
	}
	if(Percent <= 0 || !(GpuBudgetMb > 0.0f))
		return 0;
	const float BudgetMb = GpuBudgetMb * std::clamp(Percent, 0, 100) / 100.0f;
	// 先限制可表示的整MB，再转整数；超限不能绕回表示不限制预算的0。
	if(BudgetMb >= static_cast<float>(MaxWholeMb) + 1.0f)
		return MaxBytes;
	return static_cast<TByteCount>(static_cast<TByteCount>(BudgetMb) * BytesPerMb);
}

inline int ComputePreviewBudgetedTextureSize(int MaxTextureSize, int MinTextureSize, uint64_t TextureBudgetBytes, uint64_t CurrentTextureMemoryBytes, uint64_t ReplacedResidentPreviewBytes)
{
	if(MaxTextureSize <= 0)
		return std::max(MinTextureSize, 0);
	if(MinTextureSize <= 0)
		MinTextureSize = MaxTextureSize;

	int TextureSize = std::clamp(MaxTextureSize, MinTextureSize, MaxTextureSize);
	if(TextureBudgetBytes == 0)
		return TextureSize;

	const uint64_t BaseTextureBytes = CurrentTextureMemoryBytes > ReplacedResidentPreviewBytes ? CurrentTextureMemoryBytes - ReplacedResidentPreviewBytes : 0;
	while(TextureSize > MinTextureSize)
	{
		const uint64_t EstimatedBytes = PreviewTextureSizeBytesEstimate(TextureSize);
		// 用剩余额度比较，避免已有纹理与候选估算相加后绕回较小值。
		if(BaseTextureBytes <= TextureBudgetBytes && EstimatedBytes <= TextureBudgetBytes - BaseTextureBytes)
			break;
		const int NextTextureSize = PreviewBudgetNextTier(TextureSize, MinTextureSize);
		if(NextTextureSize == TextureSize)
			break;
		TextureSize = NextTextureSize;
	}

	return std::clamp(TextureSize, MinTextureSize, MaxTextureSize);
}

inline bool WorkshopEntityBgAllowsImageUrlFallback(const char *pCategoryId)
{
	return pCategoryId == nullptr || std::string_view(pCategoryId) != "entity_bg";
}

inline bool WorkshopAssetHasRequiredDownloadUrl(const char *pCategoryId, bool HasDownloadUrl)
{
	return HasDownloadUrl || WorkshopEntityBgAllowsImageUrlFallback(pCategoryId);
}

inline bool WorkshopInstalledAssetUsesInstallPreviewSource(const char *pCategoryId)
{
	return pCategoryId == nullptr || std::string_view(pCategoryId) != "entity_bg";
}

inline bool WorkshopAssetPreviewDecodeUsesInstallSource(const char *pCategoryId, bool Installed)
{
	return Installed && WorkshopInstalledAssetUsesInstallPreviewSource(pCategoryId);
}

inline bool WorkshopAssetCanDecodeInstalledPreview(const char *pCategoryId, bool Installed, bool ThumbCacheFailed)
{
	return WorkshopAssetPreviewDecodeUsesInstallSource(pCategoryId, Installed) && !ThumbCacheFailed;
}

inline bool WorkshopAssetCanDecodeAnyLocalPreview(const char *pCategoryId, bool Installed, bool ThumbCacheFailed, bool HasThumbCache)
{
	return WorkshopAssetCanDecodeInstalledPreview(pCategoryId, Installed, ThumbCacheFailed) ||
	       (!ThumbCacheFailed && HasThumbCache);
}

inline SWorkshopPreviewDecodeSourcePlan BuildWorkshopPreviewDecodeSourcePlan(const char *pCategoryId, bool Installed, bool HasThumbCache)
{
	return {
		WorkshopAssetPreviewDecodeUsesInstallSource(pCategoryId, Installed),
		HasThumbCache,
	};
}

inline bool DetectCorruptEntityBgInstallHeader(const void *pData, size_t DataSize)
{
	if(pData == nullptr || DataSize == 0)
		return false;

	const uint8_t *pBytes = static_cast<const uint8_t *>(pData);
	const bool IsPng = DataSize >= 8 &&
			   std::memcmp(pBytes, "\x89PNG\r\n\x1a\n", 8) == 0;
	const bool IsWebp = DataSize >= 12 &&
			    std::memcmp(pBytes, "RIFF", 4) == 0 &&
			    std::memcmp(pBytes + 8, "WEBP", 4) == 0;
	return IsPng || IsWebp;
}

#endif
