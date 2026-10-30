#ifndef TEST_SUPPORT_QM_ANIMATION_LIFECYCLE_FIXTURE_H
#define TEST_SUPPORT_QM_ANIMATION_LIFECYCLE_FIXTURE_H

#include <game/client/QmUi/QmAnimCurves.h>
#include <game/client/QmUi/QmAnimResolve.h>

#include <gtest/gtest.h>
#include <test/support/qm_anim_test_helpers.h>

class CQmAnimationLifecycle : public ::testing::Test
{
protected:
	CScopedQmFullMotion m_FullMotion;
	CUiV2AnimationRuntime m_Runtime;
	static constexpr uint64_t NODE = 0x4c494645;
	static constexpr EUiAnimProperty PROPERTY = EUiAnimProperty::POS_X;

	static SUiAnimRequest Request(float Target, uint32_t Id, EUiAnimInterruptPolicy Policy = EUiAnimInterruptPolicy::MERGE_TARGET)
	{
		SUiAnimRequest Result;
		Result.m_NodeKey = NODE;
		Result.m_Property = PROPERTY;
		Result.m_Target = Target;
		Result.m_TrackId = Id;
		Result.m_Transition.m_DurationSec = 1.0f;
		Result.m_Transition.m_Easing = EEasing::LINEAR;
		Result.m_Transition.m_Priority = 2;
		Result.m_Transition.m_Interrupt = Policy;
		return Result;
	}
	void AdvanceToRest()
	{
		for(int Frame = 0; Frame < 600 && m_Runtime.ActiveTrackCount() > 0; ++Frame)
			m_Runtime.Advance(1.0f / 60.0f);
		ASSERT_EQ(m_Runtime.ActiveTrackCount(), 0);
	}
};

#endif
