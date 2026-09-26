#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_SKIN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_SKIN_H

#include <base/str.h>

#include <engine/shared/config.h>

inline bool QmCopyScoreboardSkin(CConfig &Config, bool Sixup, const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet)
{
	// 0.7 uses separate skin-part configuration; do not write a compatible name into 0.6 settings.
	if(Sixup || !pSkinName || !pSkinName[0])
		return false;
	const bool Dummy = Config.m_ClDummy != 0;
	str_copy(Dummy ? Config.m_ClDummySkin : Config.m_ClPlayerSkin, pSkinName, sizeof(Config.m_ClPlayerSkin));
	(Dummy ? Config.m_ClDummyUseCustomColor : Config.m_ClPlayerUseCustomColor) = UseCustomColor;
	(Dummy ? Config.m_ClDummyColorBody : Config.m_ClPlayerColorBody) = ColorBody;
	(Dummy ? Config.m_ClDummyColorFeet : Config.m_ClPlayerColorFeet) = ColorFeet;
	return true;
}

#endif
