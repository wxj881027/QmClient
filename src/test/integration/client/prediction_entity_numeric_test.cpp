#include <generated/protocol.h>

#include <game/client/laser_data.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/prediction_world.h>

#include <cmath>
#include <limits>
#include <utility>

namespace
{
	CProjectileData ProjectileData(int StartTick, int Type = WEAPON_GUN)
	{
		CProjectileData Data{};
		Data.m_StartPos = vec2(0, 0);
		Data.m_StartVel = vec2(1, 0);
		Data.m_StartTick = StartTick;
		Data.m_Type = Type;
		return Data;
	}

	void AddSpentLaser(CPredictionTestWorld &World, int StartTick)
	{
		World.TuningList()->m_LaserReach = -1.0f;
		CLaserData Data{};
		Data.m_From = vec2(0, 0);
		Data.m_To = vec2(1, 0);
		Data.m_Type = LASERTYPE_RIFLE;
		Data.m_StartTick = StartTick;
		World.InsertEntity(new CLaser(&World, 7, &Data));
	}
}

TEST(PredictionEntityNumeric, PastProjectileLifetimeDoesNotWrapIntoFuture)
{
	CPredictionTestWorld World;
	World.m_GameTick = 200;
	World.TuningList()->m_GunLifetime = 2.0f;
	const auto Data = ProjectileData(std::numeric_limits<int>::min());
	CProjectile Projectile(&World, 7, &Data);
	EXPECT_EQ(Projectile.GetLifeSpan(), -2147483748LL);
	EXPECT_EQ(Projectile.GetData().m_StartTick, std::numeric_limits<int>::min());
}

TEST(PredictionEntityNumeric, FutureProjectileLifetimeDoesNotWrapNegative)
{
	CPredictionTestWorld World;
	World.m_GameTick = -200;
	World.TuningList()->m_GunLifetime = 2.0f;
	const auto Data = ProjectileData(std::numeric_limits<int>::max());
	CProjectile Projectile(&World, 7, &Data);
	EXPECT_EQ(Projectile.GetLifeSpan(), 2147483947LL);
}

TEST(PredictionEntityNumeric, OrdinaryProjectileLifetimesKeepNegativeSentinels)
{
	CPredictionTestWorld World;
	World.TuningList()->m_GunLifetime = 2.0f;
	const auto Data = ProjectileData(0);
	for(const auto &[Tick, Expected] : {std::pair{99, 1}, {100, 0}, {101, -1}, {102, -2}})
	{
		SCOPED_TRACE(Tick);
		World.m_GameTick = Tick;
		CProjectile Projectile(&World, 7, &Data);
		EXPECT_EQ(Projectile.GetLifeSpan(), Expected);
	}
}

TEST(PredictionEntityNumeric, MatchingDDRaceShotgunRefreshKeepsWideLifetime)
{
	CPredictionTestWorld World;
	World.m_GameTick = 1000;
	World.m_WorldConfig.m_IsDDRace = true;
	World.m_WorldConfig.m_PredictWeapons = true;
	const auto Data = ProjectileData(std::numeric_limits<int>::min(), WEAPON_SHOTGUN);
	auto *pProjectile = new CProjectile(&World, 7, &Data);
	World.InsertEntity(pProjectile);
	ASSERT_EQ(pProjectile->GetLifeSpan(), -2147483648LL);
	World.m_GameTick = 2000;
	CNetObj_Projectile Net{};
	Net.m_Type = WEAPON_SHOTGUN;
	Net.m_VelX = 100;
	Net.m_StartTick = std::numeric_limits<int>::min();
	World.NetObjAdd(7, NETOBJTYPE_PROJECTILE, &Net, nullptr);
	ASSERT_EQ(World.GetEntity(7, CGameWorld::ENTTYPE_PROJECTILE), pProjectile);
	EXPECT_EQ(pProjectile->GetLifeSpan(), -2147484648LL);
	EXPECT_EQ(pProjectile->m_SnapTicks, 0);
}

TEST(PredictionEntityNumeric, WorldCopiesPreserveWideLifetimeAndWireStartTick)
{
	CPredictionTestWorld Source, Linked, Clean;
	Source.m_GameTick = -200;
	Source.TuningList()->m_GunLifetime = 2.0f;
	const auto Data = ProjectileData(std::numeric_limits<int>::max());
	Source.InsertEntity(new CProjectile(&Source, 7, &Data));
	ASSERT_TRUE(Linked.CopyWorld(&Source));
	ASSERT_TRUE(Clean.CopyWorldClean(&Source));
	for(CGameWorld *pWorld : {&Linked, &Clean})
	{
		auto *pProjectile = static_cast<CProjectile *>(pWorld->GetEntity(7, CGameWorld::ENTTYPE_PROJECTILE));
		ASSERT_NE(pProjectile, nullptr);
		EXPECT_EQ(pProjectile->GetLifeSpan(), 2147483947LL);
		EXPECT_EQ(pProjectile->GetData().m_StartTick, std::numeric_limits<int>::max());
	}
}

TEST(PredictionEntityNumeric, ProjectileComputedOutOfRangePointIsRemovedBeforeMapQuery)
{
	CCollision Collision;
	CPredictionTestWorld World;
	World.Init(&Collision, World.TuningList(), nullptr);
	World.m_GameTick = 1;
	World.TuningList()->m_GunSpeed = 6400.0f;
	World.TuningList()->m_GunCurvature = 0.0f;
	auto Data = ProjectileData(0);
	Data.m_StartPos = vec2(std::nextafter(2147483648.0f, 0.0f), 0);
	auto *pProjectile = new CProjectile(&World, 7, &Data);
	World.InsertEntity(pProjectile);
	const int64_t LifeSpan = pProjectile->GetLifeSpan();
	// 默认碰撞对象没有地图；被测路径必须在访问瓦片之前拒绝几何。
	pProjectile->Tick();
	EXPECT_EQ(pProjectile->GetLifeSpan(), LifeSpan);
	World.Tick();
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), nullptr);
}

TEST(PredictionEntityNumeric, LayerClipRejectsInvalidNumericPositions)
{
	CCollision Collision;
	CPredictionTestWorld World;
	World.Init(&Collision, World.TuningList(), nullptr);
	CEntity Entity(&World, CGameWorld::ENTTYPE_FLAG);
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	for(const vec2 Pos : {vec2(Nan, 0), vec2(0, Nan), vec2(Infinity, 0), vec2(0, -Infinity), vec2(2147483648.0f, 0)})
		EXPECT_TRUE(Entity.GameLayerClipped(Pos));
}

TEST(PredictionEntityNumeric, LayerClipKeepsExistingTwoHundredTileMargin)
{
	CCollision Collision;
	CPredictionTestWorld World;
	World.Init(&Collision, World.TuningList(), nullptr);
	CEntity Entity(&World, CGameWorld::ENTTYPE_FLAG);
	for(const vec2 Pos : {vec2(6431, 0), vec2(-6431, 0), vec2(0, 6431), vec2(0, -6431)})
		EXPECT_FALSE(Entity.GameLayerClipped(Pos));
	for(const vec2 Pos : {vec2(6432, 0), vec2(-6432, 0), vec2(0, 6432), vec2(0, -6432), vec2(-2147483648.0f, 0), vec2(2147483520.0f, 0)})
		EXPECT_TRUE(Entity.GameLayerClipped(Pos));
}

TEST(PredictionEntityNumeric, BaseConstructionInitializesGameLayerForStackCopies)
{
	CPredictionTestWorld World;
	CEntity Source(&World, CGameWorld::ENTTYPE_FLAG);
	CEntity Copy(Source);
	EXPECT_EQ(Source.m_Number, 0);
	EXPECT_EQ(Source.m_Layer, LAYER_GAME);
	EXPECT_EQ(Copy.m_Number, Source.m_Number);
	EXPECT_EQ(Copy.m_Layer, Source.m_Layer);
}

TEST(PredictionEntityNumeric, NonVanillaLaserDelayHandlesOppositeTickExtremes)
{
	CPredictionTestWorld World;
	World.m_GameTick = std::numeric_limits<int>::max();
	AddSpentLaser(World, std::numeric_limits<int>::min());
	World.Tick();
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_LASER), nullptr);
}

TEST(PredictionEntityNumeric, NonVanillaLaserDelayKeepsStrictFractionalThreshold)
{
	CPredictionTestWorld World;
	World.TuningList()->m_LaserBounceDelay = 150.0f;
	World.m_GameTick = std::numeric_limits<int>::max() - 1;
	AddSpentLaser(World, std::numeric_limits<int>::max() - 8);
	World.Tick();
	ASSERT_NE(World.FindFirst(CGameWorld::ENTTYPE_LASER), nullptr);
	World.m_GameTick = std::numeric_limits<int>::max();
	World.Tick();
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_LASER), nullptr);
}

TEST(PredictionEntityNumeric, VanillaLaserKeepsExistingUpperTickFloatRounding)
{
	CPredictionTestWorld World;
	World.m_WorldConfig.m_IsVanilla = true;
	World.TuningList()->m_LaserBounceDelay = 0.0f;
	World.m_GameTick = std::numeric_limits<int>::max();
	AddSpentLaser(World, std::numeric_limits<int>::max() - 1);
	World.Tick();
	EXPECT_NE(World.FindFirst(CGameWorld::ENTTYPE_LASER), nullptr);
}

TEST(PredictionEntityNumeric, LocalLaserComputedOutOfRangeEndpointIsRemovedBeforeMapQuery)
{
	CCollision Collision;
	CPredictionTestWorld World;
	World.Init(&Collision, World.TuningList(), nullptr);
	const vec2 Start(std::nextafter(2147483648.0f, 0.0f), 0);
	auto *pLaser = new CLaser(&World, Start, vec2(1, 0), 128.0f, -1, WEAPON_LASER);
	EXPECT_EQ(pLaser->GetPos(), Start);
	World.Tick();
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_LASER), nullptr);
}
