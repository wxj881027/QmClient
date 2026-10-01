#include <game/client/components/qmclient/netease/netease_shared_memory.h>
#include <game/client/components/system_media_controls.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace
{
	QmNeteaseHook::SSharedBlockV5 ValidBlock()
	{
		QmNeteaseHook::SSharedBlockV5 Block{};
		Block.m_Sequence = 2;
		Block.m_Snapshot.m_Sequence = 2;
		Block.m_Snapshot.m_CloudMusicPid = 42;
		Block.m_Snapshot.m_Flags = QmNeteaseHook::V5_FLAG_HAS_SONG | QmNeteaseHook::V5_FLAG_LYRIC_VALID;
		Block.m_Snapshot.m_SongId = 123;
		Block.m_Snapshot.m_Generation = 1;
		Block.m_Snapshot.m_LyricSource = (uint32_t)QmNeteaseHook::ENeteaseLyricSource::Frontend;
		Block.m_Snapshot.m_LineStartMs = 1000;
		Block.m_Snapshot.m_LineEndMs = 2000;
		Block.m_Snapshot.m_UpdatedAtTick = 100;
		std::strcpy(Block.m_Snapshot.m_aCurrentLyric, "你好");
		QmNeteaseHook::FinalizeSnapshotV5(&Block.m_Snapshot);
		return Block;
	}
}

TEST(NeteaseSharedMemory, ReadsStableEvenSequence)
{
	QmNeteaseHook::SSharedBlockV5 Block = ValidBlock();
	QmNeteaseHook::SSnapshotV5 Snapshot{};
	EXPECT_TRUE(NeteaseLyrics::ReadStableV5(Block, &Snapshot));
	EXPECT_EQ(Snapshot.m_SongId, 123u);
	EXPECT_EQ(Snapshot.m_aCurrentLyric, std::string("你好"));
}

TEST(NeteaseSharedMemory, RejectsOddOrMismatchedSequence)
{
	QmNeteaseHook::SSharedBlockV5 Block = ValidBlock();
	QmNeteaseHook::SSnapshotV5 Snapshot{};
	Block.m_Sequence = 3;
	EXPECT_FALSE(NeteaseLyrics::ReadStableV5(Block, &Snapshot));
	Block.m_Sequence = 2;
	Block.m_Snapshot.m_Sequence = 4;
	QmNeteaseHook::FinalizeSnapshotV5(&Block.m_Snapshot);
	EXPECT_FALSE(NeteaseLyrics::ReadStableV5(Block, &Snapshot));
}

TEST(NeteaseSharedMemory, ValidatesVersionAndStaleTimestamp)
{
	auto Block = ValidBlock();
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshotV5(Block.m_Snapshot));
	Block.m_Snapshot.m_SchemaVersion = 4;
	Block.m_Snapshot.m_Checksum = QmNeteaseHook::CalculateChecksumV5(Block.m_Snapshot);
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshotV5(Block.m_Snapshot));
	Block = ValidBlock();
	EXPECT_TRUE(QmNeteaseHook::IsStaleV5(Block.m_Snapshot, 2000, 100));
}

TEST(NeteaseSharedMemory, AcceptsActiveLyricTimelineFlag)
{
	auto Block = ValidBlock();
	Block.m_Snapshot.m_Flags = QmNeteaseHook::V5_FLAG_HAS_SONG | QmNeteaseHook::V5_FLAG_LYRIC_TIMELINE_VALID;
	Block.m_Snapshot.m_aCurrentLyric[0] = '\0';
	Block.m_Snapshot.m_LineStartMs = -1;
	Block.m_Snapshot.m_LineEndMs = -1;
	QmNeteaseHook::FinalizeSnapshotV5(&Block.m_Snapshot);
	EXPECT_EQ(Block.m_Snapshot.m_Flags, QmNeteaseHook::V5_FLAG_HAS_SONG | QmNeteaseHook::V5_FLAG_LYRIC_TIMELINE_VALID);
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshotV5(Block.m_Snapshot));
}

TEST(NeteaseSharedMemory, RejectsReservedLegacyApiSource)
{
	auto Block = ValidBlock();
	Block.m_Snapshot.m_LyricSource = (uint32_t)QmNeteaseHook::ENeteaseLyricSource::ReservedLegacyApi;
	QmNeteaseHook::FinalizeSnapshotV5(&Block.m_Snapshot);
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshotV5(Block.m_Snapshot));
}

TEST(NeteaseSharedMemory, RejectsPartiallyWrittenPayload)
{
	auto Block = ValidBlock();
	Block.m_Snapshot.m_aCurrentLyric[0] = 'X';
	QmNeteaseHook::SSnapshotV5 Snapshot{};
	EXPECT_FALSE(NeteaseLyrics::ReadStableV5(Block, &Snapshot));
}

TEST(NeteaseSharedMemory, AcceptsWriterRestartAndPidChange)
{
	auto Block = ValidBlock();
	QmNeteaseHook::SSnapshotV5 Snapshot{};
	ASSERT_TRUE(NeteaseLyrics::ReadStableV5(Block, &Snapshot));
	EXPECT_EQ(Snapshot.m_CloudMusicPid, 42u);

	Block.m_Sequence = 2;
	Block.m_Snapshot.m_Sequence = 2;
	Block.m_Snapshot.m_CloudMusicPid = 84;
	Block.m_Snapshot.m_Generation = 2;
	QmNeteaseHook::FinalizeSnapshotV5(&Block.m_Snapshot);
	ASSERT_TRUE(NeteaseLyrics::ReadStableV5(Block, &Snapshot));
	EXPECT_EQ(Snapshot.m_CloudMusicPid, 84u);
	EXPECT_EQ(Snapshot.m_Sequence, 2u);
}

TEST(NeteaseSharedMemory, Utf8CopyNeverLeavesPartialCodepoint)
{
	char aBuffer[8];
	const size_t Copied = QmNeteaseHook::CopyUtf8Truncated(aBuffer, sizeof(aBuffer), "中文😀", 10);
	EXPECT_EQ(Copied, std::strlen("中文"));
	EXPECT_STREQ(aBuffer, "中文");
}

TEST(NeteaseSharedMemory, HookRefreshCadenceHasABoundedRetryInterval)
{
	EXPECT_TRUE(SystemMediaControls::ShouldRefreshNeteaseHookSnapshot(0, 0, false));
	EXPECT_FALSE(SystemMediaControls::ShouldRefreshNeteaseHookSnapshot(1, 0, true));
	EXPECT_FALSE(SystemMediaControls::ShouldRefreshNeteaseHookSnapshot(2, 0, true));
	EXPECT_TRUE(SystemMediaControls::ShouldRefreshNeteaseHookSnapshot(3, 0, true));
	EXPECT_TRUE(SystemMediaControls::ShouldRefreshNeteaseHookSnapshot(9, 10, true));
}

TEST(NeteaseSharedMemory, SmtcSourceChangesForceImmediateMetadataRefresh)
{
	EXPECT_TRUE(SystemMediaControls::MediaSourceChanged("cloudmusic.exe", "chrome.exe"));
	EXPECT_TRUE(SystemMediaControls::MediaSourceChanged("", "cloudmusic.exe"));
	EXPECT_FALSE(SystemMediaControls::MediaSourceChanged("cloudmusic.exe", "cloudmusic.exe"));
}
