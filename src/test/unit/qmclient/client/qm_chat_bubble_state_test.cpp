#include <game/client/components/qmclient/chat_bubble_state.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	struct SText
	{
		bool m_FailDelete = false;
		void DeleteTextContainer(STextContainerIndex &Index)
		{
			if(Index.m_Index < 0)
				return;
			if(m_FailDelete)
				throw std::bad_alloc();
			Index.m_UseCount->Invalidate();
			Index.Reset();
		}
	};
	struct SLayoutText : SText
	{
		vec2 m_Size = vec2(40, 10);
		int m_Created = 0;
		bool CreateTextContainer(STextContainerIndex &Index, CTextCursor *, const char *)
		{
			Index.m_UseCount = std::make_shared<STextContainerUsages>();
			Index.m_Index = ++m_Created;
			return true;
		}
		STextBoundingBox GetBoundingBoxTextContainer(STextContainerIndex)
		{
			STextBoundingBox Result{};
			Result.m_W = m_Size.x;
			Result.m_H = m_Size.y;
			return Result;
		}
		uint64_t GlyphAtlasRevision() const { return 7; }
	};
}

TEST(QmChatBubbleState, InvalidInitialMetricsRemainOwnedAndRetryable)
{
	SLayoutText Text;
	CQmChatBubbleState State;
	Text.m_Size.x = std::numeric_limits<float>::infinity();
	EXPECT_THROW(State.PrepareLayout(Text, 14, 230, "message"), std::length_error);
	EXPECT_FALSE(State.m_TextContainerIndex.Valid());
	EXPECT_TRUE(State.HasTextResources());
	EXPECT_STREQ(State.m_aLayoutText, "");
	EXPECT_EQ(State.m_CachedTextWidth, 0);
	Text.m_Size = vec2(40, 10);
	State.PrepareLayout(Text, 14, 230, "message");
	EXPECT_TRUE(State.m_TextContainerIndex.Valid());
	EXPECT_EQ(Text.m_Created, 2);
	EXPECT_EQ(State.m_CachedTextWidth, 40);
	EXPECT_STREQ(State.m_aLayoutText, "message");
	State.Reset(Text);
	EXPECT_FALSE(State.HasTextResources());
}

TEST(QmChatBubbleState, InvalidRefreshDoesNotPublishItsMetricsAndCanRecover)
{
	SLayoutText Text;
	CQmChatBubbleState State;
	State.PrepareLayout(Text, 14, 230, "old");
	Text.m_Size.y = -1;
	EXPECT_THROW(State.PrepareLayout(Text, 16, 230, "new"), std::length_error);
	EXPECT_FALSE(State.m_TextContainerIndex.Valid());
	EXPECT_TRUE(State.HasTextResources());
	EXPECT_EQ(State.m_CachedFontSize, 14);
	EXPECT_EQ(State.m_CachedTextHeight, 10);
	EXPECT_STREQ(State.m_aLayoutText, "old");
	Text.m_Size = vec2(60, 12);
	State.PrepareLayout(Text, 16, 230, "new");
	EXPECT_TRUE(State.m_TextContainerIndex.Valid());
	EXPECT_EQ(State.m_CachedFontSize, 16);
	EXPECT_EQ(State.m_CachedTextHeight, 12);
	EXPECT_STREQ(State.m_aLayoutText, "new");
	State.Reset(Text);
}

TEST(QmChatBubbleState, RepeatedResetDoesNotAllocateANewUsageIdentity)
{
	SText Text;
	CQmChatBubbleState State;
	const auto *pUsage = State.m_TextContainerIndex.m_UseCount.get();
	State.m_Initialized = State.m_LastVisible = State.m_LastTyping = true;
	State.m_LastChatBubbleStartTick = 99;
	State.m_CachedTextWidth = 100;
	str_copy(State.m_aCachedText, "old message");
	bool Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		State.Reset(Text);
		State.Reset(Text);
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(State.m_TextContainerIndex.m_UseCount.get(), pUsage);
	EXPECT_FALSE(State.m_Initialized);
	EXPECT_FALSE(State.m_LastVisible);
	EXPECT_FALSE(State.m_LastTyping);
	EXPECT_EQ(State.m_LastChatBubbleStartTick, 0);
	EXPECT_EQ(State.m_CachedTextWidth, 0);
	EXPECT_STREQ(State.m_aCachedText, "");
	EXPECT_TRUE(std::isnan(State.m_LastTargetAlpha));
}

TEST(QmChatBubbleState, ResetDeletionFailureClearsDisplayMetadataButKeepsTheOwner)
{
	SText Text;
	CQmChatBubbleState State;
	State.m_TextContainerIndex.m_Index = 7;
	State.m_Initialized = true;
	str_copy(State.m_aCachedText, "old message");
	Text.m_FailDelete = true;
	EXPECT_THROW(State.Reset(Text), std::bad_alloc);
	EXPECT_FALSE(State.m_Initialized);
	EXPECT_STREQ(State.m_aCachedText, "");
	EXPECT_TRUE(State.HasTextResources());
	EXPECT_EQ(State.m_TextContainerIndex.m_Index, 7);
	Text.m_FailDelete = false;
	State.Reset(Text);
	EXPECT_FALSE(State.HasTextResources());
}

TEST(QmChatBubbleState, InitializationIsPublishedOnlyAfterAllSeedsSucceed)
{
	SText Text;
	CQmChatBubbleState State;
	EXPECT_THROW(State.Initialize(Text, [] { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_FALSE(State.m_Initialized);
	bool Seeded = false;
	State.Initialize(Text, [&] { Seeded = true; });
	EXPECT_TRUE(Seeded);
	EXPECT_TRUE(State.m_Initialized);
}

TEST(QmChatBubbleFrame, PreparationAllocationFailureReturnsFalse)
{
	CQmChatBubbleFrame Frame;
	EXPECT_FALSE(Frame.Run([] { throw std::bad_alloc(); }));
}

TEST(QmChatBubbleFrame, PreparationLengthFailureReturnsFalse)
{
	CQmChatBubbleFrame Frame;
	EXPECT_FALSE(Frame.Run([] { throw std::length_error("bubble"); }));
}

TEST(QmChatBubbleFrame, DrawingAllocationFailurePropagates)
{
	CQmChatBubbleFrame Frame;
	EXPECT_THROW(Frame.Run([&] {
		Frame.BeginDrawing();
		throw std::bad_alloc();
	}),
		std::bad_alloc);
}

TEST(QmChatBubbleFrame, UnexpectedPreparationExceptionPropagates)
{
	CQmChatBubbleFrame Frame;
	EXPECT_THROW(Frame.Run([] { throw 7; }), int);
}

TEST(QmChatBubbleFrame, ReusingTheFrameStartsBeforeTheDrawingBoundaryAgain)
{
	CQmChatBubbleFrame Frame;
	ASSERT_TRUE(Frame.Run([&] { Frame.BeginDrawing(); }));
	EXPECT_FALSE(Frame.Run([] { throw std::bad_alloc(); }));
}

TEST(QmChatBubbleFrame, AdmissionAndDrawingMarkerDoNotAllocate)
{
	bool Completed = false, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		CQmChatBubbleFrame Frame;
		Completed = Frame.Run([&] { Frame.BeginDrawing(); });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Completed);
	EXPECT_FALSE(Triggered);
}
