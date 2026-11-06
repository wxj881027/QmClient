#include <base/system.h>

#include <engine/shared/datafile.h>
#include <engine/shared/packer.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/uuid_manager.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <limits>
#include <vector>

TEST(UuidPacket, RegisteredIdWritesExactUuid)
{
	CUuidManager Manager;
	Manager.RegisterName(OFFSET_UUID, "packet@uuid-boundaries");
	CPacker Packer;
	Packer.Reset();
	ASSERT_TRUE(Manager.PackUuid(OFFSET_UUID, &Packer));
	const CUuid Expected = CalculateUuid("packet@uuid-boundaries");
	ASSERT_EQ(Packer.Size(), sizeof(Expected));
	EXPECT_EQ(mem_comp(Packer.Data(), &Expected, sizeof(Expected)), 0);
}

TEST(UuidPacket, InvalidIdReturnsFailureWithoutChangingExistingPrefix)
{
	CUuidManager Manager;
	Manager.RegisterName(OFFSET_UUID, "packet@uuid-boundaries");
	const std::array<int, 5> Ids = {std::numeric_limits<int>::min(), UUID_UNKNOWN, OFFSET_UUID - 1, OFFSET_UUID + 1, std::numeric_limits<int>::max()};
	for(int Id : Ids)
	{
		SCOPED_TRACE(Id);
		CPacker Packer;
		Packer.Reset();
		Packer.AddInt(42);
		const std::vector<unsigned char> Prefix(Packer.Data(), Packer.Data() + Packer.Size());
		EXPECT_FALSE(Manager.PackUuid(Id, &Packer));
		ASSERT_EQ(Packer.Size(), Prefix.size());
		EXPECT_FALSE(Packer.Error());
		EXPECT_EQ(mem_comp(Packer.Data(), Prefix.data(), Prefix.size()), 0);
	}
}

TEST(UuidPacket, ExactOutputCapacitySucceeds)
{
	CUuidManager Manager;
	Manager.RegisterName(OFFSET_UUID, "packet@uuid-boundaries");
	CPacker Packer;
	Packer.Reset();
	const std::vector<unsigned char> Prefix(CPacker::PACKER_BUFFER_SIZE - sizeof(CUuid), 0xa5);
	Packer.AddRaw(Prefix.data(), Prefix.size());
	ASSERT_TRUE(Manager.PackUuid(OFFSET_UUID, &Packer));
	EXPECT_EQ(Packer.Size(), CPacker::PACKER_BUFFER_SIZE);
	EXPECT_FALSE(Packer.Error());
	EXPECT_EQ(mem_comp(Packer.Data(), Prefix.data(), Prefix.size()), 0);
}

TEST(UuidPacket, ShortOutputCapacityReturnsFailureWithoutPartialUuid)
{
	CUuidManager Manager;
	Manager.RegisterName(OFFSET_UUID, "packet@uuid-boundaries");
	CPacker Packer;
	Packer.Reset();
	const std::vector<unsigned char> Prefix(CPacker::PACKER_BUFFER_SIZE - sizeof(CUuid) + 1, 0xa5);
	Packer.AddRaw(Prefix.data(), Prefix.size());
	EXPECT_FALSE(Manager.PackUuid(OFFSET_UUID, &Packer));
	EXPECT_TRUE(Packer.Error());
	EXPECT_EQ(Packer.Size(), Prefix.size());
	EXPECT_EQ(mem_comp(Packer.Data(), Prefix.data(), Prefix.size()), 0);
}

TEST(UuidPacket, ExistingPackerFailureIsPropagated)
{
	CUuidManager Manager;
	Manager.RegisterName(OFFSET_UUID, "packet@uuid-boundaries");
	CPacker Packer;
	Packer.Reset();
	Packer.AddRaw(nullptr, -1);
	ASSERT_TRUE(Packer.Error());
	EXPECT_FALSE(Manager.PackUuid(OFFSET_UUID, &Packer));
	EXPECT_EQ(Packer.Size(), 0);
}

TEST(UuidSnapshot, InvalidRegisteredTypeCannotAliasNilUuidDefinition)
{
	CSnapshotBuffer Buffer;
	// 两个有效条目：空UUID类型定义，以及该类型的id=7、载荷123。
	const int aSnapshot[] = {28, 2, 0, 20, CSnapshot::OFFSET_UUID_TYPE, 0, 0, 0, 0, (CSnapshot::OFFSET_UUID_TYPE << 16) | 7, 123};
	mem_copy(Buffer.m_aData, aSnapshot, sizeof(aSnapshot));
	ASSERT_TRUE(Buffer.AsSnapshot()->IsValid(sizeof(aSnapshot)));
	const int Unknown = OFFSET_UUID + g_UuidManager.NumUuids();
	ASSERT_FALSE(g_UuidManager.IsValidId(Unknown));
	EXPECT_EQ(g_UuidManager.GetUuid(Unknown), UUID_ZEROED);
	EXPECT_EQ(Buffer.AsSnapshot()->FindItem(Unknown, 7), nullptr);
}

TEST(UuidSnapshot, InvalidLookupKeyCannotAliasExistingOrdinalItem)
{
	CSnapshotBuffer Buffer;
	const int aSnapshot[] = {8, 1, 0, (1 << 16) | 1, 42};
	mem_copy(Buffer.m_aData, aSnapshot, sizeof(aSnapshot));
	ASSERT_TRUE(Buffer.AsSnapshot()->IsValid(sizeof(aSnapshot)));
	ASSERT_NE(Buffer.AsSnapshot()->FindItem(1, 1), nullptr);
	const std::array<std::array<int, 2>, 3> Keys = {{{-1, 1}, {1, -1}, {1, 65537}}};
	for(const auto &Key : Keys)
	{
		SCOPED_TRACE(testing::Message() << Key[0] << ":" << Key[1]);
		EXPECT_EQ(Buffer.AsSnapshot()->FindItem(Key[0], Key[1]), nullptr);
	}
}

TEST(UuidSnapshot, ShortTypeDefinitionCannotMatchBytesBeyondItsPayload)
{
	CSnapshotBuffer Buffer;
	const int aSnapshot[] = {12, 2, 0, 8, (CSnapshot::OFFSET_UUID_TYPE << 16) | 7, 123, CSnapshot::OFFSET_UUID_TYPE};
	mem_copy(Buffer.m_aData, aSnapshot, sizeof(aSnapshot));
	const CUuid Expected = g_UuidManager.GetUuid(OFFSET_UUID);
	auto Words = Buffer.AsMutSlice();
	// 声明范围之外的保护数据恰好等于目标UUID，旧查找会跨条目误匹配。
	for(size_t i = 0; i < sizeof(CUuid) / sizeof(int32_t); ++i)
		Words[sizeof(aSnapshot) / sizeof(int32_t) + i] = bytes_be_to_uint(Expected.m_aData + i * sizeof(int32_t));
	ASSERT_TRUE(Buffer.AsSnapshot()->IsValid(sizeof(aSnapshot)));
	EXPECT_EQ(Buffer.AsSnapshot()->FindItem(OFFSET_UUID, 7), nullptr);
}

TEST(UuidSnapshot, ShortTypeDefinitionAtMaximumSnapshotEndIsIgnored)
{
	CSnapshotBuffer Buffer;
	mem_zero(Buffer.m_aData, sizeof(Buffer.m_aData));
	auto Words = Buffer.AsMutSlice();
	Words[0] = CSnapshot::MAX_SIZE - 4 * sizeof(int32_t);
	Words[1] = 2;
	Words[2] = 0;
	Words[3] = Words[0] - sizeof(int32_t);
	Words[4] = (1 << 16) | 1;
	Words[Words.size() - 1] = CSnapshot::OFFSET_UUID_TYPE;
	ASSERT_TRUE(Buffer.AsSnapshot()->IsValid(CSnapshot::MAX_SIZE));
	EXPECT_EQ(Buffer.AsSnapshot()->FindItem(OFFSET_UUID, 7), nullptr);
}

TEST(UuidDatafile, InvalidRegisteredTypeCannotAliasPreservedNilUuidItem)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "uuid-boundary.map"));
	const int Value = 123;
	ASSERT_GE(Writer.AddItem(-1, 7, sizeof(Value), &Value, &UUID_ZEROED), 0);
	Writer.Finish();
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage.get(), "uuid-boundary.map", IStorage::TYPE_SAVE));
	bool Found = false;
	for(int i = 0; i < Reader.NumItems(); ++i)
	{
		int Type, Id;
		CUuid Uuid;
		const auto *pValue = (const int *)Reader.GetItem(i, &Type, &Id, &Uuid);
		if(Type == UUID_UNKNOWN && Id == 7)
		{
			ASSERT_EQ(Reader.GetItemSize(i), sizeof(Value));
			EXPECT_EQ(Uuid, UUID_ZEROED);
			EXPECT_EQ(*pValue, Value);
			Found = true;
		}
	}
	ASSERT_TRUE(Found);
	const int Unknown = OFFSET_UUID + g_UuidManager.NumUuids();
	int Start = -1, Num = -1;
	Reader.GetType(Unknown, &Start, &Num);
	EXPECT_EQ(Start, 0);
	EXPECT_EQ(Num, 0);
	EXPECT_EQ(Reader.FindItem(Unknown, 7), nullptr);
}
