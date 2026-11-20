#include <engine/client/client.h>

#include <gtest/gtest.h>

#include <array>

TEST(DemoSnapshotWriteGuard, ShortItemsAreHiddenWhileLaterCameraItemsRemainAvailable)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	const std::array aLocalData{0, 1250, 10, 20};
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_DDNETSPECTATORINFO, 0, aLocalData.data(), sizeof(int)));
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_DDNETSPECTATORINFO, 1, aLocalData.data(), sizeof(aLocalData)));

	CSnapshotBuffer Buffer;
	const int Size = Builder.Finish(&Buffer);
	ASSERT_GT(Size, 0);
	CSnapshot &Snapshot = *Buffer.AsSnapshot();
	ASSERT_TRUE(Snapshot.IsValid(Size));
	const void *pShort = Snapshot.FindItem(NETOBJTYPE_DDNETSPECTATORINFO, 0);
	const void *pLegacy = Snapshot.FindItem(NETOBJTYPE_DDNETSPECTATORINFO, 1);
	ASSERT_NE(pShort, nullptr);
	ASSERT_NE(pLegacy, nullptr);

	{
		const CDemoSnapshotWriteGuard Guard(Snapshot);
		EXPECT_EQ(Snapshot.FindItem(NETOBJTYPE_DDNETSPECTATORINFO, 0), nullptr);
		EXPECT_EQ(Snapshot.FindItem(NETOBJTYPE_DDNETSPECTATORINFO, 1), pLegacy);
	}

	EXPECT_EQ(Snapshot.FindItem(NETOBJTYPE_DDNETSPECTATORINFO, 0), pShort);
	EXPECT_EQ(Snapshot.FindItem(NETOBJTYPE_DDNETSPECTATORINFO, 1), pLegacy);
	EXPECT_TRUE(Snapshot.IsValid(Size));
}

TEST(DemoSnapshotWriteGuard, ShortProjectileIsSkippedWithoutHidingAnUnrelatedType)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	const int Data = 0;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_PROJECTILE, 0, &Data, sizeof(Data)));
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_SPECTATORCOUNT, 0, &Data, sizeof(Data)));
	const CNetObj_Projectile Projectile{};
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_PROJECTILE, 1, &Projectile, sizeof(Projectile)));

	CSnapshotBuffer Buffer;
	const int Size = Builder.Finish(&Buffer);
	ASSERT_GT(Size, 0);
	CSnapshot &Snapshot = *Buffer.AsSnapshot();
	ASSERT_TRUE(Snapshot.IsValid(Size));
	const void *pShort = Snapshot.FindItem(NETOBJTYPE_PROJECTILE, 0);
	ASSERT_NE(pShort, nullptr);

	{
		const CDemoSnapshotWriteGuard Guard(Snapshot);
		EXPECT_EQ(Snapshot.FindItem(NETOBJTYPE_PROJECTILE, 0), nullptr);
		EXPECT_NE(Snapshot.FindItem(NETOBJTYPE_SPECTATORCOUNT, 0), nullptr);
		EXPECT_NE(Snapshot.FindItem(NETOBJTYPE_PROJECTILE, 1), nullptr);
	}

	EXPECT_EQ(Snapshot.FindItem(NETOBJTYPE_PROJECTILE, 0), pShort);
}
