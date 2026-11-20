#include <game/client/qm_icon_font_render.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmIconFontLifetime : public qm_ui_test::CRealUiFixture
	{
	protected:
		ITextRender &Text() { return *m_Ui.TextRender(); }
		IEngineTextRender &Engine() { return *static_cast<IEngineTextRender *>(m_Ui.TextRender()); }
		CTextCursor Cursor(bool WithCaret = false)
		{
			CTextCursor Result;
			Result.SetPosition({10, 20});
			Result.m_FontSize = 14;
			Result.m_LineWidth = 200;
			Result.m_Flags = TEXTFLAG_RENDER;
			Result.m_vColorSplits.emplace_back(0, 1, ColorRGBA(0.2f, 0.4f, 0.8f, 0.3f));
			if(WithCaret)
			{
				Result.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_SET;
				Result.m_CursorCharacter = 0;
			}
			return Result;
		}
		void PrepareState()
		{
			Text().TextColor({0.2f, 0.3f, 0.4f, 0.5f});
			Text().TextOutlineColor({0.7f, 0.6f, 0.5f, 0.4f});
			Text().SetRenderFlags(TEXT_RENDER_FLAG_NO_X_BEARING);
		}
		void Draw(CTextCursor &Input)
		{
			QmRenderImmediateFontIcon(Text(), &Input, "abc", -1, {0.9f, 0.8f, 0.2f, 0.25f}, {0, 0, 0, 0.0875f});
		}
		void ExpectState(const CTextCursor &Input, const STextColorSplit *pSplits)
		{
			EXPECT_EQ(Text().GetTextColor(), ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f));
			EXPECT_EQ(Text().GetTextOutlineColor(), ColorRGBA(0.7f, 0.6f, 0.5f, 0.4f));
			EXPECT_EQ(Text().GetRenderFlags(), TEXT_RENDER_FLAG_NO_X_BEARING);
			EXPECT_EQ(Input.m_vColorSplits.data(), pSplits);
			ASSERT_EQ(Input.m_vColorSplits.size(), 1u);
			EXPECT_EQ(Input.m_vColorSplits[0].m_Color, ColorRGBA(0.2f, 0.4f, 0.8f, 0.3f));
		}
	};
}

TEST_F(QmIconFontLifetime, PartialUploadTransfersBufferAcrossDeferredDeleteFailureAndReusesItsSlot)
{
	STextContainerIndex Probe;
	auto First = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Probe, &First, "abc"));
	const int Slot = Probe.m_Index;
	Text().DeleteTextContainer(Probe);
	PrepareState();
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;
	auto Input = Cursor();
	const auto *pSplits = Input.m_vColorSplits.data();
	EXPECT_THROW(Draw(Input), std::bad_alloc);
	ExpectState(Input, pSplits);
	ASSERT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	Graphics().m_BufferObjectDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure::LENGTH;
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	STextContainerIndex Reused;
	auto Retry = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Reused, &Retry, "abc"));
	EXPECT_EQ(Reused.m_Index, Slot);
	Text().DeleteTextContainer(Reused);
}

TEST_F(QmIconFontLifetime, DrawFailureRestoresCallerStateAndFrameEndReclaimsBothBuffers)
{
	PrepareState();
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailTextDraw = true;
	auto Input = Cursor();
	const auto *pSplits = Input.m_vColorSplits.data();
	EXPECT_THROW(Draw(Input), std::bad_alloc);
	ExpectState(Input, pSplits);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	auto Retry = Cursor();
	EXPECT_NO_THROW(Draw(Retry));
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmIconFontLifetime, CaretDeleteFailureDefersRemainingQuadAndDoesNotBecomeForgottenOnResize)
{
	PrepareState();
	const size_t Buffers = Graphics().m_Buffers.size(), Quads = Graphics().QuadContainerCount();
	Graphics().m_FailQuadContainerDelete = true;
	auto Input = Cursor(true);
	const auto *pSplits = Input.m_vColorSplits.data();
	EXPECT_THROW(Draw(Input), std::bad_alloc);
	ExpectState(Input, pSplits);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().QuadContainerCount(), Quads + 1);
	EXPECT_NO_THROW(Engine().OnPreWindowResize());
	EXPECT_EQ(Graphics().QuadContainerCount(), Quads);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().QuadContainerCount(), Quads);
}

TEST_F(QmIconFontLifetime, ExplicitRetirementClearsOnlyItsHandleAndKeepsIndependentOwnerAlive)
{
	STextContainerIndex Owned, Temporary;
	auto First = Cursor(), Second = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Owned, &First, "abc"));
	Text().SetRenderFlags(TEXT_RENDER_FLAG_ONE_TIME_USE);
	ASSERT_TRUE(Text().CreateTextContainer(Temporary, &Second, "abc"));
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	Text().ReleaseTemporaryTextContainer(Temporary);
	EXPECT_FALSE(Temporary.Valid());
	EXPECT_EQ(Temporary.m_Index, -1);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes);
	Text().ReleaseTemporaryTextContainer(Temporary);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers - 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers - 1);
	ASSERT_TRUE(Owned.Valid());
	EXPECT_NO_THROW(Text().RenderTextContainer(Owned, {1, 1, 1, 1}, {0, 0, 0, 1}));
	Text().DeleteTextContainer(Owned);
}
