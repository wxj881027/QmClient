#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/editor/mapitems/load_envelopes.h>
#include <game/gamecore.h>
#include <game/map/envelope_extrema.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

namespace
{
	class MapEnvelopeItems : public ::testing::Test
	{
	protected:
		struct SEnvelope
		{
			CMapItemEnvelope m_Item{};
			size_t m_Size = sizeof(CMapItemEnvelope);
			SEnvelope()
			{
				m_Item.m_Version = 2;
				m_Item.m_Channels = 3;
				m_Item.m_NumPoints = 2;
				m_Item.m_Synchronized = 1;
				StrToInts(m_Item.m_aName, std::size(m_Item.m_aName), "Named");
			}
		};
		struct SItems
		{
			std::vector<SEnvelope> m_vEnvelopes{{}};
			std::vector<CEnvPoint_runtime> m_vPoints;
			bool m_HasPoints = true;
			bool m_Upstream = false;
			int m_BezierCount = 2;
			std::optional<size_t> m_PointBytes;
			SItems() :
				m_vPoints(2)
			{
				m_vPoints[0].m_Time = CFixedTime(0);
				m_vPoints[0].m_aValues[0] = 1024;
				m_vPoints[1].m_Time = CFixedTime(1000);
				m_vPoints[1].m_aValues[0] = 2048;
			}
			CMapItemEnvelope &Item() { return m_vEnvelopes[0].m_Item; }
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CMap> m_pMap;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
		}
		// 游戏层保持有效，包络与点记录通过真实 writer/reader 进入消费接口。
		void WriteMap(const SItems &Items)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "envelopes.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			for(size_t i = 0; i < Items.m_vEnvelopes.size(); ++i)
			{
				const auto &Envelope = Items.m_vEnvelopes[i];
				Writer.AddItem(MAPITEMTYPE_ENVELOPE, static_cast<int>(i), Envelope.m_Size, &Envelope.m_Item);
			}
			if(Items.m_HasPoints)
			{
				std::vector<CEnvPoint> vPoints;
				std::vector<CEnvPointBezier> vBezier;
				std::vector<CEnvPointBezier_upstream> vUpstream;
				for(const auto &Point : Items.m_vPoints)
				{
					vPoints.push_back(Point);
					vBezier.push_back(Point.m_Bezier);
					CEnvPointBezier_upstream Upstream{};
					static_cast<CEnvPoint &>(Upstream) = Point;
					Upstream.m_Bezier = Point.m_Bezier;
					vUpstream.push_back(Upstream);
				}
				const size_t Bytes = Items.m_Upstream ? vUpstream.size() * sizeof(CEnvPointBezier_upstream) : vPoints.size() * sizeof(CEnvPoint);
				const void *pPoints = Items.m_Upstream ? static_cast<const void *>(vUpstream.data()) : static_cast<const void *>(vPoints.data());
				Writer.AddItem(MAPITEMTYPE_ENVPOINTS, 0, Items.m_PointBytes.value_or(Bytes), pPoints);
				if(!Items.m_Upstream && Items.m_BezierCount > 0)
					Writer.AddItem(MAPITEMTYPE_ENVPOINTS_BEZIER, 0, Items.m_BezierCount * sizeof(CEnvPointBezier), vBezier.data());
			}
			const std::array<CTile, 4> aTiles{};
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 4;
			Game.m_Width = Game.m_Height = 2;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = Game.m_Image = -1;
			StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
			Game.m_Data = Writer.AddData(sizeof(aTiles), aTiles.data());
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Game), &Game);
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = 1;
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
			Writer.Finish();
		}
		void Load(const SItems &Items)
		{
			ASSERT_NO_FATAL_FAILURE(WriteMap(Items));
			ASSERT_TRUE(m_pMap->Load("envelopes.map", IStorage::TYPE_SAVE));
		}
	};
}

TEST_F(MapEnvelopeItems, UnloadedCandidateCannotValidateEnvelopes)
{
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, MapWithoutEnvelopeItemsRemainsLoadable)
{
	SItems Items;
	Items.m_vEnvelopes.clear();
	Items.m_HasPoints = false;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, ValidStandardEnvelopeKeepsMetadataAndPointValues)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
	const auto Item = MapRenderItems::ReadEnvelope(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_ENVELOPE, 0));
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_Synchronized, 1);
	CMapBasedEnvelopePointAccess Points(m_pMap.get());
	ASSERT_EQ(Points.NumPoints(), 2);
	EXPECT_EQ(Points.GetPoint(1)->m_aValues[0], 2048);
	EXPECT_NE(Points.GetBezier(1), nullptr);
}

TEST_F(MapEnvelopeItems, LegacyBaseWithoutSyncTailRemainsValid)
{
	SItems Items;
	Items.Item().m_Version = 1;
	Items.m_vEnvelopes[0].m_Size = sizeof(CMapItemEnvelope_v1);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, RenderingPrefixDoesNotRequireTheEditorsNameTail)
{
	SItems Items;
	Items.m_vEnvelopes[0].m_Size = offsetof(CMapItemEnvelope_v1, m_aName);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(MapRenderItems::ReadEnvelope(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_ENVELOPE, 0)));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, EditorRejectsVersionTwoMissingSynchronization)
{
	SItems Items;
	Items.m_vEnvelopes[0].m_Size = sizeof(CMapItemEnvelope_v1);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, ShortFixedHeaderIsRejectedAfterSharedMapAcceptsIt)
{
	SItems Items;
	Items.m_vEnvelopes[0].m_Size = sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, EmptyEnvelopeItemDoesNotSelectAFalseBezierLayout)
{
	SItems Items;
	Items.m_vEnvelopes[0].m_Size = 0;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
	CMapBasedEnvelopePointAccess Points(m_pMap.get());
	ASSERT_EQ(Points.NumPoints(), 2);
	EXPECT_EQ(Points.GetPoint(1)->m_aValues[0], 2048);
}

TEST_F(MapEnvelopeItems, LaterShortEnvelopeRejectsTheCandidate)
{
	SItems Items;
	Items.m_vEnvelopes.push_back({});
	Items.m_vEnvelopes[1].m_Size = sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, CorrectedCandidateCanValidateAfterRangeFailure)
{
	SItems Items;
	Items.Item().m_NumPoints = -1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	ASSERT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
	m_pMap->Unload();
	ASSERT_NO_FATAL_FAILURE(Load({}));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, NegativePointCountIsRejectedBeforeEditorAllocation)
{
	SItems Items;
	Items.Item().m_NumPoints = -1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, HugeDeclaredCountCannotExceedActualPoints)
{
	SItems Items;
	Items.Item().m_NumPoints = INT_MAX;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, NegativeNonemptyStartIsRejected)
{
	SItems Items;
	Items.Item().m_StartPoint = -1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, StartBeyondActualPointsIsRejected)
{
	SItems Items;
	Items.Item().m_StartPoint = INT_MAX;
	Items.Item().m_NumPoints = 1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, OverflowingEndIsRejectedWithoutAddingSignedFields)
{
	SItems Items;
	Items.Item().m_StartPoint = 1;
	Items.Item().m_NumPoints = INT_MAX;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, RangeEndingAtTheActualLastPointRemainsValid)
{
	SItems Items;
	Items.Item().m_StartPoint = 1;
	Items.Item().m_NumPoints = 1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, EmptyEnvelopeDoesNotConsumeItsStart)
{
	SItems Items;
	Items.Item().m_StartPoint = INT_MAX;
	Items.Item().m_NumPoints = 0;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, MissingPointItemCannotSatisfyNonemptyRange)
{
	SItems Items;
	Items.m_HasPoints = false;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, MissingPointItemIsAllowedForAnEmptyEnvelope)
{
	SItems Items;
	Items.m_HasPoints = false;
	Items.Item().m_NumPoints = 0;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
}

TEST_F(MapEnvelopeItems, PartialPointRecordCannotSatisfyDeclaredCount)
{
	SItems Items;
	Items.m_PointBytes = 2 * sizeof(CEnvPoint) - sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
	CMapBasedEnvelopePointAccess Points(m_pMap.get());
	EXPECT_EQ(Points.NumPoints(), 1);
}

TEST_F(MapEnvelopeItems, UpstreamVersionUsesInterleavedBezierRecords)
{
	SItems Items;
	Items.Item().m_Version = CMapItemEnvelope::VERSION_TEEWORLDS_BEZIER;
	Items.m_Upstream = true;
	Items.m_vPoints[1].m_Bezier.m_aInTangentDeltaY[0] = -77;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
	CMapBasedEnvelopePointAccess Points(m_pMap.get());
	ASSERT_EQ(Points.NumPoints(), 2);
	ASSERT_NE(Points.GetBezier(1), nullptr);
	EXPECT_EQ(Points.GetBezier(1)->m_aInTangentDeltaY[0], -77);
	EXPECT_EQ(Points.GetPoint(1)->m_aValues[0], 2048);
}

TEST_F(MapEnvelopeItems, VersionPrefixStillChoosesTheGlobalUpstreamPointFormat)
{
	SItems Items;
	Items.Item().m_Version = CMapItemEnvelope::VERSION_TEEWORLDS_BEZIER;
	Items.m_vEnvelopes[0].m_Size = sizeof(int);
	Items.m_Upstream = true;
	Items.m_vPoints[1].m_Bezier.m_aInTangentDeltaY[0] = -77;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
	CMapBasedEnvelopePointAccess Points(m_pMap.get());
	ASSERT_EQ(Points.NumPoints(), 2);
	ASSERT_NE(Points.GetBezier(1), nullptr);
	EXPECT_EQ(Points.GetBezier(1)->m_aInTangentDeltaY[0], -77);
}

TEST_F(MapEnvelopeItems, MismatchedSeparateBezierCountRetainsLinearFallback)
{
	SItems Items;
	Items.m_BezierCount = 1;
	Items.m_vPoints[0].m_Curvetype = CURVETYPE_BEZIER;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CMapBasedEnvelopePointAccess Points(m_pMap.get());
	EXPECT_EQ(Points.GetBezier(0), nullptr);
	ColorRGBA Result(0, 0, 0, 0);
	CRenderMap::RenderEvalEnvelope(&Points, std::chrono::milliseconds(500), Result, 1);
	EXPECT_FLOAT_EQ(Result.r, 1.5f);
}

TEST_F(MapEnvelopeItems, InvalidChannelsRemainAvailableForExistingEditorFallback)
{
	SItems Items;
	Items.Item().m_Channels = -1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_TRUE(EditorMapLoad::ValidateEnvelopeItems(*m_pMap));
	const auto Item = MapRenderItems::ReadEnvelope(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_ENVELOPE, 0));
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_Channels, -1);
}

TEST_F(MapEnvelopeItems, MetadataSnapshotRemainsOwnedAfterCandidateUnload)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	const auto Item = MapRenderItems::ReadEnvelope(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_ENVELOPE, 0));
	ASSERT_TRUE(Item);
	m_pMap->Unload();
	char aName[32];
	IntsToStr(Item->m_aName, std::size(Item->m_aName), aName, sizeof(aName));
	EXPECT_STREQ(aName, "Named");
	EXPECT_EQ(Item->m_NumPoints, 2);
}

TEST_F(MapEnvelopeItems, ReaderWithoutPointItemsDoesNotUseAnotherItemsSize)
{
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "no_points.map"));
	SEnvelope Envelope;
	Writer.AddItem(MAPITEMTYPE_ENVELOPE, 0, sizeof(Envelope.m_Item), &Envelope.m_Item);
	Writer.Finish();
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "no_points.map", IStorage::TYPE_SAVE));
	CMapBasedEnvelopePointAccess Points(&Reader);
	EXPECT_EQ(Points.NumPointsMax(), 0);
	EXPECT_EQ(Points.GetPoint(0), nullptr);
	ColorRGBA Result(3, 4, 5, 6);
	CRenderMap::RenderEvalEnvelope(&Points, std::chrono::milliseconds(1), Result, 1);
	EXPECT_FLOAT_EQ(Result.r, 3);
}

TEST_F(MapEnvelopeItems, RangeClampingNeverExposesUnavailablePoints)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	CMapBasedEnvelopePointAccess Points(m_pMap.get());
	Points.SetPointsRange(1, INT_MAX);
	ASSERT_EQ(Points.NumPoints(), 1);
	EXPECT_EQ(Points.GetPoint(0)->m_aValues[0], 2048);
	EXPECT_EQ(Points.GetPoint(1), nullptr);
	Points.SetPointsRange(INT_MAX, INT_MAX);
	EXPECT_EQ(Points.NumPoints(), 0);
}

TEST_F(MapEnvelopeItems, TruncatedEnvelopeDisablesItsEarlyClipping)
{
	SItems Items;
	Items.m_vEnvelopes[0].m_Size = sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CEnvelopeExtrema Extrema(m_pMap.get());
	EXPECT_FALSE(Extrema.GetExtrema(0).m_Available);
}

TEST_F(MapEnvelopeItems, OverflowingPointRangeDoesNotEnterExtremaIteration)
{
	SItems Items;
	Items.Item().m_StartPoint = 1;
	Items.Item().m_NumPoints = INT_MAX;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CEnvelopeExtrema Extrema(m_pMap.get());
	EXPECT_FALSE(Extrema.GetExtrema(0).m_Available);
}

TEST_F(MapEnvelopeItems, ValidExtremaRetainThePointsMinimumAndMaximum)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	CEnvelopeExtrema Extrema(m_pMap.get());
	const auto &Value = Extrema.GetExtrema(0);
	ASSERT_TRUE(Value.m_Available);
	EXPECT_FALSE(Value.m_Rotating);
	EXPECT_EQ(Value.m_Minima.x, 1024);
	EXPECT_EQ(Value.m_Maxima.x, 2048);
}

TEST_F(MapEnvelopeItems, EmptyEnvelopeHasZeroMovementClippingBounds)
{
	SItems Items;
	Items.Item().m_NumPoints = 0;
	Items.Item().m_StartPoint = INT_MAX;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CEnvelopeExtrema Extrema(m_pMap.get());
	const auto &Value = Extrema.GetExtrema(0);
	ASSERT_TRUE(Value.m_Available);
	EXPECT_EQ(Value.m_Minima, ivec2(0, 0));
	EXPECT_EQ(Value.m_Maxima, ivec2(0, 0));
}

TEST_F(MapEnvelopeItems, OverflowingOutgoingTangentDisablesEarlyClipping)
{
	SItems Items;
	Items.m_vPoints[0].m_Curvetype = CURVETYPE_BEZIER;
	Items.m_vPoints[0].m_aValues[0] = INT_MAX;
	Items.m_vPoints[0].m_Bezier.m_aOutTangentDeltaY[0] = 1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CEnvelopeExtrema Extrema(m_pMap.get());
	EXPECT_FALSE(Extrema.GetExtrema(0).m_Available);
}

TEST_F(MapEnvelopeItems, UnderflowingIncomingTangentDisablesEarlyClipping)
{
	SItems Items;
	Items.m_vPoints[0].m_Curvetype = CURVETYPE_BEZIER;
	Items.m_vPoints[1].m_aValues[0] = INT_MIN;
	Items.m_vPoints[1].m_Bezier.m_aInTangentDeltaY[0] = -1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CEnvelopeExtrema Extrema(m_pMap.get());
	EXPECT_FALSE(Extrema.GetExtrema(0).m_Available);
}

TEST_F(MapEnvelopeItems, RepresentableBezierOffsetsRemainInClippingBounds)
{
	SItems Items;
	Items.m_vPoints[0].m_Curvetype = CURVETYPE_BEZIER;
	Items.m_vPoints[0].m_Bezier.m_aOutTangentDeltaY[0] = -2048;
	Items.m_vPoints[1].m_Bezier.m_aInTangentDeltaY[0] = 4096;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CEnvelopeExtrema Extrema(m_pMap.get());
	const auto &Value = Extrema.GetExtrema(0);
	ASSERT_TRUE(Value.m_Available);
	EXPECT_EQ(Value.m_Minima.x, -1024);
	EXPECT_EQ(Value.m_Maxima.x, 6144);
}

TEST_F(MapEnvelopeItems, RotationFlagStillReflectsTheThirdPositionChannel)
{
	SItems Items;
	Items.m_vPoints[1].m_aValues[2] = 10;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	CEnvelopeExtrema Extrema(m_pMap.get());
	EXPECT_TRUE(Extrema.GetExtrema(0).m_Rotating);
}
