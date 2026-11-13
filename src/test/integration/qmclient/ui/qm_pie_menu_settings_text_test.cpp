// 圆饼设置预览走真实 CUi、文字布局和容器，使用设备边界与原生分配故障注入。
#include <game/client/QmUi/QmPieMenuSettingsControls.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	class CPiePreviewGraphics : public qm_ui_test::CRealUiTestGraphics
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
		bool m_RecordText = true;
		int m_TextDrawCalls = 0;

		void RenderText(int Container, int Quads, int TextureSize, CTextureHandle Texture, CTextureHandle Outline, const ColorRGBA &Color, const ColorRGBA &OutlineColor) override
		{
			++m_TextDrawCalls;
			const EFailure Failure = std::exchange(m_NextFailure, EFailure::NONE);
			if(Failure == EFailure::ALLOCATION)
				throw std::bad_alloc();
			if(Failure == EFailure::LENGTH)
				throw std::length_error("controlled pie preview device limit");
			if(Failure == EFailure::OTHER)
				throw std::logic_error("unrelated pie preview device failure");
			if(m_RecordText)
				CRealUiTestGraphics::RenderText(Container, Quads, TextureSize, Texture, Outline, Color, OutlineColor);
			else
				CIconBenchmarkGraphics::RenderText(Container, Quads, TextureSize, Texture, Outline, Color, OutlineColor);
		}
	};

	class QmPieMenuSettingsText : public qm_ui_test::CRealUiFixture
	{
	protected:
		CPiePreviewGraphics m_PreviewGraphics;
		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_PreviewGraphics; }

		void PrepareCallerState()
		{
			auto *pText = m_Ui.TextRender();
			pText->SetFontPreset(EFontPreset::DEFAULT_FONT);
			pText->SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
			pText->TextColor({0.2f, 0.3f, 0.4f, 0.5f});
			pText->TextOutlineColor({0.7f, 0.6f, 0.5f, 0.4f});
			pText->TextSelectionColor({0.8f, 0.7f, 0.6f, 0.3f});
		}
		void ExpectCallerState(bool DefaultTextColor = false)
		{
			const auto *pText = m_Ui.TextRender();
			EXPECT_EQ(pText->GetFontPreset(), EFontPreset::DEFAULT_FONT);
			EXPECT_EQ(pText->GetRenderFlags(), TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
			EXPECT_EQ(pText->GetTextColor(), DefaultTextColor ? pText->DefaultTextColor() : ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f));
			EXPECT_EQ(pText->GetTextOutlineColor(), ColorRGBA(0.7f, 0.6f, 0.5f, 0.4f));
			EXPECT_EQ(pText->GetTextSelectionColor(), ColorRGBA(0.8f, 0.7f, 0.6f, 0.3f));
		}
		bool DrawIcon()
		{
			return QmDrawPieSettingsPreviewIcon(m_Ui, {120.0f, 80.0f}, 20.0f, FontIcons::FONT_ICON_CHEVRON_RIGHT, 0.35f);
		}
		bool DrawTitle()
		{
			return QmDrawPieSettingsPreviewTitle(*m_Ui.TextRender(), {120.0f, 80.0f}, 18.0f, 32.0f, "Invite to team");
		}
		bool Draw(bool Title) { return Title ? DrawTitle() : DrawIcon(); }

		void CheckNativeFailures(bool Title)
		{
			// 原生钩子只观察生产分配，设备替身的记账容器不参与故障矩阵。
			m_PreviewGraphics.m_RecordText = false;
			m_PreviewGraphics.m_ValidateLifecycle = false;
			PrepareCallerState();
			ASSERT_TRUE(Draw(Title));
			m_Ui.OnElementsReset();
			PrepareCallerState();
			size_t Allocations = 0;
			bool Completed = false;
			{
				CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
				Completed = Draw(Title);
				Allocations = Count.Attempts();
			}
			ASSERT_TRUE(Completed);
			ASSERT_GT(Allocations, 0u);
			ASSERT_LE(Allocations, 4096u);
			ExpectCallerState(Title);
			size_t Skipped = 0;
			for(size_t Budget = 0; Budget < Allocations; ++Budget)
			{
				SCOPED_TRACE(Budget);
				m_Ui.TextRender()->QmTextFrameEnd();
				m_Ui.OnElementsReset();
				PrepareCallerState();
				bool Triggered = false;
				{
					CPredictionAllocationFailure Fault(Budget);
					Completed = Draw(Title);
					Triggered = Fault.Triggered();
				}
				if(Triggered && !Completed)
					++Skipped;
				ExpectCallerState(Title && Completed);
				m_Ui.TextRender()->QmTextFrameEnd();
				m_Ui.OnElementsReset();
				PrepareCallerState();
				const int Calls = m_PreviewGraphics.m_TextDrawCalls;
				ASSERT_TRUE(Draw(Title));
				EXPECT_GT(m_PreviewGraphics.m_TextDrawCalls, Calls);
				ExpectCallerState(Title);
			}
			EXPECT_GT(Skipped, 0u);
		}
	};
}

TEST_F(QmPieMenuSettingsText, IconDrawRestoresTheCallerFontFlagsAndAllColors)
{
	PrepareCallerState();
	ASSERT_TRUE(DrawIcon());
	EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, 1);
	EXPECT_EQ(m_PreviewGraphics.m_vTextSubmissions.size(), 1u);
	ExpectCallerState();
}

TEST_F(QmPieMenuSettingsText, TitleDrawFinishesWithDefaultTextColorAndRestoresTheOtherCallerState)
{
	PrepareCallerState();
	ASSERT_TRUE(DrawTitle());
	EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, 1);
	EXPECT_EQ(m_PreviewGraphics.m_vTextSubmissions.size(), 1u);
	ExpectCallerState(true);
}

TEST_F(QmPieMenuSettingsText, IconResourceFailuresSkipTheLabelAndAllowTheNextFrame)
{
	for(const auto Failure : {CPiePreviewGraphics::EFailure::ALLOCATION, CPiePreviewGraphics::EFailure::LENGTH})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		PrepareCallerState();
		m_PreviewGraphics.m_vTextSubmissions.clear();
		const int Calls = m_PreviewGraphics.m_TextDrawCalls;
		m_PreviewGraphics.m_NextFailure = Failure;
		EXPECT_FALSE(DrawIcon());
		EXPECT_EQ(m_PreviewGraphics.m_NextFailure, CPiePreviewGraphics::EFailure::NONE);
		EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, Calls + 1);
		EXPECT_TRUE(m_PreviewGraphics.m_vTextSubmissions.empty());
		ExpectCallerState();
		m_Ui.TextRender()->QmTextFrameEnd();
		EXPECT_TRUE(DrawIcon());
		EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, Calls + 2);
		EXPECT_EQ(m_PreviewGraphics.m_vTextSubmissions.size(), 1u);
		ExpectCallerState();
	}
}

TEST_F(QmPieMenuSettingsText, TitleResourceFailuresKeepTheEntryColorAndAllowTheNextFrame)
{
	for(const auto Failure : {CPiePreviewGraphics::EFailure::ALLOCATION, CPiePreviewGraphics::EFailure::LENGTH})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		PrepareCallerState();
		m_PreviewGraphics.m_vTextSubmissions.clear();
		const int Calls = m_PreviewGraphics.m_TextDrawCalls;
		m_PreviewGraphics.m_NextFailure = Failure;
		EXPECT_FALSE(DrawTitle());
		EXPECT_EQ(m_PreviewGraphics.m_NextFailure, CPiePreviewGraphics::EFailure::NONE);
		EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, Calls + 1);
		EXPECT_TRUE(m_PreviewGraphics.m_vTextSubmissions.empty());
		ExpectCallerState();
		m_Ui.TextRender()->QmTextFrameEnd();
		EXPECT_TRUE(DrawTitle());
		EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, Calls + 2);
		EXPECT_EQ(m_PreviewGraphics.m_vTextSubmissions.size(), 1u);
		ExpectCallerState(true);
	}
}

TEST_F(QmPieMenuSettingsText, UnrelatedIconDeviceFailureRestoresStateAndStillPropagates)
{
	PrepareCallerState();
	m_PreviewGraphics.m_NextFailure = CPiePreviewGraphics::EFailure::OTHER;
	EXPECT_THROW(DrawIcon(), std::logic_error);
	EXPECT_EQ(m_PreviewGraphics.m_NextFailure, CPiePreviewGraphics::EFailure::NONE);
	EXPECT_TRUE(m_PreviewGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	m_Ui.TextRender()->QmTextFrameEnd();
	EXPECT_TRUE(DrawIcon());
	ExpectCallerState();
}

TEST_F(QmPieMenuSettingsText, UnrelatedTitleDeviceFailureRestoresStateAndStillPropagates)
{
	PrepareCallerState();
	m_PreviewGraphics.m_NextFailure = CPiePreviewGraphics::EFailure::OTHER;
	EXPECT_THROW(DrawTitle(), std::logic_error);
	EXPECT_EQ(m_PreviewGraphics.m_NextFailure, CPiePreviewGraphics::EFailure::NONE);
	EXPECT_TRUE(m_PreviewGraphics.m_vTextSubmissions.empty());
	ExpectCallerState();
	m_Ui.TextRender()->QmTextFrameEnd();
	EXPECT_TRUE(DrawTitle());
	ExpectCallerState(true);
}

TEST_F(QmPieMenuSettingsText, TitleFailureDoesNotReplayTheAlreadyDrawnIcon)
{
	PrepareCallerState();
	ASSERT_TRUE(DrawIcon());
	ASSERT_EQ(m_PreviewGraphics.m_vTextSubmissions.size(), 1u);
	const int Calls = m_PreviewGraphics.m_TextDrawCalls;
	m_PreviewGraphics.m_NextFailure = CPiePreviewGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(DrawTitle());
	EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, Calls + 1);
	EXPECT_EQ(m_PreviewGraphics.m_vTextSubmissions.size(), 1u);
	ExpectCallerState();
	m_Ui.TextRender()->QmTextFrameEnd();
	EXPECT_TRUE(DrawTitle());
	EXPECT_EQ(m_PreviewGraphics.m_TextDrawCalls, Calls + 2);
	EXPECT_EQ(m_PreviewGraphics.m_vTextSubmissions.size(), 2u);
	ExpectCallerState(true);
}

TEST_F(QmPieMenuSettingsText, NativeIconAllocationsRestoreStateAndAllowTheNextFrame)
{
	CheckNativeFailures(false);
}

TEST_F(QmPieMenuSettingsText, NativeTitleAllocationsKeepTheCorrectEndStateAndAllowTheNextFrame)
{
	CheckNativeFailures(true);
}
