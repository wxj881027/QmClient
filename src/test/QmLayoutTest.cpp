#include "test.h"

#include <game/client/QmUi/QmLayout.h>

#include <gtest/gtest.h>

#include <vector>

TEST(UiV2Layout, RowPaddingGapAndPosition)
{
	CUiV2LayoutEngine Engine;
	SUiStyle ContainerStyle;
	ContainerStyle.m_Axis = EUiAxis::ROW;
	ContainerStyle.m_Gap = 5.0f;
	ContainerStyle.m_Padding = {10.0f, 10.0f, 10.0f, 10.0f};
	ContainerStyle.m_AlignItems = EUiAlign::START;

	SUiLayoutBox ContainerBox{0.0f, 0.0f, 200.0f, 100.0f};

	std::vector<SUiLayoutChild> vChildren(2);
	vChildren[0].m_Style.m_Width = SUiLength::Px(50.0f);
	vChildren[0].m_Style.m_Height = SUiLength::Px(20.0f);
	vChildren[1].m_Style.m_Width = SUiLength::Px(50.0f);
	vChildren[1].m_Style.m_Height = SUiLength::Px(20.0f);

	Engine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_X, 10.0f);
	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_Y, 10.0f);
	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_W, 50.0f);
	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_H, 20.0f);

	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_X, 65.0f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_Y, 10.0f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_W, 50.0f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_H, 20.0f);
}

TEST(UiV2Layout, RowFlexDistribution)
{
	CUiV2LayoutEngine Engine;
	SUiStyle ContainerStyle;
	ContainerStyle.m_Axis = EUiAxis::ROW;
	ContainerStyle.m_Gap = 10.0f;

	SUiLayoutBox ContainerBox{0.0f, 0.0f, 230.0f, 40.0f};

	std::vector<SUiLayoutChild> vChildren(3);
	vChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
	vChildren[0].m_Style.m_Height = SUiLength::Px(20.0f);
	vChildren[1].m_Style.m_Width = SUiLength::Flex(2.0f);
	vChildren[1].m_Style.m_Height = SUiLength::Px(20.0f);
	vChildren[2].m_Style.m_Width = SUiLength::Px(30.0f);
	vChildren[2].m_Style.m_Height = SUiLength::Px(20.0f);

	Engine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_W, 60.0f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_W, 120.0f);
	EXPECT_FLOAT_EQ(vChildren[2].m_Box.m_W, 30.0f);
	EXPECT_FLOAT_EQ(vChildren[2].m_Box.m_X, 200.0f);
}

TEST(UiV2Layout, ColumnJustifyCenter)
{
	CUiV2LayoutEngine Engine;
	SUiStyle ContainerStyle;
	ContainerStyle.m_Axis = EUiAxis::COLUMN;
	ContainerStyle.m_Gap = 10.0f;
	ContainerStyle.m_JustifyContent = EUiAlign::CENTER;

	SUiLayoutBox ContainerBox{0.0f, 0.0f, 80.0f, 100.0f};

	std::vector<SUiLayoutChild> vChildren(2);
	vChildren[0].m_Style.m_Width = SUiLength::Px(20.0f);
	vChildren[0].m_Style.m_Height = SUiLength::Px(20.0f);
	vChildren[1].m_Style.m_Width = SUiLength::Px(20.0f);
	vChildren[1].m_Style.m_Height = SUiLength::Px(20.0f);

	Engine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_Y, 25.0f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_Y, 55.0f);
}

TEST(UiV2Layout, AlignStretchExpandsCrossAxis)
{
	CUiV2LayoutEngine Engine;
	SUiStyle ContainerStyle;
	ContainerStyle.m_Axis = EUiAxis::ROW;
	ContainerStyle.m_Padding = {0.0f, 10.0f, 0.0f, 10.0f};
	ContainerStyle.m_AlignItems = EUiAlign::STRETCH;

	SUiLayoutBox ContainerBox{0.0f, 0.0f, 100.0f, 100.0f};

	std::vector<SUiLayoutChild> vChildren(1);
	vChildren[0].m_Style.m_Width = SUiLength::Px(20.0f);
	vChildren[0].m_Style.m_Height = SUiLength::Auto();

	Engine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_H, 80.0f);
	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_Y, 10.0f);
}

TEST(UiV2Layout, ApplyConstraintsMinMaxPercent)
{
	CUiV2LayoutEngine Engine;
	SUiStyle Style;
	Style.m_Width = SUiLength::Percent(0.5f);
	Style.m_Height = SUiLength::Px(100.0f);
	Style.m_MinWidth = SUiLength::Px(120.0f);
	Style.m_MaxWidth = SUiLength::Px(180.0f);
	Style.m_MaxHeight = SUiLength::Px(70.0f);

	SUiLayoutBox Parent{0.0f, 0.0f, 300.0f, 300.0f};
	const SUiLayoutBox Box = Engine.ApplyConstraints(Style, Parent);

	EXPECT_FLOAT_EQ(Box.m_W, 150.0f);
	EXPECT_FLOAT_EQ(Box.m_H, 70.0f);
}

TEST(UiV2Layout, ScoreboardTeamColumnsWithGap)
{
	CUiV2LayoutEngine Engine;
	SUiStyle ContainerStyle;
	ContainerStyle.m_Axis = EUiAxis::ROW;
	ContainerStyle.m_Gap = 7.5f;
	ContainerStyle.m_AlignItems = EUiAlign::STRETCH;

	SUiLayoutBox ContainerBox{0.0f, 0.0f, 850.0f, 385.0f};

	std::vector<SUiLayoutChild> vChildren(2);
	vChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
	vChildren[1].m_Style.m_Width = SUiLength::Flex(1.0f);

	Engine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_X, 0.0f);
	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_W, 421.25f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_X, 428.75f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_W, 421.25f);
}

TEST(UiV2Layout, ScoreboardThreeColumnsEqualWidth)
{
	CUiV2LayoutEngine Engine;
	SUiStyle ContainerStyle;
	ContainerStyle.m_Axis = EUiAxis::ROW;
	ContainerStyle.m_AlignItems = EUiAlign::STRETCH;

	SUiLayoutBox ContainerBox{0.0f, 0.0f, 900.0f, 320.0f};

	std::vector<SUiLayoutChild> vChildren(3);
	vChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
	vChildren[1].m_Style.m_Width = SUiLength::Flex(1.0f);
	vChildren[2].m_Style.m_Width = SUiLength::Flex(1.0f);

	Engine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_W, 300.0f);
	EXPECT_FLOAT_EQ(vChildren[1].m_Box.m_X, 300.0f);
	EXPECT_FLOAT_EQ(vChildren[2].m_Box.m_X, 600.0f);
}

TEST(UiV2Layout, ScoreboardSoundMuteVerticalButtons)
{
	CUiV2LayoutEngine Engine;
	SUiStyle ContainerStyle;
	ContainerStyle.m_Axis = EUiAxis::COLUMN;
	ContainerStyle.m_Gap = 4.0f;
	ContainerStyle.m_AlignItems = EUiAlign::STRETCH;

	SUiLayoutBox ContainerBox{0.0f, 0.0f, 22.0f, 230.0f};

	std::vector<SUiLayoutChild> vChildren(9);
	for(SUiLayoutChild &Child : vChildren)
	{
		Child.m_Style.m_Height = SUiLength::Px(22.0f);
	}

	Engine.ComputeChildren(ContainerStyle, ContainerBox, vChildren);

	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_X, 0.0f);
	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_W, 22.0f);
	EXPECT_FLOAT_EQ(vChildren[0].m_Box.m_Y, 0.0f);
	EXPECT_FLOAT_EQ(vChildren[8].m_Box.m_Y, 208.0f);
}
