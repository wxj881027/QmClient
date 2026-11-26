#include <game/client/components/qmclient/scoreboard_score_format.h>
#include <game/client/gameclient.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace
{
	using namespace scoreboard_score;

	std::string Format(SValue Value)
	{
		char aScore[32];
		Value.Format(aScore, sizeof(aScore));
		return aScore;
	}

	TEST(QmScoreboardScore, DetectsFngFlagsAndModNames)
	{
		CGameInfo Info{};
		EXPECT_TRUE(Info.ScoreboardMode(false, "DDFightNet FNG").m_Fng);
		EXPECT_TRUE(Info.ScoreboardMode(false, "solofng2++").m_Fng);
		EXPECT_FALSE(Info.ScoreboardMode(false, "DDRace").m_Fng);
		Info.m_PredictFNG = true;
		EXPECT_TRUE(Info.ScoreboardMode(false, "DM").m_Fng);
		Info.m_PredictFNG = false;
		Info.m_EntitiesFNG = true;
		EXPECT_TRUE(Info.ScoreboardMode(false, "DM").m_Fng);
	}

	TEST(QmScoreboardScore, ProtocolFngIdentitySurvivesWithoutPredictionEntitiesOrName)
	{
		CGameInfo Info{};
		Info.m_Fng = true;
		EXPECT_TRUE(Info.ScoreboardMode(false, "Custom").m_Fng);
	}

	TEST(QmScoreboardScore, MixedFngSnapshotPreservesFinishBesideUnsetPlayer)
	{
		const SMode Mode{false, true, true};
		const bool UseFinishFields = Mode.UseFinishFields(true, false);
		const auto Finisher = Mode.Resolve(104, UseFinishFields, 59, 123);
		const auto Scorer = Mode.Resolve(120, UseFinishFields, FinishTime::UNSET, 0);
		EXPECT_EQ(Finisher.m_Kind, EKind::DDNET_TIME);
		EXPECT_EQ(Finisher.m_Value, 59123);
		EXPECT_EQ(Format(Finisher), "00:59");
		EXPECT_EQ(Scorer.m_Kind, EKind::POINTS);
		EXPECT_EQ(Format(Scorer), "120");
		EXPECT_TRUE(Finisher.Precedes(Scorer));
		EXPECT_EQ(Mode.Column(Finisher.IsTime(), Scorer.m_Kind == EKind::POINTS), EColumn::TIME_AND_SCORE);
	}

	TEST(QmScoreboardScore, MixedColumnKeepsEachTeamTitleScore)
	{
		CNetObj_GameData GameData{};
		GameData.m_TeamscoreRed = 17;
		GameData.m_TeamscoreBlue = 23;
		const auto Red = ResolveTitleScore(EColumn::TIME_AND_SCORE, true, &GameData, TEAM_RED);
		const auto Blue = ResolveTitleScore(EColumn::TIME_AND_SCORE, true, &GameData, TEAM_BLUE);
		EXPECT_EQ(Red.m_Source, ETitleSource::TEAM_SCORE);
		EXPECT_EQ(Red.m_TeamScore, 17);
		EXPECT_EQ(Blue.m_Source, ETitleSource::TEAM_SCORE);
		EXPECT_EQ(Blue.m_TeamScore, 23);
		EXPECT_EQ(ResolveTitleScore(EColumn::TIME_AND_SCORE, false, &GameData, TEAM_GAME).m_Source, ETitleSource::MAP_BEST);
	}

	TEST(QmScoreboardScore, LegacyDdraceTime)
	{
		const SMode Mode{false, true, false};
		EXPECT_EQ(Format(Mode.Resolve(-59, false, -1, 0)), "00:59");
		EXPECT_EQ(Format(Mode.Resolve(0, false, -1, 0)), "00:00");
		EXPECT_EQ(Format(Mode.Resolve(std::numeric_limits<int>::min(), false, -1, 0)), "596523:14:08");
		EXPECT_EQ(Format(Mode.Resolve(-9999, false, -1, 0)), "");
		EXPECT_EQ(Mode.Column(false, false), EColumn::TIME);
	}

	TEST(QmScoreboardScore, DdraceFinishFieldTakesPrecedence)
	{
		const SMode Mode{false, true, false};
		const auto Time = Mode.Resolve(-9999, true, 60, 901);
		EXPECT_EQ(Time.m_Kind, EKind::DDNET_TIME);
		EXPECT_EQ(Time.m_Value, 60901);
		EXPECT_EQ(Format(Time), "01:00");
		EXPECT_TRUE(Mode.Resolve(-60, true, 60, 900).Precedes(Time));
		EXPECT_EQ(Format(Mode.Resolve(-59, true, -1, 0)), "");
		EXPECT_EQ(Format(Mode.Resolve(-9999, true, 0, 0)), "00:00");
	}

	TEST(QmScoreboardScore, PointsIgnoreFinishFieldsAndPutMissingLast)
	{
		for(bool Fng : {false, true})
		{
			const SMode Mode{false, false, Fng};
			for(bool HasFinishTimes : {false, true})
			{
				SCOPED_TRACE(::testing::Message() << "Fng=" << Fng << ", HasFinishTimes=" << HasFinishTimes);
				std::array<int, 7> aScores = {-9, -9999, 61, -10500, 0, 104, 100001};
				auto Resolve = [&](int Score) { return Mode.Resolve(Score, HasFinishTimes, -1, 0); };
				std::stable_sort(aScores.begin(), aScores.end(), [&](int A, int B) { return Resolve(A).Precedes(Resolve(B)); });
				EXPECT_EQ(aScores, (std::array<int, 7>{100001, 104, 61, 0, -9, -10500, -9999}));
				for(int Score : aScores)
					EXPECT_EQ(Format(Resolve(Score)), Score == -9999 ? "" : std::to_string(Score));
				EXPECT_EQ(Format(Mode.Resolve(-9, HasFinishTimes, 60, 123)), "-9");
			}
			EXPECT_EQ(Mode.Column(false, true), EColumn::SCORE);
		}
	}

	TEST(QmScoreboardScore, MixedFngOrdersFinishersThenPointsThenMissing)
	{
		const SMode Mode{false, true, true};
		const auto First = Mode.Resolve(-9999, true, 0, 0);
		const auto Second = Mode.Resolve(104, true, 59, 123);
		const auto High = Mode.Resolve(94, true, -1, 0);
		const auto Low = Mode.Resolve(59, true, -1, 0);
		const auto Negative = Mode.Resolve(-9, true, -1, 0);
		const auto Missing = Mode.Resolve(-9999, true, -1, 0);
		EXPECT_EQ(Format(First), "00:00");
		EXPECT_EQ(Format(Second), "00:59");
		EXPECT_EQ(Format(High), "94");
		EXPECT_EQ(Format(Low), "59");
		EXPECT_EQ(Format(Negative), "-9");
		EXPECT_EQ(Format(Missing), "");
		EXPECT_TRUE(First.Precedes(Second));
		EXPECT_TRUE(Second.Precedes(High));
		EXPECT_TRUE(High.Precedes(Low));
		EXPECT_TRUE(Low.Precedes(Negative));
		EXPECT_TRUE(Negative.Precedes(Missing));
		EXPECT_FALSE(High.Precedes(High));
		EXPECT_FALSE(Missing.Precedes(High));
		EXPECT_EQ(Mode.Column(false, true), EColumn::SCORE);
		EXPECT_EQ(Mode.Column(true, false), EColumn::TIME);
		EXPECT_EQ(Mode.Column(true, true), EColumn::TIME_AND_SCORE);
	}

	TEST(QmScoreboardScore, LegacyMixedFngRetainsNegativeRaceSeconds)
	{
		const SMode Mode{false, true, true};
		EXPECT_EQ(Format(Mode.Resolve(59, false, -1, 0)), "59");
		EXPECT_EQ(Format(Mode.Resolve(0, false, -1, 0)), "0");
		EXPECT_EQ(Format(Mode.Resolve(-59, false, -1, 0)), "00:59");
		EXPECT_EQ(Format(Mode.Resolve(-59, true, FinishTime::UNSET, 0)), "00:59");
		EXPECT_EQ(Format(Mode.Resolve(-9999, false, -1, 0)), "");
	}

	TEST(QmScoreboardScore, RankPreferenceOnlyAppliesOutsideFngMatches)
	{
		EXPECT_FALSE((SMode{false, false, true}.AllowsRankSort()));
		EXPECT_FALSE((SMode{false, true, true}.AllowsRankSort()));
		EXPECT_FALSE((SMode{true, true, true}.AllowsRankSort()));
		EXPECT_TRUE((SMode{true, true, false}.AllowsRankSort()));
		EXPECT_TRUE((SMode{false, true, false}.AllowsRankSort()));
	}

	TEST(QmScoreboardScore, Race7AlwaysUsesSnapshotMilliseconds)
	{
		const SMode Mode{true, false, true};
		const auto Finish = Mode.Resolve(61234, true, -1, 0);
		const auto Missing = Mode.Resolve(-1, true, 100, 10);
		EXPECT_EQ(Format(Finish), "01:01.23");
		EXPECT_EQ(Format(Missing), "");
		EXPECT_TRUE(Mode.Resolve(61233, false, -1, 0).Precedes(Finish));
		EXPECT_TRUE(Finish.Precedes(Missing));
		EXPECT_EQ(Mode.Column(false, false), EColumn::TIME);
	}
}
