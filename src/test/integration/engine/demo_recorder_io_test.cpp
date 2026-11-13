// 记录器、真实隔离文件与保存状态协作；故障只注入文件操作的返回结果。
#include <engine/shared/config.h>
#include <engine/shared/demo.h>
#include <engine/shared/network.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/race_demo_storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <chrono>
#include <memory>
#include <string>

namespace
{
	class CObservedDemoFileOperations : public CDemoRecorder::CFileOperations
	{
	public:
		enum EOperation
		{
			READ,
			WRITE,
			SEEK,
			LENGTH,
			FLUSH,
			ERROR,
			CLOSE,
			COUNT,
		};

		std::array<int, COUNT> m_aCalls{};
		EOperation m_Failure = COUNT;
		int m_FailureCall = 0;

		void Fail(EOperation Operation, int AfterCalls = 1)
		{
			m_Failure = Operation;
			m_FailureCall = m_aCalls[Operation] + AfterCalls;
		}

		bool Fails(EOperation Operation) noexcept
		{
			return ++m_aCalls[Operation] == m_FailureCall && m_Failure == Operation;
		}

		unsigned Read(IOHANDLE File, void *pData, unsigned Size) noexcept override
		{
			return CFileOperations::Read(File, pData, Fails(READ) && Size > 0 ? Size - 1 : Size);
		}

		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept override
		{
			return CFileOperations::Write(File, pData, Fails(WRITE) && Size > 0 ? Size - 1 : Size);
		}

		int Seek(IOHANDLE File, int64_t Offset, ESeekOrigin Origin) noexcept override
		{
			return Fails(SEEK) ? -1 : CFileOperations::Seek(File, Offset, Origin);
		}

		int64_t Length(IOHANDLE File) noexcept override
		{
			return Fails(LENGTH) ? -1 : CFileOperations::Length(File);
		}

		int Flush(IOHANDLE File) noexcept override
		{
			const int Result = CFileOperations::Flush(File);
			return Fails(FLUSH) ? -1 : Result;
		}

		int Error(IOHANDLE File) noexcept override
		{
			return Fails(ERROR) ? 1 : CFileOperations::Error(File);
		}

		int Close(IOHANDLE File) noexcept override
		{
			const int Result = CFileOperations::Close(File);
			return Fails(CLOSE) ? 1 : Result;
		}
	};

	class DemoRecorderIoIntegration : public ::testing::Test
	{
	protected:
		using COperations = CObservedDemoFileOperations;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		COperations m_Operations;
		CDemoRecorder m_Recorder{nullptr, false, &m_Operations};
		IOHANDLE m_MapFile = nullptr;
		int m_SliceBegin = -1;
		int m_SliceEnd = -1;
		static constexpr const char *SOURCE = "capture.demo";
		static constexpr const char *TARGET = "saved.demo";

		void SetUp() override
		{
			m_SliceBegin = g_Config.m_ClDemoSliceBegin;
			m_SliceEnd = g_Config.m_ClDemoSliceEnd;
			ASSERT_NE(m_pStorage, nullptr);
			CNetBase::Init();
		}

		void TearDown() override
		{
			m_Operations.m_Failure = COperations::COUNT;
			if(m_Recorder.IsRecording())
				m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE);
			if(m_MapFile)
				io_close(m_MapFile);
			g_Config.m_ClDemoSliceBegin = m_SliceBegin;
			g_Config.m_ClDemoSliceEnd = m_SliceEnd;
		}

		int Start(unsigned char *pMapData = nullptr, unsigned MapSize = 0, IOHANDLE MapFile = nullptr)
		{
			unsigned char EmptyMap = 0;
			return m_Recorder.Start(m_pStorage.get(), nullptr, SOURCE, "test", "map", SHA256_DIGEST{}, 0, "client", MapSize, pMapData ? pMapData : (MapFile ? nullptr : &EmptyMap), MapFile, nullptr, nullptr);
		}

		void Write(const char *pFilename, const char *pText)
		{
			IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, pText, str_length(pText)), static_cast<unsigned>(str_length(pText)));
			EXPECT_EQ(io_close(File), 0);
		}

		std::string Read(const char *pFilename)
		{
			IOHANDLE File = m_pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_SAVE);
			EXPECT_NE(File, nullptr);
			if(!File)
				return {};
			const int64_t Size = io_length(File);
			std::string Bytes(Size > 0 ? static_cast<size_t>(Size) : 0, '\0');
			EXPECT_EQ(io_read(File, Bytes.data(), Bytes.size()), Bytes.size());
			EXPECT_EQ(io_close(File), 0);
			return Bytes;
		}

		void OpenMap()
		{
			Write("input.map", "mapbytes");
			m_MapFile = m_pStorage->OpenFile("input.map", IOFLAG_READ, IStorage::TYPE_SAVE);
			ASSERT_NE(m_MapFile, nullptr);
		}

		void RecordMessage()
		{
			const int aMessage[] = {1, 2, 3};
			m_Recorder.RecordMessage(aMessage, sizeof(aMessage));
		}

		void ExpectClosedFailure()
		{
			EXPECT_TRUE(m_Recorder.HasWriteError());
			EXPECT_FALSE(m_Recorder.IsRecording());
			EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 1);
			EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
			EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 1);
		}
	};
}

TEST_F(DemoRecorderIoIntegration, HealthyRecordingKeepsReadableHeaderLengthAndTimeline)
{
	ASSERT_EQ(Start(), 0);
	m_Recorder.RecordTickMarker(100, true);
	m_Recorder.RecordTickMarker(150, true);
	ASSERT_TRUE(m_Recorder.AddDemoMarker(150));
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE, TARGET), 0);
	EXPECT_FALSE(m_Recorder.HasWriteError());
	EXPECT_FALSE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	CDemoPlayer Player(nullptr, nullptr, false);
	CDemoHeader Header{};
	CTimelineMarkers Markers{};
	CMapInfo MapInfo{};
	ASSERT_TRUE(Player.GetDemoInfo(m_pStorage.get(), nullptr, TARGET, IStorage::TYPE_SAVE, &Header, &Markers, &MapInfo));
	EXPECT_TRUE(Header.Valid());
	EXPECT_EQ(bytes_be_to_uint(Header.m_aLength), 1U);
	EXPECT_EQ(bytes_be_to_uint(Markers.m_aNumTimelineMarkers), 1U);
	EXPECT_EQ(bytes_be_to_uint(Markers.m_aTimelineMarkers[0]), 150U);
	EXPECT_EQ(Player.Load(m_pStorage.get(), nullptr, TARGET, IStorage::TYPE_SAVE), 0);
	Player.Stop();
}

TEST_F(DemoRecorderIoIntegration, HealthySnapshotAndMessageChunksRemainReadable)
{
	ASSERT_EQ(Start(), 0);
	const CSnapshot *pSnapshot = CSnapshot::EmptySnapshot();
	const int SnapshotSize = static_cast<int>(pSnapshot->AsSlice().size() * sizeof(int32_t));
	m_Recorder.RecordSnapshot(100, pSnapshot, SnapshotSize);
	RecordMessage();
	m_Recorder.RecordSnapshot(150, pSnapshot, SnapshotSize, true);
	ASSERT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
	CDemoPlayer Player(nullptr, nullptr, false);
	EXPECT_EQ(Player.Load(m_pStorage.get(), nullptr, SOURCE, IStorage::TYPE_SAVE), 0);
	Player.Stop();
}

TEST_F(DemoRecorderIoIntegration, SnapshotMarkerFailureStopsBeforeSnapshotPayload)
{
	ASSERT_EQ(Start(), 0);
	const int Writes = m_Operations.m_aCalls[COperations::WRITE];
	m_Operations.Fail(COperations::WRITE);
	const CSnapshot *pSnapshot = CSnapshot::EmptySnapshot();
	m_Recorder.RecordSnapshot(100, pSnapshot, static_cast<int>(pSnapshot->AsSlice().size() * sizeof(int32_t)));
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], Writes + 1);
	EXPECT_TRUE(m_Recorder.HasWriteError());
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
}

TEST_F(DemoRecorderIoIntegration, MissingMapClosesAlreadyOpenedOutput)
{
	EXPECT_EQ(m_Recorder.Start(m_pStorage.get(), nullptr, SOURCE, "test", "missing-map", SHA256_DIGEST{}, 0, "client", 0, nullptr, nullptr, nullptr, nullptr), -1);
	EXPECT_FALSE(m_Recorder.IsRecording());
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 1);
	EXPECT_TRUE(m_pStorage->RenameFile(SOURCE, TARGET, IStorage::TYPE_SAVE));
}

TEST_F(DemoRecorderIoIntegration, ShortHeaderWriteClosesOutputAndRejectsStart)
{
	m_Operations.Fail(COperations::WRITE);
	EXPECT_EQ(Start(), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, ShortTimelineReservationClosesOutputAndRejectsStart)
{
	m_Operations.Fail(COperations::WRITE, 2);
	EXPECT_EQ(Start(), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, ShortShaExtensionClosesOutputAndRejectsStart)
{
	m_Operations.Fail(COperations::WRITE, 3);
	EXPECT_EQ(Start(), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, ShortShaDigestClosesOutputAndRejectsStart)
{
	m_Operations.Fail(COperations::WRITE, 4);
	EXPECT_EQ(Start(), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, ShortEmbeddedMapWriteRejectsStart)
{
	unsigned char aMap[] = {1, 2, 3, 4};
	m_Operations.Fail(COperations::WRITE, 5);
	EXPECT_EQ(Start(aMap, sizeof(aMap)), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, BorrowedMapSeekFailureLeavesCallerHandleOpen)
{
	OpenMap();
	m_Operations.Fail(COperations::SEEK);
	EXPECT_EQ(Start(nullptr, 0, m_MapFile), -1);
	ExpectClosedFailure();
	EXPECT_EQ(io_tell(m_MapFile), 0);
	char Byte = 0;
	EXPECT_EQ(io_read(m_MapFile, &Byte, 1), 1U);
	EXPECT_EQ(Byte, 'm');
}

TEST_F(DemoRecorderIoIntegration, NegativeMapLengthRejectsStartAndRewindsBorrowedInput)
{
	OpenMap();
	m_Operations.Fail(COperations::LENGTH);
	EXPECT_EQ(Start(nullptr, 0, m_MapFile), -1);
	ExpectClosedFailure();
	EXPECT_EQ(io_tell(m_MapFile), 0);
}

TEST_F(DemoRecorderIoIntegration, TruncatedMapReadCannotPublishPartialMap)
{
	OpenMap();
	m_Operations.Fail(COperations::READ);
	EXPECT_EQ(Start(nullptr, 0, m_MapFile), -1);
	ExpectClosedFailure();
	EXPECT_EQ(io_tell(m_MapFile), 0);
}

TEST_F(DemoRecorderIoIntegration, BorrowedMapRewindFailureRejectsOtherwiseCompleteStart)
{
	OpenMap();
	m_Operations.Fail(COperations::SEEK, 2);
	EXPECT_EQ(Start(nullptr, 0, m_MapFile), -1);
	ExpectClosedFailure();
	EXPECT_EQ(io_tell(m_MapFile), 8);
}

TEST_F(DemoRecorderIoIntegration, HealthyBorrowedMapRemainsOpenAndRewindsForCaller)
{
	OpenMap();
	ASSERT_EQ(Start(nullptr, 0, m_MapFile), 0);
	EXPECT_EQ(io_tell(m_MapFile), 0);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 1);
	CDemoPlayer Player(nullptr, nullptr, false);
	CDemoHeader Header{};
	CTimelineMarkers Markers{};
	CMapInfo MapInfo{};
	ASSERT_TRUE(Player.GetDemoInfo(m_pStorage.get(), nullptr, SOURCE, IStorage::TYPE_SAVE, &Header, &Markers, &MapInfo));
	EXPECT_EQ(bytes_be_to_uint(Header.m_aMapSize), 8U);
}

TEST_F(DemoRecorderIoIntegration, OwnedMapCloseFailureRejectsStartAndClosesOutputToo)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("maps", IStorage::TYPE_SAVE));
	Write("maps/map.map", "mapbytes");
	m_Operations.Fail(COperations::CLOSE);
	EXPECT_EQ(m_Recorder.Start(m_pStorage.get(), nullptr, SOURCE, "test", "map", SHA256_DIGEST{}, 0, "client", 0, nullptr, nullptr, nullptr, nullptr), -1);
	EXPECT_FALSE(m_Recorder.IsRecording());
	EXPECT_TRUE(m_Recorder.HasWriteError());
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 2);
}

TEST_F(DemoRecorderIoIntegration, RecordingWithoutMapDataStillReleasesOwnedMap)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("maps", IStorage::TYPE_SAVE));
	Write("maps/map.map", "mapbytes");
	CDemoRecorder Recorder(nullptr, true, &m_Operations);
	EXPECT_EQ(Recorder.Start(m_pStorage.get(), nullptr, SOURCE, "test", "map", SHA256_DIGEST{}, 0, "client", 0, nullptr, nullptr, nullptr, nullptr), 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 1);
	EXPECT_EQ(Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 2);
}

TEST_F(DemoRecorderIoIntegration, ShortTickMarkerDoesNotAdvanceLengthOrAllowFurtherChunks)
{
	ASSERT_EQ(Start(), 0);
	m_Recorder.RecordTickMarker(100);
	m_Operations.Fail(COperations::WRITE);
	m_Recorder.RecordTickMarker(200);
	EXPECT_EQ(m_Recorder.Length(), 0);
	const int Writes = m_Operations.m_aCalls[COperations::WRITE];
	m_Recorder.RecordTickMarker(300);
	RecordMessage();
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], Writes);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, ShortChunkHeaderStopsBeforePayload)
{
	ASSERT_EQ(Start(), 0);
	const int Writes = m_Operations.m_aCalls[COperations::WRITE];
	m_Operations.Fail(COperations::WRITE);
	RecordMessage();
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], Writes + 1);
	EXPECT_TRUE(m_Recorder.HasWriteError());
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
}

TEST_F(DemoRecorderIoIntegration, ShortChunkPayloadLatchesFailureAcrossLaterWrites)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::WRITE, 2);
	RecordMessage();
	EXPECT_TRUE(m_Recorder.HasWriteError());
	const int Writes = m_Operations.m_aCalls[COperations::WRITE];
	RecordMessage();
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], Writes);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
}

TEST_F(DemoRecorderIoIntegration, LengthSeekFailureStillClosesAndKeepsSource)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::SEEK);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE, TARGET), -1);
	ExpectClosedFailure();
	EXPECT_TRUE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists(TARGET, IStorage::TYPE_SAVE));
}

TEST_F(DemoRecorderIoIntegration, LengthWriteFailureStillClosesAndRejectsSuccess)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::WRITE);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, TimelineSeekFailureStillClosesAndRejectsSuccess)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::SEEK, 2);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, TimelineCountWriteFailureStillClosesAndRejectsSuccess)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::WRITE, 2);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, TimelineMarkerWriteFailureStillClosesAndRejectsSuccess)
{
	ASSERT_EQ(Start(), 0);
	m_Recorder.RecordTickMarker(100);
	ASSERT_TRUE(m_Recorder.AddDemoMarker(100));
	m_Operations.Fail(COperations::WRITE, 3);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, FlushFailureCannotRenameToExistingSuccessFile)
{
	Write(TARGET, "previous success");
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::FLUSH);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE, TARGET), -1);
	ExpectClosedFailure();
	EXPECT_TRUE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_EQ(Read(TARGET), "previous success");
}

TEST_F(DemoRecorderIoIntegration, StreamErrorAfterFullWriteRejectsStart)
{
	m_Operations.Fail(COperations::ERROR);
	EXPECT_EQ(Start(), -1);
	ExpectClosedFailure();
}

TEST_F(DemoRecorderIoIntegration, CloseFailureCannotRenameToSuccessFile)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::CLOSE);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE, TARGET), -1);
	ExpectClosedFailure();
	EXPECT_TRUE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists(TARGET, IStorage::TYPE_SAVE));
}

TEST_F(DemoRecorderIoIntegration, StreamErrorAtStopStillClosesAndKeepsSource)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::ERROR);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE, TARGET), -1);
	ExpectClosedFailure();
	EXPECT_TRUE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists(TARGET, IStorage::TYPE_SAVE));
}

TEST_F(DemoRecorderIoIntegration, ExplicitDiscardRemovesBrokenCaptureWithoutPublishingIt)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::WRITE);
	m_Recorder.RecordTickMarker(100);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::REMOVE_FILE), -1);
	ExpectClosedFailure();
	EXPECT_FALSE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
}

TEST_F(DemoRecorderIoIntegration, NewRecordingClearsPreviousIoFailure)
{
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::WRITE);
	m_Recorder.RecordTickMarker(100);
	ASSERT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::REMOVE_FILE), -1);
	ASSERT_TRUE(m_Recorder.HasWriteError());
	ASSERT_EQ(Start(), 0);
	EXPECT_FALSE(m_Recorder.HasWriteError());
	m_Recorder.RecordTickMarker(100);
	EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE, TARGET), 0);
	EXPECT_TRUE(m_pStorage->FileExists(TARGET, IStorage::TYPE_SAVE));
}

TEST_F(DemoRecorderIoIntegration, TickMarkerBeforeStartOrAfterStopDoesNotTouchAFile)
{
	m_Recorder.RecordTickMarker(100);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], 0);
	ASSERT_EQ(Start(), 0);
	ASSERT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
	const int Writes = m_Operations.m_aCalls[COperations::WRITE];
	m_Recorder.RecordTickMarker(150);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], Writes);
}

TEST_F(DemoRecorderIoIntegration, RaceFinalizerRetainsBrokenRealRecordingAndPreviousBest)
{
	Write("map_20.000.demo", "previous best");
	ASSERT_EQ(Start(), 0);
	m_Operations.Fail(COperations::WRITE);
	m_Recorder.RecordTickMarker(100);
	qm_race_demo::CFinalizer Finalizer;
	ASSERT_TRUE(Finalizer.Queue(SOURCE, "map", "", false, 10000));
	qm_race_demo::CStorageOperations StorageOperations(*m_pStorage);
	int Notifications = 0;
	const auto Result = Finalizer.Try(std::chrono::nanoseconds(0), StorageOperations, [&]() {
			EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
			return qm_race_demo::CFinalizer::CStopResult{!m_Recorder.IsRecording(), !m_Recorder.HasWriteError()}; }, []() {}, [&](bool AfterCommit) {
			EXPECT_FALSE(AfterCommit);
			++Notifications; });
	EXPECT_EQ(Result, qm_race_demo::CFinalizer::EOutcome::RETRY);
	EXPECT_TRUE(Finalizer.Pending());
	EXPECT_TRUE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_EQ(Read("map_20.000.demo"), "previous best");
	EXPECT_FALSE(m_pStorage->FileExists("map_10.000.demo", IStorage::TYPE_SAVE));
	EXPECT_EQ(Notifications, 1);
}

TEST_F(DemoRecorderIoIntegration, RaceFinalizerRefusesCaptureWhoseOnlyFailureIsClose)
{
	Write("map_20.000.demo", "previous best");
	ASSERT_EQ(Start(), 0);
	m_Recorder.RecordTickMarker(100);
	m_Operations.Fail(COperations::CLOSE);
	qm_race_demo::CFinalizer Finalizer;
	ASSERT_TRUE(Finalizer.Queue(SOURCE, "map", "", false, 10000));
	qm_race_demo::CStorageOperations StorageOperations(*m_pStorage);
	const auto Result = Finalizer.Try(std::chrono::nanoseconds(0), StorageOperations, [&]() {
			EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), -1);
			return qm_race_demo::CFinalizer::CStopResult{!m_Recorder.IsRecording(), !m_Recorder.HasWriteError()}; }, []() {}, [](bool) {});
	EXPECT_EQ(Result, qm_race_demo::CFinalizer::EOutcome::RETRY);
	EXPECT_FALSE(m_Recorder.IsRecording());
	EXPECT_TRUE(m_Recorder.HasWriteError());
	EXPECT_EQ(Read("map_20.000.demo"), "previous best");
	EXPECT_TRUE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("map_10.000.demo", IStorage::TYPE_SAVE));
}
