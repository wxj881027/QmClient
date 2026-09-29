// 集成测试：滑条输入与服务器浏览器筛选配置之间的转换。
#include <game/client/QmUi/UiDiscreteSlider.h>
#include <game/client/components/qmclient/map_vote_difficulty.h>

#include <gtest/gtest.h>

TEST(MapBrowserFilterSlider, SelectingFirstStopClearsCombinedCustomFilters)
{
	int EmptyOnly = 1;
	int StarMask = (1 << 2) | (1 << 4);
	const int Level = QmMapVotes::MapBrowserFilterLevel(EmptyOnly, StarMask);
	ASSERT_EQ(Level, -1);

	const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry({0.0f, 0.0f, 240.0f, 20.0f});
	ui_widget::SDiscreteSliderState State;
	ui_widget::SDiscreteSliderInput Input;
	Input.m_MouseX = Geometry.Position(0.0f);
	Input.m_Pressed = Input.m_Down = Input.m_Hovered = true;
	const auto Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, Level, QmMapVotes::MAP_BROWSER_FILTER_LEVEL_NONE, QmMapVotes::MAP_BROWSER_FILTER_LEVEL_LAST_STAR);
	ASSERT_TRUE(Result.m_Changed);
	QmMapVotes::ApplyMapBrowserFilterLevel(Result.m_Value, EmptyOnly, StarMask);

	EXPECT_EQ(EmptyOnly, 0);
	EXPECT_EQ(StarMask, 0);
	EXPECT_TRUE(QmMapVotes::MatchesFilter(-1, 0, EmptyOnly, StarMask, false, false));
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 4, EmptyOnly, StarMask, false, false));
}

TEST(MapBrowserFilterSlider, IdleCustomSelectionDoesNotRequestConfigRewrite)
{
	const int Level = QmMapVotes::MapBrowserFilterLevel(false, (1 << 1) | (1 << 5));
	const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry({0.0f, 0.0f, 240.0f, 20.0f});
	ui_widget::SDiscreteSliderState State;
	ui_widget::SDiscreteSliderInput Input;
	Input.m_Hovered = true;
	const auto Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, Level, QmMapVotes::MAP_BROWSER_FILTER_LEVEL_NONE, QmMapVotes::MAP_BROWSER_FILTER_LEVEL_LAST_STAR);
	EXPECT_FALSE(Result.m_Changed);
	EXPECT_EQ(Result.m_Value, -1);
}

TEST(MapBrowserFilterSlider, OutOfRangeLevelKeepsConfiguredFilters)
{
	// 越界档位不表达任何筛选：即使被误传到写回接口，也不能清掉用户已配好的筛选。
	int EmptyOnly = 1;
	int StarMask = (1 << 2) | (1 << 4);
	QmMapVotes::ApplyMapBrowserFilterLevel(-1, EmptyOnly, StarMask);
	EXPECT_EQ(EmptyOnly, 1);
	EXPECT_EQ(StarMask, (1 << 2) | (1 << 4));
	EXPECT_TRUE(QmMapVotes::MatchesFilter(4, 0, EmptyOnly, StarMask, false, false));
}

TEST(MapBrowserFilterSlider, DragFromEmptyToStarsUpdatesMatchingBeforeRelease)
{
	int EmptyOnly = 0;
	int StarMask = 0;
	const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry({0.0f, 0.0f, 240.0f, 20.0f});
	ui_widget::SDiscreteSliderState State;
	ui_widget::SDiscreteSliderInput Input;
	Input.m_MouseX = Geometry.Position(1.0f / 6.0f);
	Input.m_Pressed = Input.m_Down = Input.m_Hovered = true;
	auto Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, QmMapVotes::MapBrowserFilterLevel(EmptyOnly, StarMask), 0, 6);
	ASSERT_TRUE(Result.m_Changed);
	QmMapVotes::ApplyMapBrowserFilterLevel(Result.m_Value, EmptyOnly, StarMask);
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 0, EmptyOnly, StarMask, false, false));
	EXPECT_FALSE(QmMapVotes::MatchesFilter(3, 4, EmptyOnly, StarMask, false, false));

	Input.m_Pressed = false;
	Input.m_Active = Result.m_Active;
	Input.m_MouseX = Geometry.Position(4.0f / 6.0f);
	Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, QmMapVotes::MapBrowserFilterLevel(EmptyOnly, StarMask), 0, 6);
	ASSERT_TRUE(Result.m_Changed);
	ASSERT_TRUE(Result.m_Active);
	QmMapVotes::ApplyMapBrowserFilterLevel(Result.m_Value, EmptyOnly, StarMask);
	EXPECT_EQ(EmptyOnly, 0);
	EXPECT_EQ(StarMask, 1 << 3);
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 4, EmptyOnly, StarMask, false, false));
	EXPECT_FALSE(QmMapVotes::MatchesFilter(2, 4, EmptyOnly, StarMask, false, false));
	// 收藏仍是独立条件，不会被滑条选档清除。
	EXPECT_FALSE(QmMapVotes::MatchesFilter(3, 4, EmptyOnly, StarMask, true, false));
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 4, EmptyOnly, StarMask, true, true));
}
