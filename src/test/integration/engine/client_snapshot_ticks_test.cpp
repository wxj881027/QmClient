#include <engine/client/snapshot_ticks.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	class CClientSnapshotTicksTest : public testing::Test
	{
	protected:
		CPacker m_Packer;
		CUnpacker m_Unpacker;
		client_snapshot::STicks m_Output{7, 3};

		void SetUp() override { m_Packer.Reset(); }
		void PackTicks(int GameTick, int Distance)
		{
			m_Packer.AddInt(GameTick);
			m_Packer.AddInt(Distance);
		}
		bool Read()
		{
			m_Unpacker.Reset(m_Packer.Data(), m_Packer.Size());
			return client_snapshot::UnpackTicks(m_Unpacker, m_Output);
		}
		void ExpectUnchanged() const
		{
			EXPECT_EQ(m_Output.m_GameTick, 7);
			EXPECT_EQ(m_Output.m_DeltaTick, 3);
		}
	};
}

TEST_F(CClientSnapshotTicksTest, ValidHeaderDerivesDeltaKeyAndLeavesPayloadUnread)
{
	PackTicks(100, 3);
	m_Packer.AddInt(42);
	ASSERT_FALSE(m_Packer.Error());
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_Output.m_GameTick, 100);
	EXPECT_EQ(m_Output.m_DeltaTick, 97);
	EXPECT_EQ(m_Unpacker.GetInt(), 42);
	EXPECT_FALSE(m_Unpacker.Error());
}

TEST_F(CClientSnapshotTicksTest, FullSnapshotKeepsNegativeOneDeltaKey)
{
	PackTicks(100, 101);
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_Output.m_GameTick, 100);
	EXPECT_EQ(m_Output.m_DeltaTick, -1);
}

TEST_F(CClientSnapshotTicksTest, RepresentableExtremeKeysRemainAccepted)
{
	PackTicks(0, std::numeric_limits<int>::max());
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_Output.m_DeltaTick, -std::numeric_limits<int>::max());
	m_Packer.Reset();
	PackTicks(std::numeric_limits<int>::min(), 0);
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_Output.m_DeltaTick, std::numeric_limits<int>::min());
	m_Packer.Reset();
	PackTicks(std::numeric_limits<int>::max(), 0);
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_Output.m_DeltaTick, std::numeric_limits<int>::max());
}

TEST_F(CClientSnapshotTicksTest, PositiveOverflowRejectsWithoutPublishing)
{
	PackTicks(std::numeric_limits<int>::max(), -1);
	ASSERT_FALSE(m_Packer.Error());
	EXPECT_FALSE(Read());
	ExpectUnchanged();
	EXPECT_FALSE(m_Unpacker.Error());
}

TEST_F(CClientSnapshotTicksTest, NegativeOverflowRejectsWithoutPublishing)
{
	PackTicks(std::numeric_limits<int>::min(), 1);
	ASSERT_FALSE(m_Packer.Error());
	EXPECT_FALSE(Read());
	ExpectUnchanged();
}

TEST_F(CClientSnapshotTicksTest, MissingSecondIntegerRejectsWithoutPublishing)
{
	m_Packer.AddInt(100);
	EXPECT_FALSE(Read());
	ExpectUnchanged();
	EXPECT_TRUE(m_Unpacker.Error());
}

TEST_F(CClientSnapshotTicksTest, TruncatedFirstIntegerRejectsWithoutPublishing)
{
	const unsigned char aTruncated[]{0x80};
	m_Packer.AddRaw(aTruncated, sizeof(aTruncated));
	EXPECT_FALSE(Read());
	ExpectUnchanged();
	EXPECT_TRUE(m_Unpacker.Error());
}

TEST_F(CClientSnapshotTicksTest, ValidPacketAfterRejectedPacketCanPublish)
{
	PackTicks(0, std::numeric_limits<int>::min());
	EXPECT_FALSE(Read());
	ExpectUnchanged();
	m_Packer.Reset();
	PackTicks(200, 2);
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_Output.m_GameTick, 200);
	EXPECT_EQ(m_Output.m_DeltaTick, 198);
}
