#include <base/system.h>

#include <engine/client/ghost.h>
#include <engine/kernel.h>
#include <engine/shared/network.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <limits>
#include <memory>
#include <string>

namespace
{
	class COperations : public CGhostRecorder::CFileOperations
	{
	public:
		enum EOperation
		{
			OPEN,
			WRITE,
			SEEK,
			ERROR_FLAG,
			CLOSE,
			RENAME,
			REMOVE,
			COUNT
		};
		std::array<int, COUNT> m_aCalls{}, m_aFailAt{};
		void Fail(EOperation Operation, int After = 1) { m_aFailAt[Operation] = m_aCalls[Operation] + After; }
		void Clear() { m_aFailAt.fill(0); }
		bool Fails(EOperation Operation) { return ++m_aCalls[Operation] == m_aFailAt[Operation]; }
		IOHANDLE Open(IStorage *pStorage, const char *pFilename) noexcept override
		{
			return Fails(OPEN) ? nullptr : CFileOperations::Open(pStorage, pFilename);
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept override
		{
			return CFileOperations::Write(File, pData, Fails(WRITE) && Size > 0 ? Size - 1 : Size);
		}
		int Seek(IOHANDLE File, int64_t Offset, ESeekOrigin Origin) noexcept override
		{
			return Fails(SEEK) ? -1 : CFileOperations::Seek(File, Offset, Origin);
		}
		int Error(IOHANDLE File) noexcept override { return Fails(ERROR_FLAG) ? 1 : CFileOperations::Error(File); }
		int Close(IOHANDLE File) noexcept override
		{
			const int Result = CFileOperations::Close(File);
			return Fails(CLOSE) ? 1 : Result;
		}
		bool Rename(IStorage *pStorage, const char *pSource, const char *pDestination) noexcept override
		{
			return !Fails(RENAME) && CFileOperations::Rename(pStorage, pSource, pDestination);
		}
		bool Remove(IStorage *pStorage, const char *pFilename) noexcept override
		{
			return !Fails(REMOVE) && CFileOperations::Remove(pStorage, pFilename);
		}
	};
	class GhostRecorderIoIntegration : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		COperations m_Operations;
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		CGhostRecorder m_Recorder{&m_Operations};
		CGhostLoader m_Loader;
		std::string m_Previous;
		static constexpr const char *SOURCE = "capture.gho";
		static constexpr const char *TEMPORARY = "capture.gho.tmp";
		static constexpr const char *FINAL = "finished.gho";

		void SetUp() override
		{
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IGhostRecorder>(&m_Recorder, false);
			m_pKernel->RegisterInterface<IGhostLoader>(&m_Loader, false);
			m_Recorder.Init();
			m_Loader.Init();
			CNetBase::Init();
		}
		void TearDown() override
		{
			m_Operations.Clear();
			m_Loader.Close();
			m_Recorder.Stop(0, -1);
		}
		int Start() { return m_Recorder.Start(SOURCE, "map", SHA256_DIGEST{}, "test"); }
		void RecordOne(uint32_t Value = 7) { ASSERT_TRUE(m_Recorder.WriteData(2, &Value, sizeof(Value))); }
		std::string Read(const char *pFilename)
		{
			std::unique_ptr<void, decltype(&io_close)> File(m_pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_SAVE), io_close);
			EXPECT_NE(File, nullptr);
			if(!File)
				return {};
			const int64_t Size = io_length(File.get());
			EXPECT_GE(Size, 0);
			EXPECT_LE(Size, 65536);
			if(Size < 0 || Size > 65536)
				return {};
			std::string Data(static_cast<size_t>(Size), '\0');
			EXPECT_EQ(io_read(File.get(), Data.data(), Data.size()), Data.size());
			return Data;
		}
		void SavePrevious()
		{
			ASSERT_EQ(Start(), 0);
			ASSERT_NO_FATAL_FAILURE(RecordOne(3));
			ASSERT_TRUE(m_Recorder.Stop(1, 1000));
			m_Previous = Read(SOURCE);
			ASSERT_FALSE(m_Previous.empty());
		}
		void ExpectPrevious()
		{
			EXPECT_FALSE(m_Recorder.IsRecording());
			EXPECT_EQ(Read(SOURCE), m_Previous);
			EXPECT_FALSE(m_pStorage->FileExists(TEMPORARY, IStorage::TYPE_SAVE));
		}
		void ExpectRecord(const char *pFilename, uint32_t Value)
		{
			ASSERT_TRUE(m_Loader.Load(pFilename, "map", SHA256_DIGEST{}, 0));
			EXPECT_EQ(m_Loader.GetInfo()->m_NumTicks, 1);
			EXPECT_EQ(m_Loader.GetInfo()->m_Time, 1000);
			int Type = -1;
			ASSERT_TRUE(m_Loader.ReadNextType(&Type));
			EXPECT_EQ(Type, 2);
			uint32_t Actual = 0;
			ASSERT_TRUE(m_Loader.ReadData(Type, &Actual, sizeof(Actual)));
			EXPECT_EQ(Actual, Value);
			EXPECT_FALSE(m_Loader.ReadNextType(&Type));
			m_Loader.Close();
		}
	};
}

TEST_F(GhostRecorderIoIntegration, SuccessfulStopPublishesAClosedReadableFile)
{
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	EXPECT_FALSE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_Recorder.Stop(1, 1000));
	EXPECT_FALSE(m_Recorder.IsRecording());
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 1);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::RENAME], 1);
	EXPECT_FALSE(m_pStorage->FileExists(TEMPORARY, IStorage::TYPE_SAVE));
	ASSERT_NO_FATAL_FAILURE(ExpectRecord(SOURCE, 7));
}

TEST_F(GhostRecorderIoIntegration, HeaderShortWriteClosesTheCandidateAndKeepsThePreviousFile)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	m_Operations.Fail(COperations::WRITE);
	EXPECT_EQ(Start(), -1);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 2);
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, ChunkHeaderShortWritePreventsPublication)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::WRITE);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, ChunkPayloadShortWritePreventsPublication)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::WRITE, 2);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, HeaderSeekFailureSkipsPatchingAndKeepsThePreviousFile)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::SEEK);
	const int Writes = m_Operations.m_aCalls[COperations::WRITE];
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], Writes + 2);
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, TickCountShortWritePreventsPublication)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::WRITE, 3);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, TimeShortWritePreventsPublication)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::WRITE, 4);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, DeferredStreamErrorBeforeClosePreventsPublication)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::ERROR_FLAG, 5);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, CloseFailureClosesOnceAndKeepsThePreviousFile)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::CLOSE);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	const int Closes = m_Operations.m_aCalls[COperations::CLOSE];
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], Closes);
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, RenameFailureKeepsThePreviousFile)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::RENAME);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, FailedChunkWriteRejectsFurtherAppendsWithoutMoreIo)
{
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::WRITE);
	uint32_t Value = 11;
	EXPECT_FALSE(m_Recorder.WriteData(3, &Value, sizeof(Value)));
	const int Writes = m_Operations.m_aCalls[COperations::WRITE];
	EXPECT_FALSE(m_Recorder.WriteData(2, &Value, sizeof(Value)));
	EXPECT_EQ(m_Operations.m_aCalls[COperations::WRITE], Writes);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	EXPECT_FALSE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
}

TEST_F(GhostRecorderIoIntegration, CompressionFailureCannotPublishACompletedRecording)
{
	ASSERT_EQ(Start(), 0);
	std::array<int32_t, MAX_ITEM_SIZE / sizeof(int32_t)> aWords{};
	for(int i = 0; i < NUM_ITEMS_PER_CHUNK; ++i)
	{
		aWords.fill(i % 2 == 0 ? std::numeric_limits<int32_t>::max() : 0);
		EXPECT_EQ(m_Recorder.WriteData(2, aWords.data(), sizeof(aWords)), i + 1 < NUM_ITEMS_PER_CHUNK);
	}
	EXPECT_FALSE(m_Recorder.Stop(NUM_ITEMS_PER_CHUNK, 1000));
	EXPECT_FALSE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
}

TEST_F(GhostRecorderIoIntegration, DiscardClosesOnlyTheCandidateAndKeepsThePreviousFile)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	EXPECT_FALSE(m_Recorder.Stop(0, -1));
	EXPECT_EQ(m_Operations.m_aCalls[COperations::RENAME], 1);
	ExpectPrevious();
}

TEST_F(GhostRecorderIoIntegration, ANewStartAfterFailureCanPublishNormally)
{
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::CLOSE);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	m_Operations.Clear();
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne(11));
	ASSERT_TRUE(m_Recorder.Stop(1, 1000));
	ASSERT_NO_FATAL_FAILURE(ExpectRecord(SOURCE, 11));
}

TEST_F(GhostRecorderIoIntegration, StopCanPublishDirectlyToTheFinalFilename)
{
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	ASSERT_TRUE(m_Recorder.Stop(1, 1000, FINAL));
	EXPECT_FALSE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists(TEMPORARY, IStorage::TYPE_SAVE));
	ASSERT_NO_FATAL_FAILURE(ExpectRecord(FINAL, 7));
}

TEST_F(GhostRecorderIoIntegration, AFinalDestinationDirectoryIsPreservedOnRenameFailure)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(FINAL, IStorage::TYPE_SAVE));
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	EXPECT_FALSE(m_Recorder.Stop(1, 1000, FINAL));
	EXPECT_TRUE(m_pStorage->RemoveFolder(FINAL, IStorage::TYPE_SAVE));
}

TEST_F(GhostRecorderIoIntegration, DestructionClosesWithoutPublishingOrUsingStorage)
{
	{
		std::unique_ptr<IKernel> pKernel{IKernel::Create()};
		CGhostRecorder Recorder(&m_Operations);
		pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
		pKernel->RegisterInterface<IGhostRecorder>(&Recorder, false);
		Recorder.Init();
		ASSERT_EQ(Recorder.Start(SOURCE, "map", SHA256_DIGEST{}, "test"), 0);
		uint32_t Value = 7;
		ASSERT_TRUE(Recorder.WriteData(2, &Value, sizeof(Value)));
	}
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 1);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::RENAME], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::REMOVE], 0);
	EXPECT_FALSE(m_pStorage->FileExists(SOURCE, IStorage::TYPE_SAVE));
	EXPECT_TRUE(m_pStorage->FileExists(TEMPORARY, IStorage::TYPE_SAVE));
}

TEST_F(GhostRecorderIoIntegration, RepeatedStartDoesNotReplaceTheActiveCandidate)
{
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	EXPECT_EQ(m_Recorder.Start(FINAL, "map", SHA256_DIGEST{}, "test"), -1);
	ASSERT_TRUE(m_Recorder.Stop(1, 1000));
	EXPECT_FALSE(m_pStorage->FileExists(FINAL, IStorage::TYPE_SAVE));
	ASSERT_NO_FATAL_FAILURE(ExpectRecord(SOURCE, 7));
}

TEST_F(GhostRecorderIoIntegration, FailedTemporaryCleanupDoesNotPublishOrChangeTheOldFile)
{
	ASSERT_NO_FATAL_FAILURE(SavePrevious());
	ASSERT_EQ(Start(), 0);
	ASSERT_NO_FATAL_FAILURE(RecordOne());
	m_Operations.Fail(COperations::CLOSE);
	m_Operations.Fail(COperations::REMOVE);
	EXPECT_FALSE(m_Recorder.Stop(1, 1000));
	EXPECT_EQ(Read(SOURCE), m_Previous);
	EXPECT_TRUE(m_pStorage->FileExists(TEMPORARY, IStorage::TYPE_SAVE));
}

TEST_F(GhostRecorderIoIntegration, FailedOpenDoesNotDeleteAnExistingStagingDirectory)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(TEMPORARY, IStorage::TYPE_SAVE));
	EXPECT_EQ(Start(), -1);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::CLOSE], 0);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::REMOVE], 0);
	EXPECT_TRUE(m_pStorage->RemoveFolder(TEMPORARY, IStorage::TYPE_SAVE));
}

TEST_F(GhostRecorderIoIntegration, ANameWithoutRoomForTheStagingSuffixDoesNotOpenAFile)
{
	const std::string Name(IO_MAX_PATH_LENGTH - 4, 'a');
	EXPECT_EQ(m_Recorder.Start(Name.c_str(), "map", SHA256_DIGEST{}, "test"), -1);
	EXPECT_EQ(m_Operations.m_aCalls[COperations::OPEN], 0);
	EXPECT_FALSE(m_Recorder.IsRecording());
}
