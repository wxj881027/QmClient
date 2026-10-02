#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <string>

TEST(QmIconGlyphContract, UsesPhosphorAndRestoresTextRenderState)
{
	const std::array<const char *, 3> apGlyphSources = {"src/game/client/QmUi/UiButtons.cpp", "src/game/client/QmUi/UiForms.cpp", "src/game/client/QmUi/SettingsCard.cpp"};
	for(const char *pPath : apGlyphSources)
	{
		const std::string Source = ReadTestSourceFile(pPath);
		EXPECT_NE(Source.find("GetFontPreset"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("SetFontPreset(PreviousPreset)"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("SetRenderFlags(PreviousFlags)"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("TextColor(PreviousColor)"), std::string::npos) << pPath;
	}
	const std::string SettingsCard = ReadTestSourceFile("src/game/client/QmUi/SettingsCard.cpp");
	const size_t CollapseButton = SettingsCard.find("void RenderSettingsCardCollapseButton");
	ASSERT_NE(CollapseButton, std::string::npos);
	EXPECT_NE(SettingsCard.find("SetFontPreset(EFontPreset::ICON_FONT_BOLD)", CollapseButton), std::string::npos);
	EXPECT_NE(SettingsCard.find("FontIcons::FONT_ICON_CHEVRON_DOWN", CollapseButton), std::string::npos);
	EXPECT_NE(SettingsCard.find("FontIcons::FONT_ICON_CHEVRON_UP", CollapseButton), std::string::npos);
	const std::string Text = ReadTestSourceFile("src/engine/client/text.cpp");
	EXPECT_NE(Text.find("if(m_FontPreset == EFontPreset::ICON_FONT)"), std::string::npos);
	EXPECT_NE(Text.find("m_pGlyphMap->SetFontPreset(EFontPreset::ICON_FONT);"), std::string::npos);
}
