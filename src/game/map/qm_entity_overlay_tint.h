#ifndef GAME_MAP_QM_ENTITY_OVERLAY_TINT_H
#define GAME_MAP_QM_ENTITY_OVERLAY_TINT_H

#include <base/color.h>

#include <game/mapitems.h>

// 冻结类实体砖共用一个颜色：冻结、深度冻结、活动冻结。
inline bool QmIsFreezeEntityOverlayTile(int Index)
{
	return Index == TILE_FREEZE || Index == TILE_DFREEZE || Index == TILE_LFREEZE;
}

// 冻结砖在图集里多为黑色 + alpha，颜色形状由 alpha 承担。
// 有亮度时按亮度保留贴图明暗；纯黑像素直接上满 tint，避免再乘一次 alpha 让颜色发灰。
inline ColorRGBA QmTintEntityOverlayPixel(const ColorRGBA &Source, const ColorRGBA &Tint)
{
	if(Source.a <= 0.0f)
		return Source;

	ColorRGBA Out;
	const bool KeepSourceRgb = Tint.r >= 0.999f && Tint.g >= 0.999f && Tint.b >= 0.999f;
	if(KeepSourceRgb)
	{
		Out.r = Source.r;
		Out.g = Source.g;
		Out.b = Source.b;
	}
	else
	{
		const float Luma = Source.r * 0.2126f + Source.g * 0.7152f + Source.b * 0.0722f;
		const float Intensity = Luma > 0.02f ? Luma : 1.0f;
		Out.r = Tint.r * Intensity;
		Out.g = Tint.g * Intensity;
		Out.b = Tint.b * Intensity;
	}
	Out.a = Source.a * Tint.a;
	return Out;
}

#endif
