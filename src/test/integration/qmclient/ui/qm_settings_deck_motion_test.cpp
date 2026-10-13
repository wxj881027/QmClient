// 真实 Deck、卡片绘制、导航和动画后端协作；隔离设备与完整菜单依赖。
#include <engine/keys.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/UiNavigation.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <array>

namespace
{
	class QmSettingsDeckMotion : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CSettingsCardDeck m_Deck;
		CSettingsCardDeck m_ReferenceDeck;
		CUiV2AnimationRuntime m_Animation;
		CUiV2AnimationRuntime m_ReferenceAnimation;
		qm_card_order::CModel m_Model;
		qm_card_order::CModel m_ReferenceModel;
		float m_ContentY = 0.0f;
		float m_ReferenceContentY = 0.0f;
		const char *m_pRenderedCardId = nullptr;
		CUIRect m_ContentRect{};
		CLineInputBuffered<64> m_Text;
		FSettingsCardRender m_ContentInput;
		const std::array<CUIRect, 2> m_aTabs{{{20, 20, 100, 24}, {124, 20, 100, 24}}};
		const SSettingsPageLayoutFrame m_Layout = ResolveSettingsPageLayout({20, 60, 300, 400}, false);

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			g_Config.m_QmUiMotionLevel = 2;
			g_Config.m_QmSettingsCardCollapseMigrated = 1;
			m_Model.SetEntries({{"motion:a", "page-a", 1, 0}, {"motion:extra", "page-a", 1, 1}, {"motion:b", "page-b", 1, 0}});
			m_ReferenceModel.SetEntries({{"motion:a", "page-a", 1, 0}, {"motion:extra", "page-a", 1, 1}, {"motion:b", "page-b", 1, 0}});
			Frame(0.0f);
		}

		void Frame(float Dt, vec2 Position = {0, 0}, bool Down = false)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Down ? KEY_MOUSE_1 : 0;
			m_Animation.Advance(Dt);
			m_ReferenceAnimation.Advance(Dt);
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		SSettingsCardDeckResult Draw(CSettingsCardDeck &Deck, CUiV2AnimationRuntime &Animation, qm_card_order::CModel &Model, const bool PageB, float &ContentY, const bool ExtraCard = false)
		{
			m_Ui.SetQmAnimationRuntime(&Animation);
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pTextRender = m_Ui.TextRender();
			// 与设置页相同：背景上下文不携带可写动画指针。
			Ctx.m_pAnim = m_Ui.RenderOnly() ? nullptr : &Animation;
			SSettingsCardDeckInput Input;
			Input.m_MouseX = m_Ui.MouseX();
			Input.m_MouseY = m_Ui.MouseY();
			Input.m_MousePressed = m_Ui.MouseButtonClicked(0);
			Input.m_MouseDown = m_Ui.MouseButton(0);
			Input.m_MouseReleased = !Input.m_MouseDown && m_Ui.LastMouseButton(0);
			const auto Build = [this, PageB, ExtraCard, pContentY = &ContentY](std::vector<SSettingsCardDefinition> &vCards) {
				SSettingsCardDefinition Card;
				Card.m_Spec = {PageB ? "motion:b" : "motion:a", "Card", nullptr};
				Card.m_Measure = [](float) { return 40.0f; };
				Card.m_Render = [this, PageB, pContentY](const CUIRect &Content) {
					*pContentY = Content.y;
					m_pRenderedCardId = PageB ? "motion:b" : "motion:a";
					m_ContentRect = Content;
					if(m_ContentInput)
						m_ContentInput(Content);
				};
				vCards.push_back(std::move(Card));
				if(ExtraCard)
				{
					SSettingsCardDefinition Extra;
					Extra.m_Spec = {"motion:extra", "Extra", nullptr};
					Extra.m_Measure = [](float) { return 40.0f; };
					vCards.push_back(std::move(Extra));
				}
			};
			return Deck.RenderCached(Ctx, m_Layout, PageB ? "page-b" : "page-a", ExtraCard ? 2 : 1, Build, Model, nullptr, Input,
				ResolveCardMotionSpec(g_Config.m_QmUiMotionLevel, true, true, true, true), {});
		}

		void DrawPair(bool PageB = false, bool ExtraCard = false)
		{
			Draw(m_Deck, m_Animation, m_Model, PageB, m_ContentY, ExtraCard);
			Draw(m_ReferenceDeck, m_ReferenceAnimation, m_ReferenceModel, false, m_ReferenceContentY);
		}

		void StartPair()
		{
			m_Deck.BeginDisplayCycle(1, true);
			m_ReferenceDeck.BeginDisplayCycle(1, true);
			DrawPair();
		}
	};
}

TEST_F(QmSettingsDeckMotion, PressSelectsNewCardWithoutWaitingForRelease)
{
	m_Deck.BeginDisplayCycle(1, true);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	Frame(0.04f, {174, 32});
	IUiContext NavCtx;
	NavCtx.m_pUi = &m_Ui;
	NavCtx.m_pAnim = &m_Animation;
	int Selected = ui_widget::CapsuleTabBar(NavCtx, 1234, m_aTabs.data(), 2, 0, {});
	ASSERT_EQ(Selected, 0);
	Frame(0.01f, {174, 32}, true);
	Selected = ui_widget::CapsuleTabBar(NavCtx, 1234, m_aTabs.data(), 2, Selected, {});
	ASSERT_EQ(Selected, 1);
	m_Deck.BeginDisplayCycle(2, true);
	Draw(m_Deck, m_Animation, m_Model, Selected == 1, m_ContentY);
	EXPECT_TRUE(m_Ui.MouseButton(0));
	EXPECT_GT(m_ContentY, m_Layout.m_ContentViewport.y);
	// 新页定义已被真实 Deck 提交，不靠动画完成回调切换正文。
	EXPECT_STREQ(m_pRenderedCardId, "motion:b");
}

TEST_F(QmSettingsDeckMotion, RapidForwardAndReversePagesFollowUninterruptedEntry)
{
	StartPair();
	const float StartY = m_ContentY;
	Frame(0.04f);
	DrawPair();
	ASSERT_LT(m_ContentY, StartY);
	for(int Switch = 0; Switch < 4; ++Switch)
	{
		SCOPED_TRACE(Switch);
		m_Deck.BeginDisplayCycle(Switch + 2, true);
		DrawPair(Switch % 2 == 0);
		EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
		Frame(0.01f);
		DrawPair(Switch % 2 == 0);
		EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
		EXPECT_EQ(m_Animation.QueuedTrackCount(), 0);
	}
}

TEST_F(QmSettingsDeckMotion, ReducedMotionKeepsContinuityWithItsSmallerEntryDistance)
{
	g_Config.m_QmUiMotionLevel = 0;
	m_ReferenceDeck.BeginDisplayCycle(1, true);
	Draw(m_ReferenceDeck, m_ReferenceAnimation, m_ReferenceModel, false, m_ReferenceContentY);
	const float SettledY = m_ReferenceContentY;
	g_Config.m_QmUiMotionLevel = 1;
	m_Deck.BeginDisplayCycle(1, true);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	EXPECT_FLOAT_EQ(m_ContentY - SettledY, 6.0f);
	Frame(0.02f);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	const float BeforeSwitchY = m_ContentY;
	ASSERT_GT(BeforeSwitchY, SettledY);
	ASSERT_LT(BeforeSwitchY, SettledY + 6.0f);
	m_Deck.BeginDisplayCycle(2, true);
	Draw(m_Deck, m_Animation, m_Model, true, m_ContentY);
	EXPECT_FLOAT_EQ(m_ContentY, BeforeSwitchY);
}

TEST_F(QmSettingsDeckMotion, CardListRefreshPreservesEntryUntilItSettles)
{
	StartPair();
	Frame(0.04f);
	DrawPair();
	for(bool Extra : {true, false})
	{
		SCOPED_TRACE(Extra);
		DrawPair(false, Extra);
		EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
		Frame(0.01f);
		DrawPair(false, Extra);
		EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
	}
	for(int FrameIndex = 0; FrameIndex < 60; ++FrameIndex)
	{
		Frame(1.0f / 120.0f);
		DrawPair();
	}
	ASSERT_EQ(m_Animation.ActiveTrackCount(), 0);
	const float SettledY = m_ContentY;
	DrawPair(false, true);
	EXPECT_FLOAT_EQ(m_ContentY, SettledY);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmSettingsDeckMotion, ModelIndexRebuildKeepsTheDeckEntryTrajectory)
{
	StartPair();
	Frame(0.04f);
	DrawPair();
	m_Model.SetEntries({{"motion:extra", "page-a", 1, 1}, {"motion:a", "page-a", 1, 0}, {"motion:b", "page-b", 1, 0}});
	DrawPair();
	EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
	Frame(0.01f);
	DrawPair();
	EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
}

TEST_F(QmSettingsDeckMotion, BackgroundDoesNotConsumeTheFirstVisibleEntry)
{
	m_Deck.BeginDisplayCycle(1, true);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	}
	const float BackgroundY = m_ContentY;
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	EXPECT_FLOAT_EQ(m_ContentY - BackgroundY, ResolveCardMotionSpec(2, true, true, true, true).m_EntryDistance);
	EXPECT_GT(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmSettingsDeckMotion, BackgroundAndReturnKeepTheVisibleEntryPosition)
{
	StartPair();
	Frame(0.04f);
	Draw(m_ReferenceDeck, m_ReferenceAnimation, m_ReferenceModel, false, m_ReferenceContentY);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
		EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
	}
	Frame(0.01f);
	DrawPair();
	EXPECT_NEAR(m_ContentY, m_ReferenceContentY, 0.0001f);
}

TEST_F(QmSettingsDeckMotion, DisabledMotionInBackgroundDoesNotResumeTheOldEntry)
{
	StartPair();
	Frame(0.04f);
	DrawPair();
	g_Config.m_QmUiMotionLevel = 0;
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	}
	const float DisabledY = m_ContentY;
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	EXPECT_FLOAT_EQ(m_ContentY, DisabledY);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	g_Config.m_QmUiMotionLevel = 2;
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	EXPECT_FLOAT_EQ(m_ContentY, DisabledY);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmSettingsDeckMotion, SwitchingPageReleasesOldCardInputAndKeepsItsText)
{
	m_Text.Set("unfinished draft");
	m_ContentInput = [this](const CUIRect &Content) {
		m_Ui.SetActiveItem(&m_Text);
		m_Ui.SetKeyboardFocus(&m_Text);
		m_Ui.DoEditBox(&m_Text, &Content, 12.0f);
	};
	m_Deck.BeginDisplayCycle(1, true);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	ASSERT_TRUE(m_Text.IsActive());
	ASSERT_EQ(m_Ui.KeyboardFocusItem(), &m_Text);
	m_ContentInput = {};
	m_Deck.BeginDisplayCycle(2, true);
	Draw(m_Deck, m_Animation, m_Model, true, m_ContentY);
	EXPECT_FALSE(m_Text.IsActive());
	EXPECT_NE(m_Ui.ActiveItem(), &m_Text);
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
	EXPECT_STREQ(m_Text.GetString(), "unfinished draft");
	m_Deck.BeginDisplayCycle(3, true);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	EXPECT_FALSE(m_Text.IsActive());
	EXPECT_STREQ(m_Text.GetString(), "unfinished draft");
}

TEST_F(QmSettingsDeckMotion, ControlHitAreaFollowsTheVisibleEntryOffset)
{
	int Clicks = 0;
	char ButtonId = 0;
	m_ContentInput = [this, &Clicks, &ButtonId](const CUIRect &Content) {
		Clicks += m_Ui.DoButtonLogic(&ButtonId, 0, &Content, BUTTONFLAG_LEFT) != 0;
	};
	m_Deck.BeginDisplayCycle(1, true);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	const float SettledContentY = m_ContentY - ResolveCardMotionSpec(2, true, true, true, true).m_EntryDistance;
	Frame(0.04f);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	const vec2 VisibleOnlyPoint{m_ContentRect.x + m_ContentRect.w * 0.5f, m_ContentRect.y + m_ContentRect.h - 1.0f};
	ASSERT_GT(VisibleOnlyPoint.y, SettledContentY + m_ContentRect.h);
	// 固定当前时间，仅推进真实鼠标序列；按下和释放都位于画面内、目标矩形外。
	Frame(0.0f, VisibleOnlyPoint);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	Frame(0.0f, VisibleOnlyPoint, true);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	EXPECT_EQ(Clicks, 0);
	Frame(0.0f, VisibleOnlyPoint);
	Draw(m_Deck, m_Animation, m_Model, false, m_ContentY);
	EXPECT_EQ(Clicks, 1);
	m_ContentInput = {};
}
