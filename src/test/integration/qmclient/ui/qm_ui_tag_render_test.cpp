#include <game/client/QmUi/UiTag.h>

#include <test/support/qm_real_ui_fixture.h>

using QmUiTagRender = qm_ui_test::CRealUiFixture;

TEST_F(QmUiTagRender, FailedLabelUploadKeepsThePillAndRestoresTextColor)
{
	ITextRender &Text = *m_Ui.TextRender();
	const ColorRGBA CallerColor(0.2f, 0.3f, 0.4f, 0.5f);
	Text.TextColor(CallerColor);
	const CUIRect Tag{20.0f, 40.0f, 120.0f, 20.0f};
	const auto DrawTag = [&] {
		ui_tag::DrawInRect(&m_Ui, Tag, "Follow", ui_tag::EColorMode::FIXED, {0.1f, 0.2f, 0.6f, 0.8f});
	};
	const size_t Fills = Graphics().m_vRoundedSubmissions.size();
	const size_t Labels = Graphics().m_vTextSubmissions.size();
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;

	EXPECT_NO_THROW(DrawTag());
	ASSERT_FALSE(Graphics().m_FailBufferContainerCreate);
	EXPECT_EQ(Graphics().m_vRoundedSubmissions.size(), Fills + 1);
	EXPECT_EQ(Graphics().m_vTextSubmissions.size(), Labels);
	EXPECT_EQ(Text.GetTextColor(), CallerColor);
	Text.QmTextFrameEnd();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);

	ASSERT_NO_THROW(DrawTag());
	EXPECT_GT(Graphics().m_vTextSubmissions.size(), Labels);
	EXPECT_EQ(Text.GetTextColor(), CallerColor);
}
