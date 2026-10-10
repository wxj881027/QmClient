#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/editor/mapitems/envelope.h>
#include <game/mapitems_ex.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>

namespace
{
	void WriteEnvelope(IStorage *pStorage, const char *pFilename, const CEnvelope &Envelope, bool IndependentMetadata)
	{
		const auto Runtime = Envelope.ExportPoints();
		std::vector<CEnvPoint> vPoints;
		std::vector<CEnvPointBezier> vBezier;
		for(const auto &Point : Runtime)
		{
			vPoints.push_back(Point);
			vBezier.push_back(Point.m_Bezier);
		}
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(pStorage, pFilename));
		CMapItemEnvelope Item{};
		Item.m_Version = 2;
		Item.m_Channels = Envelope.GetChannels();
		Item.m_NumPoints = vPoints.size();
		Writer.AddItem(MAPITEMTYPE_ENVELOPE, 0, sizeof(Item), &Item);
		Writer.AddItem(MAPITEMTYPE_ENVPOINTS, 0, vPoints.size() * sizeof(CEnvPoint), vPoints.data());
		Writer.AddItem(MAPITEMTYPE_ENVPOINTS_BEZIER, 0, vBezier.size() * sizeof(CEnvPointBezier), vBezier.data());
		if(IndependentMetadata)
		{
			const auto Data = Envelope.SerializeChannels(Runtime);
			Writer.AddItem(MAPITEMTYPE_QM_ENVELOPE_CHANNELS, 0, Data.size() * sizeof(int), Data.data());
		}
		Writer.Finish();
	}

	std::vector<CEnvPoint_runtime> ReadRuntime(CMapBasedEnvelopePointAccess &Access)
	{
		std::vector<CEnvPoint_runtime> vPoints(Access.NumPoints());
		for(int i = 0; i < Access.NumPoints(); ++i)
		{
			static_cast<CEnvPoint &>(vPoints[i]) = *Access.GetPoint(i);
			if(Access.GetBezier(i))
				vPoints[i].m_Bezier = *Access.GetBezier(i);
		}
		return vPoints;
	}
}

TEST(EditorEnvelopeChannelIo, SavedMapKeepsIndependentKeysAndStandardPlayback)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CEnvelope Source(CEnvelope::EType::COLOR);
	Source.AddPoint(CFixedTime(0), {0, 0, 1024, 1024});
	Source.AddPoint(CFixedTime(1200), {1024, 1024, 0, 512});
	Source.AddPoint(CFixedTime(300), {256, 0, 0, 0}, 1);
	Source.AddPoint(CFixedTime(600), {0, 0, 0, 768}, 8);
	Source.m_vPoints[1].m_Curvetype = CURVETYPE_SMOOTH;
	ASSERT_NO_FATAL_FAILURE(WriteEnvelope(pStorage.get(), "animation.map", Source, true));

	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "animation.map", IStorage::TYPE_ALL));
	CMapBasedEnvelopePointAccess Access(&Reader);
	const auto *pItem = static_cast<const CMapItemEnvelope *>(Reader.FindItem(MAPITEMTYPE_ENVELOPE, 0));
	ASSERT_NE(pItem, nullptr);
	Access.SetPointsRange(pItem->m_StartPoint, pItem->m_NumPoints);
	const auto Runtime = ReadRuntime(Access);
	CEnvelope Loaded(pItem->m_Channels);
	Loaded.ImportPoints(Runtime);
	const int Index = Reader.FindItemIndex(MAPITEMTYPE_QM_ENVELOPE_CHANNELS, 0);
	ASSERT_GE(Index, 0);
	const auto *pData = static_cast<const int *>(Reader.GetItem(Index));
	ASSERT_TRUE(Loaded.DeserializeChannels(pData, Reader.GetItemSize(Index) / sizeof(int), Runtime));
	EXPECT_EQ(Loaded.m_vPoints.size(), Source.m_vPoints.size());
	int RedKeys = 0, GreenKeys = 0, AlphaKeys = 0;
	for(const auto &Point : Loaded.m_vPoints)
	{
		RedKeys += Point.m_Channel == 0;
		GreenKeys += Point.m_Channel == 1;
		AlphaKeys += Point.m_Channel == 3;
	}
	EXPECT_EQ(RedKeys, 3);
	EXPECT_EQ(GreenKeys, 2);
	EXPECT_EQ(AlphaKeys, 3);
	for(int Micros : {100000, 300000, 600000, 1100000, 1300000})
	{
		SCOPED_TRACE(Micros);
		ColorRGBA Expected(0, 0, 0, 0), Actual(0, 0, 0, 0), Reopened(0, 0, 0, 0);
		Source.Eval(Micros / 1000000.0f, Expected, 4);
		Loaded.Eval(Micros / 1000000.0f, Reopened, 4);
		CRenderMap::RenderEvalEnvelope(&Access, std::chrono::microseconds(Micros), Actual, 4);
		for(int c = 0; c < 4; ++c)
		{
			EXPECT_FLOAT_EQ(Reopened[c], Expected[c]);
			EXPECT_NEAR(Actual[c], Expected[c], 0.003f);
		}
	}
}

TEST(EditorEnvelopeChannelIo, MapWithoutIndependentMetadataImportsAllLegacyChannels)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CEnvelope Source(CEnvelope::EType::POSITION);
	Source.AddPoint(CFixedTime(0), {0, 0, 0, 0});
	Source.AddPoint(CFixedTime(1200), {1024, 2048, 3072, 0});
	ASSERT_NO_FATAL_FAILURE(WriteEnvelope(pStorage.get(), "legacy.map", Source, false));
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "legacy.map", IStorage::TYPE_ALL));
	EXPECT_EQ(Reader.FindItemIndex(MAPITEMTYPE_QM_ENVELOPE_CHANNELS, 0), -1);
	CMapBasedEnvelopePointAccess Access(&Reader);
	Access.SetPointsRange(0, 2);
	CEnvelope Loaded(CEnvelope::EType::POSITION);
	Loaded.ImportPoints(ReadRuntime(Access));
	ASSERT_EQ(Loaded.m_vPoints.size(), 6u);
	ColorRGBA Before(0, 0, 0, 0), After(0, 0, 0, 0);
	Loaded.Eval(0.6f, Before, 3);
	Loaded.AddPoint(CFixedTime(300), {1024, 0, 0, 0}, 1);
	Loaded.Eval(0.6f, After, 3);
	EXPECT_NE(After.r, Before.r);
	EXPECT_FLOAT_EQ(After.g, Before.g);
	EXPECT_FLOAT_EQ(After.b, Before.b);
}
