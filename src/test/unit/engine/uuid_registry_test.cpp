#include <base/system.h>

#include <engine/shared/uuid_manager.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>
#include <string>
#include <utility>

TEST(UuidRegistry, SourceBufferMutationDoesNotChangeRegisteredName)
{
	CUuidManager Manager;
	char aName[] = "original@uuid-test";
	const CUuid Expected = CalculateUuid(aName);
	Manager.RegisterName(OFFSET_UUID, aName);
	aName[0] = 'X';
	EXPECT_STREQ(Manager.GetName(OFFSET_UUID), "original@uuid-test");
	EXPECT_EQ(Manager.GetUuid(OFFSET_UUID), Expected);
	EXPECT_EQ(Manager.LookupUuid(Expected), OFFSET_UUID);
}

TEST(UuidRegistry, NameRemainsValidAfterCallerBufferIsDestroyed)
{
	CUuidManager Manager;
	{
		auto pName = std::make_unique<char[]>(64);
		str_copy(pName.get(), "released@uuid-test", 64);
		Manager.RegisterName(OFFSET_UUID, pName.get());
	}
	EXPECT_STREQ(Manager.GetName(OFFSET_UUID), "released@uuid-test");
}

TEST(UuidRegistry, ReusedInputBufferProducesIndependentEntries)
{
	CUuidManager Manager;
	char aName[64];
	for(int i = 0; i < 100; ++i)
	{
		str_format(aName, sizeof(aName), "name-%d@uuid-test", i);
		Manager.RegisterName(OFFSET_UUID + i, aName);
	}
	for(int i = 0; i < 100; ++i)
	{
		SCOPED_TRACE(i);
		const std::string Expected = "name-" + std::to_string(i) + "@uuid-test";
		EXPECT_STREQ(Manager.GetName(OFFSET_UUID + i), Expected.c_str());
		EXPECT_EQ(Manager.LookupUuid(CalculateUuid(Expected.c_str())), OFFSET_UUID + i);
	}
	EXPECT_EQ(Manager.NumUuids(), 100);
}

TEST(UuidRegistry, BorrowedShortNameStaysAtSameAddressAcrossRegistrationGrowth)
{
	CUuidManager Manager;
	Manager.RegisterName(OFFSET_UUID, "short");
	const char *pName = Manager.GetName(OFFSET_UUID);
	for(int i = 1; i < 100; ++i)
	{
		const std::string Name = "additional-" + std::to_string(i);
		Manager.RegisterName(OFFSET_UUID + i, Name.c_str());
	}
	EXPECT_EQ(Manager.GetName(OFFSET_UUID), pName);
	EXPECT_STREQ(pName, "short");
}

TEST(UuidRegistry, CopyConstructionOwnsNamesIndependentlyOfSource)
{
	char aName[] = "copied@uuid-test";
	CUuidManager Original;
	Original.RegisterName(OFFSET_UUID, aName);
	CUuidManager Copy(Original);
	aName[0] = 'X';
	Copy.RegisterName(OFFSET_UUID + 1, "copy-only@uuid-test");
	EXPECT_STREQ(Copy.GetName(OFFSET_UUID), "copied@uuid-test");
	EXPECT_STREQ(Original.GetName(OFFSET_UUID), "copied@uuid-test");
	EXPECT_EQ(Original.NumUuids(), 1);
	EXPECT_EQ(Copy.NumUuids(), 2);
}

TEST(UuidRegistry, CopyAssignmentRemainsValidAfterOriginalIsDestroyed)
{
	char aName[] = "assigned@uuid-test";
	CUuidManager Copy;
	{
		CUuidManager Original;
		Original.RegisterName(OFFSET_UUID, aName);
		Copy = Original;
	}
	aName[0] = 'X';
	EXPECT_STREQ(Copy.GetName(OFFSET_UUID), "assigned@uuid-test");
	EXPECT_EQ(Copy.LookupUuid(CalculateUuid("assigned@uuid-test")), OFFSET_UUID);
}

TEST(UuidRegistry, MoveConstructionRetainsBorrowedNameAndLookup)
{
	CUuidManager Original;
	Original.RegisterName(OFFSET_UUID, "moved@uuid-test");
	const char *pName = Original.GetName(OFFSET_UUID);
	CUuidManager Moved(std::move(Original));
	EXPECT_EQ(Moved.GetName(OFFSET_UUID), pName);
	EXPECT_STREQ(pName, "moved@uuid-test");
	EXPECT_EQ(Moved.LookupUuid(CalculateUuid("moved@uuid-test")), OFFSET_UUID);
}

TEST(UuidRegistry, LongNameIsRetainedWithoutTruncation)
{
	CUuidManager Manager;
	std::string Name(4096, 'a');
	Name += "@uuid-test";
	const std::string Expected = Name;
	const CUuid Uuid = CalculateUuid(Name.c_str());
	Manager.RegisterName(OFFSET_UUID, Name.c_str());
	Name.assign(Expected.size(), 'x');
	EXPECT_EQ(std::string(Manager.GetName(OFFSET_UUID)), Expected);
	EXPECT_EQ(Manager.GetUuid(OFFSET_UUID), Uuid);
}

TEST(UuidRegistry, EmptyRegistryRejectsAllLookupIdsWithoutIndexing)
{
	CUuidManager Manager;
	const std::array<int, 7> Ids = {std::numeric_limits<int>::min(), -1, 0, OFFSET_UUID - 1, OFFSET_UUID, OFFSET_UUID + 1, std::numeric_limits<int>::max()};
	for(int Id : Ids)
	{
		SCOPED_TRACE(Id);
		EXPECT_FALSE(Manager.IsValidId(Id));
		EXPECT_EQ(Manager.GetUuid(Id), UUID_ZEROED);
		ASSERT_NE(Manager.GetName(Id), nullptr);
		EXPECT_STREQ(Manager.GetName(Id), "");
	}
	EXPECT_EQ(Manager.NumUuids(), 0);
}

TEST(UuidRegistry, PopulatedRegistryKeepsBothValidBoundariesAndRejectsOutsideIds)
{
	CUuidManager Manager;
	Manager.RegisterName(OFFSET_UUID, "first@uuid-test");
	Manager.RegisterName(OFFSET_UUID + 1, "last@uuid-test");
	EXPECT_TRUE(Manager.IsValidId(OFFSET_UUID));
	EXPECT_TRUE(Manager.IsValidId(OFFSET_UUID + 1));
	EXPECT_EQ(Manager.GetUuid(OFFSET_UUID), CalculateUuid("first@uuid-test"));
	EXPECT_STREQ(Manager.GetName(OFFSET_UUID + 1), "last@uuid-test");
	const std::array<int, 5> InvalidIds = {std::numeric_limits<int>::min(), UUID_INVALID, OFFSET_UUID - 1, OFFSET_UUID + 2, std::numeric_limits<int>::max()};
	for(int Id : InvalidIds)
	{
		SCOPED_TRACE(Id);
		EXPECT_FALSE(Manager.IsValidId(Id));
		EXPECT_EQ(Manager.GetUuid(Id), UUID_ZEROED);
		EXPECT_STREQ(Manager.GetName(Id), "");
	}
	EXPECT_EQ(Manager.NumUuids(), 2);
}

TEST(UuidRegistry, InvalidLookupDoesNotPreventLaterRegistration)
{
	CUuidManager Manager;
	EXPECT_EQ(Manager.GetUuid(OFFSET_UUID), UUID_ZEROED);
	EXPECT_STREQ(Manager.GetName(std::numeric_limits<int>::min()), "");
	Manager.RegisterName(OFFSET_UUID, "after-lookup@uuid-test");
	EXPECT_TRUE(Manager.IsValidId(OFFSET_UUID));
	EXPECT_EQ(Manager.LookupUuid(Manager.GetUuid(OFFSET_UUID)), OFFSET_UUID);
	EXPECT_STREQ(Manager.GetName(OFFSET_UUID), "after-lookup@uuid-test");
}
