#ifndef GAME_CLIENT_COMPONENTS_ASSETS_PREVIEW_SCALE_H
#define GAME_CLIENT_COMPONENTS_ASSETS_PREVIEW_SCALE_H

#include <algorithm>

inline constexpr int LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE = 4096;
inline constexpr int WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE = 512;
inline constexpr int LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE = 64 * 1024 * 1024;

struct SPreviewTargetSize
{
	int m_Width;
	int m_Height;
	bool m_Resized;
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

#endif
