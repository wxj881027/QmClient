#include <game/client/components/qmclient/qm_identity_file.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <new>
#include <string>

namespace
{
	class CIdentityFileFaults : public qm_identity_file::CFileOperations
	{
	public:
		enum class EFailure
		{
			NONE,
			OPEN,
			OPEN_THROW,
			WRITE,
			WRITE_THROW,
			SYNC,
			ERROR_FLAG,
			CLOSE,
			PUBLISH,
			PUBLISH_THROW,
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Writes = 0;
		int m_Closes = 0;
		int m_Publishes = 0;
		IOHANDLE Open(IStorage &Storage, const char *pPath) override
		{
			if(m_Failure == EFailure::OPEN_THROW)
				throw std::bad_alloc();
			return m_Failure == EFailure::OPEN ? nullptr : CFileOperations::Open(Storage, pPath);
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
		{
			++m_Writes;
			if(m_Failure == EFailure::WRITE_THROW)
			{
				CFileOperations::Write(File, pData, Size / 2);
				throw std::bad_alloc();
			}
			return CFileOperations::Write(File, pData, m_Failure == EFailure::WRITE ? Size - 1 : Size);
		}
		int Sync(IOHANDLE File) override
		{
			const int Result = CFileOperations::Sync(File);
			return m_Failure == EFailure::SYNC ? -1 : Result;
		}
		int Error(IOHANDLE File) override
		{
			const int Result = CFileOperations::Error(File);
			return m_Failure == EFailure::ERROR_FLAG ? 1 : Result;
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CFileOperations::Close(File);
			return m_Failure == EFailure::CLOSE ? -1 : Result;
		}
		bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget) override
		{
			++m_Publishes;
			EXPECT_EQ(m_Closes, m_Writes);
			if(m_Failure == EFailure::PUBLISH_THROW)
				throw std::bad_alloc();
			return m_Failure != EFailure::PUBLISH && CFileOperations::Publish(Storage, pTemporary, pTarget);
		}
	};

	class CQmIdentityFileTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CIdentityFileFaults m_Operations;
		using EFailure = CIdentityFileFaults::EFailure;
		using EFile = qm_identity_file::EFile;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			Store(qm_identity_file::CLIENT_ID_FILE, "qmold\n");
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
		std::string Temporary()
		{
			char aPath[IO_MAX_PATH_LENGTH];
			IStorage::FormatTmpPath(aPath, sizeof(aPath), qm_identity_file::CLIENT_ID_FILE);
			return aPath;
		}
		bool Save(std::string_view Value = "qmnew", EFile Kind = EFile::CLIENT_ID)
		{
			return qm_identity_file::Save(m_pStorage.get(), Kind, Value, &m_Operations);
		}
		void ExpectOldState(bool CandidateAbsent = true)
		{
			EXPECT_EQ(Read(qm_identity_file::CLIENT_ID_FILE), "qmold\n");
			if(CandidateAbsent)
				EXPECT_FALSE(m_pStorage->FileExists(Temporary().c_str(), IStorage::TYPE_SAVE));
		}
	};
}

TEST_F(CQmIdentityFileTest, CompleteClientIdReplacesOldFileWithOneNewline)
{
	ASSERT_TRUE(Save());
	EXPECT_EQ(Read(qm_identity_file::CLIENT_ID_FILE), "qmnew\n");
	EXPECT_EQ(m_Operations.m_Publishes, 1);
	EXPECT_FALSE(m_pStorage->FileExists(Temporary().c_str(), IStorage::TYPE_SAVE));
}

TEST_F(CQmIdentityFileTest, MachineIdentityUsesItsOwnFileAndKeepsClientId)
{
	ASSERT_TRUE(Save("machine", EFile::MACHINE_ID));
	EXPECT_EQ(Read(qm_identity_file::MACHINE_ID_FILE), "machine\n");
	ExpectOldState();
}

TEST_F(CQmIdentityFileTest, MaximumLengthValueKeepsEveryByte)
{
	const std::string Value(64, 'a');
	ASSERT_TRUE(Save(Value));
	EXPECT_EQ(Read(qm_identity_file::CLIENT_ID_FILE), Value + "\n");
}

TEST_F(CQmIdentityFileTest, InvalidLineValuesDoNotTruncateOldIdentity)
{
	struct SCase
	{
		const char *m_pName;
		std::string m_Value;
	};
	const SCase aInvalid[] = {{"empty", ""}, {"too_long", std::string(65, 'x')}, {"nul", std::string("a\0b", 3)}, {"lf", "a\nb"}, {"cr", "a\rb"}};
	for(const auto &Case : aInvalid)
	{
		SCOPED_TRACE(Case.m_pName);
		EXPECT_FALSE(Save(Case.m_Value));
		ExpectOldState();
	}
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmIdentityFileTest, ShortWritePreservesOldFileAndClosesCandidate)
{
	m_Operations.m_Failure = EFailure::WRITE;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmIdentityFileTest, WriteExceptionClosesPartialCandidateAndKeepsOldFile)
{
	m_Operations.m_Failure = EFailure::WRITE_THROW;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmIdentityFileTest, SyncFailurePreventsPublication)
{
	m_Operations.m_Failure = EFailure::SYNC;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmIdentityFileTest, ErrorFlagPreventsPublication)
{
	m_Operations.m_Failure = EFailure::ERROR_FLAG;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmIdentityFileTest, CloseFailureKeepsOldFile)
{
	m_Operations.m_Failure = EFailure::CLOSE;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmIdentityFileTest, PublishRejectionKeepsOldFileAndRemovesOwnedCandidate)
{
	m_Operations.m_Failure = EFailure::PUBLISH;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Publishes, 1);
}

TEST_F(CQmIdentityFileTest, PublishExceptionKeepsOldFile)
{
	m_Operations.m_Failure = EFailure::PUBLISH_THROW;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Publishes, 1);
}

TEST_F(CQmIdentityFileTest, OpenFailureKeepsOldFileWithoutClosingInvalidHandle)
{
	m_Operations.m_Failure = EFailure::OPEN;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 0);
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmIdentityFileTest, OpenExceptionDoesNotEscapeSave)
{
	m_Operations.m_Failure = EFailure::OPEN_THROW;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(CQmIdentityFileTest, ExistingTemporaryFileIsNotOverwrittenOrRemoved)
{
	Store(Temporary().c_str(), "foreign");
	EXPECT_FALSE(Save());
	ExpectOldState(false);
	EXPECT_EQ(Read(Temporary().c_str()), "foreign");
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmIdentityFileTest, ExistingTemporaryDirectoryIsNotRemoved)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(Temporary().c_str(), IStorage::TYPE_SAVE));
	EXPECT_FALSE(Save());
	ExpectOldState(false);
	EXPECT_TRUE(m_pStorage->FolderExists(Temporary().c_str(), IStorage::TYPE_SAVE));
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmIdentityFileTest, FailureCanBeFollowedByAnExplicitSuccessfulRetry)
{
	m_Operations.m_Failure = EFailure::WRITE;
	ASSERT_FALSE(Save());
	ExpectOldState();
	m_Operations.m_Failure = EFailure::NONE;
	ASSERT_TRUE(Save());
	EXPECT_EQ(Read(qm_identity_file::CLIENT_ID_FILE), "qmnew\n");
	EXPECT_EQ(m_Operations.m_Closes, 2);
}

TEST_F(CQmIdentityFileTest, FirstSaveFailureDoesNotPublishPartialNewIdentity)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(qm_identity_file::CLIENT_ID_FILE, IStorage::TYPE_SAVE));
	m_Operations.m_Failure = EFailure::WRITE;
	EXPECT_FALSE(Save());
	EXPECT_FALSE(m_pStorage->FileExists(qm_identity_file::CLIENT_ID_FILE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists(Temporary().c_str(), IStorage::TYPE_SAVE));
}

TEST_F(CQmIdentityFileTest, InvalidStorageOrFileKindDoesNotWrite)
{
	EXPECT_FALSE(qm_identity_file::Save(nullptr, EFile::CLIENT_ID, "qmnew"));
	EXPECT_FALSE(Save("qmnew", static_cast<EFile>(255)));
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Writes, 0);
}
