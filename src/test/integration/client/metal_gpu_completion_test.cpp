#include <base/str.h>

#include <engine/client/backend/metal/metal_gpu_completion.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>

namespace
{
	class CMetalGpuCompletionTest : public ::testing::Test
	{
	protected:
		CMetalFrameState m_Frames;
		CMetalGpuCompletion m_Completion;

		CMetalFrameState::SFrameCapture Begin(size_t Slot = 0)
		{
			EXPECT_TRUE(m_Frames.BeginFrame(Slot));
			const CMetalFrameState::SFrameCapture Capture{m_Frames.CurrentFrameId(), Slot};
			EXPECT_TRUE(m_Frames.FinalizeFrameWithoutPresent());
			return Capture;
		}

		static void Describe(char *pText, size_t Size) { str_copy(pText, "device failure", Size); }
		static void WriteDetails(SGfxErrorContainer &Error, const CMetalGpuCompletion::SFailure &Failure)
		{
			Error.m_vErrors.push_back({false, Failure.m_aDescription});
		}
	};
}

TEST_F(CMetalGpuCompletionTest, NoCompletionDoesNotPublishAnError)
{
	SGfxErrorContainer Error;
	EXPECT_EQ(m_Completion.Get(), nullptr);
	EXPECT_FALSE(m_Completion.Publish(Error, WriteDetails));
	EXPECT_EQ(Error.m_ErrorType, GFX_ERROR_TYPE_NONE);
	EXPECT_TRUE(Error.m_vErrors.empty());
}

TEST_F(CMetalGpuCompletionTest, SuccessfulFrameDoesNotDescribeOrPublishFailure)
{
	const auto Capture = Begin();
	bool Described = false;
	EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, true, 12, 4, [&](char *, size_t) { Described = true; }));
	EXPECT_FALSE(Described);
	EXPECT_EQ(m_Frames.SlotState(0), CMetalFrameState::ESlotState::COMPLETED);
	EXPECT_EQ(m_Completion.Get(), nullptr);
}

TEST_F(CMetalGpuCompletionTest, FailedFramePublishesCoherentMetadata)
{
	const auto Capture = Begin();
	EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, false, 12, 5, Describe));
	EXPECT_EQ(m_Frames.SlotState(0), CMetalFrameState::ESlotState::FAILED);
	const auto *pFailure = m_Completion.Get();
	ASSERT_NE(pFailure, nullptr);
	EXPECT_EQ(pFailure->m_FrameId, Capture.m_FrameId);
	EXPECT_EQ(pFailure->m_CommandId, 12);
	EXPECT_EQ(pFailure->m_Status, 5);
	EXPECT_STREQ(pFailure->m_aDescription, "device failure");
}

TEST_F(CMetalGpuCompletionTest, DescriptionAllocationFailureStillCompletesFailedFrame)
{
	const auto Capture = Begin();
	EXPECT_NO_THROW(EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, false, 17, 5,
		[](char *, size_t) { throw std::bad_alloc(); })));
	EXPECT_EQ(m_Frames.SlotState(0), CMetalFrameState::ESlotState::FAILED);
	const auto *pFailure = m_Completion.Get();
	ASSERT_NE(pFailure, nullptr);
	EXPECT_EQ(pFailure->m_FrameId, Capture.m_FrameId);
	EXPECT_EQ(pFailure->m_CommandId, 17);
	EXPECT_EQ(pFailure->m_Status, 5);
	EXPECT_STREQ(pFailure->m_aDescription, "");
}

TEST_F(CMetalGpuCompletionTest, PartialDescriptionIsDiscardedWhenFormattingThrows)
{
	const auto Capture = Begin();
	EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, false, 12, 5,
		[](char *pText, size_t Size) {
			str_copy(pText, "unfinished domain=", Size);
			throw std::bad_alloc();
		}));
	ASSERT_NE(m_Completion.Get(), nullptr);
	EXPECT_STREQ(m_Completion.Get()->m_aDescription, "");
	EXPECT_EQ(m_Frames.SlotState(0), CMetalFrameState::ESlotState::FAILED);
}

TEST_F(CMetalGpuCompletionTest, DescriptionCannotExposeAnUnterminatedBuffer)
{
	const auto Capture = Begin();
	EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, false, 12, 5,
		[](char *pText, size_t Size) { std::memset(pText, 'x', Size); }));
	const auto *pFailure = m_Completion.Get();
	ASSERT_NE(pFailure, nullptr);
	EXPECT_EQ(std::strlen(pFailure->m_aDescription), sizeof(pFailure->m_aDescription) - 1);
}

TEST_F(CMetalGpuCompletionTest, LaterFailureKeepsFirstRecordAndCompletesItsOwnFrame)
{
	const auto First = Begin(0);
	const auto Second = Begin(1);
	EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, First, false, 12, 5, Describe));
	bool DescribedAgain = false;
	EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, Second, false, 19, 8,
		[&](char *, size_t) { DescribedAgain = true; }));
	EXPECT_FALSE(DescribedAgain);
	EXPECT_EQ(m_Frames.SlotState(1), CMetalFrameState::ESlotState::FAILED);
	ASSERT_NE(m_Completion.Get(), nullptr);
	EXPECT_EQ(m_Completion.Get()->m_FrameId, First.m_FrameId);
	EXPECT_EQ(m_Completion.Get()->m_CommandId, 12);
	EXPECT_EQ(m_Completion.Get()->m_Status, 5);
}

TEST_F(CMetalGpuCompletionTest, ReportAllocationFailureKeepsTheStoppingErrorType)
{
	const auto Capture = Begin();
	ASSERT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, false, 12, 5, Describe));
	SGfxErrorContainer Error;
	EXPECT_NO_THROW(EXPECT_TRUE(m_Completion.Publish(Error,
		[](SGfxErrorContainer &, const auto &) { throw std::bad_alloc(); })));
	EXPECT_EQ(Error.m_ErrorType, GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED);
	EXPECT_TRUE(Error.m_vErrors.empty());
}

TEST_F(CMetalGpuCompletionTest, ExistingGraphicsErrorIsPreserved)
{
	const auto Capture = Begin();
	ASSERT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, false, 12, 5, Describe));
	SGfxErrorContainer Error;
	Error.m_ErrorType = GFX_ERROR_TYPE_RENDER_CMD_FAILED;
	Error.m_vErrors.push_back({false, "first error"});
	bool Reported = false;
	EXPECT_TRUE(m_Completion.Publish(Error, [&](auto &, const auto &) { Reported = true; }));
	EXPECT_FALSE(Reported);
	EXPECT_EQ(Error.m_ErrorType, GFX_ERROR_TYPE_RENDER_CMD_FAILED);
	ASSERT_EQ(Error.m_vErrors.size(), 1u);
	EXPECT_EQ(Error.m_vErrors[0].m_Err, "first error");
}

TEST_F(CMetalGpuCompletionTest, ReportingDoesNotConsumeTheFailure)
{
	const auto Capture = Begin();
	ASSERT_TRUE(m_Completion.CompleteFrame(m_Frames, Capture, false, 12, 5, Describe));
	SGfxErrorContainer First, Second;
	EXPECT_TRUE(m_Completion.Publish(First, WriteDetails));
	EXPECT_TRUE(m_Completion.Publish(Second, WriteDetails));
	ASSERT_EQ(First.m_vErrors.size(), 1u);
	ASSERT_EQ(Second.m_vErrors.size(), 1u);
	EXPECT_EQ(First.m_vErrors[0].m_Err, Second.m_vErrors[0].m_Err);
}

TEST_F(CMetalGpuCompletionTest, ResetAfterDrainingAllowsANewDeviceFailure)
{
	const auto Old = Begin();
	ASSERT_TRUE(m_Completion.CompleteFrame(m_Frames, Old, false, 12, 5, Describe));
	m_Frames.DrainFrames();
	m_Completion.Reset();
	EXPECT_EQ(m_Completion.Get(), nullptr);
	const auto Current = Begin();
	ASSERT_TRUE(m_Completion.CompleteFrame(m_Frames, Current, false, 29, 8, Describe));
	ASSERT_NE(m_Completion.Get(), nullptr);
	EXPECT_EQ(m_Completion.Get()->m_FrameId, Current.m_FrameId);
	EXPECT_EQ(m_Completion.Get()->m_CommandId, 29);
	EXPECT_EQ(m_Completion.Get()->m_Status, 8);
}

TEST_F(CMetalGpuCompletionTest, StaleCompletionCannotChangeAReusedSlot)
{
	const auto Old = Begin();
	ASSERT_TRUE(m_Completion.CompleteFrame(m_Frames, Old, true, 12, 4, Describe));
	const auto Current = Begin();
	EXPECT_FALSE(m_Completion.CompleteFrame(m_Frames, Old, false, 12, 5, Describe));
	EXPECT_EQ(m_Frames.CurrentFrameId(), Current.m_FrameId);
	EXPECT_EQ(m_Frames.SlotState(0), CMetalFrameState::ESlotState::IN_FLIGHT);
}

TEST_F(CMetalGpuCompletionTest, PendingDescriptionIsHiddenAndAnotherCallbackCanComplete)
{
	const auto First = Begin(0);
	const auto Second = Begin(1);
	std::mutex Mutex;
	std::condition_variable Condition;
	bool Entered = false, Release = false, TimedOut = false;
	std::thread Worker([&] {
		m_Completion.CompleteFrame(m_Frames, First, false, 12, 5, [&](char *pText, size_t Size) {
			std::unique_lock<std::mutex> Lock(Mutex);
			Entered = true;
			Condition.notify_all();
			TimedOut = !Condition.wait_for(Lock, std::chrono::seconds(5), [&] { return Release; });
			str_copy(pText, "first failure", Size);
		});
	});
	bool Waiting;
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		Waiting = Condition.wait_for(Lock, std::chrono::seconds(5), [&] { return Entered; });
	}
	EXPECT_TRUE(Waiting);
	if(Waiting)
	{
		EXPECT_EQ(m_Completion.Get(), nullptr);
		EXPECT_TRUE(m_Completion.CompleteFrame(m_Frames, Second, false, 29, 8, Describe));
		EXPECT_EQ(m_Frames.SlotState(1), CMetalFrameState::ESlotState::FAILED);
	}
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Release = true;
		Condition.notify_all();
	}
	Worker.join();
	EXPECT_FALSE(TimedOut);
	ASSERT_NE(m_Completion.Get(), nullptr);
	EXPECT_EQ(m_Completion.Get()->m_FrameId, First.m_FrameId);
	EXPECT_STREQ(m_Completion.Get()->m_aDescription, "first failure");
}

TEST_F(CMetalGpuCompletionTest, ConcurrentFailuresKeepOneCoherentSnapshotAndCompleteAllFrames)
{
	const std::array Captures{Begin(0), Begin(1), Begin(2)};
	std::atomic<int> Described{0};
	std::array<std::thread, 3> aWorkers;
	for(size_t Slot = 0; Slot < aWorkers.size(); ++Slot)
	{
		aWorkers[Slot] = std::thread([&, Slot] {
			const auto Capture = Captures[Slot];
			m_Completion.CompleteFrame(m_Frames, Capture, false, static_cast<int>(Capture.m_FrameId) + 100,
				static_cast<int>(Capture.m_FrameId) + 200, [&](char *pText, size_t Size) {
					++Described;
					str_format(pText, Size, "failure %llu", static_cast<unsigned long long>(Capture.m_FrameId));
				});
		});
	}
	for(auto &Worker : aWorkers)
		Worker.join();
	EXPECT_EQ(Described, 1);
	const auto *pFailure = m_Completion.Get();
	ASSERT_NE(pFailure, nullptr);
	EXPECT_EQ(pFailure->m_CommandId, static_cast<int>(pFailure->m_FrameId) + 100);
	EXPECT_EQ(pFailure->m_Status, static_cast<int>(pFailure->m_FrameId) + 200);
	char aExpected[64];
	str_format(aExpected, sizeof(aExpected), "failure %llu", static_cast<unsigned long long>(pFailure->m_FrameId));
	EXPECT_STREQ(pFailure->m_aDescription, aExpected);
	for(size_t Slot = 0; Slot < aWorkers.size(); ++Slot)
		EXPECT_EQ(m_Frames.SlotState(Slot), CMetalFrameState::ESlotState::FAILED);
}
