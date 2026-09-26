#include "test.h"

#include <engine/shared/config.h>

#include <game/client/components/qmclient/demo_display.h>

#include <gtest/gtest.h>

// 回放显示选项的纯解析：回放中读 qm_demo_*，其余情况读 cl_* / cl_video_*。
// 这一层决定了「回放预览与视频导出能看到什么」，直接调用生产函数验证取值来源。

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
