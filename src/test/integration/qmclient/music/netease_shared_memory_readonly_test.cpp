#include <base/detect.h>

#include <game/client/components/qmclient/netease/netease_shared_memory.h>

#include <gtest/gtest.h>

#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <cstring>

TEST(NeteaseSharedMemoryMapping, ReadsSnapshotFromReadOnlyWindowsMapping)
{
	using namespace QmNeteaseHook;
	struct SMapping
	{
		HANDLE m_Handle = nullptr;
		void *m_pView = nullptr;
		~SMapping()
		{
			if(m_pView)
				UnmapViewOfFile(m_pView);
			if(m_Handle)
				CloseHandle(m_Handle);
		}
	} Mapping;
	Mapping.m_Handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SSharedBlockV5), nullptr);
	ASSERT_NE(Mapping.m_Handle, nullptr);
	Mapping.m_pView = MapViewOfFile(Mapping.m_Handle, FILE_MAP_WRITE, 0, 0, sizeof(SSharedBlockV5));
	ASSERT_NE(Mapping.m_pView, nullptr);
	SSharedBlockV5 Block{};
	Block.m_Sequence = 2;
	Block.m_Snapshot.m_Sequence = 2;
	Block.m_Snapshot.m_CloudMusicPid = 42;
	Block.m_Snapshot.m_SongId = 123;
	Block.m_Snapshot.m_Generation = 1;
	Block.m_Snapshot.m_UpdatedAtTick = 100;
	Block.m_Snapshot.m_Flags = V5_FLAG_HAS_SONG | V5_FLAG_LYRIC_VALID;
	Block.m_Snapshot.m_LyricSource = (uint32_t)ENeteaseLyricSource::Frontend;
	Block.m_Snapshot.m_LineStartMs = 1000;
	Block.m_Snapshot.m_LineEndMs = 2000;
	std::strcpy(Block.m_Snapshot.m_aCurrentLyric, "test");
	FinalizeSnapshotV5(&Block.m_Snapshot);
	std::memcpy(Mapping.m_pView, &Block, sizeof(Block));
	ASSERT_TRUE(UnmapViewOfFile(Mapping.m_pView));
	Mapping.m_pView = nullptr;
	Mapping.m_pView = MapViewOfFile(Mapping.m_Handle, FILE_MAP_READ, 0, 0, sizeof(SSharedBlockV5));
	ASSERT_NE(Mapping.m_pView, nullptr);
	SSnapshotV5 Snapshot{};
	ASSERT_TRUE(NeteaseLyrics::ReadStableV5(*static_cast<const volatile SSharedBlockV5 *>(Mapping.m_pView), &Snapshot));
	EXPECT_EQ(Snapshot.m_SongId, 123u);
	EXPECT_EQ(std::string(Snapshot.m_aCurrentLyric), "test");
}
#endif
