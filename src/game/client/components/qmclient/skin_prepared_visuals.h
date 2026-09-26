#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_PREPARED_VISUALS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_PREPARED_VISUALS_H

#include "qm_chat_avatar.h"
#include "qm_skin_outline.h"

#include <generated/client_data.h>

#include <game/client/skin.h>

// 解码任务只准备 CPU 素材，发布后由主线程接管；此处不创建或释放 GPU 纹理。
// 字段名按本地 CSkin 的口径（m_Qm*），不采用远程的 m_p* 命名。
struct SQmPreparedSkinVisuals
{
	std::shared_ptr<const QmChatAvatar::SSource> m_pOriginalAvatar;
	std::shared_ptr<const QmChatAvatar::SSource> m_pColorableAvatar;
	std::shared_ptr<CQmSkinOutline> m_pBodyOutline;
	std::shared_ptr<CQmSkinOutline> m_pFeetOutline;

	void Apply(CSkin &Skin) const
	{
		Skin.m_OriginalSkin.m_QmChatAvatar = m_pOriginalAvatar;
		Skin.m_ColorableSkin.m_QmChatAvatar = m_pColorableAvatar;
		Skin.m_OriginalSkin.m_QmBodyOutline = m_pBodyOutline;
		Skin.m_OriginalSkin.m_QmFeetOutline = m_pFeetOutline;
	}
};

// 聊天导出的头像素材：把皮肤图里用到的部件缩成 CPU 副本，之后与纹理句柄和皮肤缓存解耦。
// 单列出来供增量上传路径（设置页逐张上传收尾）复用，避免两处各留一份实现。
inline std::shared_ptr<const QmChatAvatar::SSource> QmPrepareSkinChatAvatar(const CImageInfo &Image, const CDataSprite *pSprites)
{
	auto pSource = std::make_shared<QmChatAvatar::SSource>();
	constexpr int aSprites[] = {SPRITE_TEE_BODY, SPRITE_TEE_BODY_OUTLINE, SPRITE_TEE_FOOT, SPRITE_TEE_FOOT_OUTLINE, SPRITE_TEE_EYE_NORMAL};
	for(size_t Index = 0; Index < std::size(aSprites); ++Index)
		pSource->m_aSprites[Index] = QmChatAvatar::CopySprite(Image, pSprites[aSprites[Index]]);
	return pSource;
}

inline SQmPreparedSkinVisuals QmPrepareSkinVisuals(const CImageInfo &Original, const CImageInfo &Colorable, const CDataSprite *pSprites)
{
	return {
		QmPrepareSkinChatAvatar(Original, pSprites),
		QmPrepareSkinChatAvatar(Colorable, pSprites),
		QmCreateSkinOutline(Original, pSprites[SPRITE_TEE_BODY], pSprites[SPRITE_TEE_BODY_OUTLINE], vec2(64, 64)),
		QmCreateSkinOutline(Original, pSprites[SPRITE_TEE_FOOT], pSprites[SPRITE_TEE_FOOT_OUTLINE], vec2(64, 32)),
	};
}

#endif
