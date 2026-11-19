#include <game/client/components/qmclient/nameplate_text_build.h>

#include <test/support/qm_real_ui_fixture.h>

#include <array>
#include <new>

namespace
{
	class QmNameplateTextBuildIntegration : public qm_ui_test::CRealUiFixture
	{
	protected:
		STextContainerIndex m_Index;
		CQmNameplateTextBuild m_Build;
		CQmNameplateTextCache m_Cache;
		bool m_Reuse = false;
		static constexpr unsigned SAVED_FLAGS = TEXT_RENDER_FLAG_KERNING | TEXT_RENDER_FLAG_NO_X_BEARING;
		ITextRender &Text() { return *m_Ui.TextRender(); }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(::testing::Test::HasFatalFailure())
				return;
			Text().SetRenderFlags(SAVED_FLAGS);
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
				m_Build.Reset(Text(), m_Index);
			}
			CRealUiFixture::TearDown();
		}
		bool Build(bool KeepPrevious, const char *pText, bool Caret = false, bool ThrowAfterBuild = false)
		{
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(10, 20));
			Cursor.m_FontSize = 14;
			Cursor.m_LineWidth = 200;
			if(Caret)
			{
				Cursor.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_SET;
				Cursor.m_CursorCharacter = 0;
			}
			CQmNameplateTextContext Scope(Text(), Graphics(), true);
			Scope.Apply(TEXT_RENDER_FLAG_QM_NAMEPLATE, [&] { Graphics().MapScreen(10, 20, 310, 220); });
			return m_Build.Run(Text(), m_Index, m_Reuse, KeepPrevious, m_Cache, [&] {
				if(m_Reuse)
					QmUpdateNameplateTextContainer(&Text(), m_Index, &Cursor, pText);
				else
					Text().CreateOrAppendTextContainer(m_Index, &Cursor, pText);
				if(ThrowAfterBuild)
					throw 7;
				Scope.Finish();
			});
		}
		void ExpectContextRestored()
		{
			EXPECT_EQ(Text().GetRenderFlags(), SAVED_FLAGS);
			std::array<float, 4> Screen;
			Graphics().GetScreen(&Screen[0], &Screen[1], &Screen[2], &Screen[3]);
			EXPECT_EQ(Screen, (std::array<float, 4>{1, 2, 301, 202}));
		}
		void PrepareUploadFailure()
		{
			Graphics().m_FailBufferContainerCreate = true;
			EXPECT_THROW(Build(true, "new density"), std::bad_alloc);
		}
	};
}

TEST_F(QmNameplateTextBuildIntegration, SuccessfulDensityReplacementReturnsToOneContainer)
{
	ASSERT_TRUE(Build(false, "old density"));
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	const int OldSlot = m_Index.m_Index;
	ASSERT_TRUE(Build(true, "new density"));
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_NE(m_Index.m_Index, OldSlot);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}

TEST_F(QmNameplateTextBuildIntegration, FailedDensityUploadRetainsOldAndRestoresContextBeforeRetry)
{
	ASSERT_TRUE(Build(false, "old density"));
	m_Cache.OnUpdate(Text().GlyphAtlasRevision());
	const int OldSlot = m_Index.m_Index;
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	PrepareUploadFailure();
	ASSERT_TRUE(m_Index.Valid());
	EXPECT_EQ(m_Index.m_Index, OldSlot);
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	m_Build.Cleanup(Text());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	ASSERT_TRUE(Build(true, "new density"));
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmNameplateTextBuildIntegration, FailedPendingDeletionPreventsAnotherNativeCreation)
{
	ASSERT_TRUE(Build(false, "old density"));
	const int OldSlot = m_Index.m_Index;
	const size_t Buffers = Graphics().m_Buffers.size();
	PrepareUploadFailure();
	Graphics().m_BufferObjectDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure::ALLOCATION;
	bool Created = false;
	EXPECT_THROW(m_Build.Run(Text(), m_Index, m_Reuse, true, m_Cache, [&] { Created = true; }), std::bad_alloc);
	EXPECT_FALSE(Created);
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_EQ(m_Index.m_Index, OldSlot);
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	m_Build.Cleanup(Text());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	ASSERT_TRUE(Build(true, "recovered density"));
}

TEST_F(QmNameplateTextBuildIntegration, OldDeletionFailureKeepsBothSlotsOwnedUntilCleanup)
{
	ASSERT_TRUE(Build(false, "old density", true));
	m_Cache.OnUpdate(Text().GlyphAtlasRevision());
	const int OldSlot = m_Index.m_Index;
	Graphics().m_FailQuadContainerDelete = true;
	EXPECT_THROW(Build(true, "new density"), std::bad_alloc);
	ASSERT_TRUE(m_Index.Valid());
	EXPECT_NE(m_Index.m_Index, OldSlot);
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	m_Build.Cleanup(Text());
	EXPECT_NO_THROW(Text().RenderTextContainer(m_Index, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1)));
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}

TEST_F(QmNameplateTextBuildIntegration, EmptyInitialTextCompletesAsAValidEmptyResult)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	ASSERT_TRUE(Build(false, ""));
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	ExpectContextRestored();
}

TEST_F(QmNameplateTextBuildIntegration, EmptyDensityCandidateKeepsThePreviouslyDisplayedContainer)
{
	ASSERT_TRUE(Build(false, "old density"));
	const int OldSlot = m_Index.m_Index;
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	EXPECT_FALSE(Build(true, ""));
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_EQ(m_Index.m_Index, OldSlot);
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}

TEST_F(QmNameplateTextBuildIntegration, OrdinaryUploadFailureHidesPartialTextAndAllowsSameContentRetry)
{
	ASSERT_TRUE(Build(false, "old name"));
	m_Cache.OnUpdate(Text().GlyphAtlasRevision());
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Build(false, "changed name"), std::bad_alloc);
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	ASSERT_TRUE(Build(false, "changed name"));
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}

TEST_F(QmNameplateTextBuildIntegration, ReusedContainerCallbackFailureKeepsItsResourcesForCleanup)
{
	m_Reuse = true;
	ASSERT_TRUE(Build(false, "X:1.00"));
	m_Cache.OnUpdate(Text().GlyphAtlasRevision());
	EXPECT_THROW(Build(false, "X:2.00", false, true), int);
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_TRUE(m_Reuse);
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	ASSERT_TRUE(Build(false, "X:2.00"));
	EXPECT_TRUE(m_Index.Valid());
}

TEST_F(QmNameplateTextBuildIntegration, ResetAfterFailedCreationReleasesBothNativeContainers)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	ASSERT_TRUE(Build(false, "old density"));
	PrepareUploadFailure();
	m_Build.Reset(Text(), m_Index);
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	EXPECT_NO_THROW(Text().OnPreWindowResize());
}
