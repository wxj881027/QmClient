// 投票页「预览地图」入口的真实交互闭环：图标按钮 → 二级面板 → Esc / 点击关闭。
// 面板其余行为（排版、事实文字）用同一组件的生产入口直接断言，不复制实现。
#include <engine/keys.h>

#include <game/client/QmUi/QmVoteMapPreview.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmVoteMapPreviewInteraction : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CButtonContainer m_Button;
		SPopupMenuId m_Popup;
		qm_vote_map_preview::SContext m_Context;
		CUIRect m_ButtonRect{40.0f, 40.0f, 26.0f, 26.0f};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 封面纹理属于页面侧，这里只验证入口与外壳：内容回调留空，面板仍须完整开关。
			qm_vote_map_preview::SInfo Info;
			Info.m_Name = "Multea";
			Info.m_Mapper = "Nudelsaft";
			Info.m_Category = "Moderate";
			Info.m_Stars = 4;
			Info.m_Points = 21;
			m_Context.m_Info = Info;
			m_Context.m_CoverAspect = 2.32f;
			Frame({10.0f, 10.0f}, false);
		}

		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		vec2 ButtonCenter() const { return m_ButtonRect.Center(); }

		bool DrawButton(const bool Enabled = true)
		{
			return qm_vote_map_preview::DoButton(ui_widget::ControlContext(&m_Ui), &m_Button, &m_Popup, &m_Context, m_ButtonRect, Enabled);
		}

		// 按下 → 松开才算一次点击；先画一帧让按钮进入热区。
		void ClickButton(const bool Enabled = true)
		{
			Frame(ButtonCenter(), false);
			DrawButton(Enabled);
			Frame(ButtonCenter(), true);
			DrawButton(Enabled);
			Frame(ButtonCenter(), false);
			DrawButton(Enabled);
		}

		void Key(int Code)
		{
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = IInput::FLAG_PRESS;
			ASSERT_TRUE(m_Ui.OnInput(Event));
		}
	};
}

TEST_F(QmVoteMapPreviewInteraction, ClickingTheIconButtonOpensTheSecondaryPanel)
{
	Frame(ButtonCenter(), false);
	EXPECT_FALSE(DrawButton());
	Frame(ButtonCenter(), true);
	EXPECT_FALSE(DrawButton());
	Frame(ButtonCenter(), false);
	EXPECT_TRUE(DrawButton());
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
	// 面板走与服务器列表预览同一套二级面板外壳：整块面板矩形，居中显示。
	const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*m_Ui.Screen());
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Popup);
	ASSERT_NE(pPopup, nullptr);
	EXPECT_FLOAT_EQ(pPopup->w, PanelRect.w);
	EXPECT_FLOAT_EQ(pPopup->h, PanelRect.h);
}

TEST_F(QmVoteMapPreviewInteraction, EscapeClosesThePanelAndTheButtonCanReopenIt)
{
	ClickButton();
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Popup));

	Key(KEY_ESCAPE);
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Popup));

	// 关掉后入口仍然有效：Esc 只收面板，不禁用按钮。
	ClickButton();
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
}

TEST_F(QmVoteMapPreviewInteraction, DisabledButtonWithoutATargetNeverOpensThePanel)
{
	Frame(ButtonCenter(), false);
	DrawButton(false);
	Frame(ButtonCenter(), true);
	DrawButton(false);
	Frame(ButtonCenter(), false);
	EXPECT_FALSE(DrawButton(false));
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Popup));
}

TEST(QmVoteMapPreviewFacts, MissingValuesKeepTheirRowWithAPlaceholder)
{
	qm_vote_map_preview::SInfo Info;
	Info.m_Name = "Multea";
	char aBuf[192];
	qm_vote_map_preview::FormatFact(qm_vote_map_preview::EFact::MAPPER, Info, aBuf, sizeof(aBuf));
	ASSERT_NE(str_find(aBuf, "—"), nullptr);
	qm_vote_map_preview::FormatFact(qm_vote_map_preview::EFact::DIFFICULTY, Info, aBuf, sizeof(aBuf));
	ASSERT_NE(str_find(aBuf, "—"), nullptr);

	Info.m_Mapper = "Nudelsaft";
	Info.m_Stars = 4;
	Info.m_Points = 21;
	qm_vote_map_preview::FormatFact(qm_vote_map_preview::EFact::MAPPER, Info, aBuf, sizeof(aBuf));
	EXPECT_NE(str_find(aBuf, "Nudelsaft"), nullptr);
	qm_vote_map_preview::FormatFact(qm_vote_map_preview::EFact::DIFFICULTY, Info, aBuf, sizeof(aBuf));
	EXPECT_NE(str_find(aBuf, "4 / 5"), nullptr);
	qm_vote_map_preview::FormatFact(qm_vote_map_preview::EFact::POINTS, Info, aBuf, sizeof(aBuf));
	EXPECT_NE(str_find(aBuf, "21"), nullptr);
}

TEST(QmVoteMapPreviewLayout, WidePanelPutsTheCoverLeftOfTheFactsWithoutOverlap)
{
	const CUIRect Content{0.0f, 0.0f, 480.0f, 240.0f};
	const auto Layout = qm_vote_map_preview::ResolveLayout(Content, 2.32f, 16.0f);
	ASSERT_FALSE(Layout.m_Stacked);
	ASSERT_EQ(Layout.m_NumFactRows, 3);
	EXPECT_LE(Layout.m_Cover.x + Layout.m_Cover.w, Layout.m_Facts.x);
	EXPECT_LE(Layout.m_Cover.y + Layout.m_Cover.h, Content.y + Content.h);
	for(int Index = 0; Index < Layout.m_NumFactRows; ++Index)
	{
		const CUIRect &Row = Layout.m_aFactRows[Index];
		EXPECT_GE(Row.x, Layout.m_Facts.x);
		EXPECT_LE(Row.x + Row.w, Layout.m_Facts.x + Layout.m_Facts.w);
		if(Index > 0)
			EXPECT_GE(Row.y, Layout.m_aFactRows[Index - 1].y + Layout.m_aFactRows[Index - 1].h);
	}
}

TEST(QmVoteMapPreviewLayout, NarrowPanelStacksTheCoverAboveTheFacts)
{
	const CUIRect Content{0.0f, 0.0f, 90.0f, 240.0f};
	const auto Layout = qm_vote_map_preview::ResolveLayout(Content, 2.32f, 16.0f);
	ASSERT_TRUE(Layout.m_Stacked);
	EXPECT_GE(Layout.m_Facts.y, Layout.m_Cover.y + Layout.m_Cover.h);
	EXPECT_LE(Layout.m_Facts.x + Layout.m_Facts.w, Content.x + Content.w);
}

TEST(QmVoteMapPreviewLayout, ShortPanelKeepsOnlyCompleteFactRows)
{
	// 20 高只放得下一行 16 的事实行：登记半行会让文字压到下一条事实。
	const CUIRect Content{0.0f, 0.0f, 480.0f, 20.0f};
	const auto Layout = qm_vote_map_preview::ResolveLayout(Content, 2.32f, 16.0f);
	ASSERT_EQ(Layout.m_NumFactRows, 1);
	EXPECT_FLOAT_EQ(Layout.m_aFactRows[0].h, 16.0f);
}

TEST(QmVoteMapPreviewLayout, DegenerateContentNeverProducesNegativeRects)
{
	const CUIRect Empty{0.0f, 0.0f, 0.0f, 0.0f};
	const auto EmptyLayout = qm_vote_map_preview::ResolveLayout(Empty, 2.32f, 16.0f);
	EXPECT_EQ(EmptyLayout.m_NumFactRows, 0);
	EXPECT_FLOAT_EQ(EmptyLayout.m_Cover.w, 0.0f);
	EXPECT_FLOAT_EQ(EmptyLayout.m_Cover.h, 0.0f);

	// 行高为 0 时没有可排的事实行，也不得把封面撑成负高。
	const CUIRect Content{0.0f, 0.0f, 200.0f, 120.0f};
	const auto NoRowLayout = qm_vote_map_preview::ResolveLayout(Content, 2.32f, 0.0f);
	EXPECT_EQ(NoRowLayout.m_NumFactRows, 0);
	EXPECT_GE(NoRowLayout.m_Cover.h, 0.0f);
}
