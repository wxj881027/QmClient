#include <game/client/components/qmclient/qm_title_badge.h>

#include <test/support/qm_real_ui_fixture.h>

class QmTitleBadgeText : public qm_ui_test::CRealUiFixture
{
protected:
	qm_ui_test::CRealUiTestGraphics &Graphics()
	{
		return *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	}
};

TEST_F(QmTitleBadgeText, RealGlyphsStayCenteredAtFractionalChatCoordinates)
{
	auto *pText = m_Ui.TextRender();
	for(const float ScreenHeight : {300.0f, 600.0f})
	{
		Graphics().MapScreen(0.0f, 0.0f, ScreenHeight * 16.0f / 9.0f, ScreenHeight);
		for(const float Height : {6.0f, 9.25f, 24.0f})
		{
			for(const char *pLabel : {"很有问题", "Agpy", "A猫g"})
			{
				SCOPED_TRACE(::testing::Message() << ScreenHeight << ", " << Height << ", " << pLabel);
				SQmTitleBadgePlacement Badge;
				Badge.m_Rect = {17.15f, 21.35f, 96.0f, Height};
				Badge.PrepareText(pText, pLabel);
				ASSERT_TRUE(Badge.m_TextContainerIndex.Valid());
				ASSERT_GT(Badge.m_TextVisualBounds.h, 0.0f);
				Graphics().m_vTextSubmissions.clear();
				const vec2 AnimationOffset(0.23f, 0.37f);
				Badge.RenderText(pText, 1.0f, AnimationOffset);
				ASSERT_EQ(Graphics().m_vTextSubmissions.size(), 1u);
				const auto &Submission = Graphics().m_vTextSubmissions.front();
				// 读取真实文字绘制提交时的投影，验证最终字形中心而非预先计算的位置。
				const float CenterY = Badge.m_TextVisualBounds.y + Badge.m_TextVisualBounds.h * 0.5f - Submission.m_Screen.y;
				const float CenterX = Badge.m_TextVisualBounds.x + Badge.m_TextVisualBounds.w * 0.5f - Submission.m_Screen.x;
				EXPECT_NEAR(CenterY, Badge.m_Rect.y + Height * 0.5f + AnimationOffset.y, 0.0001f);
				EXPECT_NEAR(CenterX, Badge.m_Rect.x + Badge.m_Rect.w * 0.5f + 10.5f * Height / 24.0f + AnimationOffset.x, 0.0001f);
				EXPECT_FLOAT_EQ(Submission.m_OutlineColor.a, 0.0f);
				Badge.DeleteText(pText);
			}
		}
	}
}

TEST_F(QmTitleBadgeText, PreparingBadgePreservesChatContainerAndRendererState)
{
	auto *pText = m_Ui.TextRender();
	const unsigned Flags = TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD;
	const ColorRGBA ChatColor(0.2f, 0.3f, 0.4f, 0.5f);
	pText->SetRenderFlags(Flags);
	pText->TextColor(ChatColor);
	CTextCursor ChatCursor;
	ChatCursor.m_FontSize = 12.0f;
	ChatCursor.m_LineWidth = 200.0f;
	STextContainerIndex ChatContainer;
	pText->CreateOrAppendTextContainer(ChatContainer, &ChatCursor, "1: ");
	const auto Before = pText->GetBoundingBoxTextContainer(ChatContainer);

	SQmTitleBadgePlacement Badge;
	Badge.m_Rect = {30.0f, 20.0f, 100.0f, 24.0f};
	Badge.PrepareText(pText, "很有问题");
	ASSERT_TRUE(Badge.m_TextContainerIndex.Valid());
	EXPECT_NE(Badge.m_TextContainerIndex.m_Index, ChatContainer.m_Index);
	EXPECT_EQ(pText->GetRenderFlags(), Flags);
	EXPECT_EQ(pText->GetTextColor(), ChatColor);
	const auto After = pText->GetBoundingBoxTextContainer(ChatContainer);
	EXPECT_FLOAT_EQ(Before.m_W, After.m_W);
	EXPECT_FLOAT_EQ(Before.m_H, After.m_H);

	Badge.DeleteText(pText);
	EXPECT_TRUE(ChatContainer.Valid());
	pText->DeleteTextContainer(ChatContainer);
}

TEST_F(QmTitleBadgeText, TextOpacityCombinesAppearanceAndChatFadeOnce)
{
	auto *pText = m_Ui.TextRender();
	SQmTitleBadgePlacement Badge;
	Badge.m_Rect = {10.0f, 20.0f, 100.0f, 24.0f};
	Badge.m_Appearance.m_Opacity = 40;
	Badge.PrepareText(pText, "头衔");
	Graphics().m_vTextSubmissions.clear();
	Badge.RenderText(pText, 0.5f, vec2(0.0f, 0.0f));
	ASSERT_EQ(Graphics().m_vTextSubmissions.size(), 1u);
	EXPECT_FLOAT_EQ(Graphics().m_vTextSubmissions.front().m_TextColor.a, 0.2f);
	EXPECT_FLOAT_EQ(Graphics().m_vTextSubmissions.front().m_OutlineColor.a, 0.0f);
	Badge.DeleteText(pText);
}

TEST_F(QmTitleBadgeText, RebuildingAndDeletingReleasesTheOwnedTextResources)
{
	auto *pText = m_Ui.TextRender();
	const auto InitialContainers = Graphics().m_Containers.size();
	const auto InitialBuffers = Graphics().m_Buffers.size();
	SQmTitleBadgePlacement Badge;
	Badge.m_Rect = {10.0f, 20.0f, 150.0f, 12.0f};
	Badge.PrepareText(pText, "A");
	ASSERT_TRUE(Badge.m_TextContainerIndex.Valid());
	const float InitialHeight = Badge.m_TextVisualBounds.h;
	Badge.m_Rect.h = 24.0f;
	Badge.PrepareText(pText, "Agpy");
	ASSERT_TRUE(Badge.m_TextContainerIndex.Valid());
	EXPECT_GT(Badge.m_TextVisualBounds.h, InitialHeight);
	Badge.DeleteText(pText);
	EXPECT_FALSE(Badge.m_TextContainerIndex.Valid());
	EXPECT_EQ(Graphics().m_Containers.size(), InitialContainers);
	EXPECT_EQ(Graphics().m_Buffers.size(), InitialBuffers);
	Badge.DeleteText(pText);
	EXPECT_FALSE(Badge.m_TextContainerIndex.Valid());
}
