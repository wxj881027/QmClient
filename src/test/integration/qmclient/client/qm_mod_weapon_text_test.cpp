#include <game/client/components/tclient/qm_mod_weapon_text.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmModWeaponTextLifetime : public qm_ui_test::CRealUiFixture
	{
	protected:
		ITextRender &Text() { return *m_Ui.TextRender(); }
		using EDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure;
		std::optional<float> Draw(const char *pText = "rcon kill_pl", float FontSize = 15, float Y = 200)
		{
			return QmRenderModWeaponText(Text(), pText, FontSize, {100, Y}, {1, 1, 1, 1});
		}
	};
}

TEST_F(QmModWeaponTextLifetime, TimerAndCommandReleaseTheirBuffersAndPreserveIncomingFlags)
{
	Text().SetRenderFlags(TEXT_RENDER_FLAG_NO_X_BEARING);
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	const auto Draws = Graphics().m_Draws;
	const auto Height = Draw("1.50", 25);
	ASSERT_TRUE(Height);
	EXPECT_GT(*Height, 0);
	EXPECT_TRUE(Draw("rcon kill_pl", 15, 200 + *Height + 15));
	EXPECT_EQ(Text().GetRenderFlags(), TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Graphics().m_Draws, Draws + 2);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmModWeaponTextLifetime, PartialUploadKeepsItsBufferUntilExistingFrameCleanupCanSucceed)
{
	Text().SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Draw(), std::bad_alloc);
	EXPECT_EQ(Text().GetRenderFlags(), TEXT_RENDER_FLAG_KERNING);
	ASSERT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	Graphics().m_BufferObjectDeleteFailure = EDeleteFailure::LENGTH;
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_TRUE(Draw());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmModWeaponTextLifetime, DrawingFailureTransfersBothBuffersWithoutChangingFlags)
{
	Text().SetRenderFlags(TEXT_RENDER_FLAG_NO_X_BEARING);
	const auto ScreenBefore = static_cast<IGraphics &>(Graphics()).GetScreen();
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailTextDraw = true;
	EXPECT_THROW(Draw(), std::bad_alloc);
	EXPECT_EQ(Text().GetRenderFlags(), TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers + 1);
	const auto ScreenAfter = static_cast<IGraphics &>(Graphics()).GetScreen();
	EXPECT_EQ(ScreenAfter.m_TopLeft, ScreenBefore.m_TopLeft);
	EXPECT_EQ(ScreenAfter.m_BottomRight, ScreenBefore.m_BottomRight);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmModWeaponTextLifetime, FailedImmediateDeletionIsRetriedByTheExistingTextOwner)
{
	Text().SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
	const size_t Buffers = Graphics().m_Buffers.size();
	const int Deletes = Graphics().m_DirectBufferObjectDeleteCalls;
	Graphics().m_BufferObjectDeleteFailure = EDeleteFailure::ALLOCATION;
	EXPECT_THROW(Draw(), std::bad_alloc);
	EXPECT_EQ(Text().GetRenderFlags(), TEXT_RENDER_FLAG_KERNING);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes + 1);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_DirectBufferObjectDeleteCalls, Deletes + 2);
}

TEST_F(QmModWeaponTextLifetime, ReclaimingFailedModTextKeepsAnIndependentContainerAlive)
{
	STextContainerIndex Owned;
	CTextCursor Cursor;
	Cursor.m_FontSize = 15;
	ASSERT_TRUE(Text().CreateTextContainer(Owned, &Cursor, "ordinary text"));
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Draw(), std::bad_alloc);
	EXPECT_NO_THROW(Text().QmTextFrameEnd());
	ASSERT_TRUE(Owned.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	EXPECT_NO_THROW(Text().RenderTextContainer(Owned, {1, 1, 1, 1}, {0, 0, 0, 1}));
	Text().DeleteTextContainer(Owned);
}

TEST_F(QmModWeaponTextLifetime, EmptyLabelHasNoHeightDrawOrRetainedResources)
{
	Text().SetRenderFlags(TEXT_RENDER_FLAG_NO_X_BEARING);
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	const auto Draws = Graphics().m_Draws;
	EXPECT_FALSE(Draw(""));
	EXPECT_EQ(Text().GetRenderFlags(), TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Graphics().m_Draws, Draws);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}
