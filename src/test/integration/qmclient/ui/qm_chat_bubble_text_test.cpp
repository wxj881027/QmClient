#include <game/client/QmUi/QmChatBubble.h>

#include <test/support/qm_real_ui_fixture.h>

#include <new>

namespace
{
	class QmChatBubbleText : public qm_ui_test::CRealUiFixture
	{
	protected:
		const unsigned m_CallerFlags = TEXT_RENDER_FLAG_KERNING | TEXT_RENDER_FLAG_NO_X_BEARING;
		const ColorRGBA m_CallerColor{0.6f, 0.5f, 0.4f, 0.8f};
		const ColorRGBA m_Fill{0.2f, 0.3f, 0.4f, 0.7f};
		const ColorRGBA m_Outline{0.4f, 0.3f, 0.2f, 0.5f};

		ITextRender &Text() { return *m_Ui.TextRender(); }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			Text().SetRenderFlags(m_CallerFlags);
			Text().TextColor(m_CallerColor);
		}

		void Draw(bool WithCaret = false)
		{
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(10, 20));
			Cursor.m_FontSize = 14;
			Cursor.m_LineWidth = 200;
			if(WithCaret)
			{
				Cursor.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_SET;
				Cursor.m_CursorCharacter = 0;
			}
			qm_chat_bubble::RenderText(&Text(), Cursor, "message", m_Fill, m_Outline);
		}

		void ExpectCallerState()
		{
			EXPECT_EQ(Text().GetRenderFlags(), m_CallerFlags);
			EXPECT_EQ(Text().GetTextColor(), m_CallerColor);
		}
	};
}

TEST_F(QmChatBubbleText, DrawUsesExplicitColorsAndDeletesTheContainerImmediately)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	const size_t Containers = Graphics().m_Containers.size();
	Draw();
	ASSERT_EQ(Graphics().m_vTextSubmissions.size(), 1u);
	EXPECT_EQ(Graphics().m_vTextSubmissions.back().m_TextColor, m_Fill);
	EXPECT_EQ(Graphics().m_vTextSubmissions.back().m_OutlineColor, m_Outline);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	ExpectCallerState();
}

TEST_F(QmChatBubbleText, CreationFailurePreservesCallerStateAndQueuesThePartialBuffer)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Draw(), std::bad_alloc);
	ExpectCallerState();
	ASSERT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
}

TEST_F(QmChatBubbleText, DrawFailurePreservesCallerStateAndQueuesTheContainer)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	const size_t Containers = Graphics().m_Containers.size();
	Graphics().m_FailTextDraw = true;
	EXPECT_THROW(Draw(), std::bad_alloc);
	ExpectCallerState();
	ASSERT_EQ(Graphics().m_Containers.size(), Containers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmChatBubbleText, DeletionFailureQueuesTheRemainingCaretContainer)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	const size_t Quads = Graphics().QuadContainerCount();
	Graphics().m_FailQuadContainerDelete = true;
	EXPECT_THROW(Draw(true), std::bad_alloc);
	ExpectCallerState();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	ASSERT_EQ(Graphics().QuadContainerCount(), Quads + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().QuadContainerCount(), Quads);
}
