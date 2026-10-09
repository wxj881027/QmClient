#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_SKIN_APPLY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_SKIN_APPLY_H

#include <base/str.h>

#include <engine/shared/config.h>

#include <utility>

// 皮肤列表双击的目标角色：0=本体（主号），1=分身（dummy）。
// 双预览选择只决定单击的编辑对象；双击显式指定一侧，不改变编辑对象。
// 左键双击＝本体、右键双击＝分身由调用点直接决定，不需要按键序号到角色的映射。
enum class ETeeSkinApplyTarget
{
	MAIN = 0,
	DUMMY = 1,
};

inline int QmTeeSkinApplyTargetDummy(const ETeeSkinApplyTarget Target)
{
	return Target == ETeeSkinApplyTarget::DUMMY ? 1 : 0;
}

// 双击皮肤列表项时把该皮肤（含自定义颜色）写到指定角色，与单击路径共用同一套字段语义：
// 皮肤名总是写入；带颜色键的条目同时覆盖该角色的自定义颜色开关（启用时才覆盖两端颜色）。
inline void QmApplyTeeSkinToTarget(CConfig &Config, const ETeeSkinApplyTarget Target, const char *pSkinName,
	const bool HasCustomColor, const bool UseCustomColor, const int ColorBody, const int ColorFeet)
{
	const bool Dummy = Target == ETeeSkinApplyTarget::DUMMY;
	str_copy(Dummy ? Config.m_ClDummySkin : Config.m_ClPlayerSkin, pSkinName, sizeof(Config.m_ClPlayerSkin));
	if(!HasCustomColor)
		return;
	(Dummy ? Config.m_ClDummyUseCustomColor : Config.m_ClPlayerUseCustomColor) = UseCustomColor ? 1 : 0;
	if(UseCustomColor)
	{
		(Dummy ? Config.m_ClDummyColorBody : Config.m_ClPlayerColorBody) = ColorBody;
		(Dummy ? Config.m_ClDummyColorFeet : Config.m_ClPlayerColorFeet) = ColorFeet;
	}
}

// 预览之间复制完整的皮肤配色设置，包括暂时关闭的颜色值。
inline void QmCopyTeeSkinSettings(CConfig &Config, const ETeeSkinApplyTarget Target)
{
	const bool ToDummy = Target == ETeeSkinApplyTarget::DUMMY;
	str_copy(ToDummy ? Config.m_ClDummySkin : Config.m_ClPlayerSkin, ToDummy ? Config.m_ClPlayerSkin : Config.m_ClDummySkin, sizeof(Config.m_ClPlayerSkin));
	(ToDummy ? Config.m_ClDummyUseCustomColor : Config.m_ClPlayerUseCustomColor) = ToDummy ? Config.m_ClPlayerUseCustomColor : Config.m_ClDummyUseCustomColor;
	(ToDummy ? Config.m_ClDummyColorBody : Config.m_ClPlayerColorBody) = ToDummy ? Config.m_ClPlayerColorBody : Config.m_ClDummyColorBody;
	(ToDummy ? Config.m_ClDummyColorFeet : Config.m_ClPlayerColorFeet) = ToDummy ? Config.m_ClPlayerColorFeet : Config.m_ClDummyColorFeet;
}

inline void QmSwapTeeSkinSettings(CConfig &Config)
{
	std::swap(Config.m_ClPlayerSkin, Config.m_ClDummySkin);
	std::swap(Config.m_ClPlayerUseCustomColor, Config.m_ClDummyUseCustomColor);
	std::swap(Config.m_ClPlayerColorBody, Config.m_ClDummyColorBody);
	std::swap(Config.m_ClPlayerColorFeet, Config.m_ClDummyColorFeet);
}

#endif
