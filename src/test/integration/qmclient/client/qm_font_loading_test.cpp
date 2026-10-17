#include <test/support/qm_real_ui_fixture.h>

class QmFontLoading : public qm_ui_test::CRealUiFixture
{
};

TEST_F(QmFontLoading, InvalidPreviewKeepsInstalledFontsAndExistingTextUsable)
{
	auto *pText = m_Ui.TextRender();
	ASSERT_TRUE(pText->QmHasCustomFace("DejaVu Sans"));
	const auto vFamilies = *pText->GetCustomFaces();
	const float Width = pText->TextWidth(16.0f, "Font lifetime");
	ASSERT_GT(Width, 0.0f);
	// 隔离存储中的随包索引是有效文件，但其内容不是字体。
	EXPECT_FALSE(pText->QmEnsurePreviewFace("Uninstalled preview fixture", "fonts/index.json"));
	EXPECT_EQ(*pText->GetCustomFaces(), vFamilies);
	EXPECT_TRUE(pText->QmHasCustomFace("DejaVu Sans"));
	EXPECT_FLOAT_EQ(pText->TextWidth(16.0f, "Font lifetime"), Width);
}

TEST_F(QmFontLoading, RepeatedReloadPreservesFamilyAndStyleLists)
{
	auto *pText = m_Ui.TextRender();
	const auto vFamilies = *pText->GetCustomFaces();
	const auto vStyles = *pText->GetCustomFontStyles("DejaVu Sans");
	ASSERT_FALSE(vFamilies.empty());
	ASSERT_FALSE(vStyles.empty());
	pText->ReloadCustomFonts();
	pText->ReloadCustomFonts();
	EXPECT_EQ(*pText->GetCustomFaces(), vFamilies);
	EXPECT_EQ(*pText->GetCustomFontStyles("DejaVu Sans"), vStyles);
	EXPECT_GT(pText->TextWidth(16.0f, "Reloaded font"), 0.0f);
}
