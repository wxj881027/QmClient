#include <base/str.h>

#include <engine/map.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
	// 只替换地图存储边界，解析、动画和碰撞均由生产模块执行。
	class CQuadTestMap final : public IMap
	{
		struct SItem
		{
			int m_Type;
			int m_Id;
			std::vector<int> m_vWords;
		};
		std::vector<SItem> m_vItems;
		std::vector<std::vector<int>> m_vData;

	public:
		template<typename T>
		void AddItem(int Type, int Id, const T &Item)
		{
			std::vector<int> vWords(sizeof(T) / sizeof(int));
			std::memcpy(vWords.data(), &Item, sizeof(T));
			m_vItems.push_back({Type, Id, std::move(vWords)});
		}
		template<typename T>
		int AddData(const std::vector<T> &vData)
		{
			std::vector<int> vWords(vData.size() * sizeof(T) / sizeof(int));
			if(!vData.empty())
				std::memcpy(vWords.data(), vData.data(), vData.size() * sizeof(T));
			m_vData.push_back(std::move(vWords));
			return static_cast<int>(m_vData.size()) - 1;
		}
		void AddPoints(const std::vector<CEnvPoint> &vPoints)
		{
			std::vector<int> vWords(vPoints.size() * sizeof(CEnvPoint) / sizeof(int));
			std::memcpy(vWords.data(), vPoints.data(), vPoints.size() * sizeof(CEnvPoint));
			m_vItems.push_back({MAPITEMTYPE_ENVPOINTS, 0, std::move(vWords)});
		}
		int GetDataSize(int Index) const override { return static_cast<int>(m_vData.at(Index).size() * sizeof(int)); }
		void *GetData(int Index) override { return m_vData.at(Index).data(); }
		void *GetDataSwapped(int Index) override { return GetData(Index); }
		const char *GetDataString(int) override { return ""; }
		void UnloadData(int) override {}
		int NumData() const override { return static_cast<int>(m_vData.size()); }
		int GetItemSize(int Index) override { return static_cast<int>(m_vItems.at(Index).m_vWords.size() * sizeof(int)); }
		void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr) override
		{
			auto &Item = m_vItems.at(Index);
			if(pType)
				*pType = Item.m_Type;
			if(pId)
				*pId = Item.m_Id;
			return Item.m_vWords.data();
		}
		void GetType(int Type, int *pStart, int *pNum) override
		{
			*pStart = *pNum = 0;
			for(size_t i = 0; i < m_vItems.size(); ++i)
				if(m_vItems[i].m_Type == Type)
				{
					if(*pNum == 0)
						*pStart = static_cast<int>(i);
					++*pNum;
				}
		}
		int FindItemIndex(int Type, int Id) override
		{
			for(size_t i = 0; i < m_vItems.size(); ++i)
				if(m_vItems[i].m_Type == Type && m_vItems[i].m_Id == Id)
					return static_cast<int>(i);
			return -1;
		}
		void *FindItem(int Type, int Id) override
		{
			const int Index = FindItemIndex(Type, Id);
			return Index >= 0 ? GetItem(Index) : nullptr;
		}
		int NumItems() const override { return static_cast<int>(m_vItems.size()); }
	};

	CQuad MakeQuad(vec2 Min = vec2(32, 32), vec2 Max = vec2(64, 64), int PositionEnvelope = -1, int ColorEnvelope = -1)
	{
		CQuad Quad{};
		const std::array<vec2, 5> aPoints = {Min, vec2(Max.x, Min.y), vec2(Min.x, Max.y), Max, (Min + Max) * 0.5f};
		for(size_t i = 0; i < aPoints.size(); ++i)
			Quad.m_aPoints[i] = ivec2(f2fx(aPoints[i].x), f2fx(aPoints[i].y));
		for(auto &Color : Quad.m_aColors)
			Color = CColor(255, 255, 255, 255);
		Quad.m_PosEnv = PositionEnvelope;
		Quad.m_ColorEnv = ColorEnvelope;
		return Quad;
	}

	CEnvPoint MakePoint(int Milliseconds, std::array<float, 4> aValues, int Curve = CURVETYPE_LINEAR)
	{
		CEnvPoint Point{};
		Point.m_Time = CFixedTime(Milliseconds);
		Point.m_Curvetype = Curve;
		for(size_t i = 0; i < aValues.size(); ++i)
			Point.m_aValues[i] = f2fx(aValues[i]);
		return Point;
	}

	struct SQuadFixture
	{
		struct SLayer
		{
			std::string m_Name;
			std::vector<CQuad> m_vQuads;
		};
		CQuadTestMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;
		CMapItemGroup m_QuadGroup{};
		std::vector<SLayer> m_vLayers;
		std::vector<CMapItemEnvelope> m_vEnvelopes;
		std::vector<CEnvPoint> m_vPoints;

		SQuadFixture()
		{
			m_QuadGroup.m_Version = 3;
			m_QuadGroup.m_ParallaxX = m_QuadGroup.m_ParallaxY = 100;
		}
		int AddEnvelope(int Channels, std::initializer_list<CEnvPoint> Points)
		{
			CMapItemEnvelope Envelope{};
			Envelope.m_Version = 2;
			Envelope.m_Channels = Channels;
			Envelope.m_StartPoint = static_cast<int>(m_vPoints.size());
			Envelope.m_NumPoints = static_cast<int>(Points.size());
			m_vEnvelopes.push_back(Envelope);
			m_vPoints.insert(m_vPoints.end(), Points);
			return static_cast<int>(m_vEnvelopes.size()) - 1;
		}
		void Init(bool Enable = true)
		{
			CMapItemGroup GameGroup{};
			GameGroup.m_Version = 3;
			GameGroup.m_NumLayers = 1;
			m_Map.AddItem(MAPITEMTYPE_GROUP, 0, GameGroup);
			m_QuadGroup.m_StartLayer = 1;
			m_QuadGroup.m_NumLayers = static_cast<int>(m_vLayers.size());
			m_Map.AddItem(MAPITEMTYPE_GROUP, 1, m_QuadGroup);
			CMapItemLayerTilemap Game{};
			Game.m_Version = 3;
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Width = Game.m_Height = 32;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Data = m_Map.AddData(std::vector<CTile>(32 * 32));
			m_Map.AddItem(MAPITEMTYPE_LAYER, 0, Game);
			for(size_t i = 0; i < m_vLayers.size(); ++i)
			{
				const auto &Layer = m_vLayers[i];
				CMapItemLayerQuads Quads{};
				Quads.m_Version = 2;
				Quads.m_Layer.m_Type = LAYERTYPE_QUADS;
				Quads.m_NumQuads = static_cast<int>(Layer.m_vQuads.size());
				Quads.m_Data = m_Map.AddData(Layer.m_vQuads);
				StrToInts(Quads.m_aName, std::size(Quads.m_aName), Layer.m_Name.c_str());
				m_Map.AddItem(MAPITEMTYPE_LAYER, static_cast<int>(i) + 1, Quads);
			}
			for(size_t i = 0; i < m_vEnvelopes.size(); ++i)
				m_Map.AddItem(MAPITEMTYPE_ENVELOPE, static_cast<int>(i), m_vEnvelopes[i]);
			if(!m_vPoints.empty())
			{
				// 包络点是一个地图 item，保留真实连续布局。
				m_Map.AddPoints(m_vPoints);
			}
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			m_Collision.SetQmAxiomQuadCollision(Enable);
		}
	};
}

TEST(QmAxiomQuadCollision, DisabledCollisionKeepsDecorativeQuadsNonSolid)
{
	SQuadFixture F;
	F.m_vLayers.push_back({"QHook", {MakeQuad()}});
	F.Init(false);
	EXPECT_FALSE(F.m_Collision.CheckPoint(48, 48));
	F.m_Collision.SetQmAxiomQuadCollision(true);
	EXPECT_TRUE(F.m_Collision.CheckPoint(48, 48));
	F.m_Collision.SetQmAxiomQuadCollision(false);
	EXPECT_FALSE(F.m_Collision.CheckPoint(48, 48));
}

TEST(QmAxiomQuadCollision, TickUsesRoundStartAndCanRewind)
{
	SQuadFixture F;
	const int Env = F.AddEnvelope(3, {MakePoint(0, {0, 0, 0, 0}), MakePoint(1000, {200, 0, 0, 0})});
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(0, 32), vec2(20, 64), Env)}});
	F.Init();
	F.m_Collision.SetQmAxiomQuadRoundStartTick(1000);
	F.m_Collision.SetQmAxiomQuadTick(1025, 50);
	EXPECT_TRUE(F.m_Collision.CheckPoint(110, 48));
	EXPECT_FALSE(F.m_Collision.CheckPoint(10, 48));
	F.m_Collision.SetQmAxiomQuadTick(1000, 50);
	EXPECT_TRUE(F.m_Collision.CheckPoint(10, 48));
	EXPECT_FALSE(F.m_Collision.CheckPoint(110, 48));
}

TEST(QmAxiomQuadCollision, NestedPredictionRestoresCollisionTime)
{
	SQuadFixture F;
	const int Env = F.AddEnvelope(3, {MakePoint(0, {0, 0, 0, 0}), MakePoint(1000, {200, 0, 0, 0})});
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(0, 32), vec2(20, 64), Env)}});
	F.Init();
	F.m_Collision.SetQmAxiomQuadTime(0.25);
	{
		CQmAxiomQuadTimeScope Outer(F.m_Collision, 30, 50);
		EXPECT_TRUE(F.m_Collision.CheckPoint(130, 48));
		{
			CQmAxiomQuadTimeScope Inner(F.m_Collision, 0, 50);
			EXPECT_TRUE(F.m_Collision.CheckPoint(10, 48));
		}
		EXPECT_TRUE(F.m_Collision.CheckPoint(130, 48));
	}
	EXPECT_DOUBLE_EQ(F.m_Collision.QmAxiomQuadTime(), 0.25);
	EXPECT_TRUE(F.m_Collision.CheckPoint(60, 48));
}

TEST(QmAxiomQuadCollision, FreezeAndUnfreezeRetainMapOrder)
{
	SQuadFixture F;
	F.m_vLayers = {{"QFr", {MakeQuad()}}, {"QUnFr", {MakeQuad()}}};
	F.Init();
	const std::vector<int> Expected{TILE_FREEZE, TILE_UNFREEZE};
	EXPECT_EQ(F.m_Collision.GetQmAxiomQuadTiles(vec2(48, 48)), Expected);
	EXPECT_FALSE(F.m_Collision.CheckPoint(48, 48));
}

TEST(QmAxiomQuadCollision, MovingFreezeRegionLeavesPreviousPosition)
{
	SQuadFixture F;
	const int Env = F.AddEnvelope(3, {MakePoint(0, {0, 0, 0, 0}), MakePoint(1000, {200, 0, 0, 0})});
	F.m_vLayers.push_back({"QFr", {MakeQuad(vec2(0, 32), vec2(20, 64), Env)}});
	F.Init();
	F.m_Collision.SetQmAxiomQuadTime(0.5);
	EXPECT_TRUE(F.m_Collision.HasQmAxiomQuadTile(vec2(110, 48), TILE_FREEZE));
	EXPECT_FALSE(F.m_Collision.HasQmAxiomQuadTile(vec2(10, 48), TILE_FREEZE));
	EXPECT_FALSE(F.m_Collision.CheckPoint(110, 48));
}

TEST(QmAxiomQuadCollision, ThinNoHookQuadStopsHookAndWeaponRays)
{
	SQuadFixture F;
	F.m_vLayers.push_back({"QStopa", {MakeQuad(vec2(32, 32), vec2(32.1f, 64))}});
	F.Init();
	vec2 Hit, Before;
	int TeleNr = 0;
	EXPECT_EQ(F.m_Collision.IntersectLineTeleHook(vec2(0, 48), vec2(96, 48), &Hit, &Before, &TeleNr), TILE_NOHOOK);
	EXPECT_NEAR(Hit.x, 32, 0.001f);
	EXPECT_LT(Before.x, Hit.x);
	EXPECT_EQ(F.m_Collision.IntersectLineTeleWeapon(vec2(0, 48), vec2(96, 48), &Hit, &Before, &TeleNr), TILE_NOHOOK);
	EXPECT_EQ(F.m_Collision.IntersectNoLaser(vec2(0, 48), vec2(96, 48), &Hit, &Before), TILE_NOHOOK);
}

TEST(QmAxiomQuadCollision, SolidQuadIsFoundUnderOverlappingFreeze)
{
	SQuadFixture F;
	F.m_vLayers = {{"QFr", {MakeQuad()}}, {"QHook", {MakeQuad()}}};
	F.Init();
	vec2 Hit;
	EXPECT_TRUE(F.m_Collision.CheckPoint(48, 48));
	EXPECT_EQ(F.m_Collision.IntersectLineTeleHook(vec2(0, 48), vec2(96, 48), &Hit, nullptr), TILE_SOLID);
}

TEST(QmAxiomQuadCollision, FullyContainedCharacterIsPushedOutsideSolid)
{
	SQuadFixture F;
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(32, 32), vec2(160, 160))}});
	F.Init();
	vec2 Pos(96, 96), Vel(0, 0);
	F.m_Collision.MoveBox(&Pos, &Vel, vec2(28, 28), vec2(0, 0));
	EXPECT_FALSE(F.m_Collision.TestBox(Pos, vec2(28, 28)));
	EXPECT_TRUE(std::isfinite(Pos.x));
	EXPECT_TRUE(std::isfinite(Pos.y));
}

TEST(QmAxiomQuadCollision, ClippingKeepsOnlyVisibleCollisionArea)
{
	SQuadFixture F;
	F.m_QuadGroup.m_UseClipping = 1;
	F.m_QuadGroup.m_ClipX = 32;
	F.m_QuadGroup.m_ClipY = 32;
	F.m_QuadGroup.m_ClipW = 16;
	F.m_QuadGroup.m_ClipH = 32;
	F.m_vLayers.push_back({"QHook", {MakeQuad()}});
	F.Init();
	EXPECT_TRUE(F.m_Collision.CheckPoint(40, 48));
	EXPECT_FALSE(F.m_Collision.CheckPoint(56, 48));
	EXPECT_TRUE(F.m_Collision.TestBox(vec2(49, 48), vec2(8, 8)));
	EXPECT_FALSE(F.m_Collision.TestBox(vec2(56, 48), vec2(8, 8)));
}

TEST(QmAxiomQuadCollision, GroupOffsetsAndParallaxMatchAxiomServerCoordinates)
{
	SQuadFixture F;
	F.m_QuadGroup.m_OffsetX = 300;
	F.m_QuadGroup.m_OffsetY = -200;
	F.m_QuadGroup.m_ParallaxX = 50;
	F.m_vLayers.push_back({"QHook", {MakeQuad()}});
	F.Init();
	EXPECT_TRUE(F.m_Collision.CheckPoint(48, 48));
	EXPECT_FALSE(F.m_Collision.CheckPoint(348, 48));
}

TEST(QmAxiomQuadCollision, SmallQuadFarFromOriginKeepsCollision)
{
	SQuadFixture F;
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(1000000, 1000000), vec2(1000032, 1000032))}});
	F.Init();
	EXPECT_TRUE(F.m_Collision.CheckPoint(1000016, 1000016));
	EXPECT_TRUE(F.m_Collision.TestBox(vec2(1000016, 1000016), vec2(8, 8)));
}

TEST(QmAxiomQuadCollision, HookAnchorFollowsRotationAndRewind)
{
	SQuadFixture F;
	const int Env = F.AddEnvelope(3, {MakePoint(0, {0, 0, 0, 0}), MakePoint(1000, {100, 0, 180, 0})});
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(32, 32), vec2(64, 64), Env)}});
	F.Init();
	int Quad = -1;
	vec2 Local, World;
	ASSERT_TRUE(F.m_Collision.FindQmAxiomHookAnchor(vec2(64, 48), &Quad, &Local));
	F.m_Collision.SetQmAxiomQuadTime(0.5);
	ASSERT_TRUE(F.m_Collision.GetQmAxiomHookAnchor(Quad, Local, &World));
	EXPECT_NEAR(World.x, 98, 0.001f);
	EXPECT_NEAR(World.y, 64, 0.001f);
	F.m_Collision.SetQmAxiomQuadTime(0);
	ASSERT_TRUE(F.m_Collision.GetQmAxiomHookAnchor(Quad, Local, &World));
	EXPECT_NEAR(World.x, 64, 0.001f);
	EXPECT_NEAR(World.y, 48, 0.001f);
}

TEST(QmAxiomQuadCollision, InvisibleQuadReleasesHookAnchor)
{
	SQuadFixture F;
	const int Color = F.AddEnvelope(4, {MakePoint(0, {1, 1, 1, 1}, CURVETYPE_STEP), MakePoint(500, {1, 1, 1, 0}, CURVETYPE_STEP), MakePoint(1000, {1, 1, 1, 1})});
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(32, 32), vec2(64, 64), -1, Color)}});
	F.Init();
	int Quad = -1;
	vec2 Local, World;
	ASSERT_TRUE(F.m_Collision.FindQmAxiomHookAnchor(vec2(48, 48), &Quad, &Local));
	F.m_Collision.SetQmAxiomQuadTime(0.75);
	EXPECT_FALSE(F.m_Collision.CheckPoint(48, 48));
	EXPECT_FALSE(F.m_Collision.GetQmAxiomHookAnchor(Quad, Local, &World));
}

TEST(QmAxiomQuadCollision, MapReloadClearsEnabledQuadsAndTime)
{
	SQuadFixture F;
	F.m_vLayers.push_back({"QHook", {MakeQuad()}});
	F.Init();
	F.m_Collision.SetQmAxiomQuadTime(4);
	F.m_Collision.Init(&F.m_Layers);
	EXPECT_FALSE(F.m_Collision.QmAxiomQuadCollisionEnabled());
	EXPECT_DOUBLE_EQ(F.m_Collision.QmAxiomQuadTime(), 0);
	EXPECT_FALSE(F.m_Collision.CheckPoint(48, 48));
	F.m_Collision.SetQmAxiomQuadCollision(true);
	EXPECT_TRUE(F.m_Collision.CheckPoint(48, 48));
}

TEST(QmAxiomQuadCollision, InvalidTimeAndDegenerateQuadDoNotCorruptQueries)
{
	SQuadFixture F;
	F.m_vLayers = {{"QHook", {MakeQuad(), MakeQuad(vec2(80, 80), vec2(80, 80))}}};
	F.Init();
	F.m_Collision.SetQmAxiomQuadTime(std::numeric_limits<double>::quiet_NaN());
	EXPECT_DOUBLE_EQ(F.m_Collision.QmAxiomQuadTime(), 0);
	EXPECT_TRUE(F.m_Collision.CheckPoint(48, 48));
	EXPECT_FALSE(F.m_Collision.CheckPoint(80, 80));
}

TEST(QmAxiomQuadCollision, TuneZoneIsReadFromQuadAndRangeChecked)
{
	SQuadFixture F;
	CQuad Quad = MakeQuad();
	Quad.m_ColorEnvOffset = 7;
	F.m_vLayers.push_back({"QTune", {Quad}});
	F.Init();
	EXPECT_EQ(F.m_Collision.GetQmAxiomQuadTuneZone(vec2(48, 48)), 7);
	EXPECT_EQ(F.m_Collision.GetQmAxiomQuadTuneZone(vec2(80, 80)), 0);
}

TEST(QmAxiomQuadCollision, InvalidQuadCountIsIgnored)
{
	SQuadFixture F;
	F.m_vLayers.push_back({"QHook", {MakeQuad()}});
	F.Init(false);
	auto *pLayer = static_cast<CMapItemLayerQuads *>(F.m_Map.FindItem(MAPITEMTYPE_LAYER, 1));
	ASSERT_NE(pLayer, nullptr);
	pLayer->m_NumQuads = std::numeric_limits<int>::max();
	F.m_Collision.SetQmAxiomQuadCollision(true);
	EXPECT_FALSE(F.m_Collision.CheckPoint(48, 48));
}

TEST(QmAxiomQuadCollision, StaticTerrainBeforeQuadRetainsRayPriority)
{
	SQuadFixture F;
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(64, 32), vec2(96, 64))}});
	F.Init();
	F.m_Collision.SetCollisionAt(0, 48, TILE_NOHOOK);
	vec2 Hit;
	int TeleNr = 77;
	EXPECT_EQ(F.m_Collision.IntersectLineTeleHook(vec2(0, 48), vec2(128, 48), &Hit, nullptr, &TeleNr), TILE_NOHOOK);
	EXPECT_EQ(Hit, vec2(0, 48));
	EXPECT_EQ(TeleNr, 0);
}

TEST(QmAxiomQuadCollision, OutOfRangeQuadTuneZoneFallsBackToGlobalTuning)
{
	SQuadFixture F;
	CQuad Quad = MakeQuad();
	Quad.m_ColorEnvOffset = TuneZone::NUM;
	F.m_vLayers.push_back({"QTune", {Quad}});
	F.Init();
	EXPECT_EQ(F.m_Collision.GetQmAxiomQuadTuneZone(vec2(48, 48)), 0);
}

TEST(QmAxiomQuadCollision, NegativeEnvelopeTimeWrapsToSameGeometry)
{
	SQuadFixture F;
	const int Env = F.AddEnvelope(3, {MakePoint(0, {0, 0, 0, 0}), MakePoint(1000, {200, 0, 0, 0})});
	F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(0, 32), vec2(20, 64), Env)}});
	F.Init();
	F.m_Collision.SetQmAxiomQuadTime(-0.25);
	EXPECT_TRUE(F.m_Collision.CheckPoint(160, 48));
	F.m_Collision.SetQmAxiomQuadTime(0.75);
	EXPECT_TRUE(F.m_Collision.CheckPoint(160, 48));
}

TEST(QmAxiomQuadCollision, EnvelopeCurvesPositionCollisionAtQuarterCycle)
{
	struct SCurve
	{
		int m_Type;
		float m_Offset;
	};
	const SCurve aCurves[] = {{CURVETYPE_LINEAR, 50}, {CURVETYPE_STEP, 0}, {CURVETYPE_SLOW, 3.125f}, {CURVETYPE_FAST, 115.625f}, {CURVETYPE_SMOOTH, 31.25f}};
	for(const auto &Curve : aCurves)
	{
		SCOPED_TRACE(Curve.m_Type);
		SQuadFixture F;
		const int Env = F.AddEnvelope(3, {MakePoint(0, {0, 0, 0, 0}, Curve.m_Type), MakePoint(1000, {200, 0, 0, 0})});
		F.m_vLayers.push_back({"QHook", {MakeQuad(vec2(0, 32), vec2(2, 64), Env)}});
		F.Init();
		F.m_Collision.SetQmAxiomQuadTime(0.25);
		EXPECT_TRUE(F.m_Collision.CheckPoint(Curve.m_Offset + 1, 48));
		EXPECT_FALSE(F.m_Collision.CheckPoint(Curve.m_Offset + 5, 48));
	}
}
