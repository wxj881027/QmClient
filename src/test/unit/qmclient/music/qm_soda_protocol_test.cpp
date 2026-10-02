#include <gtest/gtest.h>
#include <qm/hooks/soda/qm_soda_protocol.h>

#include <cstring>

using namespace QmSodaHook;

namespace
{
	SSnapshot ValidSnapshot()
	{
		SSnapshot Snapshot;
		Snapshot.m_SodaMusicPid = 42;
		Snapshot.m_Sequence = 2;
		Snapshot.m_Flags = FLAG_HAS_SONG | FLAG_POSITION_VALID;
		Snapshot.m_Generation = 1;
		Snapshot.m_PositionMs = 5000;
		Snapshot.m_DurationMs = 250000;
		Snapshot.m_UpdatedAtTick = 100;
		std::strcpy(Snapshot.m_aMediaId, "6849382015611963393");
		std::strcpy(Snapshot.m_aTitle, "晴天");
		FinalizeSnapshot(&Snapshot);
		return Snapshot;
	}
}

TEST(QmSodaProtocol, FinalizeAndValidateRoundTrip)
{
	SSnapshot Snapshot = ValidSnapshot();
	EXPECT_TRUE(ValidateSnapshot(Snapshot));
	EXPECT_EQ(Snapshot.m_Magic, 0x514D5344u);
	EXPECT_EQ(Snapshot.m_SchemaVersion, PROTOCOL_SCHEMA_VERSION);
	EXPECT_EQ(Snapshot.m_SnapshotSize, sizeof(SSnapshot));
}

TEST(QmSodaProtocol, RejectsTornChecksum)
{
	SSnapshot Snapshot = ValidSnapshot();
	Snapshot.m_Checksum ^= 0x1;
	EXPECT_FALSE(ValidateSnapshot(Snapshot));
}

TEST(QmSodaProtocol, RejectsUnknownFlags)
{
	SSnapshot Snapshot = ValidSnapshot();
	Snapshot.m_Flags |= 1U << 20;
	FinalizeSnapshot(&Snapshot);
	EXPECT_FALSE(ValidateSnapshot(Snapshot));
}

TEST(QmSodaProtocol, RejectsSongFlagWithoutMediaId)
{
	SSnapshot Snapshot = ValidSnapshot();
	Snapshot.m_aMediaId[0] = '\0';
	FinalizeSnapshot(&Snapshot);
	EXPECT_FALSE(ValidateSnapshot(Snapshot));
}

TEST(QmSodaProtocol, RejectsCoverOrLyricFileFlagWithoutPath)
{
	SSnapshot Snapshot = ValidSnapshot();
	Snapshot.m_Flags |= FLAG_HAS_COVER;
	FinalizeSnapshot(&Snapshot);
	EXPECT_FALSE(ValidateSnapshot(Snapshot));

	Snapshot = ValidSnapshot();
	Snapshot.m_Flags |= FLAG_HAS_LYRIC_FILE;
	FinalizeSnapshot(&Snapshot);
	EXPECT_FALSE(ValidateSnapshot(Snapshot));
}

TEST(QmSodaProtocol, AcceptsLyricFileFlagWithPath)
{
	SSnapshot Snapshot = ValidSnapshot();
	Snapshot.m_Flags |= FLAG_HAS_LYRIC_FILE;
	std::strcpy(Snapshot.m_aLyricFilePath, "C:/Users/x/AppData/Local/QmClient/soda-hook/lyrics-1.json");
	FinalizeSnapshot(&Snapshot);
	EXPECT_TRUE(ValidateSnapshot(Snapshot));
}

TEST(QmSodaProtocol, StableSequenceRequiresEvenNonZero)
{
	EXPECT_FALSE(IsStableSequence(0, 0));
	EXPECT_FALSE(IsStableSequence(1, 1));
	EXPECT_TRUE(IsStableSequence(2, 2));
	EXPECT_FALSE(IsStableSequence(2, 4));
}

TEST(QmSodaProtocol, StaleUsesUpdatedAtTick)
{
	SSnapshot Snapshot = ValidSnapshot();
	EXPECT_TRUE(IsStale(Snapshot, 1000, 500));
	EXPECT_FALSE(IsStale(Snapshot, 1000, 2000));
	EXPECT_TRUE(IsStale(Snapshot, 0, 2000));
}

TEST(QmSodaProtocol, Utf8CopyNeverLeavesPartialCodepoint)
{
	char aBuffer[8] = {};
	const size_t Copied = CopyUtf8Truncated(aBuffer, sizeof(aBuffer), "中文😀", 10);
	EXPECT_LT(Copied, sizeof(aBuffer));
	EXPECT_EQ(aBuffer[Copied], '\0');
	// "中文😀" = 3+3+4 字节;7 字节容量只能放下"中文"。
	EXPECT_EQ(Copied, 6u);
	EXPECT_EQ(std::string(aBuffer), "中文");
}
