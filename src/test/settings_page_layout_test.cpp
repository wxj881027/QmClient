#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/cards/QmCardCatalogSkinMetrics.h>

#include <gtest/gtest.h>

TEST(SettingsPageLayout, DynamicVisualCardHeightsUseSharedMetrics)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, false, false), 2.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, false, true), 4.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, true, false), 7.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, true, true), 8.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualCollisionHitboxHeight(Metrics, false), Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualCollisionHitboxHeight(Metrics, true), 16.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualFocusModeHeight(Metrics), 16.0f * Metrics.m_RowStep + 3.0f * (Metrics.m_SmallSize + Metrics.m_LineSpacing) + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualSkinAppearanceHeight(Metrics), 9.0f * Metrics.m_RowStep + 2.0f * (Metrics.m_SmallSize + Metrics.m_LineSpacing));
	EXPECT_FLOAT_EQ(ResolveQmVisualSkinTransitionHeight(Metrics, true) - ResolveQmVisualSkinTransitionHeight(Metrics, false), 5.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualSkinTransitionHeight(Metrics, false), 2.0f * Metrics.m_RowStep);
}

TEST(SettingsPageLayout, GeneralDynamicCameraConsumesNoHiddenRowWhenCollapsed)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const float Collapsed = ResolveSettingsGeneralGameContentHeight(Metrics, false);
	const float Expanded = ResolveSettingsGeneralGameContentHeight(Metrics, true);
	EXPECT_FLOAT_EQ(Collapsed, ResolveSettingsRowsHeight(3, Metrics.m_LineHeight, Metrics.m_LineSpacing));
	EXPECT_FLOAT_EQ(Expanded, ResolveSettingsRowsHeight(4, Metrics.m_LineHeight, Metrics.m_LineSpacing));
	EXPECT_FLOAT_EQ(Expanded - Collapsed, Metrics.m_RowStep);
}
