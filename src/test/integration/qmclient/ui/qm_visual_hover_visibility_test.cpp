// 控件外观悬浮判定的语义矩阵：趟次门、遮挡门，以及不同控件类型在同一趟里的一致性。
// 只通过真实 CUi 的判定与真实控件提交的填充色验证，不复刻实现里的条件。
#include <engine/keys.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmVisualHoverVisibility : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void MoveCursorTo(vec2 Position)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
		}
		void Frame(vec2 Position)
		{
			MoveCursorTo(Position);
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}
	};
}

TEST_F(QmVisualHoverVisibility, SurfaceHoverFollowsRenderPassDepth)
{
	const CUIRect Row{40, 40, 240, 24};
	Frame({80, 50});

	// 正常出图趟：外观与输入判定都认为指针在控件上。
	EXPECT_TRUE(m_Ui.VisualSurfaceHovered(&Row));
	EXPECT_TRUE(m_Ui.VisualMouseHovered(&Row));
	EXPECT_TRUE(m_Ui.MouseHovered(&Row));

	// 内容 DRAW 回放（声明式卡片的最终出图）：外观保留反馈，输入不响应。
	{
		CUiScopedBackgroundRender Content(&m_Ui, true);
		EXPECT_TRUE(m_Ui.VisualSurfaceHovered(&Row));
		EXPECT_TRUE(m_Ui.VisualMouseHovered(&Row));
		EXPECT_FALSE(m_Ui.MouseHovered(&Row));
	}

	// 纯背景趟（控制台开合时的下层快照）：外观必须熄灭，否则背板与前景各说一套。
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_FALSE(m_Ui.VisualSurfaceHovered(&Row));
		EXPECT_FALSE(m_Ui.VisualMouseHovered(&Row));
	}

	// 测量 / 文字预热趟：既不推进动画也不出反馈。
	m_Ui.BeginRenderOnly();
	EXPECT_FALSE(m_Ui.VisualSurfaceHovered(&Row));
	m_Ui.EndRenderOnly();

	// 指针离开后不悬浮。
	Frame({320, 320});
	EXPECT_FALSE(m_Ui.VisualSurfaceHovered(&Row));
}

TEST_F(QmVisualHoverVisibility, BlockingPopupKeepsSurfaceHoverButBlocksInputHover)
{
	const CUIRect Row{40, 40, 240, 24};
	Frame({80, 50});

	SPopupMenuId PopupId;
	SPopupMenuProperties Props;
	Props.m_BlockUnderlyingPointerInput = true;
	const auto KeepOpen = [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; };
	m_Ui.DoPopupMenu(&PopupId, 320, 320, 200, 100, nullptr, KeepOpen, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&PopupId));
	ASSERT_TRUE(m_Ui.PointerInputBlocked());

	// 遮挡只影响输入与严格档外观：控件外观仍按渲染层判断，
	// 否则被弹层遮挡的回放趟会整片失去高亮，前景与背板对不上。
	EXPECT_TRUE(m_Ui.VisualSurfaceHovered(&Row));
	EXPECT_FALSE(m_Ui.VisualMouseHovered(&Row));
	EXPECT_FALSE(m_Ui.MouseHovered(&Row));

	m_Ui.ClosePopupMenus();
}

TEST_F(QmVisualHoverVisibility, SplitActionButtonFrameAgreesWithItsHalvesInBackgroundPass)
{
	CButtonContainer Left;
	CButtonContainer Right;
	const CUIRect Rect{40, 40, 240, 24};
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	const auto DrawGroup = [&] {
		Graphics.m_vRoundedSubmissions.clear();
		ui_widget::SplitActionButton(ui_widget::ControlContext(&m_Ui), &Left, "Left", &Right, "Right", Rect, false, false);
		EXPECT_FALSE(Graphics.m_vRoundedSubmissions.empty());
		return Graphics.m_vRoundedSubmissions.back();
	};

	// 正常出图趟、指针落在左半：外框是最后提交的一圈描边（填充为空）。
	Frame({60, 52});
	const auto ActiveFrame = DrawGroup();
	EXPECT_FLOAT_EQ(ActiveFrame.m_FillColor.w, 0.0f);
	ASSERT_GT(ActiveFrame.m_BorderColor.a, 0.0f);

	// 纯背景趟：两半不再画悬浮底色，外框也必须一起退回静息描边，
	// 否则会出现「半边不亮、外框单独发亮」的割裂。
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		const auto BackgroundFrame = DrawGroup();
		EXPECT_LT(BackgroundFrame.m_BorderColor.a, ActiveFrame.m_BorderColor.a);
	}

	// 被上层弹层遮挡：输入门关闭但外观门放行，两半仍画悬浮底色，
	// 外框必须保持同一档描边——这是内外层判定分叉时最容易裂开的一处。
	SPopupMenuId PopupId;
	SPopupMenuProperties Props;
	Props.m_BlockUnderlyingPointerInput = true;
	const auto KeepOpen = [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; };
	m_Ui.DoPopupMenu(&PopupId, 320, 320, 200, 100, nullptr, KeepOpen, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&PopupId));
	const auto BlockedFrame = DrawGroup();
	EXPECT_FLOAT_EQ(BlockedFrame.m_BorderColor.a, ActiveFrame.m_BorderColor.a);
	m_Ui.ClosePopupMenus();
}

TEST_F(QmVisualHoverVisibility, TextAndIconButtonsAgreeOnHoverVisualInEachPass)
{
	// 颜色插值会让同一状态在不同帧返回不同填充，这里只验证「状态到颜色」的映射。
	const int SavedInterpolation = g_Config.m_QmUiColorInterpolation;
	g_Config.m_QmUiColorInterpolation = 0;

	CButtonContainer TextButton;
	CButtonContainer IconButton;
	const CUIRect TextRect{40, 40, 200, 24};
	const CUIRect IconRect{40, 80, 24, 24};
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());

	const auto Fill = [&](const auto &Draw) {
		Graphics.m_vRoundedSubmissions.clear();
		Draw();
		EXPECT_FALSE(Graphics.m_vRoundedSubmissions.empty());
		return Graphics.m_vRoundedSubmissions.back().m_FillColor;
	};
	const auto TextFill = [&] {
		return Fill([&] { ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &TextButton, "Action", TextRect); });
	};
	const auto IconFill = [&] {
		return Fill([&] { ui_widget::IconButton(ui_widget::ControlContext(&m_Ui), &IconButton, "gear", IconRect, false); });
	};

	// 静息态。
	Frame({320, 320});
	const vec4 TextIdle = TextFill();
	const vec4 IconIdle = IconFill();

	// 正常出图趟：悬停各自改变自己的填充，互不串扰。
	Frame({80, 50});
	const vec4 TextHovered = TextFill();
	EXPECT_NE(TextHovered, TextIdle);
	EXPECT_EQ(IconFill(), IconIdle);

	Frame({48, 88});
	const vec4 IconHovered = IconFill();
	EXPECT_NE(IconHovered, IconIdle);
	EXPECT_EQ(TextFill(), TextIdle);

	// 纯背景趟：两类控件必须同时熄灭悬停反馈，不允许一种亮一种不亮。
	Frame({80, 50});
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_EQ(TextFill(), TextIdle);
	}
	Frame({48, 88});
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_EQ(IconFill(), IconIdle);
	}

	g_Config.m_QmUiColorInterpolation = SavedInterpolation;
}
