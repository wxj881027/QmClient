// 真实 CUi 与滚动容器协作；只替换输入设备、时钟和 GPU 提交。
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmDirectTouch : public ::testing::Test
	{
		qm_ui_test::CScopedRealUiConfig m_ConfigGuard;

	protected:
		qm_ui_test::CRealUiTestGraphics m_Graphics;
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		qm_ui_test::CRealUiTestKernel m_Kernel{&m_Graphics, nullptr, &m_Input, &m_Client};
		CUi m_Ui;
		CScrollRegion m_Region;
		CUIRect m_View{0.0f, 0.0f, 200.0f, 200.0f};
		CUIRect m_Button{20.0f, 40.0f, 150.0f, 60.0f};
		int m_ButtonId = 0;
		int m_DragId = 0;

		void SetUp() override
		{
			g_Config = CConfig();
			g_Config.m_QmUiMotionLevel = 0;
			g_Config.m_QmGaussianBlur = 0;
			m_Ui.Init(&m_Kernel);
			m_Ui.SetDirectTouchInput(true);
			Frame();
		}

		void TearDown() override
		{
			m_Ui.CancelTouchInput();
			CUIRect::Init(nullptr, nullptr);
			CUIElementBase::Init(nullptr);
			CLineInput::Init(nullptr, nullptr, nullptr, nullptr);
		}

		IInput::CTouchFingerState Finger(float X, float Y, int Press = 1)
		{
			IInput::CTouchFingerState Result{};
			Result.m_Position = vec2(X, Y) / m_Ui.Screen()->Size();
			Result.m_PressTime = std::chrono::nanoseconds(Press);
			return Result;
		}

		void Frame()
		{
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		void Touch(float X, float Y, int Press = 1)
		{
			m_Input.SetTouchFingers({Finger(X, Y, Press)});
			Frame();
		}

		void Release()
		{
			m_Input.SetTouchFingers({});
			Frame();
		}

		int DrawList(CScrollRegion &Region, CUIRect View, bool Button = true, bool Horizontal = false)
		{
			CScrollRegionParams Params;
			Params.m_ScrollHorizontal = Horizontal;
			Region.SetContentHeightForNextFrame(500.0f);
			vec2 Offset;
			Region.Begin(&View, &Offset, &Params);
			Region.AddRect({View.x + Offset.x, View.y + Offset.y, Horizontal ? 500.0f : View.w, Horizontal ? View.h : 500.0f});
			const int Click = Button ? m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT) : 0;
			Region.End();
			return Click;
		}
	};
}

TEST_F(QmDirectTouch, FirstPressUsesAbsolutePositionAndTapClicksOnlyOnRelease)
{
	Touch(70.0f, 60.0f);
	EXPECT_NEAR(m_Ui.MouseX(), 70.0f, 0.001f);
	EXPECT_NEAR(m_Ui.MouseY(), 60.0f, 0.001f);
	EXPECT_TRUE(m_Ui.MouseButtonClicked(0));
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
	EXPECT_EQ(m_Ui.ActiveItem(), &m_ButtonId);
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 1);
	Frame();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
}

TEST_F(QmDirectTouch, SmallMotionKeepsTapWithoutScrolling)
{
	Touch(70.0f, 60.0f);
	EXPECT_EQ(DrawList(m_Region, m_View), 0);
	Touch(73.0f, 62.0f);
	EXPECT_EQ(DrawList(m_Region, m_View), 0);
	EXPECT_FLOAT_EQ(m_Region.State().Offset(), 0.0f);
	Release();
	EXPECT_EQ(DrawList(m_Region, m_View), 1);
}

TEST_F(QmDirectTouch, SingleFingerScrollCancelsButtonAndTracksContentWithoutLag)
{
	Touch(70.0f, 90.0f);
	EXPECT_EQ(DrawList(m_Region, m_View), 0);
	Touch(70.0f, 60.0f);
	EXPECT_EQ(DrawList(m_Region, m_View), 0);
	EXPECT_NEAR(m_Region.State().Offset(), 30.0f, 0.001f);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	Touch(70.0f, 40.0f);
	EXPECT_EQ(DrawList(m_Region, m_View), 0);
	EXPECT_NEAR(m_Region.State().Offset(), 50.0f, 0.001f);
	Release();
	EXPECT_EQ(DrawList(m_Region, m_View), 0);
}

TEST_F(QmDirectTouch, NonInteractivePassesPreserveScrollDeltaForInteractivePass)
{
	Touch(70.0f, 90.0f);
	DrawList(m_Region, m_View, false);
	Touch(70.0f, 60.0f);
	m_Ui.BeginBackgroundRender();
	DrawList(m_Region, m_View, false);
	m_Ui.EndBackgroundRender();
	EXPECT_FLOAT_EQ(m_Region.State().Offset(), 0.0f);
	m_Ui.BeginRenderOnly();
	DrawList(m_Region, m_View, false);
	m_Ui.EndRenderOnly();
	EXPECT_FLOAT_EQ(m_Region.State().Offset(), 0.0f);
	DrawList(m_Region, m_View, false);
	EXPECT_NEAR(m_Region.State().Offset(), 30.0f, 0.001f);
}

TEST_F(QmDirectTouch, ScrollBelongsToStartingListAfterCrossingIntoAnotherList)
{
	CScrollRegion Other;
	const CUIRect OtherView{200.0f, 0.0f, 200.0f, 200.0f};
	Touch(70.0f, 90.0f);
	DrawList(m_Region, m_View, false);
	DrawList(Other, OtherView, false);
	Touch(250.0f, 60.0f);
	DrawList(m_Region, m_View, false);
	DrawList(Other, OtherView, false);
	EXPECT_NEAR(m_Region.State().Offset(), 30.0f, 0.001f);
	EXPECT_FLOAT_EQ(Other.State().Offset(), 0.0f);
	Release();
	DrawList(m_Region, m_View, false);
	DrawList(Other, OtherView, false);
}

TEST_F(QmDirectTouch, HorizontalListConsumesHorizontalFingerMotion)
{
	Touch(130.0f, 100.0f);
	DrawList(m_Region, m_View, false, true);
	Touch(90.0f, 100.0f);
	DrawList(m_Region, m_View, false, true);
	EXPECT_NEAR(m_Region.State().Offset(), 40.0f, 0.001f);
}

TEST_F(QmDirectTouch, CapturedTimelineKeepsDragOutsideRectAndPreventsListScroll)
{
	Touch(70.0f, 60.0f);
	DrawList(m_Region, m_View, false);
	ASSERT_TRUE(m_Ui.CaptureTouchDrag(&m_DragId, &m_Button));
	m_Ui.SetActiveItem(&m_DragId);
	Touch(260.0f, 120.0f);
	DrawList(m_Region, m_View, false);
	EXPECT_TRUE(m_Ui.CaptureTouchDrag(&m_DragId, &m_Button));
	EXPECT_TRUE(m_Ui.MouseButton(0));
	EXPECT_EQ(m_Ui.ActiveItem(), &m_DragId);
	EXPECT_FLOAT_EQ(m_Region.State().Offset(), 0.0f);
	Release();
	EXPECT_FALSE(m_Ui.CaptureTouchDrag(&m_DragId, &m_Button));
	EXPECT_FALSE(m_Ui.MouseButton(0));
}

TEST_F(QmDirectTouch, SettingsSliderStartsOnFirstTouchAndWinsOverList)
{
	const CUIRect Slider{20.0f, 50.0f, 150.0f, 20.0f};
	Touch(60.0f, 60.0f);
	DrawList(m_Region, m_View, false);
	float Value = m_Ui.DoScrollbarH(&m_DragId, &Slider, 0.0f);
	EXPECT_GT(Value, 0.0f);
	EXPECT_EQ(m_Ui.ActiveItem(), &m_DragId);
	Touch(145.0f, 120.0f);
	DrawList(m_Region, m_View, false);
	Value = m_Ui.DoScrollbarH(&m_DragId, &Slider, Value);
	EXPECT_GT(Value, 0.7f);
	EXPECT_FLOAT_EQ(m_Region.State().Offset(), 0.0f);
	Release();
	m_Ui.DoScrollbarH(&m_DragId, &Slider, Value);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
}

TEST_F(QmDirectTouch, SecondFingerCancelsPressUntilEveryFingerIsReleased)
{
	Touch(70.0f, 60.0f);
	m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT);
	m_Input.SetTouchFingers({Finger(70.0f, 60.0f), Finger(90.0f, 60.0f, 2)});
	Frame();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	Touch(70.0f, 60.0f);
	EXPECT_FALSE(m_Ui.MouseButton(0));
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
	Touch(70.0f, 60.0f, 3);
	m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT);
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 1);
}

TEST_F(QmDirectTouch, ReplacedFingerCannotContinueCapturedDrag)
{
	Touch(70.0f, 60.0f);
	ASSERT_TRUE(m_Ui.CaptureTouchDrag(&m_DragId, &m_Button));
	m_Ui.SetActiveItem(&m_DragId);
	Touch(75.0f, 60.0f, 2);
	EXPECT_FALSE(m_Ui.MouseButton(0));
	EXPECT_FALSE(m_Ui.CaptureTouchDrag(&m_DragId, &m_Button));
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	Release();
	EXPECT_FALSE(m_Ui.MouseButton(0));
}

TEST_F(QmDirectTouch, DisableAndResumeRequiresReleaseBeforeNextPress)
{
	Touch(70.0f, 60.0f);
	m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT);
	m_Ui.SetEnabled(false);
	Frame();
	m_Ui.SetEnabled(true);
	Frame();
	EXPECT_FALSE(m_Ui.MouseButton(0));
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
	Touch(70.0f, 60.0f, 2);
	m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT);
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 1);
}

TEST_F(QmDirectTouch, ResizeCancelsGestureWithoutSynthesizingReleaseClick)
{
	Touch(70.0f, 60.0f);
	m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT);
	m_Graphics.SetFramebufferSize(1280, 720);
	Frame();
	EXPECT_FALSE(m_Ui.MouseButton(0));
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
}

TEST_F(QmDirectTouch, ExplicitInterruptionClearsPressAndRequiresAllFingersReleased)
{
	Touch(70.0f, 60.0f);
	m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT);
	m_Ui.CancelTouchInput();
	EXPECT_FALSE(m_Ui.MouseButton(0));
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
	Frame();
	EXPECT_FALSE(m_Ui.MouseButtonClicked(0));
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
}

TEST_F(QmDirectTouch, RemovedListCannotTransferHeldFingerToNewPageButton)
{
	Touch(70.0f, 90.0f);
	DrawList(m_Region, m_View);
	Touch(70.0f, 60.0f);
	// 原列表本帧不再渲染；下一帧必须取消其仍按住的手势。
	Frame();
	EXPECT_FALSE(m_Ui.MouseButton(0));
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
}

TEST_F(QmDirectTouch, LongPressCancelsLeftClickBeforeStartingRightClick)
{
	Touch(70.0f, 60.0f);
	m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT);
	for(int FrameIndex = 0; FrameIndex < 40; ++FrameIndex)
		Frame();
	EXPECT_FALSE(m_Ui.MouseButton(0));
	EXPECT_TRUE(m_Ui.MouseButton(1));
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
	Release();
	EXPECT_EQ(m_Ui.DoButtonLogic(&m_ButtonId, 0, &m_Button, BUTTONFLAG_LEFT), 0);
}

TEST_F(QmDirectTouch, IndependentTouchStateReportsOnlyValidTapRelease)
{
	CUi::CTouchState State;
	Touch(70.0f, 60.0f);
	m_Ui.UpdateTouchState(State);
	EXPECT_TRUE(State.m_PrimaryPressed);
	EXPECT_FALSE(State.m_TapReleased);
	Release();
	m_Ui.UpdateTouchState(State);
	EXPECT_TRUE(State.m_TapReleased);
	EXPECT_NEAR(State.m_PrimaryPosition.x * m_Ui.Screen()->w, 70.0f, 0.001f);
	m_Ui.UpdateTouchState(State);
	EXPECT_FALSE(State.m_TapReleased);
	Touch(70.0f, 60.0f, 2);
	m_Ui.UpdateTouchState(State);
	Touch(70.0f, 90.0f, 2);
	m_Ui.UpdateTouchState(State);
	Release();
	m_Ui.UpdateTouchState(State);
	EXPECT_FALSE(State.m_TapReleased);
}

TEST_F(QmDirectTouch, ExplicitLegacyConsumerKeepsDraggingWhileMainUiIsDisabled)
{
	CUi::CTouchState State;
	m_Ui.SetEnabled(false);
	Touch(70.0f, 60.0f);
	m_Ui.UpdateTouchState(State, false);
	EXPECT_TRUE(State.m_PrimaryPressed);
	Touch(150.0f, 100.0f);
	m_Ui.UpdateTouchState(State, false);
	EXPECT_TRUE(State.m_PrimaryPressed);
}
