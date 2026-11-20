#include <game/client/QmUi/UiScaledLabel.h>

#include <test/support/qm_real_ui_fixture.h>

#include <stdexcept>
#include <utility>

namespace
{
	class CScaledLabelGraphics : public qm_ui_test::CRealUiTestGraphics
	{
	public:
		enum class EFailure
		{
			NONE,
			LENGTH,
			OTHER,
		};
		EFailure m_CreateFailure = EFailure::NONE;
		EFailure m_DrawFailure = EFailure::NONE;

		static void FailOnce(EFailure &Next)
		{
			const EFailure Failure = std::exchange(Next, EFailure::NONE);
			if(Failure == EFailure::LENGTH)
				throw std::length_error("controlled label device limit");
			if(Failure == EFailure::OTHER)
				throw std::logic_error("unrelated label device failure");
		}
		int CreateBufferObject(size_t Size, void *pData, int Flags, bool Moved = false) override
		{
			FailOnce(m_CreateFailure);
			return CRealUiTestGraphics::CreateBufferObject(Size, pData, Flags, Moved);
		}
		void RenderText(int Container, int Quads, int TextureSize, CTextureHandle Texture, CTextureHandle Outline, const ColorRGBA &Color, const ColorRGBA &OutlineColor) override
		{
			FailOnce(m_DrawFailure);
			CRealUiTestGraphics::RenderText(Container, Quads, TextureSize, Texture, Outline, Color, OutlineColor);
		}
	};

	class QmScaledLabel : public qm_ui_test::CRealUiFixture
	{
	protected:
		CScaledLabelGraphics m_ScaledGraphics;
		CUIElement m_Labels;
		const CUIRect m_Base{20, 30, 280, 30};
		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_ScaledGraphics; }
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
		void PrepareCallerState()
		{
			auto *pText = m_Ui.TextRender();
			pText->SetFontPreset(EFontPreset::DEFAULT_FONT);
			pText->SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
			pText->TextColor({0.2f, 0.3f, 0.4f, 0.5f});
			pText->TextOutlineColor({0.7f, 0.6f, 0.5f, 0.4f});
			pText->TextSelectionColor({0.8f, 0.7f, 0.6f, 0.3f});
			m_ScaledGraphics.MapScreen(-25.0f, 31.0f, 1595.0f, 931.0f);
		}
		void ExpectCallerState()
		{
			const auto *pText = m_Ui.TextRender();
			EXPECT_EQ(pText->GetFontPreset(), EFontPreset::DEFAULT_FONT);
			EXPECT_EQ(pText->GetRenderFlags(), TEXT_RENDER_FLAG_KERNING);
			EXPECT_EQ(pText->GetTextColor(), ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f));
			EXPECT_EQ(pText->GetTextOutlineColor(), ColorRGBA(0.7f, 0.6f, 0.5f, 0.4f));
			EXPECT_EQ(pText->GetTextSelectionColor(), ColorRGBA(0.8f, 0.7f, 0.6f, 0.3f));
			float X0, Y0, X1, Y1;
			m_ScaledGraphics.GetScreen(&X0, &Y0, &X1, &Y1);
			EXPECT_FLOAT_EQ(X0, -25.0f);
			EXPECT_FLOAT_EQ(Y0, 31.0f);
			EXPECT_FLOAT_EQ(X1, 1595.0f);
			EXPECT_FLOAT_EQ(Y1, 931.0f);
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

TEST_F(QmScaledLabel, PreparationAllocationFailureRestoresStateAndNextFrameCanDraw)
{
	PrepareCallerState();
	const auto Buffers = m_ScaledGraphics.m_Buffers.size(), Containers = m_ScaledGraphics.m_Containers.size();
	m_ScaledGraphics.m_FailBufferObjectCreate = true;
	EXPECT_NO_THROW(Draw(1.08f));
	EXPECT_FALSE(m_ScaledGraphics.m_FailBufferObjectCreate);
	ExpectCallerState();
	EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_EQ(m_ScaledGraphics.m_Buffers.size(), Buffers);
	EXPECT_EQ(m_ScaledGraphics.m_Containers.size(), Containers);
	EXPECT_NO_THROW(Draw(1.08f));
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_TRUE(m_Labels.Rect(0)->m_UITextContainer.Valid());
	EXPECT_EQ(m_Labels.Rect(0)->m_Text, "Settings");
	EXPECT_EQ(m_ScaledGraphics.m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(m_ScaledGraphics.m_Containers.size(), Containers + 1);
	ExpectCallerState();
}

TEST_F(QmScaledLabel, PreparationLengthFailureRestoresStateAndCanRetryChangedText)
{
	PrepareCallerState();
	Draw(1.0f, "Run server");
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	m_ScaledGraphics.m_vTextSubmissions.clear();
	m_ScaledGraphics.m_CreateFailure = CScaledLabelGraphics::EFailure::LENGTH;
	EXPECT_NO_THROW(Draw(1.04f, "Stop server"));
	EXPECT_EQ(m_ScaledGraphics.m_CreateFailure, CScaledLabelGraphics::EFailure::NONE);
	ExpectCallerState();
	EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_NO_THROW(Draw(1.04f, "Stop server"));
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_TRUE(m_Labels.Rect(0)->m_UITextContainer.Valid());
	EXPECT_EQ(m_Labels.Rect(0)->m_Text, "Stop server");
	ExpectCallerState();
}

TEST_F(QmScaledLabel, BufferedDrawAllocationFailureRestoresScreenAndRetriesWithoutUpload)
{
	PrepareCallerState();
	Draw(1.0f);
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	const uint64_t Uploads = m_ScaledGraphics.m_Uploads;
	const uint64_t BufferBytes = m_ScaledGraphics.m_BufferBytes;
	m_ScaledGraphics.m_vTextSubmissions.clear();
	m_ScaledGraphics.m_FailTextDraw = true;
	EXPECT_NO_THROW(Draw(1.08f));
	EXPECT_FALSE(m_ScaledGraphics.m_FailTextDraw);
	EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	EXPECT_NO_THROW(Draw(1.04f));
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_EQ(m_ScaledGraphics.m_Uploads, Uploads);
	EXPECT_EQ(m_ScaledGraphics.m_BufferBytes, BufferBytes);
	ExpectCallerState();
}

TEST_F(QmScaledLabel, BufferedDrawLengthFailureRestoresScreenAndRetainsCachedText)
{
	PrepareCallerState();
	Draw(1.0f);
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	const uint64_t Uploads = m_ScaledGraphics.m_Uploads;
	const uint64_t BufferBytes = m_ScaledGraphics.m_BufferBytes;
	m_ScaledGraphics.m_vTextSubmissions.clear();
	m_ScaledGraphics.m_DrawFailure = CScaledLabelGraphics::EFailure::LENGTH;
	EXPECT_NO_THROW(Draw(0.94f));
	EXPECT_EQ(m_ScaledGraphics.m_DrawFailure, CScaledLabelGraphics::EFailure::NONE);
	EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	EXPECT_NO_THROW(Draw(1.0f));
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_EQ(m_ScaledGraphics.m_Uploads, Uploads);
	EXPECT_EQ(m_ScaledGraphics.m_BufferBytes, BufferBytes);
	ExpectCallerState();
}

TEST_F(QmScaledLabel, UnknownPreparationFailureRestoresCallerStateAndStillPropagates)
{
	PrepareCallerState();
	m_ScaledGraphics.m_CreateFailure = CScaledLabelGraphics::EFailure::OTHER;
	EXPECT_THROW(Draw(1.08f), std::logic_error);
	EXPECT_EQ(m_ScaledGraphics.m_CreateFailure, CScaledLabelGraphics::EFailure::NONE);
	EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	EXPECT_NO_THROW(Draw(1.08f));
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
}

TEST_F(QmScaledLabel, UnknownBufferedDrawFailureRestoresOuterScreenAndStillPropagates)
{
	PrepareCallerState();
	Draw(1.0f);
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	const uint64_t Uploads = m_ScaledGraphics.m_Uploads;
	m_ScaledGraphics.m_vTextSubmissions.clear();
	m_ScaledGraphics.m_DrawFailure = CScaledLabelGraphics::EFailure::OTHER;
	EXPECT_THROW(Draw(1.04f), std::logic_error);
	EXPECT_EQ(m_ScaledGraphics.m_DrawFailure, CScaledLabelGraphics::EFailure::NONE);
	EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	EXPECT_NO_THROW(Draw(1.04f));
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_EQ(m_ScaledGraphics.m_Uploads, Uploads);
	ExpectCallerState();
}

TEST_F(QmScaledLabel, MeasurementWarmsTheLabelWithoutDrawingOrChangingCallerState)
{
	PrepareCallerState();
	m_ScaledGraphics.m_FailTextDraw = true;
	{
		const CUiScopedMeasurement Measurement(&m_Ui);
		EXPECT_NO_THROW(Draw(1.04f));
		EXPECT_TRUE(m_Ui.MeasurementOnly());
		EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
		EXPECT_TRUE(m_ScaledGraphics.m_FailTextDraw);
		EXPECT_TRUE(m_Labels.Rect(0)->m_UITextContainer.Valid());
		ExpectCallerState();
	}
	m_ScaledGraphics.m_FailTextDraw = false;
	const uint64_t BufferBytes = m_ScaledGraphics.m_BufferBytes;
	EXPECT_NO_THROW(Draw(1.04f));
	ASSERT_FALSE(m_ScaledGraphics.m_vTextSubmissions.empty());
	EXPECT_EQ(m_ScaledGraphics.m_BufferBytes, BufferBytes);
	ExpectCallerState();
}

TEST_F(QmScaledLabel, PreLayoutInputDoesNotPrepareTextOrChangeCallerState)
{
	PrepareCallerState();
	m_ScaledGraphics.m_FailBufferObjectCreate = true;
	m_Ui.BeginPreLayoutInput();
	EXPECT_NO_THROW(Draw(1.08f));
	m_Ui.EndPreLayoutInput();
	EXPECT_TRUE(m_ScaledGraphics.m_FailBufferObjectCreate);
	EXPECT_FALSE(m_Labels.Rect(0)->m_UITextContainer.Valid());
	EXPECT_TRUE(m_Labels.Rect(0)->m_Text.empty());
	EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	m_ScaledGraphics.m_FailBufferObjectCreate = false;
}

TEST_F(QmScaledLabel, NonpositiveScaleDoesNotPrepareTextOrChangeCallerState)
{
	PrepareCallerState();
	m_ScaledGraphics.m_FailBufferObjectCreate = true;
	for(const float Scale : {0.0f, -0.1f})
	{
		SCOPED_TRACE(Scale);
		EXPECT_NO_THROW(Draw(Scale));
		EXPECT_TRUE(m_ScaledGraphics.m_FailBufferObjectCreate);
		EXPECT_FALSE(m_Labels.Rect(0)->m_UITextContainer.Valid());
		EXPECT_TRUE(m_Labels.Rect(0)->m_Text.empty());
		EXPECT_TRUE(m_ScaledGraphics.m_vTextSubmissions.empty());
		ExpectCallerState();
	}
	m_ScaledGraphics.m_FailBufferObjectCreate = false;
}
