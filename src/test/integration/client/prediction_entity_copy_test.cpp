#include <game/client/prediction/copy_links.h>
#include <game/client/prediction/entity.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/prediction_world.h>

#include <new>

namespace
{
	class CProbeEntity : public CEntity
	{
	public:
		CProbeEntity(CGameWorld *pWorld, int Id) :
			CEntity(pWorld, CGameWorld::ENTTYPE_FLAG, vec2(10, 20), 5)
		{
			m_Id = Id;
			m_Number = 0;
			m_Layer = LAYER_GAME;
		}
		void MarkForDestroy() { m_MarkedForDestroy = true; }
		bool IsMarkedForDestroy() const { return m_MarkedForDestroy; }
		int ObjectType() const { return m_ObjType; }
	};

	struct CFailingMember
	{
		CFailingMember() = default;
		CFailingMember(const CFailingMember &) { throw std::bad_alloc(); }
	};

	class CFailingEntity : public CProbeEntity
	{
		CFailingMember m_Member;

	public:
		CFailingEntity(CGameWorld *pWorld, int Id) :
			CProbeEntity(pWorld, Id) {}
	};
}

TEST(PredictionEntityCopy, CopiesDataWithoutCopyingListOrFamilyIdentity)
{
	CPredictionTestWorld World;
	CProbeEntity Source(&World, 7);
	Source.m_Pos = vec2(-2.5f, 100.0f);
	Source.m_ProximityRadius = 3.5f;
	Source.m_Number = 4;
	Source.m_Layer = LAYER_SWITCH;
	Source.m_SnapTicks = 3;
	Source.m_DestroyTick = 50;
	Source.m_LastRenderTick = 60;
	Source.MarkForDestroy();
	CProbeEntity Copy(Source);
	EXPECT_EQ(Copy.GameWorld(), &World);
	EXPECT_EQ(Copy.ObjectType(), Source.ObjectType());
	EXPECT_EQ(Copy.GetId(), 7);
	EXPECT_EQ(Copy.m_Pos, Source.m_Pos);
	EXPECT_EQ(Copy.m_ProximityRadius, 3.5f);
	EXPECT_EQ(Copy.m_Number, 4);
	EXPECT_EQ(Copy.m_Layer, LAYER_SWITCH);
	EXPECT_EQ(Copy.m_SnapTicks, 3);
	EXPECT_EQ(Copy.m_DestroyTick, 50);
	EXPECT_EQ(Copy.m_LastRenderTick, 60);
	EXPECT_TRUE(Copy.IsMarkedForDestroy());
	EXPECT_EQ(Copy.TypeNext(), nullptr);
	EXPECT_EQ(Copy.TypePrev(), nullptr);
	EXPECT_EQ(Copy.m_pParent, nullptr);
	EXPECT_EQ(Copy.m_pChild, nullptr);
}

TEST(PredictionEntityCopy, DestroyingUnregisteredHeadCopyKeepsSourceAndNeighborLinked)
{
	CPredictionTestWorld World;
	CProbeEntity Source(&World, 1), Neighbor(&World, 2);
	World.InsertEntity(&Source);
	World.InsertEntity(&Neighbor, true);
	{
		CProbeEntity Copy(Source);
		EXPECT_EQ(Copy.TypeNext(), nullptr);
	}
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_FLAG), &Source);
	EXPECT_EQ(Source.TypeNext(), &Neighbor);
	EXPECT_EQ(Neighbor.TypePrev(), &Source);
}

TEST(PredictionEntityCopy, DestroyingUnregisteredMiddleCopyKeepsBothSourceNeighbors)
{
	CPredictionTestWorld World;
	CProbeEntity Before(&World, 1), Source(&World, 2), After(&World, 3);
	World.InsertEntity(&Before);
	World.InsertEntity(&Source, true);
	World.InsertEntity(&After, true);
	{
		CProbeEntity Copy(Source);
	}
	EXPECT_EQ(Before.TypeNext(), &Source);
	EXPECT_EQ(Source.TypePrev(), &Before);
	EXPECT_EQ(Source.TypeNext(), &After);
	EXPECT_EQ(After.TypePrev(), &Source);
}

TEST(PredictionEntityCopy, DerivedMemberCopyFailureDoesNotUnlinkSource)
{
	CPredictionTestWorld World;
	CFailingEntity Source(&World, 1);
	CProbeEntity Neighbor(&World, 2);
	World.InsertEntity(&Source);
	World.InsertEntity(&Neighbor, true);
	EXPECT_THROW({ CFailingEntity Copy(Source); }, std::bad_alloc);
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_FLAG), &Source);
	EXPECT_EQ(Source.TypeNext(), &Neighbor);
	EXPECT_EQ(Neighbor.TypePrev(), &Source);
	EXPECT_EQ(Source.GetId(), 1);
}

TEST(PredictionEntityCopy, CopyDestructionDoesNotDetachSourcesFamily)
{
	CPredictionTestWorld World;
	CProbeEntity Parent(&World, 1), Child(&World, 2), Source(&World, 3);
	World.InsertEntity(&Parent);
	World.InsertEntity(&Child, true);
	World.InsertEntity(&Source, true);
	QmCopyLinks::Attach(static_cast<CEntity &>(Parent), static_cast<CEntity &>(Source));
	QmCopyLinks::Attach(static_cast<CEntity &>(Source), static_cast<CEntity &>(Child));
	{
		CProbeEntity Copy(Source);
		EXPECT_EQ(Copy.m_pParent, nullptr);
		EXPECT_EQ(Copy.m_pChild, nullptr);
	}
	EXPECT_EQ(Parent.m_pChild, &Source);
	EXPECT_EQ(Source.m_pParent, &Parent);
	EXPECT_EQ(Source.m_pChild, &Child);
	EXPECT_EQ(Child.m_pParent, &Source);
}

TEST(PredictionEntityCopy, CopyCanBeRegisteredInAnotherWorldWithoutChangingSource)
{
	CPredictionTestWorld SourceWorld, CopyWorld;
	CProbeEntity Source(&SourceWorld, 1), Neighbor(&SourceWorld, 2);
	SourceWorld.InsertEntity(&Source);
	SourceWorld.InsertEntity(&Neighbor, true);
	{
		CProbeEntity Copy(Source);
		CopyWorld.InsertEntity(&Copy);
		EXPECT_EQ(Copy.GameWorld(), &CopyWorld);
		EXPECT_EQ(CopyWorld.FindFirst(CGameWorld::ENTTYPE_FLAG), &Copy);
		EXPECT_EQ(Source.TypeNext(), &Neighbor);
	}
	EXPECT_EQ(CopyWorld.FindFirst(CGameWorld::ENTTYPE_FLAG), nullptr);
	EXPECT_EQ(SourceWorld.FindFirst(CGameWorld::ENTTYPE_FLAG), &Source);
	EXPECT_EQ(Neighbor.TypePrev(), &Source);
}
