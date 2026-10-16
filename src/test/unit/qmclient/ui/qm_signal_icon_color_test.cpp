// 连接质量图标独立颜色策略：档位语义色 / 跟随全局图标色 / 自定义色三模式。
// 根因回归：DrawQmIcon 内部用全局图标色覆盖调用方颜色，hud 按网络档位传的
// 语义色（白/琥珀/暗灰）被架空；策略必须把着色决定权交还配置模式。
#include <engine/shared/config.h>

#include <game/client/qm_icon.h>

#include <gtest/gtest.h>

namespace
{
	class CQmSignalIconColorTest : public ::testing::Test
	{
		int m_Mode = g_Config.m_QmHudIslandSignalColorMode;
		unsigned m_Custom = g_Config.m_QmHudIslandSignalColor;
		int m_Preset = g_Config.m_QmUiIconColor;
		int m_CustomEnabled = g_Config.m_QmUiIconCustomColorEnabled;
		unsigned m_GlobalCustom = g_Config.m_QmUiIconCustomColor;

	protected:
		void TearDown() override
		{
			g_Config.m_QmHudIslandSignalColorMode = m_Mode;
			g_Config.m_QmHudIslandSignalColor = m_Custom;
			g_Config.m_QmUiIconColor = m_Preset;
			g_Config.m_QmUiIconCustomColorEnabled = m_CustomEnabled;
			g_Config.m_QmUiIconCustomColor = m_GlobalCustom;
		}
	};
}

TEST_F(CQmSignalIconColorTest, QualityModeKeepsCallerTierColor)
{
	g_Config.m_QmHudIslandSignalColorMode = QM_SIGNAL_COLOR_QUALITY;
	const ColorRGBA Tier(1.0f, 0.72f, 0.30f, 0.85f);
	EXPECT_EQ(ConfiguredQmSignalIconColor(Tier, Tier), Tier);
	// 非法配置值回退档位模式。
	g_Config.m_QmHudIslandSignalColorMode = 0;
	EXPECT_EQ(ConfiguredQmSignalIconColor(Tier, Tier), Tier);
	g_Config.m_QmHudIslandSignalColorMode = 99;
	EXPECT_EQ(ConfiguredQmSignalIconColor(Tier, Tier), Tier);
}

TEST_F(CQmSignalIconColorTest, GlobalModeFollowsUiIconPresetAndKeepsTierAlpha)
{
	g_Config.m_QmHudIslandSignalColorMode = QM_SIGNAL_COLOR_GLOBAL;
	const ColorRGBA Tier(1.0f, 0.72f, 0.30f, 0.85f);
	g_Config.m_QmUiIconColor = 1;
	EXPECT_FLOAT_EQ(ConfiguredQmSignalIconColor(Tier, Tier).r, 1.0f);
	EXPECT_FLOAT_EQ(ConfiguredQmSignalIconColor(Tier, Tier).g, 1.0f);
	EXPECT_FLOAT_EQ(ConfiguredQmSignalIconColor(Tier, Tier).b, 1.0f);
	EXPECT_FLOAT_EQ(ConfiguredQmSignalIconColor(Tier, Tier).a, 0.85f);
	g_Config.m_QmUiIconColor = 2;
	const ColorRGBA Black = ConfiguredQmSignalIconColor(Tier, Tier);
	EXPECT_FLOAT_EQ(Black.r, 0.0f);
	EXPECT_FLOAT_EQ(Black.g, 0.0f);
	EXPECT_FLOAT_EQ(Black.b, 0.0f);
	EXPECT_FLOAT_EQ(Black.a, 0.85f);
	// 全局自定义开关打开时采用全局自定义色。
	g_Config.m_QmUiIconCustomColorEnabled = 1;
	g_Config.m_QmUiIconCustomColor = ColorHSLA(0.37f, 0.8f, 0.45f).Pack(false);
	const ColorRGBA Expected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiIconCustomColor)).WithAlpha(0.85f);
	EXPECT_EQ(ConfiguredQmSignalIconColor(Tier, Tier), Expected);
}

TEST_F(CQmSignalIconColorTest, CustomModeUsesDedicatedConfigColor)
{
	g_Config.m_QmHudIslandSignalColorMode = QM_SIGNAL_COLOR_CUSTOM;
	g_Config.m_QmHudIslandSignalColor = ColorHSLA(0.0f, 1.0f, 0.5f).Pack(false);
	const ColorRGBA Tier(0.6f, 0.64f, 0.70f, 0.42f);
	const ColorRGBA Actual = ConfiguredQmSignalIconColor(Tier, Tier);
	const ColorRGBA Expected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHudIslandSignalColor));
	EXPECT_FLOAT_EQ(Actual.r, Expected.r);
	EXPECT_FLOAT_EQ(Actual.g, Expected.g);
	EXPECT_FLOAT_EQ(Actual.b, Expected.b);
	// alpha 沿用档位色的动画透明度，不被配置覆盖。
	EXPECT_FLOAT_EQ(Actual.a, 0.42f);
	// 自定义模式不受全局预设影响。
	g_Config.m_QmUiIconColor = 4;
	EXPECT_EQ(ConfiguredQmSignalIconColor(Tier, Tier), Actual);
}

TEST_F(CQmSignalIconColorTest, ModeIndexMatchesConfigValue)
{
	g_Config.m_QmHudIslandSignalColorMode = 2;
	EXPECT_EQ(QmSignalIconColorMode(), QM_SIGNAL_COLOR_GLOBAL);
	g_Config.m_QmHudIslandSignalColorMode = 3;
	EXPECT_EQ(QmSignalIconColorMode(), QM_SIGNAL_COLOR_CUSTOM);
}
