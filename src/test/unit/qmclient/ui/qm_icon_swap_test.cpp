// 图标交换组件：资产名映射是纯函数，直接验证生产接口，避免烘焙文件名与运行时错位。
#include <game/client/QmUi/QmIconSwap.h>

#include <gtest/gtest.h>

TEST(QmIconSwap, ChevronAssetCoversOutlineWeightsOnly)
{
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::CHEVRON, 0), "qmclient/ui/chevron_swap_regular.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::CHEVRON, 1), "qmclient/ui/chevron_swap_bold.png");
	// 2 是旧配置的 Light 别名，与 4 共用同一套资产。
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::CHEVRON, 2), "qmclient/ui/chevron_swap_light.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::CHEVRON, 4), "qmclient/ui/chevron_swap_light.png");
	// Fill 的 chevron 是实心块，烘焙脚本跳过，运行时回落到交叉淡化。
	EXPECT_EQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::CHEVRON, 3), nullptr);
}

TEST(QmIconSwap, EyeKeepsHistoricAssetPrefix)
{
	// 眼睛的帧条沿用密码图标时期的文件名前缀，避免为了改名重新烘焙与同步数据清单。
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::EYE, 0), "qmclient/ui/password_icon_regular.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::EYE, 1), "qmclient/ui/password_icon_bold.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::EYE, 2), "qmclient/ui/password_icon_light.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::EYE, 4), "qmclient/ui/password_icon_light.png");
	EXPECT_EQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::EYE, 3), nullptr);
}

TEST(QmIconSwap, PlayPauseAssetCoversOutlineWeightsOnly)
{
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_PAUSE, 0), "qmclient/ui/play_pause_swap_regular.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_PAUSE, 1), "qmclient/ui/play_pause_swap_bold.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_PAUSE, 2), "qmclient/ui/play_pause_swap_light.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_PAUSE, 4), "qmclient/ui/play_pause_swap_light.png");
	// Fill 的 play/pause 是实心形，轮廓配对不成立，回落交叉淡化。
	EXPECT_EQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_PAUSE, 3), nullptr);
}

TEST(QmIconSwap, PlayStopAndPlusMinusAssetsCoverOutlineWeightsOnly)
{
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_STOP, 0), "qmclient/ui/play_stop_swap_regular.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_STOP, 1), "qmclient/ui/play_stop_swap_bold.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_STOP, 2), "qmclient/ui/play_stop_swap_light.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_STOP, 4), "qmclient/ui/play_stop_swap_light.png");
	EXPECT_EQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::PLAY_STOP, 3), nullptr);
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::SQUARE_PLUS_MINUS, 0), "qmclient/ui/plus_minus_swap_regular.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::SQUARE_PLUS_MINUS, 1), "qmclient/ui/plus_minus_swap_bold.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::SQUARE_PLUS_MINUS, 2), "qmclient/ui/plus_minus_swap_light.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::SQUARE_PLUS_MINUS, 4), "qmclient/ui/plus_minus_swap_light.png");
	EXPECT_EQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::SQUARE_PLUS_MINUS, 3), nullptr);
	// 收藏点亮：描边 ↔ Fill 实心，跨字面配对；Fill 字重恒实心无切换。
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::STAR_FAVORITE, 0), "qmclient/ui/star_favorite_swap_regular.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::STAR_FAVORITE, 1), "qmclient/ui/star_favorite_swap_bold.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::STAR_FAVORITE, 2), "qmclient/ui/star_favorite_swap_light.png");
	EXPECT_STREQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::STAR_FAVORITE, 4), "qmclient/ui/star_favorite_swap_light.png");
	EXPECT_EQ(ui_widget::IconSwapAssetName(EQmIconSwapPair::STAR_FAVORITE, 3), nullptr);
}

TEST(QmIconSwap, EveryPairAndWeightMapsToDistinctAsset)
{
	// 源资产每种字重一份：字重或交换对漏映射会让两处共用同一张帧条。
	const EQmIconSwapPair aPairs[] = {EQmIconSwapPair::EYE, EQmIconSwapPair::CHEVRON, EQmIconSwapPair::PLAY_PAUSE, EQmIconSwapPair::PLAY_STOP, EQmIconSwapPair::SQUARE_PLUS_MINUS, EQmIconSwapPair::STAR_FAVORITE};
	for(EQmIconSwapPair Pair : aPairs)
	{
		ASSERT_NE(ui_widget::IconSwapAssetName(Pair, 0), nullptr);
		ASSERT_NE(ui_widget::IconSwapAssetName(Pair, 1), nullptr);
		EXPECT_STRNE(ui_widget::IconSwapAssetName(Pair, 0), ui_widget::IconSwapAssetName(Pair, 1));
		EXPECT_STRNE(ui_widget::IconSwapAssetName(Pair, 1), ui_widget::IconSwapAssetName(Pair, 2));
	}
	for(EQmIconSwapPair PairA : aPairs)
		for(EQmIconSwapPair PairB : aPairs)
			if(PairA != PairB)
				EXPECT_STRNE(ui_widget::IconSwapAssetName(PairA, 1), ui_widget::IconSwapAssetName(PairB, 1));
}
