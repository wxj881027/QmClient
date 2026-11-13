// 真实录像文件、播放器及提取/裁剪协作；仅在分配和文件操作结果处注入失败。
#include <engine/shared/config.h>
#include <engine/shared/demo.h>
#include <engine/shared/network.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
	class CObservedMapDataOperations : public CDemoPlayer::CMapDataOperations
	{
	public:
		enum EOperation
		{
			ALLOCATE,
			FREE,
			TELL,
			SEEK,
			READ,
			ERROR,
			COUNT,
		};

		std::array<int, COUNT> m_aCalls{};
		EOperation m_Failure = COUNT;
		int m_FailureCall = 0;
		IOHANDLE m_File = nullptr;
		int64_t m_OriginalPosition = -1;

		void Fail(EOperation Operation, int AfterCalls = 1)
		{
			m_Failure = Operation;
			m_FailureCall = m_aCalls[Operation] + AfterCalls;
		}

		bool Fails(EOperation Operation) noexcept
		{
			return ++m_aCalls[Operation] == m_FailureCall && m_Failure == Operation;
		}

		unsigned char *Allocate(unsigned Size) noexcept override
		{
			return Fails(ALLOCATE) ? nullptr : CMapDataOperations::Allocate(Size);
		}

		void Free(unsigned char *pData) noexcept override
		{
			++m_aCalls[FREE];
			CMapDataOperations::Free(pData);
		}

		int64_t Tell(IOHANDLE File) noexcept override
		{
			m_File = File;
			m_OriginalPosition = CMapDataOperations::Tell(File);
			return Fails(TELL) ? -1 : m_OriginalPosition;
		}

		int Seek(IOHANDLE File, int64_t Offset) noexcept override
		{
			const int Result = CMapDataOperations::Seek(File, Offset);
			// 失败不保证游标未变，必须通过第二次真实定位恢复。
			return Fails(SEEK) ? -1 : Result;
		}

		unsigned Read(IOHANDLE File, void *pData, unsigned Size) noexcept override
		{
			return CMapDataOperations::Read(File, pData, Fails(READ) && Size > 0 ? Size - 1 : Size);
		}

		int Error(IOHANDLE File) noexcept override
		{
			return Fails(ERROR) ? 1 : CMapDataOperations::Error(File);
		}
	};

	class DemoMapReadIntegration : public ::testing::Test
	{
	protected:
		using COperations = CObservedMapDataOperations;
		using CMapBuffer = std::unique_ptr<unsigned char, decltype(&std::free)>;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		COperations m_Operations;
		rust::Box<CSnapshotDelta> m_pDelta = CSnapshotDelta::New();
		rust::Box<CSnapshotDelta> m_pDeltaSixup = CSnapshotDelta::New();
		CDemoRecorder m_Recorder{&*m_pDelta};
		CDemoPlayer m_Player{&*m_pDelta, &*m_pDeltaSixup, false, &m_Operations};
		int m_SliceBegin = -1;
		int m_SliceEnd = -1;
		static constexpr const char *SOURCE = "map-read.demo";
		static constexpr const char *TARGET = "existing.demo";
		static constexpr const char *MAP_BYTES = "mapbytes";

		void SetUp() override
		{
			m_SliceBegin = g_Config.m_ClDemoSliceBegin;
			m_SliceEnd = g_Config.m_ClDemoSliceEnd;
			ASSERT_NE(m_pStorage, nullptr);
			CNetBase::Init();
		}

		void TearDown() override
		{
			m_Player.Stop();
			if(m_Recorder.IsRecording())
				m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE);
			g_Config.m_ClDemoSliceBegin = m_SliceBegin;
			g_Config.m_ClDemoSliceEnd = m_SliceEnd;
		}

		void Write(const char *pName, const std::string &Bytes)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
			EXPECT_EQ(io_close(File), 0);
		}

		std::string Read(const char *pName)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_READ, IStorage::TYPE_SAVE);
			EXPECT_NE(File, nullptr);
			if(!File)
				return {};
			const int64_t Size = io_length(File);
			std::string Bytes(Size > 0 ? static_cast<size_t>(Size) : 0, '\0');
			EXPECT_EQ(io_read(File, Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
			EXPECT_EQ(io_close(File), 0);
			return Bytes;
		}

		void CreateDemo(bool WithMap = true)
		{
			unsigned char aMap[] = {'m', 'a', 'p', 'b', 'y', 't', 'e', 's'};
			ASSERT_EQ(m_Recorder.Start(m_pStorage.get(), nullptr, SOURCE, "test", "map", SHA256_DIGEST{}, 0, "client", WithMap ? static_cast<unsigned>(sizeof(aMap)) : 0, aMap, nullptr, nullptr, nullptr), 0);
			const CSnapshot *pSnapshot = CSnapshot::EmptySnapshot();
			const int SnapshotSize = static_cast<int>(pSnapshot->AsSlice().size() * sizeof(int32_t));
			m_Recorder.RecordSnapshot(100, pSnapshot, SnapshotSize);
			m_Recorder.RecordSnapshot(150, pSnapshot, SnapshotSize, true);
			ASSERT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
		}

		void Load(bool WithMap = true)
		{
			CreateDemo(WithMap);
			ASSERT_EQ(m_Player.Load(m_pStorage.get(), nullptr, SOURCE, IStorage::TYPE_SAVE), 0);
		}

		void ExpectOriginalPosition()
		{
			ASSERT_TRUE(m_Player.IsPlaying());
			ASSERT_NE(m_Operations.m_File, nullptr);
			EXPECT_GE(m_Operations.m_OriginalPosition, 0);
			EXPECT_EQ(io_tell(m_Operations.m_File), m_Operations.m_OriginalPosition);
		}

		CMapBuffer MapData()
		{
			return CMapBuffer(m_Player.GetMapData(m_pStorage.get()), &std::free);
		}

		std::string MapDestination()
		{
			char aSha[SHA256_MAXSTRSIZE];
			sha256_str(SHA256_DIGEST{}, aSha, sizeof(aSha));
			return std::string("downloadedmaps/map_") + aSha + ".map";
		}

		void MakeLegacyDemo()
		{
			// 从生产记录器生成的容器去掉 v6 扩展，作为受支持 v5 格式的输入。
			std::string Bytes = Read(SOURCE);
			ASSERT_GT(Bytes.size(), sizeof(CDemoHeader) + sizeof(CTimelineMarkers) + sizeof(CUuid) + sizeof(SHA256_DIGEST));
			Bytes[offsetof(CDemoHeader, m_Version)] = 5;
			Bytes.erase(sizeof(CDemoHeader) + sizeof(CTimelineMarkers), sizeof(CUuid) + sizeof(SHA256_DIGEST));
			Write(SOURCE, Bytes);
		}

		bool Slice()
		{
			CDemoEditor Editor;
			Editor.Init(&*m_pDelta, &*m_pDeltaSixup, nullptr, m_pStorage.get(), &m_Operations);
			return Editor.Slice(SOURCE, TARGET, -1, -1, nullptr, nullptr);
		}
	};
}

TEST_F(DemoMapReadIntegration, BeforeLoadDoesNotAttemptFileOperations)
{
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::TELL], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::ALLOCATE], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::READ], 0);
}

TEST_F(DemoMapReadIntegration, HealthyReadReturnsEmbeddedBytesAndRestoresPlaybackPosition)
{
	Load();
	const CMapBuffer Data = MapData();
	ASSERT_NE(Data, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(Data.get()), 8), MAP_BYTES);
	ExpectOriginalPosition();
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 0);
}

TEST_F(DemoMapReadIntegration, DefaultOperationsPreserveTheExistingPlayerApi)
{
	CreateDemo();
	CDemoPlayer Player(&*m_pDelta, &*m_pDeltaSixup, false);
	ASSERT_EQ(Player.Load(m_pStorage.get(), nullptr, SOURCE, IStorage::TYPE_SAVE), 0);
	const CMapBuffer Data(Player.GetMapData(m_pStorage.get()), &std::free);
	EXPECT_NE(Data, nullptr);
	if(Data)
		EXPECT_EQ(std::string(reinterpret_cast<const char *>(Data.get()), 8), MAP_BYTES);
	Player.Stop();
}

TEST_F(DemoMapReadIntegration, RepeatedReadsHaveIndependentCallerOwnedBuffers)
{
	Load();
	CMapBuffer First = MapData();
	CMapBuffer Second = MapData();
	ASSERT_NE(First, nullptr);
	ASSERT_NE(Second, nullptr);
	EXPECT_NE(First.get(), Second.get());
	First.get()[0] = 'x';
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(Second.get()), 8), MAP_BYTES);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, AllocationFailureDoesNotReadOrMovePlaybackPosition)
{
	Load();
	m_Operations.Fail(COperations::ALLOCATE);
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::SEEK], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::READ], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 0);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, ReadCanBeRetriedAfterAllocationFailure)
{
	Load();
	m_Operations.Fail(COperations::ALLOCATE);
	ASSERT_EQ(MapData(), nullptr);
	const CMapBuffer Data = MapData();
	ASSERT_NE(Data, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(Data.get()), 8), MAP_BYTES);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::ALLOCATE], 2);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, TellFailureDoesNotAllocateSeekOrRead)
{
	Load();
	m_Operations.Fail(COperations::TELL);
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::ALLOCATE], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::SEEK], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::READ], 0);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, InitialSeekFailureRestoresPositionAndFreesCandidate)
{
	Load();
	m_Operations.Fail(COperations::SEEK);
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::SEEK], 2);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::READ], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 1);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, ShortReadRestoresPositionAndFreesPartialCandidate)
{
	Load();
	m_Operations.Fail(COperations::READ);
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::SEEK], 2);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 1);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, FullReadWithStreamErrorDoesNotReturnCandidate)
{
	Load();
	m_Operations.Fail(COperations::ERROR);
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 1);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, LaterReadSucceedsAfterShortRead)
{
	Load();
	m_Operations.Fail(COperations::READ);
	ASSERT_EQ(MapData(), nullptr);
	const CMapBuffer Data = MapData();
	ASSERT_NE(Data, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(Data.get()), 8), MAP_BYTES);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 1);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, RestoreFailureStopsPlaybackAndFreesCandidate)
{
	Load();
	m_Operations.Fail(COperations::SEEK, 2);
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_FALSE(m_Player.IsPlaying());
	EXPECT_STRNE(m_Player.ErrorMessage(), "");
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 1);
}

TEST_F(DemoMapReadIntegration, ReadAfterRestoreFailureDoesNotTouchClosedFile)
{
	Load();
	m_Operations.Fail(COperations::SEEK, 2);
	ASSERT_EQ(MapData(), nullptr);
	const auto Calls = m_Operations.m_aCalls;
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls, Calls);
}

TEST_F(DemoMapReadIntegration, EmptyEmbeddedMapDoesNotAllocateOrSeek)
{
	Load(false);
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Operations.m_aCalls[COperations::TELL], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::ALLOCATE], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::SEEK], 0);
}

TEST_F(DemoMapReadIntegration, ReadAfterOrdinaryStopDoesNotTouchClosedFile)
{
	Load();
	m_Player.Stop();
	EXPECT_EQ(MapData(), nullptr);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::TELL], 0);
}

TEST_F(DemoMapReadIntegration, ExtractionReturnsFalseAndPreservesOldMapOnAllocationFailure)
{
	Load();
	ASSERT_TRUE(m_pStorage->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE));
	const std::string Destination = MapDestination();
	Write(Destination.c_str(), "previous map");
	m_Operations.Fail(COperations::ALLOCATE);
	EXPECT_FALSE(m_Player.ExtractMap(m_pStorage.get()));
	EXPECT_EQ(Read(Destination.c_str()), "previous map");
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, ExtractionReturnsFalseAndPreservesOldMapOnShortRead)
{
	Load();
	ASSERT_TRUE(m_pStorage->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE));
	const std::string Destination = MapDestination();
	Write(Destination.c_str(), "previous map");
	m_Operations.Fail(COperations::READ);
	EXPECT_FALSE(m_Player.ExtractMap(m_pStorage.get()));
	EXPECT_EQ(Read(Destination.c_str()), "previous map");
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, ExtractionCanSucceedAfterAnAllocationFailure)
{
	Load();
	ASSERT_TRUE(m_pStorage->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE));
	m_Operations.Fail(COperations::ALLOCATE);
	ASSERT_FALSE(m_Player.ExtractMap(m_pStorage.get()));
	EXPECT_TRUE(m_Player.ExtractMap(m_pStorage.get()));
	EXPECT_EQ(Read(MapDestination().c_str()), MAP_BYTES);
	ExpectOriginalPosition();
}

TEST_F(DemoMapReadIntegration, HealthySliceKeepsEmbeddedMapAndPlayableSnapshots)
{
	CreateDemo();
	ASSERT_TRUE(Slice());
	ASSERT_EQ(m_Player.Load(m_pStorage.get(), nullptr, TARGET, IStorage::TYPE_SAVE), 0);
	const CMapBuffer Data = MapData();
	ASSERT_NE(Data, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(Data.get()), 8), MAP_BYTES);
	EXPECT_EQ(m_Player.BaseInfo()->m_FirstTick, 100);
	EXPECT_EQ(m_Player.BaseInfo()->m_LastTick, 150);
}

TEST_F(DemoMapReadIntegration, HealthyLegacySliceKeepsEmbeddedMap)
{
	CreateDemo();
	MakeLegacyDemo();
	ASSERT_TRUE(m_pStorage->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Slice());
	ASSERT_EQ(m_Player.Load(m_pStorage.get(), nullptr, TARGET, IStorage::TYPE_SAVE), 0);
	const CMapBuffer Data = MapData();
	ASSERT_NE(Data, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(Data.get()), 8), MAP_BYTES);
}

TEST_F(DemoMapReadIntegration, SliceCannotReplaceFailedEmbeddedMapReadWithLocalMap)
{
	CreateDemo();
	Write(TARGET, "previous demo");
	ASSERT_TRUE(m_pStorage->CreateFolder("maps", IStorage::TYPE_SAVE));
	Write("maps/map.map", "fallback map");
	m_Operations.Fail(COperations::ALLOCATE);
	EXPECT_FALSE(Slice());
	EXPECT_EQ(Read(TARGET), "previous demo");
	EXPECT_EQ(m_Operations.m_aCalls[COperations::READ], 0);
}

TEST_F(DemoMapReadIntegration, SliceKeepsExistingTargetWhenEmbeddedMapReadIsShort)
{
	CreateDemo();
	Write(TARGET, "previous demo");
	m_Operations.Fail(COperations::READ);
	EXPECT_FALSE(Slice());
	EXPECT_EQ(Read(TARGET), "previous demo");
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 1);
}

TEST_F(DemoMapReadIntegration, SliceKeepsExistingTargetWhenMapPositionCannotBeRestored)
{
	CreateDemo();
	Write(TARGET, "previous demo");
	m_Operations.Fail(COperations::SEEK, 2);
	EXPECT_FALSE(Slice());
	EXPECT_EQ(Read(TARGET), "previous demo");
	EXPECT_EQ(m_Operations.m_aCalls[COperations::FREE], 1);
}

TEST_F(DemoMapReadIntegration, LegacySliceFailureStopsItsLoadedPlayerBeforeReturning)
{
	CreateDemo();
	MakeLegacyDemo();
	Write(TARGET, "previous demo");
	m_Operations.Fail(COperations::ALLOCATE);
	EXPECT_FALSE(Slice());
	EXPECT_EQ(Read(TARGET), "previous demo");
}
