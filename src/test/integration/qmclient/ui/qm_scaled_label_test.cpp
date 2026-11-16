#include <game/client/QmUi/UiScaledLabel.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmScaledLabel : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUIElement m_Labels;
		const CUIRect m_Base{20, 30, 280, 30};
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			m_Labels.Init(&m_Ui, 1);
		}
		void Draw(float Scale, const char *pText = "Settings")
		{
			CUIRect Rect = m_Base;
			Rect.x -= Rect.w * (Scale - 1.0f) * 0.5f;
			Rect.y -= Rect.h * (Scale - 1.0f) * 0.5f;
			Rect.w *= Scale;
			Rect.h *= Scale;
			DrawUiScaledLabel(m_Ui, *m_Labels.Rect(0), m_Base, Rect, pText, 14.0f, Scale);
		}
	};
}

TEST_F(QmScaledLabel, FractionalScalingAndReversalReuseGlyphsAndRestoreScreen)
{
	Draw(1.0f);
	const auto Cursor = m_Labels.Rect(0)->m_Cursor;
	const uint64_t Uploads = UiGraphics()->m_Uploads;
	float X0, Y0, X1, Y1;
	UiGraphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	const unsigned Flags = m_Ui.TextRender()->GetRenderFlags();
	for(float Scale : {1.001f, 1.002f, 1.08f, 1.04f, 0.999f, 0.94f, 1.0f})
	{
		SCOPED_TRACE(Scale);
		UiGraphics()->m_vTextSubmissions.clear();
		Draw(Scale);
		ASSERT_FALSE(UiGraphics()->m_vTextSubmissions.empty());
		const auto &Screen = UiGraphics()->m_vTextSubmissions.back().m_Screen;
		EXPECT_NEAR((X1 - X0) / Screen.w, Scale, 0.00001f);
		EXPECT_NEAR((Y1 - Y0) / Screen.h, Scale, 0.00001f);
		EXPECT_EQ(UiGraphics()->m_Uploads, Uploads);
		EXPECT_FLOAT_EQ(m_Labels.Rect(0)->m_Cursor.m_LongestLineWidth, Cursor.m_LongestLineWidth);
		EXPECT_EQ(m_Ui.TextRender()->GetRenderFlags(), Flags);
		float RestoredX0, RestoredY0, RestoredX1, RestoredY1;
		UiGraphics()->GetScreen(&RestoredX0, &RestoredY0, &RestoredX1, &RestoredY1);
		EXPECT_FLOAT_EQ(RestoredX0, X0);
		EXPECT_FLOAT_EQ(RestoredY0, Y0);
		EXPECT_FLOAT_EQ(RestoredX1, X1);
		EXPECT_FLOAT_EQ(RestoredY1, Y1);
	}
}

TEST_F(QmScaledLabel, ChangedLabelRefreshesCachedText)
{
	Draw(1.04f, "Run server");
	Draw(1.02f, "Stop server");
	EXPECT_EQ(m_Labels.Rect(0)->m_Text, "Stop server");
	EXPECT_TRUE(m_Labels.Rect(0)->m_UITextContainer.Valid());
}

TEST_F(QmScaledLabel, UiResetRebuildsLabelAtNewFramebufferSize)
{
	Draw(1.0f);
	m_Ui.OnElementsReset();
	EXPECT_FALSE(m_Labels.Rect(0)->m_UITextContainer.Valid());
	UiGraphics()->SetFramebufferSize(1280, 720);
	Draw(1.08f);
	EXPECT_TRUE(m_Labels.Rect(0)->m_UITextContainer.Valid());
	EXPECT_EQ(m_Labels.Rect(0)->m_Text, "Settings");
}
