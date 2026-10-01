#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <sstream>
#include <string>

TEST(QmHudEditorContract, ScreenEdgeSnappingHasNoProximityRadius)
{
	// 相邻吸附半径是玩家可感知的手感合同：屏幕边只认重合，任何按距离吸附的常量都必须消失，
	// 该约束无法由纯运行时接口观察（命中依赖拖拽输入与 Ui 状态），故以静态合同固定。
	std::ifstream File(TestSourcePath("src/game/client/components/hud_editor.cpp"), std::ios::binary);
	ASSERT_TRUE(File.good());
	std::ostringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	EXPECT_EQ(Source.find("HudEditorEdgeSnapDistance"), std::string::npos);
	EXPECT_EQ(Source.find("HUD_EDITOR_EDGE_ANCHOR_DISTANCE"), std::string::npos);
	EXPECT_NE(Source.find("QmHudEditor::ResolveAxisSnapEx"), std::string::npos);
	EXPECT_NE(Source.find("HUD_EDITOR_EDGE_COINCIDENCE_DISTANCE"), std::string::npos);
}

TEST(QmHudEditorContract, ScreenEdgeCoincidenceSharesOneTolerance)
{
	std::ifstream File(TestSourcePath("src/game/client/components/hud_editor.h"), std::ios::binary);
	ASSERT_TRUE(File.good());
	std::ostringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Header = Buffer.str();
	EXPECT_NE(Header.find("EDGE_COINCIDENCE_DISTANCE = EPSILON"), std::string::npos);
	EXPECT_EQ(Header.find("MEDIA_ISLAND_EDGE_SNAP_DISTANCE"), std::string::npos);
}
