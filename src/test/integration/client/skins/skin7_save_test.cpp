#include <game/client/components/qmclient/skin7_info_job.h>
#include <game/client/components/qmclient/skin7_save.h>
#include <game/client/components/skins7.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <new>

namespace
{
	class CSaveFaults : public qm_skin7_save::CFileOperations
	{
	public:
		enum class EFailure
		{
			NONE,
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
		const std::vector<CSkins7::CSkin> *m_pVisibleSkins = nullptr;
		bool m_SawOldCache = false;
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
			m_SawOldCache = m_pVisibleSkins && m_pVisibleSkins->size() == 1 && m_pVisibleSkins->front().m_Flags == 0;
			if(m_Failure == EFailure::PUBLISH_THROW)
				throw std::bad_alloc();
			return m_Failure != EFailure::PUBLISH && CFileOperations::Publish(Storage, pTemporary, pTarget);
		}
	};

	class CQmSkin7SaveTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::vector<CSkins7::CSkin> m_Skins;
		CSaveFaults m_Operations;
		qm_skin7_save::TParts m_Parts{};
		const std::string m_Old = "{\"skin\":{\"body\":{\"filename\":\"old\"}}}\n";

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
			CSkins7::CSkin Skin{};
			str_copy(Skin.m_aName, "old");
			m_Skins.push_back(Skin);
			m_Operations.m_pVisibleSkins = &m_Skins;
			m_Parts[protocol7::SKINPART_BODY] = {"body\"name", true, 0x12345678u};
			m_Parts[protocol7::SKINPART_MARKING] = {"mark", true, 0x9abcdef0u};
			m_Parts[protocol7::SKINPART_HANDS] = {"standard"};
			Store("skins7/saved.json", m_Old);
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
		bool Save(const char *pName = "saved")
		{
			return qm_skin7_save::SaveAndPublish(m_pStorage.get(), pName, m_Parts, m_Skins, [](auto &Candidate) {
				Candidate.front().m_Flags = 7;
				CSkins7::CSkin Skin{};
				str_copy(Skin.m_aName, "new");
				Candidate.push_back(Skin); }, &m_Operations);
		}
		void ExpectOldState(bool CandidateAbsent = true)
		{
			EXPECT_EQ(Read("skins7/saved.json"), m_Old);
			ASSERT_EQ(m_Skins.size(), 1u);
			EXPECT_STREQ(m_Skins.front().m_aName, "old");
			EXPECT_EQ(m_Skins.front().m_Flags, 0);
			if(CandidateAbsent)
				EXPECT_FALSE(m_pStorage->FileExists(qm_skin7_save::TemporaryPath("skins7/saved.json").c_str(), IStorage::TYPE_SAVE));
		}
	};
}

TEST_F(CQmSkin7SaveTest, NativeJsonKeepsColorFieldsEscapingAndEmptyPartOmission)
{
	ASSERT_TRUE(Save());
	const std::string Json = Read("skins7/saved.json");
	auto Info = QmParseSkin7Info(Json.data(), Json.size());
	ASSERT_NE(Info, nullptr);
	const auto &Skin = (*Info)["skin"];
	EXPECT_STREQ(Skin["body"]["filename"], "body\"name");
	EXPECT_EQ(Skin["body"]["hue"].u.integer, 0x34);
	EXPECT_EQ(Skin["body"]["sat"].u.integer, 0x56);
	EXPECT_EQ(Skin["body"]["lgt"].u.integer, 0x78);
	EXPECT_EQ(Skin["body"]["alp"].type, json_none);
	EXPECT_EQ(Skin["marking"]["alp"].u.integer, 0x9a);
	EXPECT_FALSE(Skin["hands"]["custom_colors"].u.boolean);
	EXPECT_EQ(Skin["hands"]["hue"].type, json_none);
	EXPECT_EQ(Skin["eyes"].type, json_none);
	EXPECT_EQ(Json.back(), '\n');
}

TEST_F(CQmSkin7SaveTest, FileReplacementSeesOldCacheAndOnlyThenPublishesPreparedList)
{
	ASSERT_TRUE(Save());
	EXPECT_TRUE(m_Operations.m_SawOldCache);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 1);
	ASSERT_EQ(m_Skins.size(), 2u);
	EXPECT_EQ(m_Skins.front().m_Flags, 7);
	EXPECT_STREQ(m_Skins.back().m_aName, "new");
}

TEST_F(CQmSkin7SaveTest, MissingTargetIsCreatedFromCompleteJson)
{
	ASSERT_TRUE(Save("created"));
	const std::string Json = Read("skins7/created.json");
	EXPECT_NE(QmParseSkin7Info(Json.data(), Json.size()), nullptr);
	EXPECT_EQ(Read("skins7/saved.json"), m_Old);
}

TEST_F(CQmSkin7SaveTest, ShortWritePreservesOldFileAndListThenRetrySucceeds)
{
	m_Operations.m_Failure = CSaveFaults::EFailure::WRITE;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Publishes, 0);
	m_Operations.m_Failure = CSaveFaults::EFailure::NONE;
	ASSERT_TRUE(Save());
}

TEST_F(CQmSkin7SaveTest, SyncFailurePreservesOldFileAndList)
{
	m_Operations.m_Failure = CSaveFaults::EFailure::SYNC;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmSkin7SaveTest, StreamErrorPreservesOldFileAndList)
{
	m_Operations.m_Failure = CSaveFaults::EFailure::ERROR_FLAG;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmSkin7SaveTest, CloseFailurePreservesOldFileAndListWithoutDoubleClose)
{
	m_Operations.m_Failure = CSaveFaults::EFailure::CLOSE;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmSkin7SaveTest, ExceptionDuringPartialWriteClosesCandidateAndKeepsOldState)
{
	m_Operations.m_Failure = CSaveFaults::EFailure::WRITE_THROW;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CQmSkin7SaveTest, RenameRejectionPreservesOldFileAndList)
{
	m_Operations.m_Failure = CSaveFaults::EFailure::PUBLISH;
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_TRUE(m_Operations.m_SawOldCache);
}

TEST_F(CQmSkin7SaveTest, ExceptionBeforeRenamePreservesOldFileAndList)
{
	m_Operations.m_Failure = CSaveFaults::EFailure::PUBLISH_THROW;
	EXPECT_FALSE(Save());
	ExpectOldState();
}

TEST_F(CQmSkin7SaveTest, PreexistingCandidateFileIsNotOverwrittenOrRemoved)
{
	const std::string Temporary = qm_skin7_save::TemporaryPath("skins7/saved.json");
	Store(Temporary.c_str(), "unrelated candidate");
	EXPECT_FALSE(Save());
	ExpectOldState(false);
	EXPECT_EQ(Read(Temporary.c_str()), "unrelated candidate");
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmSkin7SaveTest, PreexistingCandidateDirectoryIsPreserved)
{
	const std::string Temporary = qm_skin7_save::TemporaryPath("skins7/saved.json");
	ASSERT_TRUE(m_pStorage->CreateFolder(Temporary.c_str(), IStorage::TYPE_SAVE));
	EXPECT_FALSE(Save());
	ExpectOldState();
	EXPECT_TRUE(m_pStorage->FolderExists(Temporary.c_str(), IStorage::TYPE_SAVE));
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmSkin7SaveTest, InvalidNameDoesNotWriteOrPublishCache)
{
	EXPECT_FALSE(Save("../escape"));
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmSkin7SaveTest, UnrepresentablePhysicalPathDoesNotWriteOrPublishCache)
{
	const std::string Name(IO_MAX_PATH_LENGTH, 'a');
	EXPECT_FALSE(Save(Name.c_str()));
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(CQmSkin7SaveTest, CachePreparationExceptionHappensBeforeFileWrite)
{
	EXPECT_FALSE(qm_skin7_save::SaveAndPublish(m_pStorage.get(), "saved", m_Parts, m_Skins, [](auto &Candidate) {
		Candidate.clear();
		throw std::bad_alloc(); }, &m_Operations));
	ExpectOldState();
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
}

TEST_F(CQmSkin7SaveTest, NonCustomPartsKeepExistingExportSchema)
{
	qm_skin7_save::TParts Parts{};
	Parts[protocol7::SKINPART_BODY] = {"export"};
	Parts[protocol7::SKINPART_FEET] = {"export"};
	Parts[protocol7::SKINPART_EYES] = {"standard"};
	ASSERT_TRUE(qm_skin7_save::Save(m_pStorage.get(), "export", Parts));
	const std::string Json = Read("skins7/export.json");
	auto Info = QmParseSkin7Info(Json.data(), Json.size());
	ASSERT_NE(Info, nullptr);
	EXPECT_STREQ((*Info)["skin"]["feet"]["filename"], "export");
	EXPECT_FALSE((*Info)["skin"]["body"]["custom_colors"].u.boolean);
	EXPECT_EQ((*Info)["skin"]["marking"].type, json_none);
}
