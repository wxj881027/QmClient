#include <game/client/QmUi/SettingsMenuPrewarmScope.h>
#include <game/client/QmUi/UiTokens.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	using QmSettingsTextRender = qm_ui_test::CRealUiFixture;
}

TEST_F(QmSettingsTextRender, LabelAndPreviewReuseDrawsInsideSettingsOnly)
{
	auto &Text = *m_Ui.TextRender();
	const CUIRect Rect{10, 20, 200, 18};
	UiGraphics()->m_TextBuffering = true;
	m_Ui.MapScreen();
	{
		const ITextRender::CScopedDrawCache Cache(Text);
		m_Ui.DoLabel(&Rect, "1920x1080", 11, TEXTALIGN_ML);
		CTextCursor Input;
		Input.SetPosition(Rect.TopLeft());
		Input.m_FontSize = 12;
		CTextCursor First = Input;
		Text.TextEx(&First, "Track preview");
		ASSERT_GT(First.m_GlyphCount, 0);
		const auto Built = UiGraphics()->m_BufferBytes;
		const auto Draws = UiGraphics()->m_vTextSubmissions.size();
		ASSERT_GT(Built, 0u);

		CTextCursor Again = Input;
		m_Ui.DoLabel(&Rect, "1920x1080", 11, TEXTALIGN_ML);
		Text.TextEx(&Again, "Track preview");
		EXPECT_EQ(Again, First);
		EXPECT_EQ(UiGraphics()->m_BufferBytes, Built);
		EXPECT_EQ(UiGraphics()->m_vTextSubmissions.size(), Draws + 2);

		m_Ui.DoLabel(&Rect, "2560x1440", 11, TEXTALIGN_ML);
		EXPECT_GT(UiGraphics()->m_BufferBytes, Built);
	}
	const auto Built = UiGraphics()->m_BufferBytes;
	m_Ui.DoLabel(&Rect, "2560x1440", 11, TEXTALIGN_ML);
	EXPECT_GT(UiGraphics()->m_BufferBytes, Built);
}

TEST_F(QmSettingsTextRender, ChangedTextColorRebuildsAndFailedUploadCanRetry)
{
	auto &Text = *m_Ui.TextRender();
	const ITextRender::CScopedDrawCache Cache(Text);
	m_Ui.MapScreen();
	Text.Text(8, 8, 12, "Preview");
	const auto Built = UiGraphics()->m_BufferBytes;
	Text.TextColor(0.2f, 0.7f, 1.0f, 1.0f);
	UiGraphics()->m_FailBufferObjectCreate = true;
	EXPECT_THROW(Text.Text(8, 8, 12, "Preview"), std::bad_alloc);
	Text.Text(8, 8, 12, "Preview");
	EXPECT_GT(UiGraphics()->m_BufferBytes, Built);
	const auto Retried = UiGraphics()->m_BufferBytes;
	Text.Text(8, 8, 12, "Preview");
	EXPECT_EQ(UiGraphics()->m_BufferBytes, Retried);
}

TEST_F(QmSettingsTextRender, FontPreviewKeepsItsOwnCachedDraw)
{
	auto &Text = *m_Ui.TextRender();
	const ITextRender::CScopedDrawCache Cache(Text);
	m_Ui.MapScreen();
	ASSERT_TRUE(Text.QmHasCustomFace("DejaVu Sans"));
	Text.Text(8, 8, 12, "Sample");
	const auto DefaultBytes = UiGraphics()->m_BufferBytes;
	Text.SetFontPreviewFace("DejaVu Sans");
	Text.Text(8, 8, 12, "Sample");
	const auto PreviewBytes = UiGraphics()->m_BufferBytes;
	EXPECT_GT(PreviewBytes, DefaultBytes);
	Text.SetFontPreviewFace(nullptr);
	Text.Text(8, 8, 12, "Sample");
	EXPECT_EQ(UiGraphics()->m_BufferBytes, PreviewBytes);
}

TEST_F(QmSettingsTextRender, ChangedProjectionRebuildsRetainedLabel)
{
	m_Ui.MapScreen();
	CUIElement Element;
	Element.Init(&m_Ui, 1);
	const CUIRect Rect{10, 10, 120, 22};
	bool Recreated = false;
	auto Prepare = [&] {
		m_Ui.DoLabelStreamed(*Element.Rect(0), &Rect, "Refresh rate", 10, TEXTALIGN_ML, {}, -1, nullptr, false, &Recreated);
	};
	Prepare();
	ASSERT_TRUE(Recreated);
	Prepare();
	ASSERT_FALSE(Recreated);
	UiGraphics()->MapScreen(0, 0, 1920, 1080);
	Prepare();
	EXPECT_TRUE(Recreated);
	Prepare();
	EXPECT_FALSE(Recreated);
}

TEST_F(QmSettingsTextRender, PrewarmUsesMenuPixelsAndRestoresPerformanceGraphProjection)
{
	IGraphics &Graphics = *m_Ui.Graphics();
	Graphics.MapScreen(0, 0, 1920, 1080);
	const auto Original = Graphics.GetScreen();
	CUIElement Element;
	Element.Init(&m_Ui, 1);
	const float Size = ui_token::page_title::FONT_SIZE;
	const char *pTitle = "设置";
	RunSettingsTextPlanMeasurement(&m_Ui, [&] {
		EXPECT_FLOAT_EQ(Graphics.GetScreen().Height(), m_Ui.Screen()->h);
		const float Width = m_Ui.TextRender()->TextWidth(Size, pTitle);
		const auto Group = ui_token::page_title::ResolveTitleGroup({0, 0, Width, 32}, Width);
		SLabelProperties Props;
		Props.m_EnableWidthCheck = false;
		m_Ui.DoLabelStreamed(*Element.Rect(0), &Group.m_Text, pTitle, Size, TEXTALIGN_MC, Props, -1, nullptr, false);
	});
	EXPECT_EQ(Graphics.GetScreen().Size(), Original.Size());
	EXPECT_EQ(Graphics.GetScreen().m_TopLeft, Original.m_TopLeft);
	const auto &Cached = *Element.Rect(0);
	ASSERT_TRUE(Cached.m_UITextContainer.Valid());
	EXPECT_EQ(Cached.m_Cursor.m_LineCount, 1);
	m_Ui.MapScreen();
	EXPECT_EQ(Cached.m_TextPixelScale, Graphics.ScreenPixelScale());
	EXPECT_FLOAT_EQ(Cached.m_Cursor.m_LongestLineWidth, m_Ui.TextRender()->TextWidth(Size, pTitle));
}
