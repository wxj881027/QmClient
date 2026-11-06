// 真实滚动区域与滚动状态协作，显示位置不能在滚轮动画之外再落后一层。
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmScrollDisplay : public ::testing::Test
	{
		qm_ui_test::CScopedRealUiConfig m_ConfigGuard;
		qm_ui_test::CRealUiTestGraphics m_Graphics;
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		qm_ui_test::CRealUiTestKernel m_Kernel{&m_Graphics, nullptr, &m_Input, &m_Client};
		uint64_t m_Frame = 0;

	protected:
		CUi m_Ui;
		CScrollRegion m_Region;

		void SetUp() override
		{
			g_Config = CConfig();
			g_Config.m_QmUiMotionLevel = 2;
			g_Config.m_UiSmoothScrollTime = 500;
			m_Ui.Init(&m_Kernel);
		}

		void TearDown() override
		{
			CUIRect::Init(nullptr, nullptr);
			CUIElementBase::Init(nullptr);
			CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
		}

		float Frame(float ContentHeight = 2000.0f, float WheelDelta = 0.0f, bool Horizontal = false)
		{
			const vec2 Pixels = vec2(40, 40) * vec2(m_Graphics.WindowWidth(), m_Graphics.WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Client.AdvanceFrame();
			m_Ui.Update();
			// Update 已开启设备输入帧；使用独立 ID 注入本次明确的滚轮幅度。
			m_Ui.BeginWheelOwnershipFrame(1000 + ++m_Frame, WheelDelta, false);

			m_Region.SetContentHeightForNextFrame(ContentHeight);
			CScrollRegionParams Params;
			Params.m_ScrollUnit = 120.0f;
			Params.m_ScrollHorizontal = Horizontal;
			CUIRect View{0, 0, 100, 100};
			vec2 Offset;
			m_Region.Begin(&View, &Offset, &Params);
			m_Region.AddRect({View.x + Offset.x, View.y + Offset.y, Horizontal ? ContentHeight : View.w, Horizontal ? View.h : ContentHeight});
			m_Region.End();
			return Horizontal ? Offset.x : Offset.y;
		}
	};
}

TEST_F(QmScrollDisplay, RepeatedAndReversedWheelDisplaysTheExistingAnimation)
{
	Frame();
	Frame(2000.0f, -360.0f);
	ASSERT_TRUE(m_Region.WheelConsumedThisFrame());
	ASSERT_GT(m_Region.State().Offset(), 24.0f);
	for(const float WheelDelta : {-120.0f, -240.0f, 240.0f, 120.0f, 0.0f})
	{
		const float Expected = -m_Region.State().Offset();
		EXPECT_FLOAT_EQ(Frame(2000.0f, WheelDelta), Expected);
		EXPECT_FLOAT_EQ(m_Region.ContentScrollOffsetY(), Expected);
	}
	for(int i = 0; i < 40; ++i)
	{
		const float Expected = -m_Region.State().Offset();
		EXPECT_FLOAT_EQ(Frame(), Expected);
	}
	EXPECT_FALSE(m_Region.Animating());
	EXPECT_FLOAT_EQ(Frame(), -m_Region.State().Offset());
}

TEST_F(QmScrollDisplay, HorizontalWheelUsesTheSameAnimationPosition)
{
	Frame(2000.0f, -360.0f, true);
	ASSERT_GT(m_Region.State().Offset(), 24.0f);
	const float Expected = -m_Region.State().Offset();
	EXPECT_FLOAT_EQ(Frame(2000.0f, 0.0f, true), Expected);
	EXPECT_FLOAT_EQ(m_Region.ContentScrollOffsetY(), 0.0f);
}

TEST_F(QmScrollDisplay, DisabledSmoothScrollDisplaysTheWholeWheelStep)
{
	g_Config.m_UiSmoothScrollTime = 0;
	Frame(2000.0f, -360.0f);
	ASSERT_FLOAT_EQ(m_Region.State().Offset(), 360.0f);
	EXPECT_FLOAT_EQ(Frame(), -360.0f);
	EXPECT_FALSE(m_Region.Animating());
}

TEST_F(QmScrollDisplay, DisabledMotionDisplaysTheWholeWheelStep)
{
	g_Config.m_QmUiMotionLevel = 0;
	Frame(2000.0f, -360.0f);
	ASSERT_FLOAT_EQ(m_Region.State().Offset(), 360.0f);
	EXPECT_FLOAT_EQ(Frame(), -360.0f);
}

TEST_F(QmScrollDisplay, DirectPositionAndThumbDragHaveNoDisplayDelay)
{
	Frame();
	m_Region.SetScrollOffsetY(-600.0f);
	EXPECT_FLOAT_EQ(Frame(), -600.0f);
	m_Region.State().BeginThumbDrag(0.0f);
	m_Region.SetScrollOffsetY(-300.0f);
	EXPECT_FLOAT_EQ(Frame(), -300.0f);
}

TEST_F(QmScrollDisplay, ResetDropsThePreviousDisplayPosition)
{
	Frame();
	m_Region.SetScrollOffsetY(-600.0f);
	Frame();
	m_Region.Reset();
	EXPECT_FLOAT_EQ(Frame(100.0f), 0.0f);
	EXPECT_FLOAT_EQ(m_Region.ContentScrollOffsetY(), 0.0f);
}

TEST_F(QmScrollDisplay, ContentShrinkReturnsSmoothlyWithoutALargeFinalJump)
{
	Frame();
	m_Region.SetScrollOffsetY(-600.0f);
	Frame();
	Frame(200.0f);
	ASSERT_FLOAT_EQ(m_Region.State().Offset(), 100.0f);
	float Previous = m_Region.ContentScrollOffsetY();
	for(int i = 0; i < 40; ++i)
	{
		const float Current = Frame(200.0f);
		EXPECT_GE(Current, Previous);
		EXPECT_LE(Current, -100.0f);
		// 回位每帧连续收敛，不能在还差几十像素时一次性跳到终点。
		EXPECT_LE(Current - Previous, (-100.0f - Previous) * 0.5f + m_Ui.PixelSize() * 0.25f);
		Previous = Current;
	}
	EXPECT_FLOAT_EQ(Previous, -100.0f);
}

TEST_F(QmScrollDisplay, WheelInterruptsContentShrinkReturn)
{
	Frame();
	m_Region.SetScrollOffsetY(-600.0f);
	Frame();
	Frame(200.0f);
	Frame(200.0f, 120.0f);
	ASSERT_TRUE(m_Region.Animating());
	const float Expected = -m_Region.State().Offset();
	EXPECT_FLOAT_EQ(Frame(200.0f), Expected);
}

TEST_F(QmScrollDisplay, FullyCollapsedContentReturnsToTheTop)
{
	Frame();
	m_Region.SetScrollOffsetY(-600.0f);
	Frame();
	Frame(50.0f);
	ASSERT_FLOAT_EQ(m_Region.State().Offset(), 0.0f);
	EXPECT_LT(Frame(50.0f), 0.0f);
	for(int i = 0; i < 40; ++i)
		Frame(50.0f);
	EXPECT_FLOAT_EQ(m_Region.ContentScrollOffsetY(), 0.0f);
}

TEST_F(QmScrollDisplay, DisablingMotionFinishesContentShrinkReturn)
{
	Frame();
	m_Region.SetScrollOffsetY(-600.0f);
	Frame();
	Frame(200.0f);
	ASSERT_LT(Frame(200.0f), -100.0f);
	g_Config.m_QmUiMotionLevel = 0;
	EXPECT_FLOAT_EQ(Frame(200.0f), -100.0f);
	g_Config.m_QmUiMotionLevel = 2;
	EXPECT_FLOAT_EQ(Frame(200.0f), -100.0f);
}

TEST_F(QmScrollDisplay, BackgroundDrawDoesNotAdvanceContentShrinkReturn)
{
	Frame();
	m_Region.SetScrollOffsetY(-600.0f);
	Frame();
	Frame(200.0f);
	const float Previous = m_Region.ContentScrollOffsetY();
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_FLOAT_EQ(Frame(200.0f), Previous);
	}
	EXPECT_GT(Frame(200.0f), Previous);
}
