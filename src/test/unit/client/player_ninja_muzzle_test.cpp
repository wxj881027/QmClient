#include <game/client/components/players.h>

#include <gtest/gtest.h>

namespace
{
	class PlayerNinjaMuzzle : public ::testing::Test
	{
	protected:
		struct SSnapshotFrames
		{
			CNetObj_Character m_Prev{};
			CNetObj_Character m_Cur{};
		};

		CNetObj_Character m_RenderPrev{};
		CNetObj_Character m_RenderCur{};
		SSnapshotFrames m_aCharacters[1]{};

		PlayerNinjaMuzzle()
		{
			m_RenderPrev.m_X = 20;
			m_RenderPrev.m_Y = 30;
			m_RenderCur.m_X = 25;
			m_RenderCur.m_Y = 28;
			m_aCharacters[0].m_Prev.m_X = 3;
			m_aCharacters[0].m_Prev.m_Y = 2;
			m_aCharacters[0].m_Cur.m_X = 4;
			m_aCharacters[0].m_Cur.m_Y = 6;
		}
	};
}

TEST_F(PlayerNinjaMuzzle, ReplayUsesItsOwnFrames)
{
	const vec2 Motion = QmNinjaMuzzleMotion(m_RenderPrev, m_RenderCur, -2, false, m_aCharacters);
	EXPECT_FLOAT_EQ(Motion.x, 5.0f);
	EXPECT_FLOAT_EQ(Motion.y, -2.0f);
}

TEST_F(PlayerNinjaMuzzle, RemotePlayerUsesSnapshotFrames)
{
	const vec2 Motion = QmNinjaMuzzleMotion(m_RenderPrev, m_RenderCur, 0, false, m_aCharacters);
	EXPECT_FLOAT_EQ(Motion.x, 1.0f);
	EXPECT_FLOAT_EQ(Motion.y, 4.0f);
}

TEST_F(PlayerNinjaMuzzle, LocalWeaponPredictionUsesRenderFrames)
{
	const vec2 Motion = QmNinjaMuzzleMotion(m_RenderPrev, m_RenderCur, 0, true, m_aCharacters);
	EXPECT_FLOAT_EQ(Motion.x, 5.0f);
	EXPECT_FLOAT_EQ(Motion.y, -2.0f);
}
