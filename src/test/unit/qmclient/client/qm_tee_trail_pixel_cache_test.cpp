#include <game/client/components/qmclient/tee_trail_styles.h>

#include <gtest/gtest.h>

TEST(QmTeeTrailPixelCache, ReusedCellsAndSeedsKeepTheUncachedNoise)
{
	qm_tee_trail::CPixelNoiseCache Cache;
	for(const unsigned Seed : {17u, 148u, 17u})
	{
		SCOPED_TRACE(Seed);
		// 超过容量并再次访问旧格，约束碰撞替换与负坐标不能复用另一格的结果。
		for(int X = -4200; X < 4200; ++X)
		{
			SCOPED_TRACE(X);
			const auto Expected = qm_tee_trail::SamplePixelNoise(X, X % 31, Seed);
			const auto Actual = Cache.Get(X, X % 31, Seed);
			EXPECT_FLOAT_EQ(Actual.m_Cluster, Expected.m_Cluster);
			EXPECT_FLOAT_EQ(Actual.m_Grain, Expected.m_Grain);
		}
	}
}

TEST(QmTeeTrailPixelCache, CacheKeepsGeometryAfterTimeColorWidthAndSeedChanges)
{
	std::vector<CTrailPart> vTrail(24);
	for(size_t i = 0; i < vTrail.size(); ++i)
	{
		auto &Part = vTrail[i];
		Part.m_Pos = vec2(200.0f - float(i) * 6.0f, 40.0f + float(i % 5) * 3.0f);
		Part.m_Time = 40.0 - double(i);
		Part.m_Distance = 200.0 - double(i) * 6.0;
		Part.m_Speed = 20.0f;
		Part.m_Life = 55.0f;
	}
	qm_tee_trail::CPixelNoiseCache Cache;
	std::vector<qm_tee_trail::SQuad> vExpected, vActual;
	for(const double Time : {40.0, 41.5, 48.0})
		for(const float Width : {8.0f, 20.0f})
			for(const int Seed : {17, 148, 17})
				for(const bool Preset : {false, true})
				{
					SCOPED_TRACE(Time);
					SCOPED_TRACE(Width);
					SCOPED_TRACE(Seed);
					SCOPED_TRACE(Preset);
					for(auto &Part : vTrail)
						Part.m_Col = Time < 42.0 ? ColorRGBA(0.2f, 0.6f, 0.9f, 0.54f) : ColorRGBA(0.9f, 0.3f, 0.1f, 0.8f);
					qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, Preset, Time, Width, Seed, vExpected, 0.4f, false, false);
					qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, Preset, Time, Width, Seed, vActual, 0.4f, false, false, &Cache);
					ASSERT_FALSE(vExpected.empty());
					ASSERT_EQ(vActual.size(), vExpected.size());
					for(size_t i = 0; i < vExpected.size(); ++i)
					{
						SCOPED_TRACE(i);
						EXPECT_EQ(vActual[i].m_Additive, vExpected[i].m_Additive);
						for(int Corner = 0; Corner < 4; ++Corner)
						{
							EXPECT_EQ(vActual[i].m_aPos[Corner], vExpected[i].m_aPos[Corner]);
							EXPECT_EQ(vActual[i].m_aColor[Corner], vExpected[i].m_aColor[Corner]);
						}
					}
				}
}
