#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsIconFeedback.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	struct SFeedbackResult
	{
		float m_Scale;
		bool m_Threw;
		bool m_Triggered;
		size_t m_Attempts;
	};

	SFeedbackResult ResolveWithFailure(CQmAnimationBackend &Anim, size_t BeforeFailure, bool Hovered, bool Pressed, bool Enabled)
	{
		CPredictionAllocationFailure Failure(BeforeFailure);
		float Scale = 0.0f;
		bool Threw = false;
		try
		{
			Scale = ResolveSettingsIconScale(Anim, 1, Hovered, Pressed, Enabled);
		}
		catch(...)
		{
			Threw = true;
		}
		return {Scale, Threw, Failure.Triggered(), Failure.Attempts()};
	}

	class CSettingsIconFeedbackAllocationTest : public ::testing::Test
	{
		int m_PreviousMotionLevel;

	protected:
		void SetUp() override
		{
			m_PreviousMotionLevel = g_Config.m_QmUiMotionLevel;
			g_Config.m_QmUiMotionLevel = 2;
		}
		void TearDown() override { g_Config.m_QmUiMotionLevel = m_PreviousMotionLevel; }
	};
}

TEST_F(CSettingsIconFeedbackAllocationTest, FirstHoverFailureUsesBaseSizeAndLatestPointerState)
{
	bool ReachedSuccess = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 64; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CQmAnimationBackend Anim;
		const auto Result = ResolveWithFailure(Anim, BeforeFailure, true, false, true);
		ASSERT_FALSE(Result.m_Threw);
		if(!Result.m_Triggered)
		{
			EXPECT_GT(Result.m_Scale, 1.0f);
			EXPECT_TRUE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
			ReachedSuccess = true;
			break;
		}
		EXPECT_FLOAT_EQ(Result.m_Scale, 1.0f);
		EXPECT_EQ(Result.m_Attempts, BeforeFailure + 1);
		EXPECT_FLOAT_EQ(Anim.GetValue(1, EUiAnimProperty::SCALE, 1.0f), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, false, false, true), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		EXPECT_GT(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
		EXPECT_TRUE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		Anim.Advance(0.25f);
		EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST_F(CSettingsIconFeedbackAllocationTest, PressFailureRetriesSameInputAndKeepsIndependentAnimation)
{
	bool ReachedSuccess = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 64; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CQmAnimationBackend Anim;
		ASSERT_GT(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
		Anim.SetValue(99, EUiAnimProperty::POS_X, 5.0f);
		SUiAnimRequest Independent;
		Independent.m_NodeKey = 99;
		Independent.m_Property = EUiAnimProperty::POS_X;
		Independent.m_Target = 10.0f;
		Independent.m_Transition.m_DurationSec = 5.0f;
		Independent.m_Transition.m_RespectMotionLevel = false;
		ASSERT_TRUE(Anim.RequestAnimation(Independent));
		const auto Result = ResolveWithFailure(Anim, BeforeFailure, true, true, true);
		ASSERT_FALSE(Result.m_Threw);
		EXPECT_FLOAT_EQ(Anim.GetValue(99, EUiAnimProperty::POS_X), 5.0f);
		EXPECT_TRUE(Anim.HasActiveAnimation(99, EUiAnimProperty::POS_X));
		if(!Result.m_Triggered)
		{
			EXPECT_LT(Result.m_Scale, 1.0f);
			EXPECT_TRUE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
			ReachedSuccess = true;
			break;
		}
		EXPECT_FLOAT_EQ(Result.m_Scale, 1.0f);
		EXPECT_EQ(Result.m_Attempts, BeforeFailure + 1);
		EXPECT_FLOAT_EQ(Anim.GetValue(1, EUiAnimProperty::SCALE), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		EXPECT_LT(ResolveSettingsIconScale(Anim, 1, true, true, true), 1.0f);
		EXPECT_TRUE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		Anim.Advance(0.25f);
		EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, true, true), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST_F(CSettingsIconFeedbackAllocationTest, DisabledPreparationFailureKeepsBaseSizeAndCanEnableLater)
{
	bool ReachedSuccess = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 64; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CQmAnimationBackend Anim;
		const auto Result = ResolveWithFailure(Anim, BeforeFailure, true, true, false);
		ASSERT_FALSE(Result.m_Threw);
		EXPECT_FLOAT_EQ(Result.m_Scale, 1.0f);
		EXPECT_FLOAT_EQ(Anim.GetValue(1, EUiAnimProperty::SCALE, 1.0f), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		if(Result.m_Triggered)
			EXPECT_EQ(Result.m_Attempts, BeforeFailure + 1);
		EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, true, false), 1.0f);
		EXPECT_GT(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
		EXPECT_TRUE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		if(!Result.m_Triggered)
		{
			ReachedSuccess = true;
			break;
		}
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST_F(CSettingsIconFeedbackAllocationTest, MotionOffCompletionFailureKeepsBaseSizeAndCanRetry)
{
	g_Config.m_QmUiMotionLevel = 0;
	CQmAnimationBackend Anim;
	ASSERT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
	bool ReachedFailure = false;
	// 复用已有属性，直到正常完成队列需要扩容；不绑定某个 STL 的块大小。
	for(int Pulse = 0; Pulse < 1024; ++Pulse)
	{
		SCOPED_TRACE(Pulse);
		ASSERT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, false, false, true), 1.0f);
		const auto Result = ResolveWithFailure(Anim, 0, true, false, true);
		ASSERT_FALSE(Result.m_Threw);
		EXPECT_FLOAT_EQ(Result.m_Scale, 1.0f);
		EXPECT_FLOAT_EQ(Anim.GetValue(1, EUiAnimProperty::SCALE), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		if(!Result.m_Triggered)
			continue;
		EXPECT_EQ(Result.m_Attempts, 1u);
		EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
		const auto Held = ResolveWithFailure(Anim, 0, true, false, true);
		EXPECT_FALSE(Held.m_Threw);
		EXPECT_FALSE(Held.m_Triggered);
		EXPECT_EQ(Held.m_Attempts, 0u);
		EXPECT_FLOAT_EQ(Held.m_Scale, 1.0f);
		EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, true, true), 1.0f);
		EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
		ReachedFailure = true;
		break;
	}
	EXPECT_TRUE(ReachedFailure);
}
