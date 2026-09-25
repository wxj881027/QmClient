#ifndef ENGINE_GFX_SPRITE_IMAGE_H
#define ENGINE_GFX_SPRITE_IMAGE_H

#include <base/log.h>

#include <engine/image.h>

#include <generated/data_types.h>

#include <cstdlib>
#include <limits>
#include <utility>

// 只计算像素矩形和复制 CPU 数据，可由资源解码任务调用。
inline bool GetSpriteImageRect(const CImageInfo &ImageInfo, const CDataSprite *pSprite, size_t &x, size_t &y, size_t &w, size_t &h)
{
	if(pSprite == nullptr || pSprite->m_pSet == nullptr || pSprite->m_pSet->m_Gridx <= 0 || pSprite->m_pSet->m_Gridy <= 0 ||
		pSprite->m_X < 0 || pSprite->m_Y < 0 || pSprite->m_W <= 0 || pSprite->m_H <= 0)
	{
		return false;
	}

	const size_t Gridx = pSprite->m_pSet->m_Gridx;
	const size_t Gridy = pSprite->m_pSet->m_Gridy;
	if(ImageInfo.m_Width == 0 || ImageInfo.m_Height == 0 || ImageInfo.m_Width % Gridx != 0 || ImageInfo.m_Height % Gridy != 0)
		return false;

	const size_t GridWidth = ImageInfo.m_Width / Gridx;
	const size_t GridHeight = ImageInfo.m_Height / Gridy;
	const size_t SpriteX = pSprite->m_X;
	const size_t SpriteY = pSprite->m_Y;
	const size_t SpriteW = pSprite->m_W;
	const size_t SpriteH = pSprite->m_H;
	if(SpriteX > std::numeric_limits<size_t>::max() / GridWidth ||
		SpriteY > std::numeric_limits<size_t>::max() / GridHeight ||
		SpriteW > std::numeric_limits<size_t>::max() / GridWidth ||
		SpriteH > std::numeric_limits<size_t>::max() / GridHeight)
	{
		return false;
	}

	x = SpriteX * GridWidth;
	y = SpriteY * GridHeight;
	w = SpriteW * GridWidth;
	h = SpriteH * GridHeight;
	if(w == 0 || h == 0 || x > ImageInfo.m_Width || y > ImageInfo.m_Height ||
		w > ImageInfo.m_Width - x || h > ImageInfo.m_Height - y)
	{
		return false;
	}
	return true;
}

inline bool ExtractSpriteImage(const CImageInfo &FromImageInfo, const CDataSprite *pSprite, CImageInfo &Result)
{
	const char *pSpriteName = pSprite && pSprite->m_pName ? pSprite->m_pName : "(no name)";
	size_t x = 0;
	size_t y = 0;
	size_t w = 0;
	size_t h = 0;
	if(FromImageInfo.m_pData == nullptr || !GetSpriteImageRect(FromImageInfo, pSprite, x, y, w, h))
	{
		log_error("graphics/texture", "Ignoring invalid sprite texture '%s'.", pSpriteName);
		return false;
	}

	CImageInfo SpriteInfo;
	SpriteInfo.m_Width = w;
	SpriteInfo.m_Height = h;
	SpriteInfo.m_Format = FromImageInfo.m_Format;
	size_t SpriteDataSize = 0;
	if(!SpriteInfo.DataSize(SpriteDataSize))
	{
		log_error("graphics/texture", "Ignoring sprite texture '%s' with invalid data size.", pSpriteName);
		return false;
	}
	SpriteInfo.m_pData = static_cast<uint8_t *>(malloc(SpriteDataSize));
	if(SpriteInfo.m_pData == nullptr)
	{
		log_error("graphics/texture", "Failed to allocate sprite texture '%s'.", pSpriteName);
		SpriteInfo.Free();
		return false;
	}
	SpriteInfo.CopyRectFrom(FromImageInfo, x, y, w, h, 0, 0);
	Result = std::move(SpriteInfo);
	return true;
}

#endif
