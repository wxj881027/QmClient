#include <game/client/QmUi/SettingsRowReveal.h>

#include <test/support/qm_real_ui_fixture.h>

using QmSettingsRevealText = qm_ui_test::CRealUiFixture;

TEST_F(QmSettingsRevealText, FailedInlineHintRestoresTheColorForTheNextControl)
{
	ITextRender &Text = *m_Ui.TextRender();
	const ColorRGBA CallerColor(0.2f, 0.3f, 0.4f, 0.5f);
	Text.TextColor(CallerColor);
	const CUIRect Row{20.0f, 40.0f, 180.0f, 20.0f};
	const auto DrawHint = [&] { DoSettingsInlineHint(&m_Ui, "predict-events", Row, 12.0f, "Sound", 11.0f); };
	const size_t Labels = Graphics().m_vTextSubmissions.size();
	Graphics().m_FailBufferContainerCreate = true;

	EXPECT_NO_THROW(DrawHint());
	ASSERT_FALSE(Graphics().m_FailBufferContainerCreate);
	EXPECT_EQ(Graphics().m_vTextSubmissions.size(), Labels);
	EXPECT_EQ(Text.GetTextColor(), CallerColor);

	ASSERT_NO_THROW(DrawHint());
	EXPECT_GT(Graphics().m_vTextSubmissions.size(), Labels);
}

TEST_F(QmSettingsRevealText, FailedSectionHeadingKeepsTheRowHeight)
{
	const SSettingsContentMetrics Metrics;
	const CUIRect Original{20.0f, 40.0f, 180.0f, 150.0f};
	const auto DrawHeading = [&] {
		CUIRect Content = Original;
		CSettingsContentLayoutFlow Flow(Content);
		DoSettingsSectionHeading(&m_Ui, Flow, "voice-status-title", "Voice", Metrics);
		return Content.y;
	};
	const float NormalNextRow = DrawHeading();
	ASSERT_GT(NormalNextRow, Original.y);
	const size_t Labels = Graphics().m_vTextSubmissions.size();
	Graphics().m_FailBufferContainerCreate = true;

	EXPECT_FLOAT_EQ(DrawHeading(), NormalNextRow);
	EXPECT_FALSE(Graphics().m_FailBufferContainerCreate);
	EXPECT_EQ(Graphics().m_vTextSubmissions.size(), Labels);
}
