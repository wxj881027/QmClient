// 调用真实文字渲染和按钮输入；只替换窗口、时钟与输入设备。
#include <engine/keys.h>

#include <game/client/QmUi/UiButtons.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmSettingsActionGroup : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		ui_widget::SSettingsActionGroupState m_State;
		CButtonContainer m_aButtons[2];
		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		int Draw(const SSettingsActionGroupLayout &Layout, const char *const *ppLabels, bool DisableFirst = false)
		{
			int Result = -1;
			for(int Index = 0; Index < Layout.m_Count; ++Index)
			{
				if(ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &m_aButtons[Index], ppLabels[Index], Layout.m_aButtons[Index], Index == 0 && DisableFirst))
					Result = Index;
			}
			return Result;
		}
	};
}

TEST_F(QmSettingsActionGroup, ContentWidthUsesLongestRealTranslationAndTextChangesInvalidateCache)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect Row{40, 40, 900, Metrics.m_LineHeight};
	const char *apLabels[] = {"Edit", "Directory"};
	const auto Short = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	apLabels[1] = "Open the directory containing the audio packs";
	const auto Long = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	EXPECT_GT(Long.m_aButtons[0].w, Short.m_aButtons[0].w);
	EXPECT_FLOAT_EQ(Long.m_aButtons[0].w, Long.m_aButtons[1].w);
	EXPECT_GE(Long.m_aButtons[0].w, m_Ui.TextRender()->TextWidth(Metrics.m_BodySize, apLabels[1]));
	apLabels[1] = "Directory";
	const auto Restored = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	EXPECT_FLOAT_EQ(Restored.m_aButtons[0].w, Short.m_aButtons[0].w);
	EXPECT_FLOAT_EQ(Restored.m_aButtons[0].h, Row.h);
}

TEST_F(QmSettingsActionGroup, PureButtonRowsFillTheWholeWidthWithEqualSlots)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const char *apLabels[] = {"Settings file", "Saves file", "Open folder"};
	for(const int Count : {1, 2, 3})
	{
		for(const float Width : {0.0f, 1.0f, 24.0f, 200.0f, 900.0f})
		{
			SCOPED_TRACE(Count);
			SCOPED_TRACE(Width);
			const CUIRect Row{40, 40, Width, Metrics.m_LineHeight};
			const auto Layout = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, Count, Metrics);
			ASSERT_EQ(Layout.m_Count, Count);
			EXPECT_FLOAT_EQ(Layout.m_aButtons[0].x, Row.x);
			EXPECT_NEAR(Layout.m_aButtons[Count - 1].x + Layout.m_aButtons[Count - 1].w, Row.x + Width, 0.0001f);
			for(int Index = 0; Index < Count; ++Index)
			{
				EXPECT_FLOAT_EQ(Layout.m_aButtons[Index].w, Layout.m_aButtons[0].w);
				EXPECT_GE(Layout.m_aButtons[Index].w, 0);
				EXPECT_FLOAT_EQ(Layout.m_aButtons[Index].h, Row.h);
			}
		}
	}
}

TEST_F(QmSettingsActionGroup, WidthPolicyChangesPreserveContentCacheAndFillIgnoresLabelLength)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect Row{40, 40, 1800, Metrics.m_LineHeight};
	const char *apLabels[] = {"Edit", "Directory"};
	const auto Content = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	const auto Fill = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics);
	EXPECT_GT(Fill.m_aButtons[0].w, Content.m_aButtons[0].w);
	apLabels[1] = "Open the directory containing all installed audio packs";
	const auto LongerFill = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics);
	EXPECT_FLOAT_EQ(LongerFill.m_aButtons[0].w, Fill.m_aButtons[0].w);
	const auto LongerContent = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	EXPECT_GT(LongerContent.m_aButtons[0].w, Content.m_aButtons[0].w);
	EXPECT_LT(LongerContent.m_aButtons[1].x + LongerContent.m_aButtons[1].w, Row.x + Row.w);
}

TEST_F(QmSettingsActionGroup, FilledRowRightEdgeBelongsToLastActionAndGapCannotSubmit)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect Row{40, 40, 900, Metrics.m_LineHeight};
	const char *apLabels[] = {"Settings file", "Saves file"};
	const auto Layout = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics);
	const vec2 RightEdge{Row.x + Row.w - 1.0f, Row.y + Row.h * 0.5f};
	Frame(RightEdge, false);
	EXPECT_EQ(Draw(Layout, apLabels), -1);
	Frame(RightEdge, true);
	EXPECT_EQ(Draw(Layout, apLabels), -1);
	Frame(RightEdge, false);
	EXPECT_EQ(Draw(Layout, apLabels), 1);
	const vec2 Gap{(Layout.m_aButtons[0].x + Layout.m_aButtons[0].w + Layout.m_aButtons[1].x) * 0.5f, RightEdge.y};
	Frame(Gap, false);
	EXPECT_EQ(Draw(Layout, apLabels), -1);
	Frame(Gap, true);
	EXPECT_EQ(Draw(Layout, apLabels), -1);
	Frame(Gap, false);
	EXPECT_EQ(Draw(Layout, apLabels), -1);
}

TEST_F(QmSettingsActionGroup, NarrowRowKeepsSeparateHitTargetsAndDisabledActionCannotSubmit)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect Row{40, 40, 90, Metrics.m_LineHeight};
	const char *apLabels[] = {"Edit audio pack", "Open audio pack directory"};
	const auto Layout = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	ASSERT_EQ(Layout.m_Count, 2);
	EXPECT_FLOAT_EQ(Layout.m_aButtons[0].w, Layout.m_aButtons[1].w);
	EXPECT_LE(Layout.m_aButtons[1].x + Layout.m_aButtons[1].w, Row.x + Row.w);
	for(int Index = 0; Index < 2; ++Index)
	{
		SCOPED_TRACE(Index);
		const auto &Rect = Layout.m_aButtons[Index];
		const vec2 Center{Rect.x + Rect.w * 0.5f, Rect.y + Rect.h * 0.5f};
		Frame(Center, false);
		EXPECT_EQ(Draw(Layout, apLabels, true), -1);
		Frame(Center, true);
		EXPECT_EQ(Draw(Layout, apLabels, true), -1);
		Frame(Center, false);
		EXPECT_EQ(Draw(Layout, apLabels, true), Index == 0 ? -1 : 1);
		EXPECT_EQ(Draw(Layout, apLabels, true), -1);
	}
}

TEST_F(QmSettingsActionGroup, InputAndIconActionsStayInsideEvenATinyControlColumn)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	// 设备选择用一个动作，关键词规则用三个动作；都保留输入宽度与独立命中区域。
	for(int Count : {1, 2, 3})
	{
		SCOPED_TRACE(Count);
		for(float Width : {0.0f, 1.0f, 24.0f, 100.0f, 500.0f})
		{
			SCOPED_TRACE(Width);
			const CUIRect Row{40, 40, Width, Metrics.m_LineHeight};
			const auto Layout = ResolveSettingsInputActionsLayout(Row, Count, Metrics);
			ASSERT_EQ(Layout.m_Actions.m_Count, Count);
			EXPECT_GE(Layout.m_Input.w, Width * 0.4f);
			float PreviousRight = Layout.m_Input.x + Layout.m_Input.w;
			for(int Index = 0; Index < Count; ++Index)
			{
				const auto &Button = Layout.m_Actions.m_aButtons[Index];
				EXPECT_GE(Button.w, 0);
				EXPECT_FLOAT_EQ(Button.w, Layout.m_Actions.m_aButtons[0].w);
				EXPECT_GE(Button.x, PreviousRight);
				EXPECT_LE(Button.x + Button.w, Row.x + Row.w + 0.0001f);
				EXPECT_FLOAT_EQ(Button.h, Row.h);
				PreviousRight = Button.x + Button.w;
			}
		}
	}
}

TEST_F(QmSettingsActionGroup, FontSizeChangesRecomputeActionWidthsWithoutChangingRowHeight)
{
	auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect Row{40, 40, 1200, Metrics.m_LineHeight};
	const char *apLabels[] = {"Preview selected file", "Preview import file"};
	const auto Normal = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	Metrics.m_BodySize *= 1.5f;
	const auto LargerText = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	EXPECT_GT(LargerText.m_aButtons[0].w, Normal.m_aButtons[0].w);
	EXPECT_FLOAT_EQ(LargerText.m_aButtons[0].h, Normal.m_aButtons[0].h);
	EXPECT_FLOAT_EQ(LargerText.m_aButtons[0].w, LargerText.m_aButtons[1].w);
}

TEST_F(QmSettingsActionGroup, RemovingLongestActionDropsItsWidthAndRepeatedCountChangesRecover)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect Row{40, 40, 1800, Metrics.m_LineHeight};
	const char *apLabels[] = {"Edit", "Directory", "Open the directory containing all installed audio packs"};
	const auto Baseline = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	for(int Repeat = 0; Repeat < 16; ++Repeat)
	{
		SCOPED_TRACE(Repeat);
		const auto Three = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 3, Metrics, ESettingsActionGroupWidth::CONTENT);
		ASSERT_EQ(Three.m_Count, 3);
		EXPECT_GT(Three.m_aButtons[0].w, Baseline.m_aButtons[0].w);
		const auto Two = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
		ASSERT_EQ(Two.m_Count, 2);
		EXPECT_FLOAT_EQ(Two.m_aButtons[0].w, Baseline.m_aButtons[0].w);
		EXPECT_FLOAT_EQ(Two.m_aButtons[0].w, Two.m_aButtons[1].w);
	}
}

TEST_F(QmSettingsActionGroup, EmptyAndInvalidActionsHaveNoSlotsAndDoNotPreventReuse)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect Row{40, 40, 900, Metrics.m_LineHeight};
	const char *apLabels[] = {"Edit", "Directory"};
	const auto Baseline = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	EXPECT_EQ(ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, nullptr, 2, Metrics).m_Count, 0);
	for(int Count : {-1, 0, SSettingsChoiceLayout::MAX_SEGMENTS + 1})
		EXPECT_EQ(ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, Count, Metrics).m_Count, 0);
	const auto Restored = ui_widget::PrepareSettingsActionGroup(m_Ui, m_State, Row, apLabels, 2, Metrics, ESettingsActionGroupWidth::CONTENT);
	EXPECT_EQ(Restored.m_Count, 2);
	EXPECT_FLOAT_EQ(Restored.m_aButtons[0].w, Baseline.m_aButtons[0].w);
}

TEST_F(QmSettingsActionGroup, FourIconActionsKeepOriginalSquaresAndShrinkTogetherOnNarrowRows)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	const CUIRect WideRow{40, 40, 800, Metrics.m_ButtonHeight};
	const auto Wide = ResolveSettingsInputActionsLayout(WideRow, 4, Metrics);
	ASSERT_EQ(Wide.m_Actions.m_Count, 4);
	for(int Index = 0; Index < 4; ++Index)
		EXPECT_FLOAT_EQ(Wide.m_Actions.m_aButtons[Index].w, WideRow.h);
	const CUIRect NarrowRow{40, 40, 40, WideRow.h};
	const auto Narrow = ResolveSettingsInputActionsLayout(NarrowRow, 4, Metrics);
	ASSERT_EQ(Narrow.m_Actions.m_Count, 4);
	EXPECT_GE(Narrow.m_Input.w, NarrowRow.w * 0.4f);
	for(int Index = 0; Index < 4; ++Index)
	{
		const auto &Button = Narrow.m_Actions.m_aButtons[Index];
		EXPECT_FLOAT_EQ(Button.w, Narrow.m_Actions.m_aButtons[0].w);
		EXPECT_GT(Button.w, 0);
		EXPECT_LT(Button.w, WideRow.h);
		EXPECT_LE(Button.x + Button.w, NarrowRow.x + NarrowRow.w + 0.0001f);
	}
}
