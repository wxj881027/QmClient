#include <base/mem.h>

#include <engine/shared/snapshot.h>
#include <engine/shared/uuid_manager.h>

#include <generated/protocol.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(Snapshot, CrcOneInt)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 4;
	Flag.m_Y = 0;
	Flag.m_Team = 0;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 4);
}

TEST(Snapshot, CrcTwoInts)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 1;
	Flag.m_Y = 1;
	Flag.m_Team = 0;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 2);
}

TEST(Snapshot, CrcBiggerInts)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 99999999;
	Flag.m_Y = 1;
	Flag.m_Team = 1;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 100000001);
}

TEST(Snapshot, CrcOverflow)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	CNetObj_Flag Flag;
	Flag.m_X = 0xFFFFFFFF;
	Flag.m_Y = 1;
	Flag.m_Team = 1;
	ASSERT_TRUE(Builder.NewItem(NETOBJTYPE_FLAG, 0, &Flag, sizeof(Flag)));

	CSnapshotBuffer Buffer;
	Builder.Finish(&Buffer);
	ASSERT_EQ(Buffer.AsSnapshot()->Crc(), 1);
}

TEST(Snapshot, RejectsUnalignedItemOffset)
{
	CSnapshotBuffer Buffer;
	mem_zero(&Buffer, sizeof(Buffer));
	int *pData = (int *)Buffer.m_aData;
	pData[0] = sizeof(CSnapshotItem) + sizeof(int32_t);
	pData[1] = 1;
	pData[2] = 1;

	EXPECT_FALSE(Buffer.AsSnapshot()->IsValid(sizeof(CSnapshot) + sizeof(int32_t) + pData[0]));
}

TEST(Snapshot, RejectsUnalignedItemSize)
{
	CSnapshotBuffer Buffer;
	mem_zero(&Buffer, sizeof(Buffer));
	int *pData = (int *)Buffer.m_aData;
	pData[0] = sizeof(CSnapshotItem) + 1;
	pData[1] = 1;
	pData[2] = 0;

	EXPECT_FALSE(Buffer.AsSnapshot()->IsValid(sizeof(CSnapshot) + sizeof(int32_t) + pData[0]));
}

TEST(Snapshot, ExtendedTypeFailsWhenUuidItemExhaustsCapacity)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	const int32_t Data = 0;
	for(int i = 0; i < CSnapshot::MAX_ITEMS - 1; i++)
	{
		ASSERT_TRUE(Builder.NewItem(1, i, &Data, sizeof(Data)));
	}

	// 扩展类型需要额外的 UUID 说明项。说明项占用最后一个槽位后，
	// 实际对象必须被拒绝，而不是越过 m_aOffsets 边界。
	EXPECT_FALSE(Builder.NewItem(OFFSET_UUID, 0, &Data, sizeof(Data)));
	EXPECT_EQ(Builder.FinishIfNoDroppedItems(nullptr), -1);
}

TEST(SnapshotDelta, LargeDeltaNeedsDoubleSizedBuffer)
{
	// 官方 be3e5e6a3：对象多、数据大时 delta 会比快照本身还大，
	// 所以缓冲要用两倍 CSnapshot::MAX_SIZE；容量不足时 CreateDelta 返回 -1，
	// 调用方必须按失败处理，而不是当成“零变化”。
	constexpr int ITEM_DATA_INTS = 8189; // 两个对象刚好把快照填满 64 KiB

	CSnapshotDelta Delta;
	Delta.SetStaticsize(0, 0);
	Delta.SetStaticsize(1, ITEM_DATA_INTS * sizeof(int32_t));

	CSnapshotBuilder Builder;
	Builder.Init();
	const std::vector<int32_t> vItemData(ITEM_DATA_INTS, 7);
	ASSERT_TRUE(Builder.NewItem(1, 0, vItemData.data(), (int)vItemData.size() * sizeof(int32_t)));
	ASSERT_TRUE(Builder.NewItem(1, 1, vItemData.data(), (int)vItemData.size() * sizeof(int32_t)));

	CSnapshotBuffer Snapshot;
	const int SnapshotSize = Builder.Finish(&Snapshot);
	ASSERT_EQ(SnapshotSize, (int)CSnapshot::MAX_SIZE);

	// 一倍缓冲装不下这个 delta，必须返回 -1 而不是写越界。
	std::vector<int32_t> vSmallBuffer(CSnapshot::MAX_SIZE / sizeof(int32_t), 0);
	EXPECT_EQ(Delta.CreateDelta(CSnapshot::EmptySnapshot(), Snapshot.AsSnapshot(), vSmallBuffer.data(), (int)(vSmallBuffer.size() * sizeof(int32_t))), -1);

	// 两倍缓冲足够，并且确实用到了超过一个快照上限的容量。
	CSnapshotDeltaBuffer BigBuffer;
	const int DeltaSize = Delta.CreateDelta(CSnapshot::EmptySnapshot(), Snapshot.AsSnapshot(), BigBuffer.Data(), sizeof(BigBuffer.m_aData));
	EXPECT_GT(DeltaSize, (int)CSnapshot::MAX_SIZE);
	EXPECT_LE(DeltaSize, (int)sizeof(BigBuffer.m_aData));
}

TEST(SnapshotDelta, MalformedDeltaIsRejected)
{
	// 官方 0198e362b 修的是 C++ 解包里的指针加法溢出；
	// 这里用畸形 delta 覆盖同类边界：越界和非法计数都必须返回失败。
	CSnapshotDelta Delta;
	Delta.SetStaticsize(0, 0);

	CSnapshotBuffer To;

	// 空 delta
	{
		EXPECT_LT(Delta.UnpackDelta(CSnapshot::EmptySnapshot(), &To, nullptr, 0), 0);
	}

	// 头部不完整
	{
		std::vector<int32_t> vTruncated = {0};
		EXPECT_LT(Delta.UnpackDelta(CSnapshot::EmptySnapshot(), &To, vTruncated.data(), (int)vTruncated.size() * sizeof(int32_t)), 0);
	}

	// 删除项数量超出剩余数据（官方溢出修复针对的形态）
	{
		std::vector<int32_t> vHugeDeleted = {std::numeric_limits<int32_t>::max(), 0, 0};
		EXPECT_LT(Delta.UnpackDelta(CSnapshot::EmptySnapshot(), &To, vHugeDeleted.data(), (int)vHugeDeleted.size() * sizeof(int32_t)), 0);
	}

	// 更新项或临时项为负：循环会当成零次而静默成功，必须在进入循环前拒绝。
	{
		std::vector<int32_t> vNegativeUpdate = {0, -1, 0};
		EXPECT_LT(Delta.UnpackDelta(CSnapshot::EmptySnapshot(), &To, vNegativeUpdate.data(), (int)vNegativeUpdate.size() * sizeof(int32_t)), 0);
		std::vector<int32_t> vNegativeTemp = {0, 0, -1};
		EXPECT_LT(Delta.UnpackDelta(CSnapshot::EmptySnapshot(), &To, vNegativeTemp.data(), (int)vNegativeTemp.size() * sizeof(int32_t)), 0);
	}
}

TEST(SnapshotDelta, CreateRejectsTooSmallOutput)
{
	// 目标容量连 delta 头部（三个 int）都放不下时，必须在写入前失败。
	CSnapshotDelta Delta;
	char aBuffer[sizeof(int32_t)];
	EXPECT_EQ(Delta.CreateDelta(CSnapshot::EmptySnapshot(), CSnapshot::EmptySnapshot(), aBuffer, sizeof(aBuffer)), -1);
}

TEST(Snapshot, StorageGet)
{
	CSnapshotStorage Storage;
	const char aData[8] = {};
	Storage.Add(10, 1000, 1, aData, 0, nullptr);
	Storage.Add(20, 2000, 2, aData, 0, nullptr);
	Storage.Add(30, 3000, 3, aData, 0, nullptr);
	Storage.Add(40, 4000, 4, aData, 0, nullptr);

	int64_t Tagtime = -1;
	EXPECT_EQ(Storage.Get(40, &Tagtime, nullptr, nullptr), 4);
	EXPECT_EQ(Tagtime, 4000);
	EXPECT_EQ(Storage.Get(10, &Tagtime, nullptr, nullptr), 1);
	EXPECT_EQ(Tagtime, 1000);
	EXPECT_EQ(Storage.Get(30, &Tagtime, nullptr, nullptr), 3);
	EXPECT_EQ(Tagtime, 3000);
	EXPECT_EQ(Storage.Get(50, nullptr, nullptr, nullptr), -1);
	EXPECT_EQ(Storage.Get(5, nullptr, nullptr, nullptr), -1);
	EXPECT_EQ(Storage.Get(25, nullptr, nullptr, nullptr), -1);
}
