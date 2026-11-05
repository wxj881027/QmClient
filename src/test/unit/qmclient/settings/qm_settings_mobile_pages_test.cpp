#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_runtime_cache.h>

#include <gtest/gtest.h>

// 手机端（Android/iOS）是观战与回放端：桌面专属页签不出现，观战相关的页签保留。
TEST(SettingsMobilePages, MobileHidesDesktopOnlyPages)
{
	EXPECT_TRUE(SettingsPageHiddenForMobile(CMenus::SETTINGS_TCLIENT, true));
	// 绑键、资源、DDNet 在手机端是需要的，不能被一起隐藏。
	EXPECT_FALSE(SettingsPageHiddenForMobile(CMenus::SETTINGS_CONTROLS, true));
	EXPECT_FALSE(SettingsPageHiddenForMobile(CMenus::SETTINGS_ASSETS, true));
	EXPECT_FALSE(SettingsPageHiddenForMobile(CMenus::SETTINGS_DDNET, true));
}

TEST(SettingsMobilePages, MobileKeepsSpectatorPages)
{
	const int aKept[] = {
		CMenus::SETTINGS_GENERAL,
		CMenus::SETTINGS_TEE,
		CMenus::SETTINGS_APPEARANCE,
		CMenus::SETTINGS_CONTROLS,
		CMenus::SETTINGS_GRAPHICS,
		CMenus::SETTINGS_SOUND,
		CMenus::SETTINGS_ASSETS,
		CMenus::SETTINGS_DDNET,
		CMenus::SETTINGS_QMCLIENT,
		CMenus::SETTINGS_SEARCH,
		CMenus::SETTINGS_CONTRIBUTORS,
	};
	for(const int Page : aKept)
	{
		EXPECT_FALSE(SettingsPageHiddenForMobile(Page, true)) << "page " << Page;
		EXPECT_TRUE(SettingsPageVisibleForPlatform(Page, true)) << "page " << Page;
	}
}

// 桌面端不受手机端策略影响：判定表在非移动平台必须全部放行。
TEST(SettingsMobilePages, DesktopHidesNothing)
{
	for(int Page = 0; Page < CMenus::SETTINGS_LENGTH; ++Page)
		EXPECT_FALSE(SettingsPageHiddenForMobile(Page, false)) << "page " << Page;
}

// 语言、玩家、配置档只从其他入口进入，左右栏在所有平台都不列出它们。
TEST(SettingsMobilePages, ProgrammaticPagesStayOutOfTabBar)
{
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_LANGUAGE, true));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_PLAYER, true));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_PROFILES, true));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_CONFIGS, true));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_LANGUAGE, false));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_CONFIGS, false));
}

// 越界索引仍要被挡掉，避免旧配置把页面索引写到范围外。
TEST(SettingsMobilePages, OutOfRangePagesAreNotVisible)
{
	EXPECT_FALSE(SettingsPageVisibleForPlatform(-1, true));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_LENGTH, true));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(-1, false));
	EXPECT_FALSE(SettingsPageVisibleForPlatform(CMenus::SETTINGS_LENGTH, false));
}
// 字体卡片从 TClient 挪到画面页：实现仍由 tclient 分支渲染，但不再出现在 TClient 页。
TEST(SettingsMobilePages, FontCardBelongsToGraphicsPage)
{
	const auto &vGraphics = qm_card_catalog::CatalogPageStableIds("graphics");
	const auto &vTClient = qm_card_catalog::CatalogPageStableIds("tclient");
	const bool InGraphics = std::any_of(vGraphics.begin(), vGraphics.end(), [](const char *pId) { return str_comp(pId, "tclient:font") == 0; });
	const bool InTClient = std::any_of(vTClient.begin(), vTClient.end(), [](const char *pId) { return str_comp(pId, "tclient:font") == 0; });
	EXPECT_TRUE(InGraphics);
	EXPECT_FALSE(InTClient);
}
// 当前移动端只改单列排版，字号与行高保持桌面基线；等排版允许再整体放大。
TEST(SettingsMobilePages, MobileTextScaleMatchesDesktopBaseline)
{
	const SSettingsContentMetrics Desktop = ResolveSettingsContentMetrics(900.0f, ResolveSettingsTextScale(false));
	const SSettingsContentMetrics Mobile = ResolveSettingsContentMetrics(900.0f, ResolveSettingsTextScale(true));
	EXPECT_FLOAT_EQ(Mobile.m_BodySize, Desktop.m_BodySize);
	EXPECT_FLOAT_EQ(Mobile.m_HeadlineSize, Desktop.m_HeadlineSize);
	EXPECT_FLOAT_EQ(Mobile.m_LineHeight, Desktop.m_LineHeight);
	EXPECT_FLOAT_EQ(Mobile.m_RowStep, Desktop.m_RowStep);
	EXPECT_FLOAT_EQ(Mobile.m_SmallSize, Desktop.m_SmallSize);
}

// 桌面缩放系数必须是恒等变换，否则桌面设置页会跟着手机一起变大。
TEST(SettingsMobilePages, DesktopTextScaleIsIdentity)
{
	EXPECT_FLOAT_EQ(ResolveSettingsTextScale(false), 1.0f);
	const SSettingsContentMetrics Legacy = ResolveSettingsContentMetrics(900.0f);
	const SSettingsContentMetrics Explicit = ResolveSettingsContentMetrics(900.0f, 1.0f);
	EXPECT_FLOAT_EQ(Legacy.m_BodySize, Explicit.m_BodySize);
	EXPECT_FLOAT_EQ(Legacy.m_LineHeight, Explicit.m_LineHeight);
}
// 手机端卡片必须单列；桌面端仍按宽度决定。
TEST(SettingsMobilePages, MobileLaysCardsInSingleColumn)
{
	const SSettingsPageLayoutFrame Frame = ResolveSettingsPageLayout(CUIRect{0.0f, 0.0f, 1600.0f, 900.0f}, false);
	if(QmSettingsMobilePlatform())
		EXPECT_FALSE(Frame.m_TwoColumns);
	else
		EXPECT_TRUE(Frame.m_TwoColumns);
}
