#include <engine/client/backend/metal/metal_render_target_state.h>

#include <gtest/gtest.h>

TEST(MetalRenderTargetState, RejectsNestedBeginAndActiveTargetDestroy)
{
	CMetalRenderTargetState State;
	EXPECT_FALSE(State.Begin(-1));
	ASSERT_TRUE(State.Begin(3));
	EXPECT_TRUE(State.IsActive());
	EXPECT_EQ(State.ActiveTargetId(), 3);
	EXPECT_FALSE(State.Begin(4));
	EXPECT_FALSE(State.CanDestroy(3));
	EXPECT_TRUE(State.CanDestroy(4));
}

TEST(MetalRenderTargetState, RejectsSamplingTheActiveAttachmentAndRestoresDrawableState)
{
	CMetalRenderTargetState State;
	ASSERT_TRUE(State.Begin(2));
	EXPECT_FALSE(State.CanDraw(2));
	EXPECT_TRUE(State.CanDraw(1));
	ASSERT_TRUE(State.End());
	EXPECT_FALSE(State.IsActive());
	EXPECT_EQ(State.ActiveTargetId(), -1);
	EXPECT_TRUE(State.CanDraw(2));
	EXPECT_FALSE(State.End());
}

TEST(MetalRenderTargetState, ResetAllowsTeardownAfterAnActiveTarget)
{
	CMetalRenderTargetState State;
	ASSERT_TRUE(State.Begin(7));
	State.Reset();
	EXPECT_FALSE(State.IsActive());
	EXPECT_TRUE(State.CanDestroy(7));
}
