#include <game/client/components/emoticon.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmLocalBlinkDeadline, StartsInactive)
{
	SQmLocalBlinkState Blink;
	EXPECT_FALSE(Blink.IsActive(0));
	EXPECT_FALSE(Blink.IsActive(100));
}

TEST(QmLocalBlinkDeadline, EndsAtExclusiveFourthTick)
{
	SQmLocalBlinkState Blink;
	Blink.Trigger(100);
	EXPECT_TRUE(Blink.IsActive(100));
	EXPECT_TRUE(Blink.IsActive(103));
	EXPECT_FALSE(Blink.IsActive(104));
}

TEST(QmLocalBlinkDeadline, RetriggerExtendsDisplay)
{
	SQmLocalBlinkState Blink;
	Blink.Trigger(100);
	Blink.Trigger(103);
	EXPECT_TRUE(Blink.IsActive(106));
	EXPECT_FALSE(Blink.IsActive(107));
}

TEST(QmLocalBlinkDeadline, ResetStopsDisplayAndAllowsNextTrigger)
{
	SQmLocalBlinkState Blink;
	Blink.Trigger(100);
	Blink.Reset();
	EXPECT_FALSE(Blink.IsActive(101));
	Blink.Trigger(102);
	EXPECT_TRUE(Blink.IsActive(105));
	EXPECT_FALSE(Blink.IsActive(106));
}

TEST(QmLocalBlinkDeadline, NegativeCurrentTickNeverDisplays)
{
	SQmLocalBlinkState Blink;
	Blink.Trigger(100);
	EXPECT_FALSE(Blink.IsActive(-1));
}

TEST(QmLocalBlinkDeadline, DeadlineBeyondIntMaximumKeepsDisplayActive)
{
	SQmLocalBlinkState Blink;
	constexpr int LastTick = std::numeric_limits<int>::max();
	Blink.Trigger(LastTick - 2);
	EXPECT_TRUE(Blink.IsActive(LastTick - 2));
	EXPECT_TRUE(Blink.IsActive(LastTick));
}

TEST(QmLocalBlinkDeadline, TriggerAtLastIntTickCanBeReset)
{
	SQmLocalBlinkState Blink;
	constexpr int LastTick = std::numeric_limits<int>::max();
	Blink.Trigger(LastTick);
	EXPECT_TRUE(Blink.IsActive(LastTick));
	Blink.Reset();
	EXPECT_FALSE(Blink.IsActive(LastTick));
}

TEST(QmSuperHeadDeadline, RetainsTwoSecondDuration)
{
	EXPECT_EQ(QmEmoticon::SuperHeadExpireTick(100, 50), int64_t{200});
}

TEST(QmSuperHeadDeadline, DeadlineBeyondIntMaximumRemainsRepresentable)
{
	constexpr int LastTick = std::numeric_limits<int>::max();
	EXPECT_EQ(QmEmoticon::SuperHeadExpireTick(LastTick, 50), int64_t{2147483747});
	EXPECT_EQ(QmEmoticon::SuperHeadExpireTick(LastTick - 99, 50), int64_t{2147483648});
}
