#include <game/client/QmUi/SettingsSearchReveal.h>
#include <game/client/QmUi/SettingsSearchHighlight.h>
#include <game/client/QmUi/UiSurfaceText.h>

#include <gtest/gtest.h>

// 回归：搜索页在卡片头部点击 Locate 时，Request 会清空预览容器；同一帧
// 卡片内容仍会访问当前卡的键。键指针必须随之改指向目标键，不能悬垂。
TEST(SettingsSearchReveal, RequestWhileCardIsRenderingSwitchesToTargetKeys)
{
	qm_settings_reveal::Clear();
	qm_settings_reveal::SetPreviews({{"qm:card", {"row-a"}}});
	qm_settings_reveal::BeginCard("qm:card", nullptr, false);
	ASSERT_TRUE(qm_settings_reveal::ShouldReveal("row-a", nullptr));

	qm_settings_reveal::Request("qm:card", {"row-a", "row-b"});

	// 预览容器已被清空，当前卡必须使用 Request 刚复制的目标键。
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("row-a", nullptr));
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("row-b", nullptr));
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("row-c", nullptr));

	qm_settings_reveal::EndCard();
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("row-a", nullptr));
	qm_settings_reveal::Clear();
}

TEST(SettingsSearchReveal, ReplacingPreviewsDoesNotLeaveDanglingKeys)
{
	qm_settings_reveal::Clear();
	qm_settings_reveal::SetPreviews({{"qm:card", {"row-a"}}});
	qm_settings_reveal::BeginCard("qm:card", nullptr, false);
	ASSERT_TRUE(qm_settings_reveal::ShouldReveal("row-a", nullptr));

	// 预览整体替换后，旧指针必须失效；BeginCard 下一次重新绑定新预览。
	qm_settings_reveal::SetPreviews({{"qm:card", {"row-b"}}});
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("row-a", nullptr));

	qm_settings_reveal::EndCard();
	qm_settings_reveal::BeginCard("qm:card", nullptr, false);
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("row-a", nullptr));
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("row-b", nullptr));
	qm_settings_reveal::EndCard();
	qm_settings_reveal::Clear();
}

TEST(SettingsSearchHighlight, MiddleGraySurfaceFallsBackToReadableForeground)
{
	const ColorRGBA Surface{0.70f, 0.70f, 0.70f, 1.0f};
	CUiScopedSurfaceText Scope(nullptr, Surface);
	const ColorRGBA Match = qm_settings_highlight::ResolveMatchColor();
	const float Contrast = qm_settings_highlight::ContrastRatio(Surface, Match);
	EXPECT_GE(Contrast, 4.5f);
}

TEST(SettingsSearchHighlight, OppositeEndsKeepTheYellowMatchTokens)
{
	{
		const ColorRGBA Surface{1.0f, 1.0f, 1.0f, 1.0f};
		CUiScopedSurfaceText Scope(nullptr, Surface);
		const ColorRGBA Match = qm_settings_highlight::ResolveMatchColor();
		EXPECT_EQ(Match, ui_token::color::SEARCH_MATCH_ON_LIGHT);
	}
	{
		const ColorRGBA Surface{0.02f, 0.02f, 0.02f, 1.0f};
		CUiScopedSurfaceText Scope(nullptr, Surface);
		const ColorRGBA Match = qm_settings_highlight::ResolveMatchColor();
		EXPECT_EQ(Match, ui_token::color::SEARCH_MATCH_ON_DARK);
	}
}
