#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_DISPLAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_DISPLAY_H

#include "modes.h"

#include <engine/shared/config.h>

namespace qm_demo_display
{
	struct SConfigBindings
	{
		int *m_pHud;
		int *m_pChat;
	};

	// 普通回放与视频导出共用绑定；在线回放的聊天仍来自实时服务器。
	inline SConfigBindings ConfigBindings(CConfig &Config, bool OnlineReplay = false)
	{
		return {&Config.m_QmDemoShowHud, OnlineReplay ? &Config.m_ClShowChat : &Config.m_QmDemoShowChat};
	}

	struct SSettings
	{
		int m_Direction;
		int m_StrongWeak;
		int m_StrongWeakScope;
		bool m_Hud;
		bool m_Chat;
		bool m_DebugStrongWeak;
		float m_StrongWeakRowHeight;

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
		// 行高独立于显示档位，隐藏强弱钩时保留游戏、回放和预览的布局基准。
		const float StrongWeakRowHeight = 18.0f + 20.0f * Config.m_ClNamePlatesStrongSize / 100.0f + 5.0f;
		// 预览与视频共用只读显示配置，不临时覆写游戏配置或角色输入。
		if(DemoPlayback)
			return {Config.m_QmDemoShowDirection, Config.m_QmDemoShowStrongWeak, Config.m_QmDemoStrongWeakScope, Config.m_QmDemoShowHud != 0, Config.m_QmDemoShowChat != 0, false, StrongWeakRowHeight};
		return {
			VideoRendering ? Config.m_ClVideoShowDirection : Config.m_ClShowDirection,
			Config.m_ClNamePlatesStrong,
			Config.m_QmNameplateHookStrongWeakScope,
			(VideoRendering ? Config.m_ClVideoShowhud : Config.m_ClShowhud) != 0,
			(VideoRendering ? Config.m_ClVideoShowChat : Config.m_ClShowChat) != 0,
			Config.m_Debug != 0,
			StrongWeakRowHeight};
	}
}

#endif
