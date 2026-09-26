#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <sstream>
#include <string>

TEST(QmHudEditorContract, EdgeAnchoringUsesDragSnapDistance)
{
	std::ifstream File(TestSourcePath("src/game/client/components/hud_editor.cpp"), std::ios::binary);
	ASSERT_TRUE(File.good());
	std::ostringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	EXPECT_NE(Source.find("HUD_EDITOR_EDGE_ANCHOR_DISTANCE = QmHudEditor::SNAP_DISTANCE"), std::string::npos);
	EXPECT_EQ(Source.find("HUD_EDITOR_EDGE_ANCHOR_DISTANCE = QmHudEditor::EPSILON"), std::string::npos);
}
