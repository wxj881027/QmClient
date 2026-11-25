// 列表行距的步进语义：行距属于「行」而不是「格」。
// 覆盖 CListBox 与滚动区的协作边界：多列网格每行只推一次行距、可见范围裁剪
// （SkipItems）与正常步进走同一条路径、单列与显式关闭行距的既有调用点不回归。
#include <engine/keys.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_listbox.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <array>
#include <vector>

namespace
{
	class QmListBoxRowSpacing : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 行距步进只与几何有关：关掉入场动画与滚动弹簧，量到的就是容器的排版结果。
			g_Config.m_QmUiMotionLevel = 0;
			g_Config.m_UiSmoothScrollTime = 0;
			CUIElementBase::Init(&m_Ui);
			BeginFrame();
		}

		void BeginFrame()
		{
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		// 一次完整的列表绘制：按条目顺序逐条取矩形，返回每个条目的可见矩形。
		std::vector<CUIRect> DrawList(CListBox &List, float RowHeight, const int *pIds, int NumItems, int ItemsPerRow, int RowsPerScroll, const CUIRect &View)
		{
			std::vector<CUIRect> vRects;
			vRects.reserve(NumItems);
			BeginFrame();
			List.SetActive(false);
			List.DoStart(RowHeight, NumItems, ItemsPerRow, RowsPerScroll, -1, &View, false);
			for(int Index = 0; Index < NumItems; ++Index)
				vRects.push_back(List.DoNextItem(&pIds[Index]).m_Rect);
			List.DoEnd();
			return vRects;
		}
	};

	TEST_F(QmListBoxRowSpacing, ReflowOffsetsMoveContentAndClippingWithoutChangingRowPitch)
	{
		std::array<int, 4> aIds{};
		CListBox List;
		List.DoAutoSpacing(0);
		const CUIRect View{20, 20, 200, 100};
		List.DoStart(40, 4, 2, 1, -1, &View, false);
		const auto Moved = List.DoNextItem(&aIds[0], false, -1, vec2(100, 40));
		const auto Hidden = List.DoNextItem(&aIds[1], false, -1, vec2(0, 200));
		const auto NextRow = List.DoNextItem(&aIds[2]);
		List.DoNextItem(&aIds[3]);
		List.DoEnd();
		EXPECT_FLOAT_EQ(Moved.m_Rect.x, View.x + 100);
		EXPECT_FLOAT_EQ(Moved.m_Rect.y, View.y + 40);
		EXPECT_FLOAT_EQ(Moved.m_ContentRect.y, Moved.m_Rect.y);
		EXPECT_TRUE(Moved.m_Visible);
		EXPECT_FALSE(Hidden.m_Visible);
		EXPECT_FLOAT_EQ(NextRow.m_Rect.y, View.y + 40);
		EXPECT_FALSE(List.ScrollbarShown());
	}

	TEST_F(QmListBoxRowSpacing, ReflowHitTestingSelectsTheItemAtItsDisplayedPosition)
	{
		std::array<int, 2> aIds{};
		CListBox List;
		List.DoAutoSpacing(0);
		const CUIRect View{20, 20, 200, 40};
		const auto Frame = [&](bool Pressed) {
			const vec2 Pixels = vec2(150, 40) * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			BeginFrame();
			List.DoStart(40, 2, 2, 1, -1, &View, false);
			List.DoNextItem(&aIds[0], false, -1, vec2(100, 0));
			List.DoNextItem(&aIds[1], false, -1, vec2(-100, 0));
			return List.DoEnd();
		};
		Frame(false);
		Frame(true);
		EXPECT_EQ(Frame(false), 0);
		EXPECT_TRUE(List.WasItemSelected());
	}

	// 多列网格：同一行的格子共享 y，行距只在行首推一次。修复前每格都推一次行距，
	// 实际行距变成「行高 + 列数 × 行距」。
	TEST_F(QmListBoxRowSpacing, MultiColumnGridPushesRowSpacingOncePerRow)
	{
		constexpr float ViewY = 20.0f;
		constexpr float RowHeight = 20.0f;
		constexpr int ItemsPerRow = 2;
		const float Spacing = ui_token::list::ROW_SPACING;
		std::array<int, 8> aIds{};
		CListBox List;
		List.DoAutoSpacing(Spacing);
		const CUIRect View{20.0f, ViewY, 300.0f, 200.0f};

		const std::vector<CUIRect> vRects = DrawList(List, RowHeight, aIds.data(), (int)aIds.size(), ItemsPerRow, 1, View);

		ASSERT_EQ(vRects.size(), aIds.size());
		const int NumRows = (int)aIds.size() / ItemsPerRow;
		for(int Row = 0; Row < NumRows; ++Row)
		{
			SCOPED_TRACE(Row);
			const CUIRect &Left = vRects[Row * ItemsPerRow];
			const CUIRect &Right = vRects[Row * ItemsPerRow + 1];
			EXPECT_FLOAT_EQ(Left.y, Right.y);
			EXPECT_FLOAT_EQ(Left.h, RowHeight);
			EXPECT_FLOAT_EQ(Left.y, ViewY + Row * (RowHeight + Spacing));
		}
		// 末行底边正好等于内容高度：行距只出现在行与行之间，末尾不留一档空隙。
		const CUIRect &Last = vRects.back();
		EXPECT_FLOAT_EQ(Last.y + Last.h - ViewY, ui_token::list::RowsHeight(NumRows, RowHeight, Spacing));
	}

	// 虚拟化列表按可见范围裁剪：跳过整行时也必须推行距，否则后续行会随滚动累积上漂。
	TEST_F(QmListBoxRowSpacing, SkipItemsPushesRowSpacingForSkippedRows)
	{
		constexpr float ViewY = 20.0f;
		constexpr float RowHeight = 20.0f;
		constexpr int ItemsPerRow = 2;
		const float Spacing = ui_token::list::ROW_SPACING;
		const CUIRect View{20.0f, ViewY, 300.0f, 200.0f};
		std::array<int, 8> aIds{};

		{
			// 跳过同一行的前一格：y 不变，落点只是右半。
			CListBox List;
			List.DoAutoSpacing(Spacing);
			BeginFrame();
			List.SetActive(false);
			List.DoStart(RowHeight, (int)aIds.size(), ItemsPerRow, 1, -1, &View, false);
			List.SkipItems(1);
			const CListboxItem SecondCell = List.DoNextItem(&aIds[1]);
			List.DoEnd();
			EXPECT_FLOAT_EQ(SecondCell.m_Rect.y, ViewY);
			EXPECT_GT(SecondCell.m_Rect.x, View.x + 1.0f);
		}

		{
			// 跳过一整行后，下一行该在「行高 + 行距」之后开始。
			CListBox List;
			List.DoAutoSpacing(Spacing);
			BeginFrame();
			List.SetActive(false);
			List.DoStart(RowHeight, (int)aIds.size(), ItemsPerRow, 1, -1, &View, false);
			List.SkipItems(2);
			const CListboxItem SecondRow = List.DoNextItem(&aIds[2]);
			List.DoEnd();
			EXPECT_FLOAT_EQ(SecondRow.m_Rect.y, ViewY + RowHeight + Spacing);
			EXPECT_FLOAT_EQ(SecondRow.m_Rect.x, View.x);
		}

		{
			// 跳过 5 格 = 两整行 + 半行：第五格落在第三行，第六格落在同一行的右半。
			CListBox List;
			List.DoAutoSpacing(Spacing);
			BeginFrame();
			List.SetActive(false);
			List.DoStart(RowHeight, (int)aIds.size(), ItemsPerRow, 1, -1, &View, false);
			List.SkipItems(5);
			const CListboxItem SixthCell = List.DoNextItem(&aIds[5]);
			List.DoEnd();
			EXPECT_FLOAT_EQ(SixthCell.m_Rect.y, ViewY + 2.0f * (RowHeight + Spacing));
			EXPECT_GT(SixthCell.m_Rect.x, View.x + 1.0f);
		}
	}

	TEST_F(QmListBoxRowSpacing, LargeGridSkipKeepsColumnAndRowAfterPartialRow)
	{
		CListBox List;
		List.DoAutoSpacing(2.5f);
		List.SetActive(false);
		const CUIRect View{20.0f, 20.0f, 300.0f, 200.0f};
		int First = 0;
		int Last = 0;
		List.DoStart(20.0f, 10004, 3, 1, -1, &View, false);
		const auto FirstRect = List.DoNextItem(&First).m_Rect;
		List.SkipItems(10001);
		const auto LastRect = List.DoNextItem(&Last).m_Rect;
		List.SkipItems(10);
		List.DoEnd();
		EXPECT_FLOAT_EQ(LastRect.x, FirstRect.x);
		EXPECT_FLOAT_EQ(LastRect.y - FirstRect.y, 3334 * 22.5f);
		EXPECT_EQ(List.ItemIndex(), 10004);
	}

	// 单列列表的既有节奏不能变：相邻两行之间仍然只有一档行距（这里同时走
	// SetCapsuleRows 的默认行距与默认容器内边距，覆盖真实胶囊列表的取值路径）。
	TEST_F(QmListBoxRowSpacing, SingleColumnRowsKeepOneSpacingBetweenRows)
	{
		constexpr float ViewY = 20.0f;
		constexpr float RowHeight = 20.0f;
		const float Spacing = ui_token::list::ROW_SPACING;
		std::array<int, 3> aIds{};
		CListBox List;
		List.SetCapsuleRows(10.0f);
		const CUIRect View{20.0f, ViewY, 300.0f, 240.0f};

		const std::vector<CUIRect> vRects = DrawList(List, RowHeight, aIds.data(), (int)aIds.size(), 1, 3, View);

		ASSERT_EQ(vRects.size(), aIds.size());
		for(size_t Index = 0; Index < vRects.size(); ++Index)
		{
			SCOPED_TRACE((int)Index);
			EXPECT_FLOAT_EQ(vRects[Index].y, ViewY + (float)Index * (RowHeight + Spacing));
		}
	}

	// 旧调用点的绕开写法（行距并进行高 + DoAutoSpacing(0)）语义不变：行距由调用方
	// 折进行高，组件不再额外推一次，几何与改动前逐位一致。
	TEST_F(QmListBoxRowSpacing, ZeroAutoSpacingKeepsCallerOwnedPitch)
	{
		constexpr float ViewY = 20.0f;
		constexpr int ItemsPerRow = 3;
		// 调用点自己把行距折进行高后传进来的整行步进。
		constexpr float Pitch = 20.0f + ui_token::list::ROW_SPACING;
		std::array<int, 6> aIds{};
		CListBox List;
		List.DoAutoSpacing(0.0f);
		const CUIRect View{20.0f, ViewY, 300.0f, 200.0f};

		const std::vector<CUIRect> vRects = DrawList(List, Pitch, aIds.data(), (int)aIds.size(), ItemsPerRow, 1, View);

		ASSERT_EQ(vRects.size(), aIds.size());
		for(int Row = 0; Row < 2; ++Row)
		{
			SCOPED_TRACE(Row);
			EXPECT_FLOAT_EQ(vRects[Row * ItemsPerRow].y, ViewY + Row * Pitch);
			EXPECT_FLOAT_EQ(vRects[Row * ItemsPerRow].h, Pitch);
		}
	}

	// 自定义行只前进调用方给的高度，不吃组件行距（画廊空态行与 demo 提示行依赖这一点）。
	TEST_F(QmListBoxRowSpacing, CustomRowAdvancesOnlyItsOwnHeight)
	{
		constexpr float ViewY = 20.0f;
		constexpr float RowHeight = 20.0f;
		constexpr float CustomHeight = 15.0f;
		CListBox List;
		List.DoAutoSpacing(ui_token::list::ROW_SPACING);
		std::array<int, 2> aIds{};
		const CUIRect View{20.0f, ViewY, 300.0f, 240.0f};

		BeginFrame();
		List.SetActive(false);
		List.DoStart(RowHeight, (int)aIds.size(), 1, 3, -1, &View, false);
		const CListboxItem First = List.DoNextItem(&aIds[0]);
		const CListboxItem Custom = List.DoCustomRow(CustomHeight, false);
		List.DoEnd();

		EXPECT_FLOAT_EQ(First.m_Rect.y + First.m_Rect.h, Custom.m_Rect.y);
		EXPECT_FLOAT_EQ(Custom.m_Rect.h, CustomHeight);
	}

	// 容器内边距：行区排在内边距内侧，胶囊不再压在容器圆角上（实机反馈：网格首行的
	// 选中/悬浮填充「超出列表」）。高度按 PaddedRowsHeight 计入两档内边距，行数不变。
	TEST_F(QmListBoxRowSpacing, CapsuleRowsInsetInsideContainerCorners)
	{
		constexpr float ViewX = 20.0f;
		constexpr float ViewY = 20.0f;
		constexpr float RowHeight = 20.0f;
		constexpr int NumRows = 3;
		std::array<int, NumRows> aIds{};
		CListBox List;
		List.SetCapsuleRows(10.0f);
		// 内边距是显式能力：调用点声明档位，同时把 2×padding 算进高度。
		List.SetContainerPadding(ui_token::list::ContainerPadding(RowHeight));
		const float Spacing = ui_token::list::ROW_SPACING;
		const float Padding = ui_token::list::ContainerPadding(RowHeight);
		const CUIRect View{ViewX, ViewY, 300.0f, ui_token::list::PaddedRowsHeight(NumRows, RowHeight, Spacing, Padding)};

		const std::vector<CUIRect> vRects = DrawList(List, RowHeight, aIds.data(), NumRows, 1, 3, View);

		ASSERT_EQ(vRects.size(), (size_t)NumRows);
		ASSERT_GT(Padding, 0.0f);
		// 生效值就是调用点声明的档位（高度给足时不会被兜底缩水）。
		EXPECT_FLOAT_EQ(List.ContainerPadding(), Padding);
		// 首行不贴容器上缘、末行不贴容器下缘，行间节奏仍是一档行距。
		EXPECT_NEAR(vRects.front().y, ViewY + Padding, 0.001f);
		for(int Index = 1; Index < NumRows; ++Index)
			EXPECT_NEAR(vRects[Index].y - vRects[Index - 1].y, RowHeight + Spacing, 0.001f);
		EXPECT_NEAR(vRects.back().y + vRects.back().h, ViewY + Padding + ui_token::list::RowsHeight(NumRows, RowHeight, Spacing), 0.001f);
		// 单列胶囊行的左右不加容器内边距：行宽不能随可见行数变化（少一行就变窄是实机 bug）。
		EXPECT_NEAR(vRects.front().x, ViewX, 0.001f);
		// 只让出滚动条那一列，不额外横缩。
		EXPECT_NEAR(vRects.front().w, View.w - List.RowReserveWidth(), 0.001f);
	}

	// 多列网格的格子是整格胶囊：四边都内缩，首/末列的圆角才不会压在容器圆角上。
	TEST_F(QmListBoxRowSpacing, GridCellsInsetOnAllSides)
	{
		constexpr float ViewX = 20.0f;
		constexpr float ViewY = 20.0f;
		constexpr float RowHeight = 44.0f;
		constexpr int ItemsPerRow = 3;
		const float Spacing = ui_token::list::ROW_SPACING;
		const float Padding = ui_token::list::ContainerPadding(RowHeight);
		std::array<int, 6> aIds{};
		CListBox List;
		List.SetCapsuleRows(10.0f);
		List.SetContainerPadding(ui_token::list::ContainerPadding(RowHeight));
		List.DoAutoSpacing(0.0f);
		const CUIRect View{ViewX, ViewY, 300.0f, 400.0f};

		const std::vector<CUIRect> vRects = DrawList(List, RowHeight, aIds.data(), (int)aIds.size(), ItemsPerRow, 1, View);

		ASSERT_EQ(vRects.size(), aIds.size());
		ASSERT_GT(Padding, 0.0f);
		for(int Index = 0; Index < (int)aIds.size(); ++Index)
		{
			SCOPED_TRACE(Index);
			EXPECT_GE(vRects[Index].x, ViewX + Padding - 0.001f);
			EXPECT_LE(vRects[Index].x + vRects[Index].w, ViewX + View.w - Padding + 0.001f);
		}
		EXPECT_NEAR(vRects.front().y, ViewY + Padding, 0.001f);
	}

	// 「该 8 行还是 8 行」：可见行取整后，容器高度是「行内容 + 上下内边距」，
	// 行区高度仍然是整数行的内容高度，内边距不会把最后一行挤掉。
	TEST_F(QmListBoxRowSpacing, VisibleRowRangeKeepsRowCountWithPadding)
	{
		constexpr float RowHeight = 20.0f;
		const float Spacing = ui_token::list::ROW_SPACING;
		const float Padding = ui_token::list::ContainerPadding(RowHeight);
		std::array<int, 5> aIds{};
		CListBox List;
		List.SetCapsuleRows(10.0f);
		List.SetContainerPadding(Padding);
		List.SetVisibleRowRange(2, 2);
		const CUIRect View{20.0f, 20.0f, 300.0f, 400.0f};

		BeginFrame();
		List.SetActive(false);
		List.DoStart(RowHeight, (int)aIds.size(), 1, 3, -1, &View, false);
		const CListboxItem First = List.DoNextItem(&aIds[0]);
		const CListboxItem Second = List.DoNextItem(&aIds[1]);
		List.DoEnd();

		ASSERT_FLOAT_EQ(List.ContainerPadding(), Padding);
		// 行区（滚动裁剪区）高度 = 两行内容高度，不含内边距。
		EXPECT_NEAR(List.ViewHeight(), ui_token::list::RowsHeight(2, RowHeight, Spacing), 0.001f);
		EXPECT_NEAR(Second.m_Rect.y + Second.m_Rect.h - First.m_Rect.y, ui_token::list::RowsHeight(2, RowHeight, Spacing), 0.001f);
		EXPECT_NEAR(First.m_Rect.y, View.y + Padding, 0.001f);
	}

	// 行反馈强度与所在页面的背景透明度作用域无关：页面把背板淡化到 0.25，选中填充
	// 仍是 token 原强度（修复前会被乘成 0.25 倍，这就是「悬浮/选中比别的列表更浅」）。
	TEST_F(QmListBoxRowSpacing, RowFeedbackKeepsTokenStrengthUnderPageFade)
	{
		constexpr float RowHeight = 20.0f;
		std::array<int, 1> aIds{};
		CListBox List;
		List.DoAutoSpacing(0.0f);
		const CUIRect View{20.0f, 20.0f, 300.0f, 60.0f};
		const ColorRGBA Selected = m_Ui.QmControlTheme().m_Selected;

		// 反馈有 130ms 左右的淡入：连画两帧再量，量到的是稳定强度。
		const auto MeasureSelectionFillAlpha = [&](float Fade) {
			float FillAlpha = 0.0f;
			for(int Frame = 0; Frame < 2; ++Frame)
			{
				m_Ui.SetBackgroundAlphaScale(Fade);
				BeginFrame();
				List.SetActive(false);
				List.DoStart(RowHeight, 1, 1, 1, 0, &View, false);
				UiGraphics()->m_vRoundedSubmissions.clear();
				List.DoNextItem(&aIds[0], true);
				List.DoEnd();
				FillAlpha = 0.0f;
				for(const auto &Submission : UiGraphics()->m_vRoundedSubmissions)
					FillAlpha = std::max(FillAlpha, Submission.m_FillColor.a);
			}
			m_Ui.SetBackgroundAlphaScale(1.0f);
			return FillAlpha;
		};

		const float FullStrength = MeasureSelectionFillAlpha(1.0f);
		const float FadedPage = MeasureSelectionFillAlpha(0.25f);
		EXPECT_NEAR(FullStrength, Selected.a, 0.01f);
		EXPECT_NEAR(FadedPage, FullStrength, 0.01f);
	}
} // namespace
