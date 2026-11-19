#include <game/client/components/qmclient/chat_bubble_state.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmChatBubbleStateIntegration : public qm_ui_test::CRealUiFixture
	{
	protected:
		CQmChatBubbleState m_State;
		ITextRender &Text() { return *m_Ui.TextRender(); }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(::testing::Test::HasFatalFailure())
				return;
			Text().SetFontPreset(EFontPreset::DEFAULT_FONT);
			Text().SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
			Text().TextColor(ColorRGBA(0.6f, 0.5f, 0.4f, 0.8f));
			Text().TextOutlineColor(ColorRGBA(0.2f, 0.3f, 0.4f, 0.7f));
			Graphics().MapScreen(1, 2, 301, 202);
		}
		void TearDown() override
		{
			if(m_Ui.TextRender())
			{
				Graphics().m_FailBufferObjectCreate = false;
				Graphics().m_FailBufferContainerCreate = false;
				Graphics().m_BufferObjectDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure::NONE;
				Graphics().m_FailQuadContainerDelete = false;
				Graphics().m_FailTextDraw = false;
				m_State.Reset(Text());
			}
			CRealUiFixture::TearDown();
		}
		void Prepare(float FontSize = 14)
		{
			m_State.PrepareLayout(Text(), FontSize, 230, "same message");
		}
		bool PrepareFrame(float FontSize)
		{
			CQmChatBubbleFrame Frame;
			return Frame.Run([&] {
				CQmChatBubbleRenderContext Context(Text(), Graphics());
				Context.Apply([&] { Graphics().MapScreen(10, 20, 310, 220); });
				Prepare(FontSize);
				Context.Finish();
			});
		}
		void ExpectContextRestored()
		{
			EXPECT_EQ(Text().GetRenderFlags(), unsigned(TEXT_RENDER_FLAG_KERNING));
			EXPECT_EQ(Text().GetTextColor(), ColorRGBA(0.6f, 0.5f, 0.4f, 0.8f));
			EXPECT_EQ(Text().GetTextOutlineColor(), ColorRGBA(0.2f, 0.3f, 0.4f, 0.7f));
			float X0, Y0, X1, Y1;
			Graphics().GetScreen(&X0, &Y0, &X1, &Y1);
			EXPECT_EQ(X0, 1);
			EXPECT_EQ(Y0, 2);
			EXPECT_EQ(X1, 301);
			EXPECT_EQ(Y1, 202);
		}
	};
}

TEST_F(QmChatBubbleStateIntegration, UnchangedLayoutDoesNotAttemptAnotherNativeCreation)
{
	Prepare();
	ASSERT_TRUE(m_State.m_TextContainerIndex.Valid());
	const int Slot = m_State.m_TextContainerIndex.m_Index;
	Graphics().m_FailBufferObjectCreate = true;
	EXPECT_NO_THROW(Prepare());
	EXPECT_TRUE(Graphics().m_FailBufferObjectCreate);
	EXPECT_EQ(m_State.m_TextContainerIndex.m_Index, Slot);
	EXPECT_GT(m_State.m_CachedTextWidth, 0);
	EXPECT_STREQ(m_State.m_aLayoutText, "same message");
}

TEST_F(QmChatBubbleStateIntegration, FailedUploadHidesThePartialLayoutAndRetriesTheSameText)
{
	ASSERT_TRUE(PrepareFrame(14));
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_FALSE(PrepareFrame(16));
	EXPECT_FALSE(m_State.m_TextContainerIndex.Valid());
	EXPECT_TRUE(m_State.HasTextResources());
	EXPECT_EQ(m_State.m_CachedFontSize, 14);
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	ASSERT_TRUE(PrepareFrame(16));
	EXPECT_TRUE(m_State.m_TextContainerIndex.Valid());
	EXPECT_EQ(m_State.m_CachedFontSize, 16);
	EXPECT_STREQ(m_State.m_aLayoutText, "same message");
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}

TEST_F(QmChatBubbleStateIntegration, ResetAfterPartialCreationReleasesTheNativeSlotAndBuffers)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Prepare(), std::bad_alloc);
	EXPECT_TRUE(m_State.HasTextResources());
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	m_State.Reset(Text());
	EXPECT_FALSE(m_State.HasTextResources());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}

TEST_F(QmChatBubbleStateIntegration, PendingDeletionFailurePreventsAnotherLayoutUntilCleanupSucceeds)
{
	Prepare();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Prepare(16), std::bad_alloc);
	Graphics().m_BufferObjectDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure::ALLOCATION;
	EXPECT_THROW(Prepare(16), std::bad_alloc);
	EXPECT_FALSE(m_State.m_TextContainerIndex.Valid());
	EXPECT_TRUE(m_State.HasTextResources());
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	EXPECT_NO_THROW(Prepare(16));
	EXPECT_TRUE(m_State.m_TextContainerIndex.Valid());
}

TEST_F(QmChatBubbleStateIntegration, NormalContextFinishRestoresTheCallingColorsRepeatedly)
{
	CQmChatBubbleRenderContext Context(Text(), Graphics());
	Context.Apply([&] { Graphics().MapScreen(10, 20, 310, 220); });
	Text().TextColor(ColorRGBA(0.1f, 0.2f, 0.3f, 0.4f));
	Text().TextOutlineColor(ColorRGBA(0.4f, 0.3f, 0.2f, 0.1f));
	Context.Finish();
	Context.Finish();
	ExpectContextRestored();
}

TEST_F(QmChatBubbleStateIntegration, NativeDrawFailurePropagatesAfterContextCleanup)
{
	CQmChatBubbleFrame Frame;
	EXPECT_THROW(Frame.Run([&] {
		CQmChatBubbleRenderContext Context(Text(), Graphics());
		Context.Apply([&] { Graphics().MapScreen(10, 20, 310, 220); });
		Prepare();
		Text().TextColor(ColorRGBA(0.1f, 0.2f, 0.3f, 0.4f));
		Frame.BeginDrawing();
		Graphics().m_FailTextDraw = true;
		Text().RenderTextContainer(m_State.m_TextContainerIndex, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1));
	}),
		std::bad_alloc);
	EXPECT_TRUE(m_State.m_TextContainerIndex.Valid());
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}

TEST_F(QmChatBubbleStateIntegration, ResetKeepsTheExistingUsageObjectAfterReleasingItsLayout)
{
	Prepare();
	const auto *pUsage = m_State.m_TextContainerIndex.m_UseCount.get();
	m_State.Reset(Text());
	EXPECT_FALSE(m_State.HasTextResources());
	EXPECT_EQ(m_State.m_TextContainerIndex.m_UseCount.get(), pUsage);
	EXPECT_FALSE(m_State.m_Initialized);
}

TEST_F(QmChatBubbleStateIntegration, FailedInitializationRemainsRetryableAfterReleasingTheOldLayout)
{
	Prepare();
	EXPECT_THROW(m_State.Initialize(Text(), [] { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_FALSE(m_State.m_Initialized);
	EXPECT_FALSE(m_State.HasTextResources());
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	m_State.Initialize(Text(), [] {});
	EXPECT_TRUE(m_State.m_Initialized);
	EXPECT_NO_THROW(Prepare());
	EXPECT_TRUE(m_State.m_TextContainerIndex.Valid());
}

TEST_F(QmChatBubbleStateIntegration, EmptyLayoutKeepsThePreviousZeroSizeResult)
{
	m_State.PrepareLayout(Text(), 14, 230, "");
	EXPECT_FALSE(m_State.m_TextContainerIndex.Valid());
	EXPECT_FALSE(m_State.HasTextResources());
	EXPECT_EQ(m_State.m_CachedTextWidth, 0);
	EXPECT_EQ(m_State.m_CachedTextHeight, 0);
	EXPECT_STREQ(m_State.m_aLayoutText, "");
}
