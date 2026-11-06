#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmTextTemporaryLifetime : public qm_ui_test::CRealUiFixture
	{
	protected:
		ITextRender &Text() { return *m_Ui.TextRender(); }
		IEngineTextRender &Engine() { return *static_cast<IEngineTextRender *>(m_Ui.TextRender()); }
		using EDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure;
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
		int ReusableSlot()
		{
			STextContainerIndex Index;
			auto Draw = Cursor();
			if(!Text().CreateTextContainer(Index, &Draw, "abc"))
			{
				ADD_FAILURE() << "Failed to prepare reusable text container";
				return -1;
			}
			const int Slot = Index.m_Index;
			Text().DeleteTextContainer(Index);
			return Slot;
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
	// 本项只验证标志和后续绘制，延期回收由对应资源用例验证。
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
	Graphics().m_BufferObjectDeleteFailure = EDeleteFailure::ALLOCATION;
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

TEST_F(QmTextTemporaryLifetime, FrameEndReturnsTheCpuSlotAfterImmediateBufferCreationFails)
{
	const int Slot = ReusableSlot();
	ASSERT_GE(Slot, 0);
	Graphics().m_FailBufferObjectCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	STextContainerIndex Index;
	auto Retry = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Index, &Retry, "abc"));
	EXPECT_EQ(Index.m_Index, Slot);
	Text().DeleteTextContainer(Index);
}

TEST_F(QmTextTemporaryLifetime, FrameEndReclaimsTheImmediateBufferWhoseContainerWasNotCreated)
{
	const int Slot = ReusableSlot();
	ASSERT_GE(Slot, 0);
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	ASSERT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	STextContainerIndex Index;
	auto Retry = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Index, &Retry, "abc"));
	EXPECT_EQ(Index.m_Index, Slot);
	Text().DeleteTextContainer(Index);
}

TEST_F(QmTextTemporaryLifetime, FrameEndReclaimsAnImmediateContainerAfterDrawingThrows)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailTextDraw = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	ASSERT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	ASSERT_EQ(Graphics().m_Containers.size(), Containers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmTextTemporaryLifetime, FrameEndRetriesTheRemainingCaretContainerAfterImmediateDeletionFails)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Quads = Graphics().QuadContainerCount();
	Graphics().m_FailQuadContainerDelete = true;
	auto Failed = Cursor(true);
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	ASSERT_EQ(Graphics().QuadContainerCount(), Quads + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().QuadContainerCount(), Quads);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
}

TEST_F(QmTextTemporaryLifetime, FailedDeferredBufferDeletionKeepsTheOwnerForTheNextFrame)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	Graphics().m_BufferObjectDeleteFailure = EDeleteFailure::ALLOCATION;
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes + 2);
}

TEST_F(QmTextTemporaryLifetime, DeferredDeletionLengthFailureAlsoRetainsTheBufferForRetry)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	Graphics().m_BufferObjectDeleteFailure = EDeleteFailure::LENGTH;
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
}

TEST_F(QmTextTemporaryLifetime, UnrelatedDeferredDeletionErrorPropagatesWithoutLosingTheOwner)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	Graphics().m_BufferObjectDeleteFailure = EDeleteFailure::OTHER;
	EXPECT_THROW(Text().QmTextFrameEnd(), std::logic_error);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
}

TEST_F(QmTextTemporaryLifetime, OneFrameReclaimsMultipleFailedImmediateContainers)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	for(int Attempt = 0; Attempt < 2; ++Attempt)
	{
		Graphics().m_FailBufferContainerCreate = true;
		auto Failed = Cursor();
		EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	}
	ASSERT_EQ(Graphics().m_Buffers.size(), Buffers + 2);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes);
}

TEST_F(QmTextTemporaryLifetime, DeferredImmediateCleanupKeepsAnOrdinaryOwnedContainerAlive)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	STextContainerIndex Owned;
	auto Draw = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Owned, &Draw, "abc"));
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	ASSERT_TRUE(Owned.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().RenderTextContainer(Owned, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1)));
	Text().DeleteTextContainer(Owned);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
}

TEST_F(QmTextTemporaryLifetime, DeviceResetDiscardsPendingIndicesBeforeTheSlotsAreReused)
{
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	Graphics().ResetDeviceBuffers();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	ASSERT_TRUE(Engine().OnGraphicsResourcesReset());
	STextContainerIndex Owned;
	auto Draw = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Owned, &Draw, "abc"));
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_TRUE(Owned.Valid());
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes);
	EXPECT_NO_THROW(Text().RenderTextContainer(Owned, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1)));
	Text().DeleteTextContainer(Owned);
}

TEST_F(QmTextTemporaryLifetime, ResizeKeepsFailedPendingCleanupSeparateFromOrdinaryOwnedText)
{
	const size_t Buffers = Graphics().m_Buffers.size();
	STextContainerIndex Owned;
	auto Draw = Cursor();
	ASSERT_TRUE(Text().CreateTextContainer(Owned, &Draw, "abc"));
	Graphics().m_FailBufferContainerCreate = true;
	auto Failed = Cursor();
	EXPECT_THROW(Text().TextEx(&Failed, "abc"), std::bad_alloc);
	Graphics().m_BufferObjectDeleteFailure = EDeleteFailure::LENGTH;
	EXPECT_NO_THROW(Engine().OnPreWindowResize());
	EXPECT_NO_THROW(Engine().OnWindowResize());
	EXPECT_TRUE(Owned.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 2);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_TRUE(Owned.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	Text().DeleteTextContainer(Owned);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
}
