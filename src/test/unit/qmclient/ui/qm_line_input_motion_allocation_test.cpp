#include <game/client/QmUi/QmLineInputMotion.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{
	struct SFailureResult
	{
		bool m_Triggered;
		bool m_Threw;
	};

	template<typename TOperation>
	SFailureResult Inject(size_t BeforeFailure, TOperation &&Operation)
	{
		CPredictionAllocationFailure Fault(BeforeFailure);
		bool Threw = false;
		try
		{
			std::forward<TOperation>(Operation)();
		}
		catch(...)
		{
			Threw = true;
		}
		return {Fault.Triggered(), Threw};
	}
}

TEST(QmLineInputMotionAllocation, FirstUseFailureLeavesPlainTextAndAllowsFreshAnimation)
{
	const std::string Text(128, 'a');
	for(size_t BeforeFailure : {0u, 1u, 2u, 3u})
	{
		SCOPED_TRACE(BeforeFailure);
		CQmLineInputMotion Motion;
		const auto Failure = Inject(BeforeFailure, [&] { Motion.Update(Text.c_str(), 1s, 2); });
		EXPECT_TRUE(Failure.m_Triggered);
		EXPECT_FALSE(Failure.m_Threw);
		std::vector<STextCharOffset> vOffsets{{99, 0, 0, 0.5f}};
		Motion.FillCharOffsets(vOffsets);
		EXPECT_TRUE(vOffsets.empty());
		Motion.Update(Text.c_str(), 1s + 1ms, 2);
		Motion.FillCharOffsets(vOffsets);
		EXPECT_TRUE(vOffsets.empty());
		Motion.Update((Text + 'b').c_str(), 1s + 2ms, 2);
		Motion.FillCharOffsets(vOffsets);
		ASSERT_EQ(vOffsets.size(), 129u);
		EXPECT_EQ(vOffsets.back().m_CharIndex, 128);
		EXPECT_LT(vOffsets.back().m_Scale, 1.0f);
		EXPECT_EQ(std::count_if(vOffsets.begin(), vOffsets.end(), [](const auto &Offset) { return Offset.m_Scale != 1.0f; }), 1);
	}
}

TEST(QmLineInputMotionAllocation, UpdateFailuresNeverLeaveNewGlyphsUnderOldTextIdentity)
{
	const std::string NewText = "x" + std::string(60, 'a') + "你🙂";
	bool Completed = false;
	size_t Failures = 0;
	for(size_t BeforeFailure = 0; BeforeFailure < 512; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CQmLineInputMotion Motion;
		Motion.Update("x", 1s, 2);
		const auto Failure = Inject(BeforeFailure, [&] { Motion.Update(NewText.c_str(), 1s + 1ms, 2); });
		EXPECT_FALSE(Failure.m_Threw);
		std::vector<STextCharOffset> vOffsets;
		Motion.FillCharOffsets(vOffsets);
		if(!Failure.m_Triggered)
		{
			ASSERT_EQ(vOffsets.size(), 63u);
			EXPECT_EQ(vOffsets.back().m_CharIndex, 64);
			EXPECT_EQ(std::count_if(vOffsets.begin(), vOffsets.end(), [](const auto &Offset) { return Offset.m_Scale != 1.0f; }), CQmLineInputMotion::MAX_ANIMATED_GLYPHS);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_TRUE(vOffsets.empty());
		// 回到原字符串也必须重新建立缓存，不能命中失败前留下的文字身份。
		Motion.Update("x", 1s + 2ms, 2);
		Motion.FillCharOffsets(vOffsets);
		EXPECT_TRUE(vOffsets.empty());
		Motion.Update("x你🙂", 1s + 3ms, 2);
		Motion.FillCharOffsets(vOffsets);
		ASSERT_EQ(vOffsets.size(), 3u);
		EXPECT_EQ(vOffsets[0].m_CharIndex, 0);
		EXPECT_EQ(vOffsets[1].m_CharIndex, 1);
		EXPECT_EQ(vOffsets[2].m_CharIndex, 4);
		EXPECT_FLOAT_EQ(vOffsets[0].m_Scale, 1.0f);
		EXPECT_LT(vOffsets[1].m_Scale, 1.0f);
		EXPECT_LT(vOffsets[2].m_Scale, 1.0f);
	}
	EXPECT_TRUE(Completed);
	EXPECT_GT(Failures, 4u);
}

TEST(QmLineInputMotionAllocation, OffsetFailureClearsOutputWithoutDiscardingCurrentAnimation)
{
	CQmLineInputMotion Motion;
	Motion.Update("a", 1s, 2);
	Motion.Update("a你🙂", 1s + 1ms, 2);
	std::vector<STextCharOffset> vOffsets{{99, 0, 0, 0.5f}};
	const auto Failure = Inject(0, [&] { Motion.FillCharOffsets(vOffsets); });
	EXPECT_TRUE(Failure.m_Triggered);
	EXPECT_FALSE(Failure.m_Threw);
	EXPECT_TRUE(vOffsets.empty());
	Motion.FillCharOffsets(vOffsets);
	ASSERT_EQ(vOffsets.size(), 3u);
	EXPECT_EQ(vOffsets[1].m_CharIndex, 1);
	EXPECT_EQ(vOffsets[2].m_CharIndex, 4);
	EXPECT_FLOAT_EQ(vOffsets[0].m_Scale, 1.0f);
	EXPECT_LT(vOffsets[1].m_Scale, 1.0f);
	EXPECT_LT(vOffsets[2].m_Scale, 1.0f);
}

TEST(QmLineInputMotionAllocation, FirstCaretAllocationFailuresAlignToCurrentLayoutAndRecover)
{
	const vec2 Target(10, 20);
	bool Completed = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 64; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CQmLineInputMotion Motion;
		vec2 Result(-1, -1);
		const auto Failure = Inject(BeforeFailure, [&] { Result = Motion.ResolveCaret(Target, 12); });
		EXPECT_FALSE(Failure.m_Threw);
		EXPECT_EQ(Result, Target);
		if(!Failure.m_Triggered)
		{
			Completed = true;
			break;
		}
		Motion.Update("", 1s, 2);
		EXPECT_EQ(Motion.ResolveCaret(Target, 12), Target);
		EXPECT_EQ(Motion.ResolveCaret(Target + vec2(10, 0), 12), Target);
	}
	EXPECT_TRUE(Completed);
}

TEST(QmLineInputMotionAllocation, FailedCaretRetargetClearsPartialMotionAndKeepsExactTarget)
{
	const vec2 Start(10, 20), Target(110, 70);
	bool Completed = false;
	size_t Failures = 0;
	for(size_t BeforeFailure = 0; BeforeFailure < 128; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CQmLineInputMotion Motion;
		Motion.Update("a", 1s, 2);
		Motion.Update("ab", 1s + 1ms, 2);
		ASSERT_EQ(Motion.ResolveCaret(Start, 12), Start);
		vec2 Result(-1, -1);
		const auto Failure = Inject(BeforeFailure, [&] { Result = Motion.ResolveCaret(Target, 12, false, true); });
		EXPECT_FALSE(Failure.m_Threw);
		if(!Failure.m_Triggered)
		{
			EXPECT_EQ(Result, Start);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_EQ(Result, Target);
		std::vector<STextCharOffset> vOffsets;
		Motion.FillCharOffsets(vOffsets);
		EXPECT_TRUE(vOffsets.empty());
		Motion.Update("ab", 1s + 2ms, 2);
		Motion.FillCharOffsets(vOffsets);
		EXPECT_TRUE(vOffsets.empty());
		EXPECT_EQ(Motion.ResolveCaret(Target, 12), Target);
		EXPECT_EQ(Motion.ResolveCaret(Target + vec2(10, 0), 12), Target);
	}
	EXPECT_TRUE(Completed);
	EXPECT_GT(Failures, 0u);
}
