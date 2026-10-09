#include <game/client/QmUi/QmInputMotion.h>
#include <game/client/QmUi/QmLineInputMotion.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;

class CQmLineInputMotionTest : public ::testing::Test
{
protected:
	CQmLineInputMotion m_Motion;
	std::chrono::nanoseconds m_Now = 1s;
	std::string m_Text;
	int m_Level = 2;
	std::vector<STextCharOffset> m_vOffsets;

	void Update(const char *pText, bool AnimateGlyphs = true)
	{
		m_Text = pText;
		m_Motion.Update(pText, m_Now, m_Level, AnimateGlyphs);
		m_Motion.FillCharOffsets(m_vOffsets, 12.0f);
	}

	void Advance(std::chrono::nanoseconds Elapsed)
	{
		m_Now += Elapsed;
		m_Motion.Update(m_Text.c_str(), m_Now, m_Level);
		m_Motion.FillCharOffsets(m_vOffsets, 12.0f);
	}

	float OffsetAt(int ByteOffset) const
	{
		const auto It = std::find_if(m_vOffsets.begin(), m_vOffsets.end(), [ByteOffset](const auto &Offset) {
			return Offset.m_CharIndex == ByteOffset;
		});
		return It == m_vOffsets.end() ? 0.0f : It->m_YOffset;
	}
};

TEST_F(CQmLineInputMotionTest, ExistingTextDoesNotReplayWhenFocused)
{
	Update("already present");
	EXPECT_TRUE(m_vOffsets.empty());
}

TEST_F(CQmLineInputMotionTest, NewCharactersBounceOnceAndSettlePromptly)
{
	Update("");
	Update("a");
	EXPECT_GT(OffsetAt(0), 0.0f);
	Advance(120ms);
	EXPECT_LT(OffsetAt(0), 0.0f);
	for(int i = 0; i < 5; ++i)
		Advance(100ms);
	EXPECT_TRUE(m_vOffsets.empty());
}

TEST_F(CQmLineInputMotionTest, MixedUtf8CharactersUseTheirActualByteOffsets)
{
	Update("a");
	Update("a你🙂");
	ASSERT_EQ(m_vOffsets.size(), 3u);
	EXPECT_EQ(m_vOffsets[0].m_CharIndex, 0);
	EXPECT_EQ(m_vOffsets[1].m_CharIndex, 1);
	EXPECT_EQ(m_vOffsets[2].m_CharIndex, 4);
	EXPECT_FLOAT_EQ(OffsetAt(0), 0.0f);
	EXPECT_GT(OffsetAt(1), 0.0f);
	EXPECT_GT(OffsetAt(4), 0.0f);
}

TEST_F(CQmLineInputMotionTest, InsertingInTheMiddlePreservesNeighbouringMotion)
{
	Update("");
	Update("ab");
	Advance(20ms);
	const float A = OffsetAt(0);
	const float B = OffsetAt(1);
	Update("axb");
	EXPECT_FLOAT_EQ(OffsetAt(0), A);
	EXPECT_FLOAT_EQ(OffsetAt(2), B);
	EXPECT_GT(OffsetAt(1), A);
}

TEST_F(CQmLineInputMotionTest, NewlineDoesNotShiftTheFollowingUtf8Animation)
{
	Update("");
	Update("a\n你");
	ASSERT_EQ(m_vOffsets.size(), 3u);
	EXPECT_FLOAT_EQ(OffsetAt(1), 0.0f);
	EXPECT_GT(OffsetAt(2), 0.0f);
}

TEST_F(CQmLineInputMotionTest, CompositionCommitDropsTheReplacedLetters)
{
	Update("");
	Update("ni");
	Advance(20ms);
	Update("你");
	ASSERT_EQ(m_vOffsets.size(), 1u);
	EXPECT_EQ(m_vOffsets[0].m_CharIndex, 0);
	EXPECT_GT(m_vOffsets[0].m_YOffset, 0.0f);
}

TEST_F(CQmLineInputMotionTest, DeletingMiddleTextKeepsSuffixMotionWithoutReplayingIt)
{
	Update("");
	Update("abc");
	Advance(20ms);
	const float C = OffsetAt(2);
	Update("ac");
	ASSERT_EQ(m_vOffsets.size(), 2u);
	EXPECT_FLOAT_EQ(OffsetAt(1), C);
}

TEST_F(CQmLineInputMotionTest, UnchangedTextDoesNotRestartTheBounce)
{
	Update("");
	Update("a");
	Advance(50ms);
	const float Before = OffsetAt(0);
	Update("a");
	EXPECT_FLOAT_EQ(OffsetAt(0), Before);
}

TEST_F(CQmLineInputMotionTest, HiddenOrSelectedTextStopsGlyphMotion)
{
	Update("");
	Update("a");
	Update("a", false);
	EXPECT_TRUE(m_vOffsets.empty());
	Update("ab", false);
	EXPECT_TRUE(m_vOffsets.empty());
}

TEST_F(CQmLineInputMotionTest, TurningMotionOffImmediatelyAlignsTextAndCaret)
{
	Update("");
	m_Motion.ResolveCaret(vec2(10.0f, 0.0f), 12.0f);
	m_Motion.ResolveCaret(vec2(20.0f, 0.0f), 12.0f);
	Update("a");
	Advance(20ms);
	m_Level = 0;
	Update("a");
	EXPECT_TRUE(m_vOffsets.empty());
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(20.0f, 0.0f), 12.0f).x, 20.0f);
}

TEST_F(CQmLineInputMotionTest, ReducedMotionUsesASmallerLetterBounce)
{
	Update("");
	Update("a");
	const float Full = OffsetAt(0);
	m_Motion.Reset();
	m_Level = 1;
	Update("");
	Update("a");
	EXPECT_GT(OffsetAt(0), 0.0f);
	EXPECT_LT(OffsetAt(0), Full * 0.5f);
}

TEST_F(CQmLineInputMotionTest, ResetPreventsMotionLeakingIntoTheNextFocus)
{
	Update("");
	Update("a");
	m_Motion.ResolveCaret(vec2(10.0f, 0.0f), 12.0f);
	m_Motion.ResolveCaret(vec2(20.0f, 0.0f), 12.0f);
	m_Motion.Reset();
	Update("another field");
	EXPECT_TRUE(m_vOffsets.empty());
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(200.0f, 0.0f), 12.0f).x, 200.0f);
}

TEST_F(CQmLineInputMotionTest, LargeInsertKeepsTheAnimatedGlyphBudgetBounded)
{
	Update("");
	const std::string Text(200, 'a');
	Update(Text.c_str());
	EXPECT_EQ(std::count_if(m_vOffsets.begin(), m_vOffsets.end(), [](const auto &Offset) { return Offset.m_YOffset != 0.0f; }),
		CQmLineInputMotion::MAX_ANIMATED_GLYPHS);
	for(int i = 0; i < 7; ++i)
		Advance(100ms);
	Update("");
	Update("reused");
	EXPECT_GT(OffsetAt(0), 0.0f);
}

TEST_F(CQmLineInputMotionTest, CaretFollowsTypingAndRetargetsWithoutJumping)
{
	Update("");
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(10.0f, 0.0f), 12.0f).x, 10.0f);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(34.0f, 0.0f), 12.0f).x, 10.0f);
	Advance(40ms);
	const float Before = m_Motion.ResolveCaret(vec2(34.0f, 0.0f), 12.0f).x;
	EXPECT_GT(Before, 10.0f);
	EXPECT_LT(Before, 34.0f);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(2.0f, 0.0f), 12.0f).x, Before);
	Advance(1ms);
	EXPECT_LT(m_Motion.ResolveCaret(vec2(2.0f, 0.0f), 12.0f).x, Before);
	for(int i = 0; i < 8; ++i)
		Advance(100ms);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(2.0f, 0.0f), 12.0f).x, 2.0f);
}

TEST_F(CQmLineInputMotionTest, CaretSnapsAcrossLinesAndDuringMouseSelection)
{
	Update("");
	m_Motion.ResolveCaret(vec2(10.0f, 0.0f), 12.0f);
	const vec2 NextLine = m_Motion.ResolveCaret(vec2(2.0f, 16.0f), 12.0f);
	EXPECT_FLOAT_EQ(NextLine.x, 2.0f);
	EXPECT_FLOAT_EQ(NextLine.y, 16.0f);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(8.0f, 16.0f), 12.0f, true).x, 8.0f);
}

TEST_F(CQmLineInputMotionTest, LongRenderGapDropsExpiredAnimation)
{
	Update("");
	Update("a");
	Advance(1s);
	EXPECT_TRUE(m_vOffsets.empty());
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(20.0f, 0.0f), 12.0f).x, 20.0f);
}

TEST_F(CQmLineInputMotionTest, ClockReversalStartsFromTheCurrentText)
{
	Update("");
	Update("a");
	m_Now -= 1ms;
	Update("a");
	EXPECT_TRUE(m_vOffsets.empty());
}

TEST_F(CQmLineInputMotionTest, CaretImmediatelyAlignsAfterFontScaleChanges)
{
	Update("");
	m_Motion.ResolveCaret(vec2(10.0f, 0.0f), 12.0f);
	m_Motion.ResolveCaret(vec2(20.0f, 0.0f), 12.0f);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(30.0f, 0.0f), 18.0f).x, 30.0f);
}

TEST(QmInputMotion, PopupSpringsRespondQuicklyAndHaveOneSmallOvershoot)
{
	for(const auto &Spring : {qm_input_motion::FOLLOW, qm_input_motion::RESIZE, qm_input_motion::SELECTED})
	{
		SCOPED_TRACE(Spring.m_Stiffness);
		CQmAnimationBackend Runtime;
		Runtime.SetValue(1, EUiAnimProperty::WIDTH, 0.0f);
		qm_input_motion::ResolvePresentationValue(Runtime, 1, EUiAnimProperty::WIDTH, 100.0f, Spring, 2, 3);
		float Peak = 0.0f;
		for(int i = 0; i < 96; ++i)
		{
			Runtime.Advance(1.0f / 120.0f);
			const float Value = qm_input_motion::ResolvePresentationValue(Runtime, 1, EUiAnimProperty::WIDTH, 100.0f, Spring, 2, 3);
			Peak = std::max(Peak, Value);
			if(i == 11)
				EXPECT_GT(Value, 90.0f);
		}
		EXPECT_GT(Peak, 102.0f);
		EXPECT_LT(Peak, 112.0f);
		EXPECT_FLOAT_EQ(Runtime.GetValue(1, EUiAnimProperty::WIDTH), 100.0f);
		EXPECT_FALSE(Runtime.HasActiveAnimation(1, EUiAnimProperty::WIDTH));
	}
}

TEST(QmInputMotion, DisablingMotionFinishesAnUnchangedPopupTarget)
{
	CQmAnimationBackend Runtime;
	Runtime.SetValue(1, EUiAnimProperty::WIDTH, 100.0f);
	qm_input_motion::ResolvePresentationValue(Runtime, 1, EUiAnimProperty::WIDTH, 200.0f, qm_input_motion::RESIZE, 2, 3);
	Runtime.Advance(0.05f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(1, EUiAnimProperty::WIDTH));
	EXPECT_FLOAT_EQ(qm_input_motion::ResolvePresentationValue(Runtime, 1, EUiAnimProperty::WIDTH, 200.0f, qm_input_motion::RESIZE, 0, 3), 200.0f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(1, EUiAnimProperty::WIDTH));
}

TEST(QmInputMotion, ReducedPopupMotionHasLessOvershoot)
{
	float aPeaks[2] = {};
	for(int Level = 1; Level <= 2; ++Level)
	{
		CQmAnimationBackend Runtime;
		Runtime.SetValue(1, EUiAnimProperty::WIDTH, 0.0f);
		qm_input_motion::ResolvePresentationValue(Runtime, 1, EUiAnimProperty::WIDTH, 100.0f, qm_input_motion::RESIZE, Level, 3);
		for(int i = 0; i < 96; ++i)
		{
			Runtime.Advance(1.0f / 120.0f);
			aPeaks[Level - 1] = std::max(aPeaks[Level - 1], Runtime.GetValue(1, EUiAnimProperty::WIDTH));
		}
	}
	EXPECT_GT(aPeaks[1] - 100.0f, 2.0f);
	EXPECT_LT(aPeaks[0] - 100.0f, (aPeaks[1] - 100.0f) * 0.5f);
}

TEST_F(CQmLineInputMotionTest, CaretMovesMonotonicallyAndFinishesWithinEightyMilliseconds)
{
	Update("");
	m_Motion.ResolveCaret(vec2(0.0f, 0.0f), 12.0f);
	m_Motion.ResolveCaret(vec2(24.0f, 0.0f), 12.0f);
	float Previous = 0.0f;
	for(int i = 0; i < 8; ++i)
	{
		Advance(10ms);
		const float Current = m_Motion.ResolveCaret(vec2(24.0f, 0.0f), 12.0f).x;
		EXPECT_GE(Current, Previous);
		EXPECT_LE(Current, 24.0f);
		Previous = Current;
	}
	EXPECT_NEAR(Previous, 24.0f, 0.001f);
	Advance(100ms);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(24.0f, 0.0f), 12.0f).x, 24.0f);
}

TEST_F(CQmLineInputMotionTest, RapidRetargetsStayBetweenCurrentPositionAndNewTarget)
{
	Update("");
	m_Motion.ResolveCaret(vec2(0.0f, 0.0f), 12.0f);
	for(const float Target : {24.0f, 4.0f, 30.0f, 2.0f})
	{
		const float Start = m_Motion.ResolveCaret(vec2(Target, 0.0f), 12.0f).x;
		for(int i = 0; i < 3; ++i)
		{
			Advance(5ms);
			const float Current = m_Motion.ResolveCaret(vec2(Target, 0.0f), 12.0f).x;
			EXPECT_GE(Current, std::min(Start, Target));
			EXPECT_LE(Current, std::max(Start, Target));
		}
	}
	Advance(80ms);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(2.0f, 0.0f), 12.0f).x, 2.0f);
}

TEST_F(CQmLineInputMotionTest, ReducedMotionRetargetDoesNotRestoreSpringOvershoot)
{
	Update("");
	m_Motion.ResolveCaret(vec2(0.0f, 0.0f), 12.0f);
	m_Motion.ResolveCaret(vec2(24.0f, 0.0f), 12.0f);
	Advance(20ms);
	m_Level = 1;
	Update("");
	const float Start = m_Motion.ResolveCaret(vec2(2.0f, 0.0f), 12.0f).x;
	Advance(10ms);
	const float Current = m_Motion.ResolveCaret(vec2(2.0f, 0.0f), 12.0f).x;
	EXPECT_LT(Current, Start);
	EXPECT_GE(Current, 2.0f);
	Advance(40ms);
	EXPECT_FLOAT_EQ(m_Motion.ResolveCaret(vec2(2.0f, 0.0f), 12.0f).x, 2.0f);
}
