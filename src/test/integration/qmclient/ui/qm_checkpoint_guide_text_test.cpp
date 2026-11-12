// CP 标签执行真实文字布局、容器创建与绘制，使用设备边界和原生分配故障注入。
#include <game/client/components/qmclient/checkpoint_guide_render.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	class CCheckpointGuideGraphics : public qm_ui_test::CRealUiTestGraphics
	{
	public:
		enum class EFailure
		{
			NONE,
			ALLOCATION,
			LENGTH,
			OTHER,
		};
		EFailure m_NextFailure = EFailure::NONE;
		int m_DrawsBeforeFailure = 0;
		int m_TextDrawCalls = 0;

		void RenderText(int Container, int Quads, int TextureSize, CTextureHandle Texture, CTextureHandle Outline, const ColorRGBA &Color, const ColorRGBA &OutlineColor) override
		{
			++m_TextDrawCalls;
			if(m_NextFailure != EFailure::NONE)
			{
				if(m_DrawsBeforeFailure > 0)
					--m_DrawsBeforeFailure;
				else
				{
					const EFailure Failure = std::exchange(m_NextFailure, EFailure::NONE);
					if(Failure == EFailure::ALLOCATION)
						throw std::bad_alloc();
					if(Failure == EFailure::LENGTH)
						throw std::length_error("controlled CP label device limit");
					throw std::logic_error("unrelated CP label device failure");
				}
			}
			CRealUiTestGraphics::RenderText(Container, Quads, TextureSize, Texture, Outline, Color, OutlineColor);
		}
	};

	class QmCheckpointGuideText : public qm_ui_test::CRealUiFixture
	{
	protected:
		CCheckpointGuideGraphics m_GuideGraphics;
		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_GuideGraphics; }

		void PrepareCallerState()
		{
			auto *pText = m_Ui.TextRender();
			pText->SetFontPreset(EFontPreset::DEFAULT_FONT);
			pText->SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
			pText->TextColor({0.2f, 0.3f, 0.4f, 0.5f});
			pText->TextOutlineColor({0.7f, 0.6f, 0.5f, 0.4f});
			pText->TextSelectionColor({0.8f, 0.7f, 0.6f, 0.3f});
			m_GuideGraphics.MapScreen(-25, 31, 1595, 931);
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
			m_GuideGraphics.GetScreen(&X0, &Y0, &X1, &Y1);
			EXPECT_FLOAT_EQ(X0, -25);
			EXPECT_FLOAT_EQ(Y0, 31);
			EXPECT_FLOAT_EQ(X1, 1595);
			EXPECT_FLOAT_EQ(Y1, 931);
		}
		bool Draw(bool Warning = true)
		{
			return QmCheckpointGuide::RenderLabels(*m_Ui.TextRender(), {100, 150}, {200, 150}, 9, Warning,
				Warning ? ui_token::color::WARNING : ui_token::color::ACCENT_PRIMARY);
		}
	};
}

TEST_F(QmCheckpointGuideText, NormalAndWarningLabelsRestoreTheCallersTextState)
{
	PrepareCallerState();
	for(const bool Warning : {false, true})
	{
		SCOPED_TRACE(Warning);
		m_GuideGraphics.m_vTextSubmissions.clear();
		EXPECT_TRUE(Draw(Warning));
		EXPECT_EQ(m_GuideGraphics.m_vTextSubmissions.size(), Warning ? 2u : 1u);
		ExpectCallerState();
	}
}

TEST_F(QmCheckpointGuideText, DrawAllocationFailureSkipsTheLabelAndCanDrawNextFrame)
{
	PrepareCallerState();
	m_GuideGraphics.m_NextFailure = CCheckpointGuideGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(Draw());
	EXPECT_EQ(m_GuideGraphics.m_NextFailure, CCheckpointGuideGraphics::EFailure::NONE);
	EXPECT_TRUE(m_GuideGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	EXPECT_TRUE(Draw());
	EXPECT_EQ(m_GuideGraphics.m_vTextSubmissions.size(), 2u);
	ExpectCallerState();
}

TEST_F(QmCheckpointGuideText, DrawLengthFailureRestoresColorsAndCanDrawNextFrame)
{
	PrepareCallerState();
	m_GuideGraphics.m_NextFailure = CCheckpointGuideGraphics::EFailure::LENGTH;
	EXPECT_FALSE(Draw());
	EXPECT_EQ(m_GuideGraphics.m_NextFailure, CCheckpointGuideGraphics::EFailure::NONE);
	EXPECT_TRUE(m_GuideGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	EXPECT_TRUE(Draw());
	EXPECT_EQ(m_GuideGraphics.m_vTextSubmissions.size(), 2u);
	ExpectCallerState();
}

TEST_F(QmCheckpointGuideText, WarningFailureDoesNotReplayTheAlreadySubmittedCheckpointLabel)
{
	PrepareCallerState();
	m_GuideGraphics.m_DrawsBeforeFailure = 1;
	m_GuideGraphics.m_NextFailure = CCheckpointGuideGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(Draw());
	EXPECT_EQ(m_GuideGraphics.m_TextDrawCalls, 2);
	EXPECT_EQ(m_GuideGraphics.m_vTextSubmissions.size(), 1u);
	ExpectCallerState();
	m_GuideGraphics.m_vTextSubmissions.clear();
	EXPECT_TRUE(Draw());
	EXPECT_EQ(m_GuideGraphics.m_TextDrawCalls, 4);
	EXPECT_EQ(m_GuideGraphics.m_vTextSubmissions.size(), 2u);
	ExpectCallerState();
}

TEST_F(QmCheckpointGuideText, UnrelatedDrawExceptionRestoresStateAndStillPropagates)
{
	PrepareCallerState();
	m_GuideGraphics.m_NextFailure = CCheckpointGuideGraphics::EFailure::OTHER;
	EXPECT_THROW(Draw(), std::logic_error);
	EXPECT_EQ(m_GuideGraphics.m_NextFailure, CCheckpointGuideGraphics::EFailure::NONE);
	EXPECT_TRUE(m_GuideGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	EXPECT_TRUE(Draw());
	EXPECT_EQ(m_GuideGraphics.m_vTextSubmissions.size(), 2u);
	ExpectCallerState();
}

TEST_F(QmCheckpointGuideText, NativeAllocationFailuresRestoreStateAndAllowTheNextFrame)
{
	PrepareCallerState();
	ASSERT_TRUE(Draw());
	size_t Allocations = 0;
	bool Completed = false;
	{
		CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
		Completed = Draw();
		Allocations = Count.Attempts();
	}
	ASSERT_TRUE(Completed);
	ASSERT_GT(Allocations, 0u);
	size_t Skipped = 0;
	for(size_t Budget = 0; Budget < Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		m_GuideGraphics.m_vTextSubmissions.clear();
		bool Triggered = false;
		{
			CPredictionAllocationFailure Fault(Budget);
			Completed = Draw();
			Triggered = Fault.Triggered();
		}
		if(Triggered && !Completed)
			++Skipped;
		ExpectCallerState();
		m_GuideGraphics.m_vTextSubmissions.clear();
		ASSERT_TRUE(Draw());
		EXPECT_EQ(m_GuideGraphics.m_vTextSubmissions.size(), 2u);
		ExpectCallerState();
	}
	EXPECT_GT(Skipped, 0u);
}
