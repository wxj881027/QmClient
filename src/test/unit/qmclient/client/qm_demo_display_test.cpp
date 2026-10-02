#include <engine/shared/config.h>

#include <game/client/components/qmclient/demo_display.h>

#include <gtest/gtest.h>
#include <test/test.h>

// 回放显示与强弱钩绘制的只读判定，直接调用名牌绘制使用的配置与显示判断。

namespace
{
	CConfig MakeConfig()
	{
		CConfig Config;
		// 实时游玩用的显示配置：故意与回放配置取不同值，才能分辨出到底读了哪一组。
		Config.m_ClShowDirection = 3;
		Config.m_ClVideoShowDirection = 0;
		Config.m_ClNamePlatesStrong = 2;
		Config.m_QmNameplateHookStrongWeakScope = 1;
		Config.m_ClShowhud = 1;
		Config.m_ClVideoShowhud = 0;
		Config.m_ClShowChat = 1;
		Config.m_ClVideoShowChat = 0;
		Config.m_Debug = 0;

		Config.m_QmDemoShowDirection = 2;
		Config.m_QmDemoShowStrongWeak = 1;
		Config.m_QmDemoStrongWeakScope = 4;
		Config.m_QmDemoShowHud = 1;
		Config.m_QmDemoShowChat = 0;
		return Config;
	}
}

TEST(QmDemoDisplay, DemoPlaybackReadsTheDemoSpecificOptions)
{
	const CConfig Config = MakeConfig();
	const auto Settings = qm_demo_display::Resolve(Config, true, false);
	EXPECT_EQ(Settings.m_Direction, 2);
	EXPECT_EQ(Settings.m_StrongWeak, 1);
	EXPECT_EQ(Settings.m_StrongWeakScope, 4);
	EXPECT_TRUE(Settings.m_Hud);
	EXPECT_FALSE(Settings.m_Chat);
}

TEST(QmDemoDisplay, DemoPlaybackIgnoresVideoRenderingAndKeepsDemoOptions)
{
	// 边回放边录视频时仍用回放选项：两处渲染的是同一段回放画面。
	const CConfig Config = MakeConfig();
	const auto Settings = qm_demo_display::Resolve(Config, true, true);
	EXPECT_EQ(Settings.m_Direction, Config.m_QmDemoShowDirection);
	EXPECT_EQ(Settings.m_StrongWeak, Config.m_QmDemoShowStrongWeak);
	EXPECT_EQ(Settings.m_StrongWeakScope, Config.m_QmDemoStrongWeakScope);
	EXPECT_EQ(Settings.m_Hud, Config.m_QmDemoShowHud != 0);
	EXPECT_EQ(Settings.m_Chat, Config.m_QmDemoShowChat != 0);
}

TEST(QmDemoDisplay, LivePlayUsesTheLiveOptions)
{
	const CConfig Config = MakeConfig();
	const auto Settings = qm_demo_display::Resolve(Config, false, false);
	EXPECT_EQ(Settings.m_Direction, Config.m_ClShowDirection);
	EXPECT_EQ(Settings.m_StrongWeak, Config.m_ClNamePlatesStrong);
	EXPECT_EQ(Settings.m_StrongWeakScope, Config.m_QmNameplateHookStrongWeakScope);
	EXPECT_TRUE(Settings.m_Hud);
	EXPECT_TRUE(Settings.m_Chat);
}

TEST(QmDemoDisplay, VideoRenderingUsesTheVideoOptionsOutsideDemoPlayback)
{
	const CConfig Config = MakeConfig();
	const auto Settings = qm_demo_display::Resolve(Config, false, true);
	EXPECT_EQ(Settings.m_Direction, Config.m_ClVideoShowDirection);
	EXPECT_FALSE(Settings.m_Hud);
	EXPECT_FALSE(Settings.m_Chat);
	// 强弱钩与作用范围没有 video 专用项，录像时仍读常规配置。
	EXPECT_EQ(Settings.m_StrongWeak, Config.m_ClNamePlatesStrong);
	EXPECT_EQ(Settings.m_StrongWeakScope, Config.m_QmNameplateHookStrongWeakScope);
}

TEST(QmDemoDisplay, DefaultsMatchTheDocumentedValues)
{
	EXPECT_EQ(DefaultConfig::QmDemoShowDirection, 1);
	EXPECT_EQ(DefaultConfig::QmDemoShowStrongWeak, 0);
	EXPECT_EQ(DefaultConfig::QmDemoStrongWeakScope, 4);
	EXPECT_EQ(DefaultConfig::QmDemoShowHud, 0);
	EXPECT_EQ(DefaultConfig::QmDemoShowChat, 1);
}

TEST(QmDemoDisplay, DisabledDemoStrongWeakHidesIconsNumbersAndRow)
{
	CConfig Config = MakeConfig();
	Config.m_QmDemoShowStrongWeak = 0;
	Config.m_QmNameplateHookStrongWeakScope = QM_HOOK_STRONG_WEAK_SCOPE_ALL;
	for(const bool VideoRendering : {false, true})
	{
		for(const int Debug : {0, 1})
		{
			SCOPED_TRACE(::testing::Message() << "video=" << VideoRendering << " debug=" << Debug);
			Config.m_Debug = Debug;
			const auto Settings = qm_demo_display::Resolve(Config, true, VideoRendering);
			EXPECT_FALSE(Settings.StrongWeakEnabled());
			EXPECT_FALSE(Settings.ShowStrongWeakId());
			EXPECT_FALSE(Settings.ShowStrongWeak(true, false, false));
			EXPECT_FALSE(Settings.ShowStrongWeak(false, true, false));
			EXPECT_FALSE(Settings.ShowStrongWeak(false, false, true));
		}
	}
}

TEST(QmDemoDisplay, DemoStrongWeakModeChangesApplyWithoutChangingLiveOptions)
{
	CConfig Config = MakeConfig();
	Config.m_ClNamePlatesStrong = 0;
	struct SCase
	{
		int m_Mode;
		bool m_Enabled;
		bool m_ShowId;
	};
	const SCase aCases[] = {{1, true, false}, {2, true, true}, {0, false, false}, {2, true, true}, {1, true, false}, {0, false, false}};
	for(const bool VideoRendering : {false, true})
	{
		for(const auto &Case : aCases)
		{
			SCOPED_TRACE(::testing::Message() << "video=" << VideoRendering << " mode=" << Case.m_Mode);
			Config.m_QmDemoShowStrongWeak = Case.m_Mode;
			const auto Settings = qm_demo_display::Resolve(Config, true, VideoRendering);
			EXPECT_EQ(Settings.StrongWeakEnabled(), Case.m_Enabled);
			EXPECT_EQ(Settings.ShowStrongWeakId(), Case.m_ShowId);
			EXPECT_EQ(Settings.ShowStrongWeak(true, false, false), Case.m_Enabled);
			EXPECT_EQ(Settings.ShowStrongWeak(false, true, false), Case.m_Enabled);
			EXPECT_EQ(Settings.ShowStrongWeak(false, false, true), Case.m_Enabled);
		}
	}
	EXPECT_EQ(Config.m_ClNamePlatesStrong, 0);
	EXPECT_EQ(Config.m_QmNameplateHookStrongWeakScope, QM_HOOK_STRONG_WEAK_SCOPE_OTHERS);
}

TEST(QmDemoDisplay, DemoStrongWeakScopeFiltersIconsUsingDemoScope)
{
	CConfig Config = MakeConfig();
	Config.m_Debug = 1;
	Config.m_QmNameplateHookStrongWeakScope = QM_HOOK_STRONG_WEAK_SCOPE_ALL;
	struct SCase
	{
		int m_Scope;
		bool m_Self;
		bool m_Strong;
		bool m_Weak;
	};
	const SCase aCases[] = {
		{QM_HOOK_STRONG_WEAK_SCOPE_SELF, true, false, false},
		{QM_HOOK_STRONG_WEAK_SCOPE_OTHERS, false, true, true},
		{QM_HOOK_STRONG_WEAK_SCOPE_STRONG, false, true, false},
		{QM_HOOK_STRONG_WEAK_SCOPE_WEAK, false, false, true},
		{QM_HOOK_STRONG_WEAK_SCOPE_ALL, true, true, true}};
	for(const bool VideoRendering : {false, true})
	{
		for(const auto &Case : aCases)
		{
			SCOPED_TRACE(::testing::Message() << "video=" << VideoRendering << " scope=" << Case.m_Scope);
			Config.m_QmDemoStrongWeakScope = Case.m_Scope;
			const auto Settings = qm_demo_display::Resolve(Config, true, VideoRendering);
			EXPECT_TRUE(Settings.StrongWeakEnabled());
			EXPECT_FALSE(Settings.ShowStrongWeakId());
			EXPECT_EQ(Settings.ShowStrongWeak(true, false, false), Case.m_Self);
			EXPECT_EQ(Settings.ShowStrongWeak(false, true, false), Case.m_Strong);
			EXPECT_EQ(Settings.ShowStrongWeak(false, false, true), Case.m_Weak);
		}
	}
}

TEST(QmDemoDisplay, LeavingDemoUsesLiveStrongWeakSettings)
{
	CConfig Config = MakeConfig();
	Config.m_QmDemoShowStrongWeak = 0;
	Config.m_QmNameplateHookStrongWeakScope = QM_HOOK_STRONG_WEAK_SCOPE_WEAK;
	for(const bool VideoRendering : {false, true})
	{
		SCOPED_TRACE(VideoRendering);
		EXPECT_FALSE(qm_demo_display::Resolve(Config, true, VideoRendering).StrongWeakEnabled());
		const auto Settings = qm_demo_display::Resolve(Config, false, VideoRendering);
		EXPECT_TRUE(Settings.StrongWeakEnabled());
		EXPECT_TRUE(Settings.ShowStrongWeakId());
		EXPECT_TRUE(Settings.ShowStrongWeak(true, false, false));
		EXPECT_FALSE(Settings.ShowStrongWeak(false, true, false));
		EXPECT_TRUE(Settings.ShowStrongWeak(false, false, true));
	}
}

TEST(QmDemoDisplay, LiveDebugStillShowsStrongWeakWhenConfiguredOff)
{
	CConfig Config = MakeConfig();
	Config.m_ClNamePlatesStrong = 0;
	Config.m_Debug = 1;
	for(const bool VideoRendering : {false, true})
	{
		SCOPED_TRACE(VideoRendering);
		const auto Settings = qm_demo_display::Resolve(Config, false, VideoRendering);
		EXPECT_TRUE(Settings.StrongWeakEnabled());
		EXPECT_TRUE(Settings.ShowStrongWeakId());
		EXPECT_TRUE(Settings.ShowStrongWeak(true, false, false));
		EXPECT_TRUE(Settings.ShowStrongWeak(false, true, false));
		EXPECT_TRUE(Settings.ShowStrongWeak(false, false, true));
	}
}
