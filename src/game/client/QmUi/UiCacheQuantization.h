#ifndef GAME_CLIENT_QMUI_UICACHEQUANTIZATION_H
#define GAME_CLIENT_QMUI_UICACHEQUANTIZATION_H

#include <algorithm>
#include <cmath>
#include <limits>

// 只量化缓存身份，不限制真实布局尺寸；保留旧float乘法和半单位取整口径。
inline int UiCacheRoundedBucket(float Value, float Scale, int Invalid = 0) noexcept
{
	if(!std::isfinite(Value) || !std::isfinite(Scale))
		return Invalid;
	const float Scaled = Value * Scale;
	const float Rounded = Scaled > 0.0f ? Scaled + 0.5f : Scaled - 0.5f;
	if(static_cast<double>(Rounded) >= std::numeric_limits<int>::max())
		return std::numeric_limits<int>::max();
	if(static_cast<double>(Rounded) <= std::numeric_limits<int>::min())
		return std::numeric_limits<int>::min();
	return static_cast<int>(Rounded);
}

inline int UiCacheNonnegativeBucket(float Value, float Scale) noexcept
{
	return std::max(0, UiCacheRoundedBucket(Value, Scale));
}

inline int UiTextWidthBucket(float Width) noexcept
{
	if(Width < 0.0f || !std::isfinite(Width))
		return -1;
	// 乘回四之前限制商，保证桶值仍对齐且不会溢出。
	return std::min(UiCacheRoundedBucket(Width, 0.25f), std::numeric_limits<int>::max() / 4) * 4;
}

inline int UiTextHiDpiBucket(float Scale) noexcept
{
	return Scale >= 0.0f && std::isfinite(Scale) ? UiCacheRoundedBucket(Scale, 100.0f) : 100;
}

#endif
