#include <base/str.h>

#include <engine/storage.h>

#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/support/heap_allocation_probe.h>
#include <test/test.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
	// 生产加载使用TYPE_ALL；本视图将它限定到测试保存根，禁止回落到随包或工作目录语言。
	class CScopedLanguageStorage : public IStorage
	{
		IStorage &m_Storage;
		static int Scoped(int Type) { return Type == TYPE_ALL ? TYPE_SAVE : Type; }

	public:
		explicit CScopedLanguageStorage(IStorage &Storage) :
			m_Storage(Storage) {}
		int NumPaths() const override { return 1; }
		bool GetDataPath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetDataPath(pName, pBuffer, Size); }
		bool GetExecutablePath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetExecutablePath(pName, pBuffer, Size); }
		void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { m_Storage.ListDirectory(Scoped(Type), pPath, Callback, pUser); }
		EFsListDirectoryResult ListDirectoryChecked(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { return m_Storage.ListDirectoryChecked(Scoped(Type), pPath, Callback, pUser); }
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_Storage.ListDirectoryInfo(Scoped(Type), pPath, Callback, pUser); }
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer, int Size) override { return m_Storage.OpenFile(pName, Flags, Scoped(Type), pBuffer, Size); }
		bool FileExists(const char *pName, int Type) override { return m_Storage.FileExists(pName, Scoped(Type)); }
		bool FolderExists(const char *pName, int Type) override { return m_Storage.FolderExists(pName, Scoped(Type)); }
		bool ReadFile(const char *pName, int Type, void **ppData, unsigned *pSize) override { return m_Storage.ReadFile(pName, Scoped(Type), ppData, pSize); }
		EReadFileResult ReadFileChecked(const char *pName, int Type, void **ppData, unsigned *pSize) override { return m_Storage.ReadFileChecked(pName, Scoped(Type), ppData, pSize); }
		char *ReadFileStr(const char *pName, int Type) override { return m_Storage.ReadFileStr(pName, Scoped(Type)); }
		bool RetrieveTimes(const char *pName, int Type, time_t *pCreated, time_t *pModified) override { return m_Storage.RetrieveTimes(pName, Scoped(Type), pCreated, pModified); }
		bool CalculateHashes(const char *pName, int Type, SHA256_DIGEST *pSha, unsigned *pCrc) override { return m_Storage.CalculateHashes(pName, Scoped(Type), pSha, pCrc); }
		bool FindFile(const char *pName, const char *pPath, int Type, char *pBuffer, int Size) override { return m_Storage.FindFile(pName, pPath, Scoped(Type), pBuffer, Size); }
		size_t FindFiles(const char *pName, const char *pPath, int Type, std::set<std::string> *pEntries) override { return m_Storage.FindFiles(pName, pPath, Scoped(Type), pEntries); }
		bool RemoveFile(const char *pName, int Type) override { return m_Storage.RemoveFile(pName, Scoped(Type)); }
		bool RemoveFolder(const char *pName, int Type) override { return m_Storage.RemoveFolder(pName, Scoped(Type)); }
		bool RenameFile(const char *pOld, const char *pNew, int Type, bool Remove) override { return m_Storage.RenameFile(pOld, pNew, Scoped(Type), Remove); }
		bool CreateFolder(const char *pName, int Type) override { return m_Storage.CreateFolder(pName, Scoped(Type)); }
		void GetCompletePath(int Type, const char *pName, char *pBuffer, unsigned Size) override { m_Storage.GetCompletePath(Scoped(Type), pName, pBuffer, Size); }
		bool RemoveBinaryFile(const char *pName) override { return m_Storage.RemoveBinaryFile(pName); }
		bool RenameBinaryFile(const char *pOld, const char *pNew) override { return m_Storage.RenameBinaryFile(pOld, pNew); }
		const char *GetBinaryPath(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPath(pName, pBuffer, Size); }
		const char *GetBinaryPathAbsolute(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPathAbsolute(pName, pBuffer, Size); }
	};

	class LocalizationStorageTransaction : public testing::Test
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<IStorage> m_pReadStorage;
		CHeapAllocationProbe m_Probe;
		CLocalizationDatabase m_Database{CHeapAllocationProbe::Allocate};
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pReadStorage = std::make_unique<CScopedLanguageStorage>(*m_pStorage);
			ASSERT_TRUE(m_pStorage->CreateFolder("languages", IStorage::TYPE_SAVE));
		}
		void Write(const char *pName, const std::string &Text)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Text.data(), static_cast<unsigned>(Text.size())), Text.size());
			EXPECT_EQ(io_close(File), 0);
		}
		bool Load(const char *pName, bool Clear = true) { return m_Database.Load(pName, m_pReadStorage.get(), nullptr, Clear); }
		const char *Find(const char *pKey, const char *pContext = "", bool Fallback = true)
		{
			return m_Database.FindString(str_quickhash(pKey), str_quickhash(pContext), Fallback);
		}
		void Old()
		{
			Write("old.txt", "Old\n== previous\n");
			ASSERT_TRUE(Load("old.txt"));
			ASSERT_STREQ(Find("Old"), "previous");
		}
	};
}

TEST_F(LocalizationStorageTransaction, FailedReplacementKeepsOldValueAndAddress)
{
	Old();
	const char *pOld = Find("Old");
	Write("new.txt", "New\n== replacement\n");
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	EXPECT_FALSE(Load("new.txt"));
	EXPECT_EQ(Find("Old"), pOld);
	EXPECT_STREQ(pOld, "previous");
	EXPECT_EQ(Find("New"), nullptr);
}

TEST_F(LocalizationStorageTransaction, MidReplacementFailureDoesNotPublishEarlierCandidateEntries)
{
	Old();
	Write("new.txt", "First\n== prepared\nSecond\n== " + std::string(m_Probe.m_LastSize + 1, 'x') + "\n");
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	m_Probe.m_FailAt = m_Probe.m_Requests + 2;
	EXPECT_FALSE(Load("new.txt"));
	EXPECT_STREQ(Find("Old"), "previous");
	EXPECT_EQ(Find("First"), nullptr);
	EXPECT_EQ(Find("Second"), nullptr);
}

TEST_F(LocalizationStorageTransaction, FailedAppendDoesNotReplaceAnOldValue)
{
	Old();
	const char *pOld = Find("Old");
	Write("append.txt", "Old\n== candidate\nNew\n== " + std::string(m_Probe.m_LastSize + 1, 'x') + "\n");
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	m_Probe.m_FailAt = m_Probe.m_Requests + 2;
	EXPECT_FALSE(Load("append.txt", false));
	EXPECT_EQ(Find("Old"), pOld);
	EXPECT_STREQ(pOld, "previous");
	EXPECT_EQ(Find("New"), nullptr);
}

TEST_F(LocalizationStorageTransaction, SuccessfulAppendPreservesUntouchedAndPreviouslyBorrowedStrings)
{
	Old();
	const char *pOriginal = Find("Old");
	Write("append.txt", "New\n== added\n");
	ASSERT_TRUE(Load("append.txt", false));
	EXPECT_EQ(Find("Old"), pOriginal);
	const char *pAdded = Find("New");
	ASSERT_STREQ(pAdded, "added");
	Write("override.txt", "Old\n== overridden\n");
	ASSERT_TRUE(Load("override.txt", false));
	EXPECT_STREQ(Find("Old"), "overridden");
	EXPECT_STREQ(pOriginal, "previous");
	EXPECT_EQ(Find("New"), pAdded);
	EXPECT_STREQ(pAdded, "added");
}

TEST_F(LocalizationStorageTransaction, SuccessfulReplacementRemovesOldKeysAndPublishesSortedEntries)
{
	Old();
	Write("new.txt", "Zulu\n== last\nAlpha\n== first\nMiddle\n== middle\n");
	ASSERT_TRUE(Load("new.txt"));
	EXPECT_EQ(Find("Old"), nullptr);
	EXPECT_STREQ(Find("Zulu"), "last");
	EXPECT_STREQ(Find("Alpha"), "first");
	EXPECT_STREQ(Find("Middle"), "middle");
}

TEST_F(LocalizationStorageTransaction, FailedCandidateCanBeRetriedWithoutReloadingOldData)
{
	Old();
	Write("new.txt", "New\n== retried\n");
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::BAD_ALLOC;
	EXPECT_FALSE(Load("new.txt"));
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	ASSERT_TRUE(Load("new.txt"));
	EXPECT_STREQ(Find("New"), "retried");
	EXPECT_EQ(Find("Old"), nullptr);
}

TEST_F(LocalizationStorageTransaction, UnknownAllocationExceptionKeepsOldDatabase)
{
	Old();
	Write("new.txt", "New\n== candidate\n");
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::UNKNOWN;
	EXPECT_FALSE(Load("new.txt"));
	EXPECT_STREQ(Find("Old"), "previous");
}

TEST_F(LocalizationStorageTransaction, EmptyAppendKeepsOldStringAddress)
{
	Old();
	const char *pOld = Find("Old");
	Write("empty.txt", "# no replacements\n");
	ASSERT_TRUE(Load("empty.txt", false));
	EXPECT_EQ(Find("Old"), pOld);
}

TEST_F(LocalizationStorageTransaction, MissingFilePreservesExistingDatabase)
{
	Old();
	const char *pOld = Find("Old");
	EXPECT_FALSE(Load("missing-transaction-language.txt"));
	EXPECT_EQ(Find("Old"), pOld);
}

TEST_F(LocalizationStorageTransaction, UnloadCanRecoverFromAnUnavailableResetAllocation)
{
	Old();
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NULL_RESULT;
	EXPECT_TRUE(m_Database.Load("", nullptr, nullptr));
	EXPECT_EQ(Find("Old"), nullptr);
	m_Probe.m_Failure = CHeapAllocationProbe::EFailure::NONE;
	ASSERT_TRUE(Load("old.txt"));
	EXPECT_STREQ(Find("Old"), "previous");
}

TEST_F(LocalizationStorageTransaction, ContextAndDuplicateReplacementRulesRemainUnchanged)
{
	Write("contexts.txt", "Key\n== default\n[Editor]\nKey\n== editor\nKey\n== newest\nSkipped\n== \n");
	ASSERT_TRUE(Load("contexts.txt"));
	EXPECT_STREQ(Find("Key"), "newest");
	EXPECT_STREQ(Find("Key", "Editor", false), "editor");
	EXPECT_STREQ(Find("Key", "Other"), "newest");
	EXPECT_EQ(Find("Key", "Other", false), nullptr);
	EXPECT_EQ(Find("Skipped"), nullptr);
}

TEST_F(LocalizationStorageTransaction, MissingInitialIndexKeepsEnglishFallback)
{
	m_Database.LoadIndexfile(m_pReadStorage.get(), nullptr);
	ASSERT_EQ(m_Database.Languages().size(), 1u);
	EXPECT_EQ(m_Database.Languages()[0].m_Name, "English");
	EXPECT_TRUE(m_Database.Languages()[0].m_Filename.empty());
}

TEST_F(LocalizationStorageTransaction, FailedIndexReloadPreservesExistingCatalog)
{
	Write("languages/index.txt", "German\n== Deutsch\n== 276\n== de\n");
	m_Database.LoadIndexfile(m_pReadStorage.get(), nullptr);
	ASSERT_EQ(m_Database.Languages().size(), 2u);
	const auto *pOld = m_Database.Languages().data();
	ASSERT_TRUE(m_pStorage->RemoveFile("languages/index.txt", IStorage::TYPE_SAVE));
	m_Database.LoadIndexfile(m_pReadStorage.get(), nullptr);
	EXPECT_EQ(m_Database.Languages().data(), pOld);
	ASSERT_EQ(m_Database.Languages().size(), 2u);
	EXPECT_EQ(m_Database.Languages()[0].m_Name, "Deutsch");
}

TEST_F(LocalizationStorageTransaction, IndexCandidateKeepsMetadataAndExistingTruncatedTailRule)
{
	Write("languages/index.txt", "German\n== Deutsch\n== 276\n== de;de-DE\nIncomplete\n");
	m_Database.LoadIndexfile(m_pReadStorage.get(), nullptr);
	ASSERT_EQ(m_Database.Languages().size(), 2u);
	const auto &German = m_Database.Languages()[0];
	EXPECT_EQ(German.m_Name, "Deutsch");
	EXPECT_EQ(German.m_Filename, "languages/German.txt");
	EXPECT_EQ(German.m_CountryCode, 276);
	EXPECT_EQ(German.m_vLanguageCodes, (std::vector<std::string>{"de", "de-DE"}));
}
