#include <base/color.h>

#include <game/client/components/qmclient/hud_notifications/hud_notifications.h>

#include <gtest/gtest.h>

TEST(QmHudNotifications, BuildsEchoPresentationFromQueuedMessage)
{
	constexpr unsigned FallbackEchoColor = 0x445566;
	const auto PlainEcho = QmHudNotifications::BuildEchoNotificationPayload("Regular echo", FallbackEchoColor);
	EXPECT_STREQ(PlainEcho.m_aText, "Regular echo");
	EXPECT_EQ(PlainEcho.m_Color, FallbackEchoColor);

	const auto ColoredEcho = QmHudNotifications::BuildEchoNotificationPayload("[[$FF7F7F]]禅模式: 开启", FallbackEchoColor);
	const unsigned ExpectedColor = color_cast<ColorHSLA>(ColorRGBA(1.0f, 127.0f / 255.0f, 127.0f / 255.0f, 1.0f)).Pack(false);
	EXPECT_STREQ(ColoredEcho.m_aText, "禅模式: 开启");
	EXPECT_EQ(ColoredEcho.m_Color, ExpectedColor);

	const auto EmptyEcho = QmHudNotifications::BuildEchoNotificationPayload(nullptr, FallbackEchoColor);
	EXPECT_STREQ(EmptyEcho.m_aText, "");
	EXPECT_EQ(EmptyEcho.m_Color, FallbackEchoColor);
}

TEST(QmHudNotifications, ClampsVisibleCount)
{
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(-1), 1);
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(0), 1);
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(3), 3);
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(20), 8);
}

TEST(QmHudNotifications, ClampsTiming)
{
	EXPECT_EQ(QmHudNotifications::ClampHoldMs(200), 500);
	EXPECT_EQ(QmHudNotifications::ClampHoldMs(2500), 2500);
	EXPECT_EQ(QmHudNotifications::ClampHoldMs(30000), 10000);
	EXPECT_EQ(QmHudNotifications::ClampAnimationMs(0), 0);
	EXPECT_EQ(QmHudNotifications::ClampAnimationMs(9000), 2000);
}

TEST(QmHudNotifications, ClampsTextSize)
{
	EXPECT_EQ(QmHudNotifications::ClampTextSize(0), 1);
	EXPECT_EQ(QmHudNotifications::ClampTextSize(8), 8);
	EXPECT_EQ(QmHudNotifications::ClampTextSize(40), 24);
}

TEST(QmHudNotifications, ScalesSmallTextChrome)
{
	EXPECT_FLOAT_EQ(QmHudNotifications::SmallTextScale(1.0f), 0.33f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingX(1.0f), 1.32f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingY(1.0f), 0.825f);
	EXPECT_FLOAT_EQ(QmHudNotifications::MinBoxWidth(1.0f), 27.06f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingX(8.0f), 4.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingY(8.0f), 2.5f);
	EXPECT_FLOAT_EQ(QmHudNotifications::MinBoxWidth(8.0f), 82.0f);
}

TEST(QmHudNotifications, RepeatCounterScaleOvershootsAndSettles)
{
	EXPECT_FLOAT_EQ(QmHudNotifications::RepeatCountElasticScale(-1.0f), 1.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::RepeatCountElasticScale(0.0f), 1.0f);
	EXPECT_GT(QmHudNotifications::RepeatCountElasticScale(0.5f), 1.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::RepeatCountElasticScale(1.0f), 1.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::RepeatCountElasticScale(2.0f), 1.0f);
}

TEST(QmHudNotifications, SelectsTextColorByNotificationKind)
{
	constexpr unsigned SystemColor = 0x111111;
	constexpr unsigned EchoOverrideColor = 0xFF222222;
	constexpr unsigned ChatEchoColor = 0x333333;

	const QmHudNotifications::STextColorConfig System = QmHudNotifications::TextColorConfig(QmHudNotifications::ETextSource::System, 1, SystemColor, EchoOverrideColor, ChatEchoColor);
	EXPECT_EQ(System.m_Color, SystemColor);
	EXPECT_TRUE(System.m_HasAlpha);

	const QmHudNotifications::STextColorConfig EchoInherited = QmHudNotifications::TextColorConfig(QmHudNotifications::ETextSource::Echo, 1, SystemColor, EchoOverrideColor, ChatEchoColor);
	EXPECT_EQ(EchoInherited.m_Color, ChatEchoColor);
	EXPECT_FALSE(EchoInherited.m_HasAlpha);

	const QmHudNotifications::STextColorConfig EchoOverride = QmHudNotifications::TextColorConfig(QmHudNotifications::ETextSource::Echo, 0, SystemColor, EchoOverrideColor, ChatEchoColor);
	EXPECT_EQ(EchoOverride.m_Color, EchoOverrideColor);
	EXPECT_TRUE(EchoOverride.m_HasAlpha);
}
