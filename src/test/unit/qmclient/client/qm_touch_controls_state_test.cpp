#include <game/client/components/touch_controls.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <vector>

namespace
{
	class CTestTouchBehavior : public CTouchControls::CTouchButtonBehavior
	{
	public:
		int m_Deactivations = 0;
		std::optional<bool> m_LastByFinger;

		CTouchControls::CButtonLabel GetLabel() const override { return {CTouchControls::CButtonLabel::EType::PLAIN, ""}; }
		void WriteToConfiguration(CJsonWriter *pWriter) override {}
		const char *GetBehaviorType() const override { return "test"; }
		void OnDeactivate(bool ByFinger) override
		{
			++m_Deactivations;
			m_LastByFinger = ByFinger;
		}
	};

	IInput::CTouchFingerState PressAt(int Time)
	{
		IInput::CTouchFingerState State{};
		State.m_PressTime = std::chrono::nanoseconds(Time);
		return State;
	}
}

TEST(QmTouchControlsState, NewBehaviorStartsInactive)
{
	CTestTouchBehavior Behavior;
	EXPECT_FALSE(Behavior.IsActive());
	Behavior.Reset();
	EXPECT_EQ(Behavior.m_Deactivations, 0);
}

TEST(QmTouchControlsState, ResetReleasesHeldBehaviorOnceWithoutAFingerRelease)
{
	CTestTouchBehavior Behavior;
	Behavior.m_Active = true;
	Behavior.Reset();
	Behavior.Reset();

	EXPECT_FALSE(Behavior.IsActive());
	EXPECT_EQ(Behavior.m_Deactivations, 1);
	EXPECT_EQ(Behavior.m_LastByFinger, false);
}

TEST(QmTouchControlsState, HoldingTwoPressesDoesNotGrowLongPressExclusions)
{
	std::vector<IInput::CTouchFingerState> vExcluded;
	const std::array aPresses{PressAt(1), PressAt(2)};
	for(int Frame = 0; Frame < 200; ++Frame)
	{
		ASSERT_TRUE(qm_touch_controls::UpdateLongPressExclusions(vExcluded, aPresses));
	}

	ASSERT_EQ(vExcluded.size(), 1u);
	EXPECT_TRUE(qm_touch_controls::SamePress(vExcluded.front(), aPresses.back()));
}

TEST(QmTouchControlsState, SecondaryPressStaysExcludedUntilItsFingerIsReused)
{
	std::vector<IInput::CTouchFingerState> vExcluded;
	const std::array aPresses{PressAt(1), PressAt(2)};
	ASSERT_TRUE(qm_touch_controls::UpdateLongPressExclusions(vExcluded, aPresses));

	EXPECT_FALSE(qm_touch_controls::UpdateLongPressExclusions(vExcluded, std::span(aPresses).last(1)));
	const std::array aNewPress{PressAt(3)};
	ASSERT_EQ(aNewPress.front().m_Finger, aPresses.back().m_Finger);
	EXPECT_TRUE(qm_touch_controls::UpdateLongPressExclusions(vExcluded, aNewPress));
	EXPECT_TRUE(vExcluded.empty());
}
