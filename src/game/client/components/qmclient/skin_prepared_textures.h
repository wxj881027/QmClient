#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_PREPARED_TEXTURES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_PREPARED_TEXTURES_H

#include <engine/gfx/image_manipulation.h>

#include <generated/client_data.h>

#include <array>
#include <memory>

// 未上传的像素由任务持有；上传后 CImageInfo 被移空，析构只回收剩余 CPU 数据。
// 与远程的差别：越界精灵在本地是**预期情形**（空白材质包只做了图集的部分区域），
// 故按精灵粒度记录可用性，任一提取失败都不让整批预备作废——主线程对缺失精灵
// 仍需回落到 LoadSpriteTexture，以触发 qm_blank_asset_fallback 的空白素材回退。
class CQmPreparedSkinTextures
{
	std::array<std::array<CImageInfo, 12>, 2> m_aaImages;
	std::array<std::array<bool, 12>, 2> m_aaAvailable{};

public:
	CQmPreparedSkinTextures() = default;
	CQmPreparedSkinTextures(const CQmPreparedSkinTextures &) = delete;
	CQmPreparedSkinTextures &operator=(const CQmPreparedSkinTextures &) = delete;
	~CQmPreparedSkinTextures()
	{
		for(auto &aImages : m_aaImages)
			for(auto &Image : aImages)
				Image.Free();
	}

	static int SpriteId(size_t Index)
	{
		constexpr int aSprites[] = {SPRITE_TEE_BODY, SPRITE_TEE_BODY_OUTLINE, SPRITE_TEE_FOOT, SPRITE_TEE_FOOT_OUTLINE,
			SPRITE_TEE_HAND, SPRITE_TEE_HAND_OUTLINE, SPRITE_TEE_EYE_NORMAL, SPRITE_TEE_EYE_ANGRY,
			SPRITE_TEE_EYE_PAIN, SPRITE_TEE_EYE_HAPPY, SPRITE_TEE_EYE_DEAD, SPRITE_TEE_EYE_SURPRISE};
		return aSprites[Index];
	}
	CImageInfo &Image(size_t Variant, size_t Index) { return m_aaImages[Variant][Index]; }
	bool Available(size_t Variant, size_t Index) const { return m_aaAvailable[Variant][Index]; }
	void SetAvailable(size_t Variant, size_t Index, bool Available) { m_aaAvailable[Variant][Index] = Available; }
};

inline std::unique_ptr<CQmPreparedSkinTextures> QmPrepareSkinTextures(const CImageInfo &Original, const CImageInfo &Colorable, const CDataSprite *pSprites)
{
	auto pResult = std::make_unique<CQmPreparedSkinTextures>();
	for(size_t Variant = 0; Variant < 2; ++Variant)
	{
		const CImageInfo &Source = Variant == 0 ? Original : Colorable;
		for(size_t Index = 0; Index < 12; ++Index)
			pResult->SetAvailable(Variant, Index, ExtractSpriteImage(Source, &pSprites[CQmPreparedSkinTextures::SpriteId(Index)], pResult->Image(Variant, Index)));
	}
	return pResult;
}

#endif
