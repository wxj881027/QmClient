#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/graphics.h>

#if (defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)) && defined(CONF_BACKEND_METAL) && defined(CONF_BACKEND_METAL_READY)
#include <engine/client/backend/metal/backend_metal.h>
#endif

#include <gtest/gtest.h>

TEST(GraphicsVulkanScheduler, LightLoadsAndSingleThreadAvoidWorkerDispatch)
{
	CQmVulkanRenderScheduler Scheduler;
	for(const size_t Draws : {size_t(0), size_t(1), size_t(127)})
	{
		Scheduler.NewFrame();
		Scheduler.StartCommands(3, Draws);
		EXPECT_EQ(Scheduler.ThreadIndex(false), 0u);
	}
	Scheduler.NewFrame();
	Scheduler.StartCommands(1, 1000);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 0u);
}

TEST(GraphicsVulkanScheduler, WorkIsSplitByDrawsInsteadOfCommandCount)
{
	CQmVulkanRenderScheduler Scheduler;
	Scheduler.StartCommands(3, 256);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 1u);
	Scheduler.RecordDrawCalls(100);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 1u);
	Scheduler.RecordDrawCalls(28);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 2u);
	Scheduler.RecordDrawCalls(1000);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 2u);
}

TEST(GraphicsVulkanScheduler, WorkerCountIsLimitedByUsefulWork)
{
	CQmVulkanRenderScheduler Scheduler;
	Scheduler.StartCommands(16, 128);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 1u);
	Scheduler.RecordDrawCalls(64);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 2u);
}

TEST(GraphicsVulkanScheduler, HeavyCommandCanSkipEmptyWorkerRanges)
{
	CQmVulkanRenderScheduler Scheduler;
	Scheduler.StartCommands(5, 1024);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 1u);
	Scheduler.RecordDrawCalls(800);
	const size_t PreviousThread = Scheduler.CurrentThreadIndex();
	EXPECT_EQ(Scheduler.ThreadIndex(false), 4u);
	EXPECT_EQ(PreviousThread, 1u);
}

TEST(GraphicsVulkanScheduler, MainThreadTailPersistsAcrossCommandBuffersUntilNextFrame)
{
	CQmVulkanRenderScheduler Scheduler;
	Scheduler.StartCommands(3, 256);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 1u);
	Scheduler.UseMainThread();
	EXPECT_EQ(Scheduler.ThreadIndex(false), 0u);
	Scheduler.StartCommands(3, 512);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 0u);
	Scheduler.NewFrame();
	EXPECT_EQ(Scheduler.ThreadIndex(false), 1u);
}

TEST(GraphicsVulkanScheduler, LightOrExternalPassCannotReorderLaterHeavyWork)
{
	CQmVulkanRenderScheduler Scheduler;
	Scheduler.StartCommands(3, 10);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 0u);
	Scheduler.StartCommands(3, 512);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 0u);
	Scheduler.NewFrame();
	EXPECT_EQ(Scheduler.ThreadIndex(true), 0u);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 0u);
}

TEST(GraphicsVulkanScheduler, AssignmentNeverMovesBackwardsWithinFrame)
{
	CQmVulkanRenderScheduler Scheduler;
	Scheduler.StartCommands(5, 1024);
	Scheduler.RecordDrawCalls(800);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 4u);
	Scheduler.StartCommands(5, 128);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 4u);
}

TEST(GraphicsVulkanScheduler, EstimateAndAccumulationDoNotOverflow)
{
	CQmVulkanRenderScheduler Scheduler;
	Scheduler.StartCommands(3, std::numeric_limits<size_t>::max());
	EXPECT_EQ(Scheduler.ThreadIndex(false), 1u);
	Scheduler.RecordDrawCalls(std::numeric_limits<size_t>::max());
	Scheduler.RecordDrawCalls(100);
	EXPECT_EQ(Scheduler.ThreadIndex(false), 2u);
}
