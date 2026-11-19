#include <game/editor/mapitems/envelope.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

TEST(EditorEnvelopeExportOrder, DenseAlignedTracksKeepEachLegacyPoint)
{
	constexpr int NUM_POINTS = 4096;
	std::vector<CEnvPoint_runtime> vOriginal(NUM_POINTS);
	for(int i = 0; i < NUM_POINTS; ++i)
	{
		vOriginal[i].m_Time = CFixedTime(i * 20);
		vOriginal[i].m_Curvetype = (i % 2 == 0) ? CURVETYPE_LINEAR : CURVETYPE_SMOOTH;
		vOriginal[i].m_aValues[0] = i;
		vOriginal[i].m_aValues[2] = -i;
	}
	CEnvelope Envelope(CEnvelope::EType::POSITION);
	Envelope.ImportPoints(vOriginal);
	// 独立元数据允许按轨道排列，各轨道内部仍按时间排序。
	std::stable_sort(Envelope.m_vPoints.begin(), Envelope.m_vPoints.end(), [](const auto &Left, const auto &Right) {
		return Left.m_Channel < Right.m_Channel;
	});

	const auto vExported = Envelope.ExportPoints();
	ASSERT_EQ(vExported.size(), vOriginal.size());
	for(int i = 0; i < NUM_POINTS; ++i)
	{
		EXPECT_EQ(vExported[i].m_Time, vOriginal[i].m_Time);
		EXPECT_EQ(vExported[i].m_Curvetype, vOriginal[i].m_Curvetype);
		EXPECT_EQ(vExported[i].m_aValues[0], i);
		EXPECT_EQ(vExported[i].m_aValues[2], -i);
	}
}

TEST(EditorEnvelopeExportOrder, DuplicateTimestampKeepsBothSidesOfStep)
{
	std::vector<CEnvPoint_runtime> vOriginal(4);
	vOriginal[0].m_Time = CFixedTime(0);
	vOriginal[1].m_Time = CFixedTime(500);
	vOriginal[2].m_Time = CFixedTime(500);
	vOriginal[3].m_Time = CFixedTime(1000);
	vOriginal[0].m_Curvetype = CURVETYPE_LINEAR;
	vOriginal[2].m_Curvetype = CURVETYPE_LINEAR;
	vOriginal[1].m_aValues[0] = 100;
	vOriginal[2].m_aValues[0] = 300;
	vOriginal[3].m_aValues[0] = 500;
	CEnvelope Envelope(CEnvelope::EType::SOUND);
	Envelope.ImportPoints(vOriginal);

	const auto vExported = Envelope.ExportPoints();
	std::vector<int> vStepValues;
	for(const auto &Point : vExported)
		if(Point.m_Time == CFixedTime(500))
			vStepValues.push_back(Point.m_aValues[0]);
	EXPECT_EQ(vStepValues, (std::vector<int>{100, 300}));
}

TEST(EditorEnvelopeExportOrder, PointBetweenCoincidentKeysStaysInChannelOrder)
{
	std::vector<CEnvPoint_runtime> vOriginal(3);
	for(auto &Point : vOriginal)
		Point.m_Time = CFixedTime(500);
	CEnvelope Envelope(CEnvelope::EType::SOUND);
	Envelope.ImportPoints(vOriginal);

	EXPECT_EQ(Envelope.ClampPointTime(1, 0, CFixedTime(600)), CFixedTime(500));
}

TEST(EditorEnvelopeExportOrder, DragAfterFinalRepresentableTimeKeepsValidTime)
{
	const CFixedTime Last(std::numeric_limits<int>::max());
	std::vector<CEnvPoint_runtime> vOriginal(2);
	for(auto &Point : vOriginal)
		Point.m_Time = Last;
	CEnvelope Envelope(CEnvelope::EType::SOUND);
	Envelope.ImportPoints(vOriginal);

	EXPECT_EQ(Envelope.ClampPointTime(1, 0, CFixedTime(0)), Last);
}
