#include <game/client/ui/card_drag_logic.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
SCardDragFrame TestFrame()
{
	SCardDragFrame Frame;
	Frame.m_Viewport = {0.0f, 0.0f, 400.0f, 300.0f};
	Frame.m_aColumnRects = {{{0.0f, 0.0f, 400.0f, 100.0f}, {0.0f, 100.0f, 200.0f, 200.0f}, {200.0f, 100.0f, 200.0f, 200.0f}}};
	Frame.m_vItems = {
		{"qm.full.a", 0, {0.0f, 0.0f, 400.0f, 48.0f}},
		{"qm.full.b", 0, {0.0f, 52.0f, 400.0f, 48.0f}},
		{"qm.left.a", 1, {0.0f, 100.0f, 200.0f, 48.0f}},
		{"qm.left.b", 1, {0.0f, 152.0f, 200.0f, 48.0f}},
		{"qm.right.a", 2, {200.0f, 100.0f, 200.0f, 48.0f}},
	};
	Frame.m_TwoColumns = true;
	return Frame;
}
}

TEST(CardDragLogic, RectContainsAndThresholdUseDistance)
{
	const SCardDragRect Rect{10.0f, 10.0f, 100.0f, 50.0f};
	EXPECT_TRUE(CardDragRectContains(Rect, 10.0f, 10.0f));
	EXPECT_TRUE(CardDragRectContains(Rect, 110.0f, 60.0f));
	EXPECT_FALSE(CardDragRectContains(Rect, 111.0f, 60.0f));
	EXPECT_FALSE(CardDragRectContains(Rect, 0.0f, 0.0f));
	EXPECT_FALSE(CardDragThresholdExceeded(0.0f, 0.0f, 6.0f, 8.0f, 10.0f));
	EXPECT_TRUE(CardDragThresholdExceeded(0.0f, 0.0f, 6.0f, 8.0f, 9.0f));
	// 零距离永不超阈值；负阈值按 0 收敛。
	EXPECT_FALSE(CardDragThresholdExceeded(0.0f, 0.0f, 0.0f, 0.0f, -1.0f));
}

TEST(CardDragLogic, ReleaseBeforeThresholdIsClickNotTransaction)
{
	CCardDragState Drag;
	const SCardDragFrame Frame = TestFrame();
	Drag.Arm("home", "qm.full.a", 0, {0.0f, 0.0f, 400.0f, 24.0f}, 10.0f, 10.0f);
	EXPECT_EQ(Drag.Phase(), ECardDragPhase::ARMED);
	SCardDragInput Input;
	Input.m_Down = false;
	Input.m_Released = true;
	Input.m_X = 12.0f;
	Input.m_Y = 11.0f;
	const SCardDragUpdate Result = Drag.Update(Input, Frame, 10.0f);
	EXPECT_FALSE(Result.m_Committed);
	EXPECT_FALSE(Result.m_Cancelled);
	EXPECT_FALSE(Result.m_Started);
	EXPECT_EQ(Drag.Phase(), ECardDragPhase::IDLE);
}

TEST(CardDragLogic, StartsAfterThresholdAndCommitsTransactionOnRelease)
{
	CCardDragState Drag;
	const SCardDragFrame Frame = TestFrame();
	Drag.Arm("home", "qm.left.a", 1, {0.0f, 100.0f, 200.0f, 24.0f}, 10.0f, 110.0f);
	EXPECT_EQ(Drag.SourcePageId(), "home");
	EXPECT_EQ(Drag.CardId(), "qm.left.a");
	SCardDragInput Input;
	Input.m_Down = true;
	Input.m_X = 300.0f;
	Input.m_Y = 130.0f;
	EXPECT_TRUE(Drag.Update(Input, Frame, 10.0f).m_Started);
	EXPECT_EQ(Drag.Phase(), ECardDragPhase::DRAGGING);
	// 移到右列上半部：目标列 2、插入序 0。
	Input.m_X = 300.0f;
	Input.m_Y = 110.0f;
	const SCardDragUpdate Moved = Drag.Update(Input, Frame, 10.0f);
	EXPECT_TRUE(Moved.m_TargetChanged);
	EXPECT_EQ(Drag.TargetColumn(), 2);
	EXPECT_EQ(Drag.TargetOrder(), 0);
	// 松手提交事务：状态机复位，事务数据随结果带出。
	Input.m_Down = false;
	Input.m_Released = true;
	const SCardDragUpdate Committed = Drag.Update(Input, Frame, 10.0f);
	EXPECT_TRUE(Committed.m_Committed);
	EXPECT_FALSE(Committed.m_Cancelled);
	EXPECT_EQ(Committed.m_CardId, "qm.left.a");
	EXPECT_EQ(Committed.m_SourcePageId, "home");
	EXPECT_EQ(Committed.m_TargetPageId, "home");
	EXPECT_EQ(Committed.m_TargetColumn, 2);
	EXPECT_EQ(Committed.m_TargetOrder, 0);
	EXPECT_EQ(Drag.Phase(), ECardDragPhase::IDLE);
	EXPECT_TRUE(Drag.CardId().empty());
}

TEST(CardDragLogic, EscapeCancelsWithoutCommit)
{
	CCardDragState Drag;
	const SCardDragFrame Frame = TestFrame();
	Drag.Arm("home", "qm.full.a", 0, {0.0f, 0.0f, 400.0f, 24.0f}, 10.0f, 10.0f);
	SCardDragInput Input;
	Input.m_Down = true;
	Input.m_X = 100.0f;
	Input.m_Y = 100.0f;
	EXPECT_TRUE(Drag.Update(Input, Frame, 10.0f).m_Started);
	Input.m_Cancelled = true;
	const SCardDragUpdate Cancelled = Drag.Update(Input, Frame, 10.0f);
	EXPECT_TRUE(Cancelled.m_Cancelled);
	EXPECT_FALSE(Cancelled.m_Committed);
	EXPECT_EQ(Drag.Phase(), ECardDragPhase::IDLE);
}

TEST(CardDragLogic, LostPointerSequenceCancelsInsteadOfCommitting)
{
	CCardDragState Drag;
	const SCardDragFrame Frame = TestFrame();
	Drag.Arm("home", "qm.full.a", 0, {0.0f, 0.0f, 400.0f, 24.0f}, 10.0f, 10.0f);
	SCardDragInput Input;
	Input.m_Down = true;
	Input.m_X = 100.0f;
	Input.m_Y = 100.0f;
	EXPECT_TRUE(Drag.Update(Input, Frame, 10.0f).m_Started);
	// 指针序列异常（失焦等）：Down 与 Released 同时为 false，视为取消。
	Input.m_Down = false;
	Input.m_Released = false;
	const SCardDragUpdate Cancelled = Drag.Update(Input, Frame, 10.0f);
	EXPECT_TRUE(Cancelled.m_Cancelled);
	EXPECT_FALSE(Cancelled.m_Committed);
	EXPECT_EQ(Drag.Phase(), ECardDragPhase::IDLE);
}

TEST(CardDragLogic, HoveringPageTabSwitchesPreviewAndKeepsTransaction)
{
	CCardDragState Drag;
	SCardDragFrame Frame = TestFrame();
	Frame.m_vPageTabRects = {{0.0f, -40.0f, 100.0f, 24.0f}, {100.0f, -40.0f, 100.0f, 24.0f}};
	Frame.m_vPageTabIds = {"home", "other"};
	Drag.Arm("home", "qm.full.a", 0, {0.0f, 0.0f, 400.0f, 24.0f}, 10.0f, 10.0f);
	SCardDragInput Input;
	Input.m_Down = true;
	Input.m_X = 150.0f;
	Input.m_Y = -30.0f;
	const SCardDragUpdate Started = Drag.Update(Input, Frame, 10.0f);
	EXPECT_TRUE(Started.m_Started);
	EXPECT_TRUE(Started.m_PreviewPageChanged);
	EXPECT_EQ(Drag.PreviewPageId(), "other");
	EXPECT_EQ(Drag.SourcePageId(), "home");
	// 松手提交跨页事务：目标页为预览页。
	Input.m_Down = false;
	Input.m_Released = true;
	const SCardDragUpdate Committed = Drag.Update(Input, Frame, 10.0f);
	EXPECT_TRUE(Committed.m_Committed);
	EXPECT_EQ(Committed.m_SourcePageId, "home");
	EXPECT_EQ(Committed.m_TargetPageId, "other");
}

TEST(CardDragLogic, AutoScrollOnlyNearEdgesInsideViewport)
{
	const SCardDragRect Viewport{0.0f, 0.0f, 400.0f, 300.0f};
	// 顶部边缘向上滚动，底部边缘向下滚动，中间为 0。
	EXPECT_LT(CardDragAutoScrollDelta(10.0f, Viewport, 1.0f), 0.0f);
	EXPECT_GT(CardDragAutoScrollDelta(290.0f, Viewport, 1.0f), 0.0f);
	EXPECT_EQ(CardDragAutoScrollDelta(150.0f, Viewport, 1.0f), 0.0f);
	// 视口外的收敛由调用方命中测试门控；函数对越界指针收敛到满速。
	EXPECT_LT(CardDragAutoScrollDelta(-10.0f, Viewport, 1.0f), 0.0f);
	EXPECT_GT(CardDragAutoScrollDelta(400.0f, Viewport, 1.0f), 0.0f);
	// 零高度视口与缩放边界安全。
	EXPECT_EQ(CardDragAutoScrollDelta(10.0f, {0.0f, 0.0f, 400.0f, 0.0f}, 1.0f), 0.0f);
	EXPECT_EQ(CardDragAutoScrollDelta(150.0f, Viewport, 0.0f), 0.0f);
	// 边缘内越靠近边界速度越大。
	EXPECT_GT(std::abs(CardDragAutoScrollDelta(1.0f, Viewport, 1.0f)), std::abs(CardDragAutoScrollDelta(20.0f, Viewport, 1.0f)));
}

TEST(CardDragLogic, DropOrderExcludesDraggedCardAndUsesMidpoints)
{
	const std::vector<SCardDragItem> vItems = {
		{"qm.a", 1, {0.0f, 0.0f, 100.0f, 40.0f}},
		{"qm.b", 1, {0.0f, 44.0f, 100.0f, 40.0f}},
		{"qm.c", 1, {0.0f, 88.0f, 100.0f, 40.0f}},
	};
	// 指针在目标卡片中点之前 → 插到它前面；之后 → 插到下一位置。
	EXPECT_EQ(ResolveCardDropOrder(10.0f, 1, vItems, "qm.b"), 0);
	EXPECT_EQ(ResolveCardDropOrder(70.0f, 1, vItems, "qm.b"), 1);
	EXPECT_EQ(ResolveCardDropOrder(200.0f, 1, vItems, "qm.b"), 2);
	// 被拖卡片不占用插入序。
	EXPECT_EQ(ResolveCardDropOrder(200.0f, 1, vItems, "qm.c"), 2);
	// 只统计目标列内的条目。
	EXPECT_EQ(ResolveCardDropOrder(10.0f, 0, vItems, "qm.b"), 0);
}

TEST(CardDragLogic, PlacementPreviewMovesCardBetweenColumns)
{
	std::array<std::vector<std::string>, 3> aColumns = {{{"qm.a", "qm.b"}, {"qm.c", "qm.d"}, {"qm.x", "qm.y"}}};
	ApplyCardDragPlacement(aColumns, "qm.a", 2, 1);
	EXPECT_EQ(aColumns[0], (std::vector<std::string>{"qm.b"}));
	EXPECT_EQ(aColumns[2], (std::vector<std::string>{"qm.x", "qm.a", "qm.y"}));
	// 插入序超出列尾时收敛到末尾。
	ApplyCardDragPlacement(aColumns, "qm.a", 1, 99);
	EXPECT_EQ(aColumns[1], (std::vector<std::string>{"qm.c", "qm.d", "qm.a"}));
	// 非法目标列被忽略。
	ApplyCardDragPlacement(aColumns, "qm.a", 7, 0);
	EXPECT_EQ(aColumns[1], (std::vector<std::string>{"qm.c", "qm.d", "qm.a"}));
}

TEST(CardDragLogic, SingleColumnPreviewAlignsWithLeftColumnCommitOrder)
{
	// 单视觉列：被拖卡成为左列第 TargetOrder 个卡片；右列保持原位，
	// 与提交映射（左列可见序）一致。
	std::array<std::vector<std::string>, 3> aColumns = {{ {}, {"qm.l0", "qm.l1", "qm.l2"}, {"qm.r0", "qm.r1"} }};
	ApplyCardDragSingleColumnPlacement(aColumns, "qm.l1", 0);
	EXPECT_EQ(aColumns[1], (std::vector<std::string>{"qm.l1", "qm.l0", "qm.l2"}));
	EXPECT_EQ(aColumns[2], (std::vector<std::string>{"qm.r0", "qm.r1"}));
	// 右列卡片拖入左列：从右列移除，插入左列指定序。
	std::array<std::vector<std::string>, 3> aCross = {{ {}, {"qm.l0", "qm.l1"}, {"qm.r0", "qm.r1"} }};
	ApplyCardDragSingleColumnPlacement(aCross, "qm.r0", 1);
	EXPECT_EQ(aCross[1], (std::vector<std::string>{"qm.l0", "qm.r0", "qm.l1"}));
	EXPECT_EQ(aCross[2], (std::vector<std::string>{"qm.r1"}));
	// 序号超出左列长度时收敛到左列末尾。
	std::array<std::vector<std::string>, 3> aTail = {{ {}, {"qm.l0"}, {"qm.r0"} }};
	ApplyCardDragSingleColumnPlacement(aTail, "qm.r0", 99);
	EXPECT_EQ(aTail[1], (std::vector<std::string>{"qm.l0", "qm.r0"}));
	EXPECT_EQ(aTail[2], std::vector<std::string>{});
	// full 卡保持 full 语义。
	std::array<std::vector<std::string>, 3> aFull = {{ {"qm.f0", "qm.f1"}, {}, {} }};
	ApplyCardDragSingleColumnPlacement(aFull, "qm.f1", 0);
	EXPECT_EQ(aFull[0], (std::vector<std::string>{"qm.f1", "qm.f0"}));
	// 不存在的卡片被忽略。
	std::array<std::vector<std::string>, 3> aMissing = {{ {}, {"qm.l0"}, {} }};
	ApplyCardDragSingleColumnPlacement(aMissing, "qm.nope", 0);
	EXPECT_EQ(aMissing[1], (std::vector<std::string>{"qm.l0"}));
}

TEST(CardDragLogic, ReflowTrackContinuesFromCurrentValueAndSnapsWhenDisabled)
{
	SCardReflowTrack Track;
	// 初始化直接对齐目标。
	UpdateCardReflowTrack(Track, 100.0f, 0.016f, 0.12f, false);
	EXPECT_TRUE(Track.m_Initialized);
	EXPECT_EQ(Track.m_Value, 100.0f);
	// 目标变化：从当前位置续接，ease-out 推进。
	UpdateCardReflowTrack(Track, 200.0f, 0.06f, 0.12f, false);
	EXPECT_TRUE(Track.m_Active);
	EXPECT_GT(Track.m_Value, 100.0f);
	EXPECT_LT(Track.m_Value, 200.0f);
	const float MidValue = Track.m_Value;
	// 目标再次变化时从动画当前位置续接，不跳回目标起点。
	UpdateCardReflowTrack(Track, 300.0f, 0.0f, 0.12f, false);
	EXPECT_EQ(Track.m_From, MidValue);
	EXPECT_EQ(Track.m_Value, MidValue);
	UpdateCardReflowTrack(Track, 300.0f, 10.0f, 0.12f, false);
	EXPECT_FALSE(Track.m_Active);
	EXPECT_EQ(Track.m_Value, 300.0f);
	// Snap（动画关闭）：直接对齐目标，最终位置与输入结果一致。
	UpdateCardReflowTrack(Track, 50.0f, 0.016f, 0.12f, true);
	EXPECT_EQ(Track.m_Value, 50.0f);
	EXPECT_FALSE(Track.m_Active);
	// 微小目标变化直接收敛，不启动动画。
	UpdateCardReflowTrack(Track, 50.005f, 0.016f, 0.12f, false);
	EXPECT_EQ(Track.m_Value, 50.005f);
}
