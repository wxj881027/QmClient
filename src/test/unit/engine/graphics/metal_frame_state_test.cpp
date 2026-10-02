#include <engine/client/backend/metal/metal_frame_state.h>

#include <gtest/gtest.h>

TEST(MetalFrameState, FinalizesAFrameOnlyOnce)
{
	CMetalFrameState State;
	ASSERT_TRUE(State.BeginFrame(0));
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::ALREADY_FINALIZED);
	EXPECT_EQ(State.LastPresentedFrameId(), 1U);
}

TEST(MetalFrameState, CannotFinalizeBeforeStartingAFrame)
{
	CMetalFrameState State;
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::FAILED);
	EXPECT_FALSE(State.CaptureRetained());
}

TEST(MetalFrameState, ReadbackFinalizesWithoutReplacingPresentedCapture)
{
	CMetalFrameState State;
	CMetalFrameState::SFrameCapture Capture;
	ASSERT_TRUE(State.BeginFrame(0));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	ASSERT_TRUE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(Capture.m_FrameId, 1U);

	ASSERT_TRUE(State.BeginFrame(1));
	EXPECT_TRUE(State.FinalizeFrameWithoutPresent());
	EXPECT_TRUE(State.CurrentFrameFinalized());
	EXPECT_TRUE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(Capture.m_FrameId, 1U);
	EXPECT_FALSE(State.FinalizeFrameWithoutPresent());
}

TEST(MetalFrameState, NonPresentedSubmissionKeepsSlotInFlightUntilGpuCompletion)
{
	CMetalFrameState State;
	ASSERT_TRUE(State.BeginFrame(0));
	ASSERT_TRUE(State.FinalizeFrameWithoutPresent());
	EXPECT_EQ(State.SlotState(0), CMetalFrameState::ESlotState::IN_FLIGHT);

	const CMetalFrameState::SFrameCapture Capture{1, 0};
	EXPECT_TRUE(State.CompleteFrame(Capture, true));
	EXPECT_EQ(State.SlotState(0), CMetalFrameState::ESlotState::COMPLETED);
}

TEST(MetalFrameState, PresentedReadbackIsConsumedOnlyOnce)
{
	CMetalFrameState State;
	EXPECT_FALSE(State.ReadbackPresented());
	EXPECT_FALSE(State.ConsumeReadbackPresented());

	State.MarkReadbackPresented();
	EXPECT_TRUE(State.ReadbackPresented());
	EXPECT_TRUE(State.ConsumeReadbackPresented());
	EXPECT_FALSE(State.ReadbackPresented());
	EXPECT_FALSE(State.ConsumeReadbackPresented());
}

TEST(MetalFrameState, NewBackbufferRenderingInvalidatesPresentedReadback)
{
	CMetalFrameState State;
	State.MarkReadbackPresented();
	State.ClearReadbackPresented();
	EXPECT_FALSE(State.ReadbackPresented());
}

TEST(MetalFrameState, DrainingFramesClearsPresentedReadbackAndCapture)
{
	CMetalFrameState State;
	CMetalFrameState::SFrameCapture Capture;
	ASSERT_TRUE(State.BeginFrame(0));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	ASSERT_TRUE(State.ReadLastPresentedFrame(Capture));
	State.MarkReadbackPresented();
	EXPECT_EQ(State.DrainFrames(), 1U);
	EXPECT_FALSE(State.ReadbackPresented());
	EXPECT_FALSE(State.CaptureRetained());
	EXPECT_FALSE(State.ReadLastPresentedFrame(Capture));
}

TEST(MetalFrameState, ScreenshotAndReadPixelSharePresentedFrame)
{
	CMetalFrameState State;
	CMetalFrameState::SFrameCapture Capture;
	ASSERT_TRUE(State.BeginFrame(1));
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	ASSERT_TRUE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(Capture.m_FrameId, State.CurrentFrameId());
	EXPECT_EQ(Capture.m_Slot, 1U);
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::ALREADY_FINALIZED);
}

TEST(MetalFrameState, ScreenshotAndReadPixelCommandsFinalizeOnlyOnePresent)
{
	CMetalFrameState State;
	CMetalFrameState::SFrameCapture Capture;
	ASSERT_TRUE(State.BeginFrame(0));
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	ASSERT_TRUE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(Capture.m_FrameId, 1U);
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::ALREADY_FINALIZED);
	EXPECT_TRUE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(Capture.m_FrameId, 1U);
}

TEST(MetalFrameState, DrawableFailureDoesNotPresentOrRetainCapture)
{
	CMetalFrameState State;
	ASSERT_TRUE(State.BeginFrame(0));
	EXPECT_EQ(State.FinalizeFrameForPresent(false), CMetalFrameState::EFinalizeResult::FAILED);
	EXPECT_TRUE(State.CurrentFrameFailed());
	EXPECT_FALSE(State.CaptureRetained());
	CMetalFrameState::SFrameCapture Capture;
	EXPECT_FALSE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::FAILED);
}

TEST(MetalFrameState, PreviousCaptureLivesUntilNextSuccessfulFinalize)
{
	CMetalFrameState State;
	CMetalFrameState::SFrameCapture Capture;
	ASSERT_TRUE(State.BeginFrame(0));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	ASSERT_TRUE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(Capture.m_FrameId, 1U);

	ASSERT_TRUE(State.BeginFrame(1));
	EXPECT_TRUE(State.CaptureRetained());
	EXPECT_EQ(State.FinalizeFrameForPresent(false), CMetalFrameState::EFinalizeResult::FAILED);
	EXPECT_TRUE(State.CaptureRetained());
	ASSERT_TRUE(State.BeginFrame(2));
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	ASSERT_TRUE(State.ReadLastPresentedFrame(Capture));
	EXPECT_EQ(Capture.m_FrameId, 3U);
	EXPECT_EQ(Capture.m_Slot, 2U);
}

TEST(MetalFrameState, CannotStartAnotherFrameBeforeFinalization)
{
	CMetalFrameState State;
	ASSERT_TRUE(State.BeginFrame(0));
	EXPECT_FALSE(State.BeginFrame(1));
	EXPECT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	EXPECT_TRUE(State.BeginFrame(1));
}

TEST(MetalFrameState, SlotCannotBeReusedBeforeGpuCompletion)
{
	CMetalFrameState State(2);
	ASSERT_TRUE(State.BeginFrame(0));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);

	ASSERT_TRUE(State.BeginFrame(1));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	EXPECT_FALSE(State.BeginFrame(0));

	const CMetalFrameState::SFrameCapture Capture{1, 0};
	EXPECT_TRUE(State.CompleteFrame(Capture, true));
	EXPECT_EQ(State.SlotState(0), CMetalFrameState::ESlotState::COMPLETED);
	EXPECT_TRUE(State.BeginFrame(0));
}

TEST(MetalFrameState, CompletionErrorMarksTheSlotFailed)
{
	CMetalFrameState State;
	ASSERT_TRUE(State.BeginFrame(0));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);

	const CMetalFrameState::SFrameCapture Capture{1, 0};
	EXPECT_TRUE(State.CompleteFrame(Capture, false));
	EXPECT_TRUE(State.CurrentFrameFailed());
	EXPECT_EQ(State.SlotState(0), CMetalFrameState::ESlotState::FAILED);
	EXPECT_FALSE(State.CompleteFrame(Capture, false));
}

TEST(MetalFrameState, CompletionBeforePresentIsRejected)
{
	CMetalFrameState State;
	ASSERT_TRUE(State.BeginFrame(0));
	const CMetalFrameState::SFrameCapture Capture{1, 0};
	EXPECT_FALSE(State.CompleteFrame(Capture, true));
	EXPECT_EQ(State.SlotState(0), CMetalFrameState::ESlotState::IN_FLIGHT);
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	EXPECT_TRUE(State.CompleteFrame(Capture, true));
}

TEST(MetalFrameState, ShutdownDrainReleasesInFlightSlots)
{
	CMetalFrameState State(3);
	ASSERT_TRUE(State.BeginFrame(0));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);
	ASSERT_TRUE(State.BeginFrame(1));
	ASSERT_EQ(State.FinalizeFrameForPresent(true), CMetalFrameState::EFinalizeResult::PRESENTED);

	EXPECT_EQ(State.DrainFrames(), 2U);
	EXPECT_EQ(State.SlotState(0), CMetalFrameState::ESlotState::AVAILABLE);
	EXPECT_EQ(State.SlotState(1), CMetalFrameState::ESlotState::AVAILABLE);
	EXPECT_EQ(State.CurrentFrameId(), 0U);
	EXPECT_TRUE(State.BeginFrame(2));
}
