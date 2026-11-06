#include <game/client/components/qmclient/audio_pack_export.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <string>

namespace
{
	class CExportFaults : public qm_audio_pack::CExportFileOperations
	{
	public:
		enum class EFailure
		{
			NONE,
			READ,
			READ_THROW,
			WRITE,
			FLUSH,
			ERROR_FLAG,
			SOURCE_CLOSE,
			DESTINATION_CLOSE,
			PUBLISH,
			PUBLISH_THROW,
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Closes = 0;
		int m_Writes = 0;
		int m_Publishes = 0;

		bool ReadAll(IOHANDLE File, void **ppData, unsigned *pSize) override
		{
			const bool Read = CExportFileOperations::ReadAll(File, ppData, pSize);
			if(m_Failure == EFailure::READ_THROW)
				throw std::bad_alloc();
			return Read && m_Failure != EFailure::READ;
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
		{
			++m_Writes;
			return CExportFileOperations::Write(File, pData, m_Failure == EFailure::WRITE && Size ? Size - 1 : Size);
		}
		int Flush(IOHANDLE File) override
		{
			const int Result = CExportFileOperations::Flush(File);
			return m_Failure == EFailure::FLUSH ? -1 : Result;
		}
		int Error(IOHANDLE File) override
		{
			const int Result = CExportFileOperations::Error(File);
			return m_Failure == EFailure::ERROR_FLAG ? 1 : Result;
		}
		int Close(IOHANDLE File) noexcept override
		{
			const int Result = CExportFileOperations::Close(File);
			++m_Closes;
			return (m_Closes == 1 && m_Failure == EFailure::SOURCE_CLOSE) || (m_Closes == 2 && m_Failure == EFailure::DESTINATION_CLOSE) ? -1 : Result;
		}
		bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget) override
		{
			++m_Publishes;
			if(m_Failure == EFailure::PUBLISH_THROW)
				throw std::bad_alloc();
			return m_Failure != EFailure::PUBLISH && CExportFileOperations::Publish(Storage, pTemporary, pTarget);
		}
	};

	class CQmAudioPackExport : public ::testing::Test
	{
	protected:
		static constexpr const char *SOURCE = "source.wv";
		static constexpr const char *TARGET = "audio/pack/hit.wv";
		const std::string m_New = std::string("new\0bytes", 9);
		const std::string m_Old = "old sound";
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CExportFaults m_Operations;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("audio", IStorage::TYPE_SAVE));
			ASSERT_TRUE(m_pStorage->CreateFolder("audio/pack", IStorage::TYPE_SAVE));
			Store(SOURCE, m_New);
			Store(TARGET, m_Old);
		}
		void Store(const char *pPath, const std::string &Content)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Content.data(), Content.size()), Content.size());
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Read(const char *pPath)
		{
			void *pRaw = nullptr;
			unsigned Size = 0;
			EXPECT_TRUE(m_pStorage->ReadFile(pPath, IStorage::TYPE_SAVE, &pRaw, &Size));
			const std::unique_ptr<void, decltype(&free)> Data(pRaw, free);
			return pRaw ? std::string(static_cast<const char *>(pRaw), Size) : "";
		}
		bool Export(const char *pSource = SOURCE, const char *pTarget = TARGET)
		{
			return qm_audio_pack::ExportFile(m_pStorage.get(), pSource, IStorage::TYPE_ALL, pTarget, &m_Operations);
		}
		void ExpectFailedCopyPreservedFiles(int Closes, int Publishes = 0)
		{
			EXPECT_EQ(Read(SOURCE), m_New);
			EXPECT_EQ(Read(TARGET), m_Old);
			EXPECT_EQ(m_Operations.m_Closes, Closes);
			EXPECT_EQ(m_Operations.m_Publishes, Publishes);
			EXPECT_FALSE(m_pStorage->FileExists(qm_audio_pack::ExportTemporaryPath(TARGET).c_str(), IStorage::TYPE_SAVE));
		}
	};
}

TEST_F(CQmAudioPackExport, SuccessfulReplacementKeepsTheBinarySource)
{
	ASSERT_TRUE(Export());
	EXPECT_EQ(Read(SOURCE), m_New);
	EXPECT_EQ(Read(TARGET), m_New);
	EXPECT_EQ(m_Operations.m_Closes, 2);
	EXPECT_EQ(m_Operations.m_Publishes, 1);
	EXPECT_FALSE(m_pStorage->FileExists(qm_audio_pack::ExportTemporaryPath(TARGET).c_str(), IStorage::TYPE_SAVE));
}

TEST_F(CQmAudioPackExport, CopyingTheTargetOntoItselfPreservesAllBytes)
{
	ASSERT_TRUE(Export(TARGET));
	EXPECT_EQ(Read(TARGET), m_Old);
	EXPECT_EQ(m_Operations.m_Closes, 2);
}

TEST_F(CQmAudioPackExport, ManualAbsoluteSourceIsCopiedWithoutChangingTheOriginal)
{
	char aSource[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, SOURCE, aSource, sizeof(aSource));
	ASSERT_TRUE(qm_audio_pack::ExportFile(m_pStorage.get(), aSource, IStorage::TYPE_ABSOLUTE, TARGET, &m_Operations));
	EXPECT_EQ(Read(SOURCE), m_New);
	EXPECT_EQ(Read(TARGET), m_New);
}

TEST_F(CQmAudioPackExport, ShortWriteKeepsTheOldTargetAndAllowsRetry)
{
	m_Operations.m_Failure = CExportFaults::EFailure::WRITE;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(2);
	m_Operations.m_Failure = CExportFaults::EFailure::NONE;
	ASSERT_TRUE(Export());
	EXPECT_EQ(Read(TARGET), m_New);
}

TEST_F(CQmAudioPackExport, FlushFailurePreventsPublication)
{
	m_Operations.m_Failure = CExportFaults::EFailure::FLUSH;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(2);
}

TEST_F(CQmAudioPackExport, StreamErrorPreventsPublicationAfterAFullWrite)
{
	m_Operations.m_Failure = CExportFaults::EFailure::ERROR_FLAG;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(2);
}

TEST_F(CQmAudioPackExport, DestinationCloseFailureDoesNotReportSuccess)
{
	m_Operations.m_Failure = CExportFaults::EFailure::DESTINATION_CLOSE;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(2);
}

TEST_F(CQmAudioPackExport, SourceCloseFailureStopsBeforeTheCandidateIsOpened)
{
	m_Operations.m_Failure = CExportFaults::EFailure::SOURCE_CLOSE;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(1);
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmAudioPackExport, FailedReadClosesTheSourceAndPreservesTheTarget)
{
	m_Operations.m_Failure = CExportFaults::EFailure::READ;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(1);
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmAudioPackExport, ExceptionAfterReadClosesTheSourceWithoutWriting)
{
	m_Operations.m_Failure = CExportFaults::EFailure::READ_THROW;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(1);
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmAudioPackExport, FailedPublicationKeepsTheOldTargetAndAllowsRetry)
{
	m_Operations.m_Failure = CExportFaults::EFailure::PUBLISH;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(2, 1);
	m_Operations.m_Failure = CExportFaults::EFailure::NONE;
	ASSERT_TRUE(Export());
	EXPECT_EQ(Read(TARGET), m_New);
}

TEST_F(CQmAudioPackExport, PublicationExceptionPreservesFilesAndCleansTheCandidate)
{
	m_Operations.m_Failure = CExportFaults::EFailure::PUBLISH_THROW;
	EXPECT_FALSE(Export());
	ExpectFailedCopyPreservedFiles(2, 1);
}

TEST_F(CQmAudioPackExport, OccupiedTemporaryFileIsKeptUntouched)
{
	const std::string Temporary = qm_audio_pack::ExportTemporaryPath(TARGET);
	Store(Temporary.c_str(), "unrelated");
	EXPECT_FALSE(Export());
	EXPECT_EQ(Read(Temporary.c_str()), "unrelated");
	EXPECT_EQ(Read(TARGET), m_Old);
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(CQmAudioPackExport, OccupiedTemporaryDirectoryIsKeptUntouched)
{
	const std::string Temporary = qm_audio_pack::ExportTemporaryPath(TARGET);
	ASSERT_TRUE(m_pStorage->CreateFolder(Temporary.c_str(), IStorage::TYPE_SAVE));
	EXPECT_FALSE(Export());
	EXPECT_TRUE(m_pStorage->FolderExists(Temporary.c_str(), IStorage::TYPE_SAVE));
	EXPECT_EQ(Read(TARGET), m_Old);
}

TEST_F(CQmAudioPackExport, NewNestedDestinationPreservesTheSource)
{
	constexpr const char *pTarget = "audio/new/nested/hit.wv";
	ASSERT_TRUE(Export(SOURCE, pTarget));
	EXPECT_EQ(Read(pTarget), m_New);
	EXPECT_EQ(Read(SOURCE), m_New);
}

TEST_F(CQmAudioPackExport, EmptySourceRetainsTheExistingEmptyFileCopyBehavior)
{
	Store(SOURCE, "");
	ASSERT_TRUE(Export());
	EXPECT_EQ(Read(SOURCE), "");
	EXPECT_EQ(Read(TARGET), "");
}

TEST_F(CQmAudioPackExport, InvalidTraversalIsRejectedBeforeCreatingDirectories)
{
	EXPECT_FALSE(Export(SOURCE, "audio/pack/../unexpected/file.wv"));
	EXPECT_FALSE(m_pStorage->FolderExists("audio/unexpected", IStorage::TYPE_SAVE));
	ExpectFailedCopyPreservedFiles(0);
}

TEST_F(CQmAudioPackExport, BlockedParentDirectoryReturnsFailureWithoutChangingTheBlocker)
{
	Store("blocked", "keep");
	EXPECT_FALSE(Export(SOURCE, "blocked/child/hit.wv"));
	EXPECT_EQ(Read("blocked"), "keep");
	EXPECT_EQ(Read(TARGET), m_Old);
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(CQmAudioPackExport, MissingSourceDoesNotCreateTheNewDestinationDirectory)
{
	EXPECT_FALSE(Export("missing-source.wv", "audio/new/hit.wv"));
	EXPECT_FALSE(m_pStorage->FolderExists("audio/new", IStorage::TYPE_SAVE));
	ExpectFailedCopyPreservedFiles(0);
}

TEST_F(CQmAudioPackExport, UnavailableInputsLeaveTheExistingFilesUntouched)
{
	EXPECT_FALSE(qm_audio_pack::ExportFile(nullptr, SOURCE, IStorage::TYPE_ALL, TARGET));
	EXPECT_FALSE(Export(nullptr));
	EXPECT_FALSE(Export(""));
	EXPECT_FALSE(Export(SOURCE, nullptr));
	EXPECT_FALSE(Export(SOURCE, ""));
	ExpectFailedCopyPreservedFiles(0);
}
