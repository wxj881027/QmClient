#include <game/editor/editor_ui.h>
#include <game/editor/qm_editor_order.h>

#include <gtest/gtest.h>

TEST(QmEditorInteraction, ClickTransitionsToDragAndReleaseClearsTarget)
{
	SEditorLayerListState State;
	int Button;
	State.m_pDraggedButton = &Button;
	State.SetOperation(SEditorLayerListState::OP_CLICK);
	State.SetOperation(SEditorLayerListState::OP_LAYER_DRAG);
	EXPECT_EQ(State.m_PreviousOperation, SEditorLayerListState::OP_CLICK);
	EXPECT_EQ(State.m_Operation, SEditorLayerListState::OP_LAYER_DRAG);
	EXPECT_EQ(State.m_pDraggedButton, &Button);
	State.SetOperation(SEditorLayerListState::OP_NONE);
	EXPECT_EQ(State.m_PreviousOperation, SEditorLayerListState::OP_LAYER_DRAG);
	EXPECT_EQ(State.m_pDraggedButton, nullptr);
}

TEST(QmEditorInteraction, RepeatingOperationRetainsPreviousTransition)
{
	SEditorLayerListState State;
	State.SetOperation(SEditorLayerListState::OP_CLICK);
	State.SetOperation(SEditorLayerListState::OP_GROUP_DRAG);
	State.SetOperation(SEditorLayerListState::OP_GROUP_DRAG);
	EXPECT_EQ(State.m_PreviousOperation, SEditorLayerListState::OP_CLICK);
}

TEST(QmEditorInteraction, ResetCancelsOnlyTheOwningMapsDrag)
{
	SEditorLayerListState First, Second;
	int Button;
	First.m_pDraggedButton = &Button;
	First.m_InitialMouseY = 42.0f;
	First.SetOperation(SEditorLayerListState::OP_LAYER_DRAG);
	Second.SetOperation(SEditorLayerListState::OP_GROUP_DRAG);
	First.ResetDrag();
	EXPECT_EQ(First.m_Operation, SEditorLayerListState::OP_NONE);
	EXPECT_EQ(First.m_PreviousOperation, SEditorLayerListState::OP_NONE);
	EXPECT_EQ(First.m_pDraggedButton, nullptr);
	EXPECT_EQ(First.m_InitialMouseY, 0.0f);
	EXPECT_EQ(Second.m_Operation, SEditorLayerListState::OP_GROUP_DRAG);
}

TEST(QmEditorOrder, MovingNonContiguousLayersWithinGroupRoundTrips)
{
	std::vector<int> vLayers{0, 1, 2, 3, 4};
	ASSERT_TRUE(QmEditorOrder::MoveItemsToIndices(vLayers, vLayers, {0, 2}, {3, 4}));
	EXPECT_EQ(vLayers, (std::vector<int>{1, 3, 4, 0, 2}));
	ASSERT_TRUE(QmEditorOrder::MoveItemsToIndices(vLayers, vLayers, {3, 4}, {0, 2}));
	EXPECT_EQ(vLayers, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(QmEditorOrder, MovingLayersBetweenGroupsRoundTrips)
{
	std::vector<int> vFirst{0, 1, 2, 3};
	std::vector<int> vSecond{4, 5};
	ASSERT_TRUE(QmEditorOrder::MoveItemsToIndices(vFirst, vSecond, {1, 3}, {0, 2}));
	EXPECT_EQ(vFirst, (std::vector<int>{0, 2}));
	EXPECT_EQ(vSecond, (std::vector<int>{1, 4, 3, 5}));
	ASSERT_TRUE(QmEditorOrder::MoveItemsToIndices(vSecond, vFirst, {0, 2}, {1, 3}));
	EXPECT_EQ(vFirst, (std::vector<int>{0, 1, 2, 3}));
	EXPECT_EQ(vSecond, (std::vector<int>{4, 5}));
}

TEST(QmEditorOrder, InvalidMoveLeavesBothGroupsUnchanged)
{
	std::vector<int> vFirst{0, 1, 2};
	std::vector<int> vSecond{3, 4};
	EXPECT_FALSE(QmEditorOrder::MoveItemsToIndices(vFirst, vSecond, {1, 1}, {0, 1}));
	EXPECT_FALSE(QmEditorOrder::MoveItemsToIndices(vFirst, vSecond, {1}, {3}));
	EXPECT_FALSE(QmEditorOrder::MoveItemsToIndices(vFirst, vSecond, {-1}, {0}));
	EXPECT_EQ(vFirst, (std::vector<int>{0, 1, 2}));
	EXPECT_EQ(vSecond, (std::vector<int>{3, 4}));
}
