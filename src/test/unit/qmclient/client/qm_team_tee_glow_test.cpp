#include <game/client/components/qmclient/team_tee_glow.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	const ColorRGBA s_TeeColor(0.2f, 0.3f, 0.4f, 0.1f);
	const ColorRGBA s_TeamColor(0.8f, 0.1f, 0.6f, 0.75f);
	bool Resolve(int Team, int Mode, unsigned Custom, ColorRGBA &Color, double Seconds = 0.0, int ClientId = 2)
	{
		return QmResolveTeamTeeGlowColor(true, true, Team, false, Mode, Custom, s_TeeColor, s_TeamColor, Seconds, ClientId, Color);
	}
}

TEST(QmTeamTeeGlow, LegacyCustomColorWithoutAlphaRemainsVisible)
{
	ColorRGBA Color;
	const unsigned Red = ColorHSLA(0.0f, 1.0f, 0.5f).Pack(false);
	ASSERT_TRUE(Resolve(0, 2, Red, Color));
	EXPECT_NEAR(Color.r, 1.0f, 0.01f);
	EXPECT_NEAR(Color.g, 0.0f, 0.01f);
	EXPECT_NEAR(Color.b, 0.0f, 0.01f);
	EXPECT_FLOAT_EQ(Color.a, 1.0f);
	ColorRGBA OldAlpha;
	ASSERT_TRUE(Resolve(0, 2, Red | 0x80000000u, OldAlpha));
	EXPECT_EQ(Color, OldAlpha);
}

TEST(QmTeamTeeGlow, TeamZeroOffAndInvalidModesDoNotDraw)
{
	ColorRGBA Color;
	for(const int Mode : {0, -1, 4})
	{
		SCOPED_TRACE(Mode);
		EXPECT_FALSE(Resolve(0, Mode, 0, Color));
	}
}

TEST(QmTeamTeeGlow, TeeModeUsesBodyRgbWithIndependentIntensity)
{
	ColorRGBA Color;
	ASSERT_TRUE(Resolve(0, 1, 0, Color));
	EXPECT_EQ(Color, s_TeeColor.WithAlpha(1.0f));
}

TEST(QmTeamTeeGlow, TeamedPlayersKeepTeamColorInEveryTeamZeroMode)
{
	for(const int Mode : {0, 1, 2, 3})
	{
		SCOPED_TRACE(Mode);
		ColorRGBA Color;
		ASSERT_TRUE(Resolve(3, Mode, 0, Color));
		EXPECT_EQ(Color, s_TeamColor.WithAlpha(1.0f));
	}
}

TEST(QmTeamTeeGlow, DisabledInactiveAndSuperPlayersDoNotDraw)
{
	ColorRGBA Color;
	EXPECT_FALSE(QmResolveTeamTeeGlowColor(false, true, 1, false, 2, 0, s_TeeColor, s_TeamColor, 0, 2, Color));
	EXPECT_FALSE(QmResolveTeamTeeGlowColor(true, false, 1, false, 2, 0, s_TeeColor, s_TeamColor, 0, 2, Color));
	EXPECT_FALSE(QmResolveTeamTeeGlowColor(true, true, 1, true, 2, 0, s_TeeColor, s_TeamColor, 0, 2, Color));
	EXPECT_FALSE(Resolve(-1, 2, 0, Color));
	EXPECT_FALSE(Resolve(0, 2, 0, Color, 0, -1));
}

TEST(QmTeamTeeGlow, RainbowIsDeterministicPeriodicAndChangesWithTimeAndPlayer)
{
	ColorRGBA First, Repeated, Periodic, Later, Other;
	ASSERT_TRUE(Resolve(0, 3, 0, First, 1000000000.0));
	ASSERT_TRUE(Resolve(0, 3, 0, Repeated, 1000000000.0));
	ASSERT_TRUE(Resolve(0, 3, 0, Periodic, 1000000010.0));
	ASSERT_TRUE(Resolve(0, 3, 0, Later, 1000000000.25));
	ASSERT_TRUE(Resolve(0, 3, 0, Other, 1000000000.0, 3));
	EXPECT_EQ(First, Repeated);
	EXPECT_NEAR(First.r, Periodic.r, 0.00001f);
	EXPECT_NEAR(First.g, Periodic.g, 0.00001f);
	EXPECT_NEAR(First.b, Periodic.b, 0.00001f);
	EXPECT_NE(First, Later);
	EXPECT_NE(First, Other);
}

TEST(QmTeamTeeGlow, StrengthAndSizeAreIndependentWithFixedLayerCount)
{
	const auto Default = QmTeamTeeGlowLayers(80, 40, 1.0f);
	const auto Strong = QmTeamTeeGlowLayers(100, 40, 1.0f);
	const auto Large = QmTeamTeeGlowLayers(80, 100, 1.0f);
	const auto Hidden = QmTeamTeeGlowLayers(0, 100, 1.0f);
	const auto NoRadius = QmTeamTeeGlowLayers(80, 0, 1.0f);
	ASSERT_EQ(Default.size(), 3u);
	for(size_t i = 0; i < Default.size(); ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_GT(Strong[i].m_Alpha, Default[i].m_Alpha);
		EXPECT_FLOAT_EQ(Strong[i].m_Scale, Default[i].m_Scale);
		EXPECT_GT(Large[i].m_Scale, Default[i].m_Scale);
		EXPECT_FLOAT_EQ(Large[i].m_Alpha, Default[i].m_Alpha);
		EXPECT_FLOAT_EQ(Hidden[i].m_Alpha, 0.0f);
		EXPECT_FLOAT_EQ(NoRadius[i].m_Scale, 1.0f);
	}
	EXPECT_GT(Default[0].m_Scale, Default[1].m_Scale);
	EXPECT_GT(Default[1].m_Scale, Default[2].m_Scale);
	EXPECT_LT(Default[0].m_Alpha, Default[1].m_Alpha);
	EXPECT_LT(Default[1].m_Alpha, Default[2].m_Alpha);
}

TEST(QmTeamTeeGlow, LayersClampInputsAndRespectPlayerTransparency)
{
	const auto Max = QmTeamTeeGlowLayers(100, 100, 1.0f);
	const auto Oversized = QmTeamTeeGlowLayers(300, 300, 2.0f);
	const auto Half = QmTeamTeeGlowLayers(100, 100, 0.5f);
	const auto Negative = QmTeamTeeGlowLayers(-1, -1, -1.0f);
	const auto Invalid = QmTeamTeeGlowLayers(100, 100, std::numeric_limits<float>::quiet_NaN());
	for(size_t i = 0; i < Max.size(); ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_FLOAT_EQ(Oversized[i].m_Alpha, Max[i].m_Alpha);
		EXPECT_FLOAT_EQ(Oversized[i].m_Scale, Max[i].m_Scale);
		EXPECT_FLOAT_EQ(Half[i].m_Alpha, Max[i].m_Alpha * 0.5f);
		EXPECT_FLOAT_EQ(Negative[i].m_Alpha, 0.0f);
		EXPECT_FLOAT_EQ(Negative[i].m_Scale, 1.0f);
		EXPECT_FLOAT_EQ(Invalid[i].m_Alpha, 0.0f);
	}
}
