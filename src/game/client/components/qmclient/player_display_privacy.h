#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_DISPLAY_PRIVACY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_DISPLAY_PRIVACY_H

#include <engine/shared/protocol.h>

namespace QmPlayerDisplayPrivacy
{
	struct SSettings
	{
		bool m_HideNonFriendNames = false;
		bool m_HideNonFriendSkins = false;
		bool m_BlockEnemies = false;
	};

	struct SPlayer
	{
		int m_ClientId = -1;
		bool m_Local = false;
		bool m_Friend = false;
		bool m_Enemy = false;
	};

	struct SDecisions
	{
		bool m_HideIdentity = false;
		bool m_HideSkin = false;
		bool m_BlockChat = false;
	};

	inline SDecisions Resolve(const SSettings &Settings, const SPlayer &Player)
	{
		if(Player.m_ClientId < 0 || Player.m_ClientId >= MAX_CLIENTS || Player.m_Local)
			return {};

		// 敌对屏蔽优先于好友豁免，主播模式仍只隐藏非好友。
		const bool Blocked = Settings.m_BlockEnemies && Player.m_Enemy;
		return {
			Blocked || (Settings.m_HideNonFriendNames && !Player.m_Friend),
			Blocked || (Settings.m_HideNonFriendSkins && !Player.m_Friend),
			Blocked};
	}
}

#endif
