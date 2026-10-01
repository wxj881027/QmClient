#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_DISPLAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_DISPLAY_H

#include "modes.h"

#include <engine/shared/config.h>

namespace qm_demo_display
{
	struct SSettings
	{
		int m_Direction;
		int m_StrongWeak;
		int m_StrongWeakScope;
		bool m_Hud;
		bool m_Chat;
		bool m_DebugStrongWeak;

		bool StrongWeakEnabled() const
		{
			return m_DebugStrongWeak || m_StrongWeak > 0;
		}

		bool ShowStrongWeakId() const
		{
			return m_DebugStrongWeak || m_StrongWeak == 2;
		}

		bool ShowStrongWeak(bool Self, bool Strong, bool Weak) const
		{
			return m_DebugStrongWeak || (Self && ShowStrongWeakId()) ||
			       (m_StrongWeak > 0 && ShouldShowQmHookStrongWeakScope(m_StrongWeakScope, Self, Strong, Weak));
		}
	};

	inline SSettings Resolve(const CConfig &Config, bool DemoPlayback, bool VideoRendering)
	{
		// 预览与视频共用只读显示配置，不临时覆写游戏配置或角色输入。
		if(DemoPlayback)
			return {Config.m_QmDemoShowDirection, Config.m_QmDemoShowStrongWeak, Config.m_QmDemoStrongWeakScope, Config.m_QmDemoShowHud != 0, Config.m_QmDemoShowChat != 0, false};
		return {
			VideoRendering ? Config.m_ClVideoShowDirection : Config.m_ClShowDirection,
			Config.m_ClNamePlatesStrong,
			Config.m_QmNameplateHookStrongWeakScope,
			(VideoRendering ? Config.m_ClVideoShowhud : Config.m_ClShowhud) != 0,
			(VideoRendering ? Config.m_ClVideoShowChat : Config.m_ClShowChat) != 0,
			Config.m_Debug != 0};
	}
}

#endif
