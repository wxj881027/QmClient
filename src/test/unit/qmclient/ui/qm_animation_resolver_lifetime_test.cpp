#include <test/support/qm_animation_lifecycle_fixture.h>

using QmAnimationResolverLifetime = CQmAnimationLifecycle;

TEST_F(QmAnimationResolverLifetime, SeparateRuntimesInitializeTheirOwnValueForTheSameNode)
{
	CUiV2AnimationRuntime Other;
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 50.0f, ui_spring::SNAPPY), 50.0f);
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(Other, NODE, PROPERTY, 50.0f, ui_spring::SNAPPY), 50.0f);
	EXPECT_TRUE(Other.HasValue(NODE, PROPERTY));
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(Other, NODE, PROPERTY, 80.0f, ui_spring::SNAPPY), 50.0f);
	EXPECT_TRUE(Other.HasActiveAnimation(NODE, PROPERTY));
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 50.0f);
}

TEST_F(QmAnimationResolverLifetime, ResetRemovesThePresentationTargetWithTheRuntimeValues)
{
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 50.0f, ui_spring::SNAPPY);
	m_Runtime.Reset();
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 50.0f, ui_spring::SNAPPY), 50.0f);
	EXPECT_TRUE(m_Runtime.HasValue(NODE, PROPERTY));
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 80.0f, ui_spring::SNAPPY), 50.0f);
	EXPECT_TRUE(m_Runtime.HasActiveAnimation(NODE, PROPERTY));
}

TEST_F(QmAnimationResolverLifetime, DestroyedRuntimeCannotSuppressTheNextInstancesInitialValue)
{
	{
		CUiV2AnimationRuntime Previous;
		ResolveUiPresentationStateValue(Previous, NODE, PROPERTY, 50.0f, ui_spring::SNAPPY);
	}
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 50.0f, ui_spring::SNAPPY);
	EXPECT_TRUE(m_Runtime.HasValue(NODE, PROPERTY));
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 80.0f, ui_spring::SNAPPY), 50.0f);
}

TEST_F(QmAnimationResolverLifetime, TargetChangesInsideThePresentationThresholdDoNotRetarget)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.0f, ui_spring::SNAPPY, 2, 0.1f);
	m_Runtime.Advance(0.02f);
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.05f, ui_spring::SNAPPY, 2, 0.1f);
	ASSERT_NO_FATAL_FAILURE(AdvanceToRest());
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 10.0f);
}

TEST_F(QmAnimationResolverLifetime, TargetChangesOutsideThePresentationThresholdRetarget)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.0f, ui_spring::SNAPPY, 2, 0.1f);
	m_Runtime.Advance(0.02f);
	const float Before = m_Runtime.GetValue(NODE, PROPERTY);
	EXPECT_FLOAT_EQ(ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.2f, ui_spring::SNAPPY, 2, 0.1f), Before);
	ASSERT_NO_FATAL_FAILURE(AdvanceToRest());
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 10.2f);
}

TEST_F(QmAnimationResolverLifetime, PropertiesOfTheSameNodeKeepIndependentPresentationTargets)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	m_Runtime.SetValue(NODE, EUiAnimProperty::POS_Y, 0.0f);
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 10.0f, ui_spring::SNAPPY);
	ResolveUiPresentationStateValue(m_Runtime, NODE, EUiAnimProperty::POS_Y, 20.0f, ui_spring::SNAPPY);
	ASSERT_NO_FATAL_FAILURE(AdvanceToRest());
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 10.0f);
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, EUiAnimProperty::POS_Y), 20.0f);
}

TEST_F(QmAnimationResolverLifetime, RefusedLowPriorityTargetRetriesAfterTheOwnerCompletes)
{
	m_Runtime.SetValue(NODE, PROPERTY, 0.0f);
	auto Owner = Request(10.0f, 71);
	Owner.m_Transition.m_Driver = EUiAnimDriver::SPRING;
	Owner.m_Transition.m_Spring = ui_spring::SNAPPY;
	Owner.m_Transition.m_Priority = 5;
	ASSERT_TRUE(m_Runtime.RequestAnimation(Owner));
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 20.0f, ui_spring::SNAPPY, 2);
	ASSERT_NO_FATAL_FAILURE(AdvanceToRest());
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 10.0f);
	ResolveUiPresentationStateValue(m_Runtime, NODE, PROPERTY, 20.0f, ui_spring::SNAPPY, 2);
	ASSERT_NO_FATAL_FAILURE(AdvanceToRest());
	EXPECT_FLOAT_EQ(m_Runtime.GetValue(NODE, PROPERTY), 20.0f);
}
