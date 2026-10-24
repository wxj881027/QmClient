#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmTextTemporaryLifetime : public qm_ui_test::CRealUiFixture
	{
	protected:
		ITextRender &Text() { return *m_Ui.TextRender(); }
		CTextCursor Cursor(bool WithCaret = false)
		{
			CTextCursor Result;
			Result.SetPosition(vec2(10, 20));
			Result.m_FontSize = 14;
			Result.m_LineWidth = 200;
			if(WithCaret)
			{
				Result.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_SET;
				Result.m_CursorCharacter = 0;
			}
			return Result;
		}
	};
}

TEST_F(QmTextTemporaryLifetime, SuccessfulImmediateTextRestoresFlagsAndReleasesItsBuffers)
{
	const unsigned Flags = TEXT_RENDER_FLAG_KERNING | TEXT_RENDER_FLAG_NO_X_BEARING;
	Text().SetRenderFlags(Flags);
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	auto Draw = Cursor();
	Text().TextEx(&Draw, "abc");
	EXPECT_EQ(Text().GetRenderFlags(), Flags);
	EXPECT_EQ(Draw.m_GlyphCount, 3);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmTextTemporaryLifetime, ImmediateUploadFailureRestoresFlagsBeforeTheNextDraw)
{
	const unsigned Flags = TEXT_RENDER_FLAG_KERNING | TEXT_RENDER_FLAG_NO_X_BEARING;
	Text().SetRenderFlags(Flags);
	Graphics().m_FailBufferObjectCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	EXPECT_EQ(Text().GetRenderFlags(), Flags);
	auto Retry = Cursor();
	EXPECT_NO_THROW(Text().TextEx(&Retry, "abc"));
	EXPECT_EQ(Text().GetRenderFlags(), Flags);
	EXPECT_EQ(Retry.m_GlyphCount, 3);
	// 仅验证标志和后续绘制；临时容器失败后的全部资源恢复另行处理。
}

TEST_F(QmTextTemporaryLifetime, ImmediateFailurePreservesAnExistingOneTimeUseFlag)
{
	const unsigned Flags = TEXT_RENDER_FLAG_KERNING | TEXT_RENDER_FLAG_ONE_TIME_USE;
	Text().SetRenderFlags(Flags);
	Graphics().m_FailBufferObjectCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	EXPECT_EQ(Text().GetRenderFlags(), Flags);
}

TEST_F(QmTextTemporaryLifetime, DeletingFailedUploadReclaimsTheBufferWithoutAContainer)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	STextContainerIndex Index;
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().CreateTextContainer(Index, &Failed, "abc"), std::bad_alloc);
	ASSERT_TRUE(Index.Valid());
	ASSERT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	Text().DeleteTextContainer(Index);
	EXPECT_FALSE(Index.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes + 1);
	auto Retry = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Index, &Retry, "abc"));
	Text().DeleteTextContainer(Index);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmTextTemporaryLifetime, FailedDirectBufferDeletionKeepsOwnershipForRetry)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	STextContainerIndex Index;
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().CreateTextContainer(Index, &Failed, "abc"), std::bad_alloc);
	ASSERT_TRUE(Index.Valid());
	Graphics().m_FailBufferObjectDelete = true;
	EXPECT_THROW(Text().DeleteTextContainer(Index), std::bad_alloc);
	EXPECT_TRUE(Index.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().DeleteTextContainer(Index));
	EXPECT_FALSE(Index.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes + 2);
}

TEST_F(QmTextTemporaryLifetime, RetryingQuadCleanupDoesNotDeleteAnAlreadyReclaimedBuffer)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	STextContainerIndex Index;
	auto Draw = Cursor(true);
	ASSERT_TRUE(Text().CreateTextContainer(Index, &Draw, "abc"));
	Graphics().m_FailQuadContainerDelete = true;
	EXPECT_THROW(Text().DeleteTextContainer(Index), std::bad_alloc);
	ASSERT_TRUE(Index.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	EXPECT_NO_THROW(Text().DeleteTextContainer(Index));
	EXPECT_FALSE(Index.Valid());
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes);
}

TEST_F(QmTextTemporaryLifetime, DrawingAfterPartialDeletionCreatesANewBufferInsteadOfReusingTheFreedObject)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	STextContainerIndex Index;
	auto Draw = Cursor(true);
	ASSERT_TRUE(Text().CreateTextContainer(Index, &Draw, "abc"));
	Graphics().m_FailQuadContainerDelete = true;
	EXPECT_THROW(Text().DeleteTextContainer(Index), std::bad_alloc);
	ASSERT_TRUE(Index.Valid());
	EXPECT_NO_THROW(Text().RenderTextContainer(Index, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1)));
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers + 1);
	Text().DeleteTextContainer(Index);
	EXPECT_FALSE(Index.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmTextTemporaryLifetime, DeletingUnuploadedTextDoesNotDeleteANonexistentBuffer)
{
	Text().SetRenderFlags(TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD);
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	STextContainerIndex Index;
	auto Draw = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Index, &Draw, "abc"));
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	Text().DeleteTextContainer(Index);
	EXPECT_FALSE(Index.Valid());
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}
