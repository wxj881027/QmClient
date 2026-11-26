#include <game/client/QmUi/AssetsEditorInput.h>
#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>

namespace
{
	using namespace qmclient::assets_editor;
	constexpr float Threshold = ui_token::spacing::XS;

	struct SGestureFixture
	{
		CCanvasGesture m_Gesture;
		std::vector<int> m_vCandidates{3};
		SCanvasHit m_Hit{3, true};

		ECanvasAction Frame(bool Press, bool Down, float Time, float X = 0, bool Blocked = false)
		{
			return m_Gesture.Update(m_Hit, m_vCandidates, vec2(X, 0), Press, Down, Blocked, Time, Threshold);
		}
	};
}

TEST(AssetsEditorGesture, ReleaseSelectsAndSecondClickOpensPicker)
{
	SGestureFixture Input;
	EXPECT_EQ(Input.Frame(true, true, 0), ECanvasAction::NONE);
	EXPECT_EQ(Input.Frame(false, false, 0.1f), ECanvasAction::SELECT);
	EXPECT_EQ(Input.Frame(true, true, 0.2f), ECanvasAction::NONE);
	EXPECT_EQ(Input.Frame(false, false, 0.3f), ECanvasAction::PICK_COLOR);
}

TEST(AssetsEditorGesture, EitherCanvasStartsDraggingOnMovementWithoutAWait)
{
	for(bool Target : {false, true})
	{
		SGestureFixture Input;
		Input.m_Hit.m_Target = Target;
		Input.Frame(true, true, 0);
		EXPECT_EQ(Input.Frame(false, true, 0.01f, Threshold), ECanvasAction::NONE);
		EXPECT_EQ(Input.Frame(false, true, 0.02f, Threshold + 1), ECanvasAction::BEGIN_DRAG);
		EXPECT_TRUE(Input.m_Gesture.Dragging());
		Input.m_Hit = {7, true};
		EXPECT_EQ(Input.Frame(false, false, 0.03f, 20), ECanvasAction::DROP);
		EXPECT_FALSE(Input.m_Gesture.Dragging());
	}
}

TEST(AssetsEditorGesture, HoldingStillDoesNotStartDrag)
{
	SGestureFixture Input;
	Input.Frame(true, true, 0);
	EXPECT_EQ(Input.Frame(false, true, 1), ECanvasAction::NONE);
	EXPECT_EQ(Input.Frame(false, false, 2), ECanvasAction::SELECT);
}

TEST(AssetsEditorGesture, PopupCancelsPendingClicksAndActiveDrops)
{
	for(float X : {0.0f, Threshold + 1})
	{
		SGestureFixture Input;
		Input.Frame(true, true, 0);
		Input.Frame(false, true, 0.01f, X);
		EXPECT_EQ(Input.Frame(false, true, 0.02f, X, true), ECanvasAction::NONE);
		EXPECT_FALSE(Input.m_Gesture.Dragging());
		EXPECT_EQ(Input.Frame(false, false, 0.03f, X), ECanvasAction::NONE);
	}
}

TEST(AssetsEditorGesture, DonorClickCyclesAndDoubleClickQuickReplaces)
{
	SGestureFixture Input;
	Input.m_Hit.m_Target = false;
	Input.Frame(true, true, 0);
	EXPECT_EQ(Input.Frame(false, false, 0.01f), ECanvasAction::CYCLE);
	Input.Frame(true, true, 0.1f);
	EXPECT_EQ(Input.Frame(false, false, 0.11f), ECanvasAction::QUICK_REPLACE);
}

TEST(AssetsEditorGesture, CyclingToNextOverlappingDonorPreservesDoubleClick)
{
	SGestureFixture Input;
	Input.m_vCandidates = {3, 7};
	Input.m_Hit = {3, false};
	Input.Frame(true, true, 0);
	ASSERT_EQ(Input.Frame(false, false, 0.01f), ECanvasAction::CYCLE);
	Input.m_Hit.m_Index = 7;
	Input.Frame(true, true, 0.10f);
	EXPECT_EQ(Input.Frame(false, false, 0.11f), ECanvasAction::QUICK_REPLACE);
}

TEST(AssetsEditorGesture, EnteringAnotherDonorGroupWithinClickDistanceStartsANewCycle)
{
	SGestureFixture Input;
	Input.m_vCandidates = {3, 7};
	Input.m_Hit = {3, false};
	Input.Frame(true, true, 0);
	ASSERT_EQ(Input.Frame(false, false, 0.01f), ECanvasAction::CYCLE);
	Input.m_vCandidates = {3, 8};
	Input.Frame(true, true, 0.10f, 1);
	EXPECT_EQ(Input.Frame(false, false, 0.11f, 1), ECanvasAction::CYCLE);
}

TEST(AssetsEditorGesture, QuickReplaceOnlyFallsBackToDonorSlotWithoutASelection)
{
	EXPECT_EQ(QuickReplaceTarget(0, 7), 0);
	EXPECT_EQ(QuickReplaceTarget(-1, 7), 7);
}
