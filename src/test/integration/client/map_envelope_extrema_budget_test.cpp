#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/map/envelope_extrema.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>

TEST(MapEnvelopeExtremaBudgetIntegration, SkipsClippingWhenAnOverlappingRangeExceedsThePointBudget)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "envelope-extrema-budget.map"));

	std::array<CMapItemEnvelope, 3> aEnvelopes{};
	for(size_t i = 0; i < aEnvelopes.size(); ++i)
	{
		auto &Envelope = aEnvelopes[i];
		Envelope.m_Version = 2;
		Envelope.m_Channels = 3;
		Envelope.m_StartPoint = static_cast<int>(i);
		Envelope.m_NumPoints = i < 2 ? 2 : 1;
		Writer.AddItem(MAPITEMTYPE_ENVELOPE, static_cast<int>(i), sizeof(Envelope), &Envelope);
	}
	std::array<CEnvPoint, 3> aPoints{};
	aPoints[0].m_aValues[0] = i2fx(1);
	aPoints[1].m_aValues[0] = i2fx(2);
	aPoints[2].m_aValues[0] = i2fx(3);
	aPoints[1].m_Time = CFixedTime(1000);
	aPoints[2].m_Time = CFixedTime(2000);
	Writer.AddItem(MAPITEMTYPE_ENVPOINTS, 0, sizeof(aPoints), aPoints.data());
	ASSERT_TRUE(Writer.Finish());

	CMap Map;
	ASSERT_TRUE(Map.GetReader()->Open(pStorage.get(), "envelope-extrema-budget.map", IStorage::TYPE_SAVE));
	CEnvelopeExtrema Extrema(&Map);

	const auto &First = Extrema.GetExtrema(0);
	ASSERT_TRUE(First.m_Available);
	EXPECT_EQ(First.m_Minima.x, i2fx(1));
	EXPECT_EQ(First.m_Maxima.x, i2fx(2));
	EXPECT_FALSE(Extrema.GetExtrema(1).m_Available);
	const auto &Last = Extrema.GetExtrema(2);
	ASSERT_TRUE(Last.m_Available);
	EXPECT_EQ(Last.m_Minima.x, i2fx(3));
	EXPECT_EQ(Last.m_Maxima.x, i2fx(3));
}
