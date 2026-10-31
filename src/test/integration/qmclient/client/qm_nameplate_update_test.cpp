#include <game/client/components/qmclient/nameplate_text_build.h>
#include <game/client/components/qmclient/nameplate_update.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmNameplateUpdateIntegration : public qm_ui_test::CRealUiFixture
	{
	protected:
		STextContainerIndex m_aIndices[2];
		CQmNameplateTextBuild m_aBuild[2];
		CQmNameplateTextCache m_aCache[2];
		bool m_aReuse[2]{};
		CQmNameplateUpdate m_Update;
		ITextRender &Text() { return *m_Ui.TextRender(); }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(::testing::Test::HasFatalFailure())
				return;
			Text().SetFontPreset(EFontPreset::DEFAULT_FONT);
			Text().TextColor(ColorRGBA(0.2f, 0.4f, 0.6f, 0.8f));
			Text().SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
			Graphics().MapScreen(1, 2, 301, 202);
		}
		void ResetParts()
		{
			m_Update.Reset();
			for(int Part = 0; Part < 2; ++Part)
				m_aBuild[Part].Reset(Text(), m_aIndices[Part]);
		}
		void TearDown() override
		{
			if(m_Ui.TextRender())
			{
				Graphics().m_FailBufferObjectCreate = false;
				Graphics().m_FailBufferContainerCreate = false;
				Graphics().m_BufferObjectDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure::NONE;
				Graphics().m_FailQuadContainerDelete = false;
				ResetParts();
			}
			CRealUiFixture::TearDown();
		}
		bool BuildPart(int Part, const char *pText, bool KeepPrevious)
		{
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(10, 20));
			Cursor.m_FontSize = 14;
			CQmNameplateTextContext Context(Text(), Graphics(), true);
			Context.Apply(TEXT_RENDER_FLAG_QM_NAMEPLATE, [&] { Graphics().MapScreen(10, 20, 310, 220); });
			if(!m_aBuild[Part].Run(Text(), m_aIndices[Part], m_aReuse[Part], KeepPrevious && m_aIndices[Part].Valid(), m_aCache[Part], [&] {
				   Text().CreateOrAppendTextContainer(m_aIndices[Part], &Cursor, pText);
				   Context.Finish();
			   }))
				return false;
			m_aCache[Part].OnUpdate(Text().GlyphAtlasRevision());
			return true;
		}
		bool Prepare(bool FailSecond = false, bool KeepPrevious = false)
		{
			return m_Update.Run([&] {
				if(!BuildPart(0, "first", KeepPrevious))
					return false;
				if(FailSecond)
					Graphics().m_FailBufferContainerCreate = true;
				return BuildPart(1, "second", KeepPrevious);
			});
		}
		void ExpectContextRestored()
		{
			EXPECT_EQ(Text().GetRenderFlags(), unsigned(TEXT_RENDER_FLAG_KERNING));
			float X0, Y0, X1, Y1;
			Graphics().GetScreen(&X0, &Y0, &X1, &Y1);
			EXPECT_EQ(X0, 1);
			EXPECT_EQ(Y0, 2);
			EXPECT_EQ(X1, 301);
			EXPECT_EQ(Y1, 202);
		}
	};
}

TEST_F(QmNameplateUpdateIntegration, MultiPartUploadFailureKeepsOwnersUntilACompleteRetry)
{
	const size_t InitialBuffers = Graphics().m_Buffers.size(), InitialContainers = Graphics().m_Containers.size();
	ASSERT_TRUE(Prepare());
	const int OldSecond = m_aIndices[1].m_Index;
	EXPECT_FALSE(Prepare(true, true));
	EXPECT_FALSE(m_Update.Ready());
	EXPECT_TRUE(m_aIndices[0].Valid());
	EXPECT_TRUE(m_aIndices[1].Valid());
	EXPECT_EQ(m_aIndices[1].m_Index, OldSecond);
	EXPECT_FALSE(m_aCache[0].NeedsUpdate(true, false));
	EXPECT_TRUE(m_aCache[1].NeedsUpdate(true, false));
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	ASSERT_TRUE(Prepare(false, true));
	EXPECT_TRUE(m_Update.Ready());
	for(const auto &Index : m_aIndices)
		EXPECT_NO_THROW(Text().RenderTextContainer(Index, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1)));
	ResetParts();
	EXPECT_EQ(Graphics().m_Buffers.size(), InitialBuffers);
	EXPECT_EQ(Graphics().m_Containers.size(), InitialContainers);
}

TEST_F(QmNameplateUpdateIntegration, InitialPartialPreparationCanBeResetAndRetried)
{
	const size_t Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	EXPECT_FALSE(Prepare(true));
	EXPECT_FALSE(m_Update.Ready());
	EXPECT_TRUE(m_aIndices[0].Valid());
	EXPECT_FALSE(m_aIndices[1].Valid());
	ExpectContextRestored();
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	ResetParts();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	ASSERT_TRUE(Prepare());
	EXPECT_TRUE(m_Update.Ready());
}

TEST_F(QmNameplateUpdateIntegration, PendingDeletionFailureRemainsContainedUntilLaterRetry)
{
	ASSERT_TRUE(Prepare());
	EXPECT_FALSE(Prepare(true, true));
	Graphics().m_BufferObjectDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure::ALLOCATION;
	EXPECT_FALSE(Prepare(false, true));
	EXPECT_FALSE(m_Update.Ready());
	EXPECT_TRUE(m_aIndices[0].Valid());
	EXPECT_TRUE(m_aIndices[1].Valid());
	EXPECT_NO_THROW(Text().OnPreWindowResize());
	ASSERT_TRUE(Prepare(false, true));
	EXPECT_TRUE(m_Update.Ready());
	ExpectContextRestored();
}

TEST_F(QmNameplateUpdateIntegration, TitleColorOverrideRestoresAfterBuildAllocationFailure)
{
	const ColorRGBA Previous = Text().GetTextColor();
	EXPECT_THROW(QmWithNameplateTextColor(Text(), ColorRGBA(1, 1, 1, 1), [] { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_EQ(Text().GetTextColor(), Previous);
}

TEST_F(QmNameplateUpdateIntegration, FontOverrideFailurePreservesTheOriginalColorForTheDefaultFont)
{
	const ColorRGBA Previous = Text().GetTextColor();
	EXPECT_THROW(QmWithNameplateFontPreset(Text(), EFontPreset::ICON_FONT, [] { throw std::length_error("nameplate"); }), std::length_error);
	EXPECT_EQ(Text().GetFontPreset(), EFontPreset::DEFAULT_FONT);
	EXPECT_EQ(Text().GetTextColor(), Previous);
	Text().SetFontPreset(EFontPreset::DEFAULT_FONT);
	EXPECT_EQ(Text().GetTextColor(), Previous);
}

TEST_F(QmNameplateUpdateIntegration, SuccessfulNestedStyleOverridesRestoreTheCallerState)
{
	const ColorRGBA Previous = Text().GetTextColor();
	QmWithNameplateTextColor(Text(), ColorRGBA(1, 1, 1, 1), [&] {
		QmWithNameplateFontPreset(Text(), EFontPreset::ICON_FONT, [&] {
			EXPECT_EQ(Text().GetFontPreset(), EFontPreset::ICON_FONT);
			EXPECT_TRUE(BuildPart(0, "A", false));
		});
		EXPECT_EQ(Text().GetFontPreset(), EFontPreset::DEFAULT_FONT);
		EXPECT_EQ(Text().GetTextColor(), ColorRGBA(1, 1, 1, 1));
	});
	EXPECT_EQ(Text().GetFontPreset(), EFontPreset::DEFAULT_FONT);
	EXPECT_EQ(Text().GetTextColor(), Previous);
	ExpectContextRestored();
}

TEST_F(QmNameplateUpdateIntegration, UnexpectedBuildExceptionRestoresNestedStylesAndPropagates)
{
	const ColorRGBA Previous = Text().GetTextColor();
	EXPECT_THROW(QmWithNameplateTextColor(Text(), ColorRGBA(1, 1, 1, 1), [&] {
		QmWithNameplateFontPreset(Text(), EFontPreset::ICON_FONT, [] { throw 7; });
	}),
		int);
	EXPECT_EQ(Text().GetFontPreset(), EFontPreset::DEFAULT_FONT);
	EXPECT_EQ(Text().GetTextColor(), Previous);
}
