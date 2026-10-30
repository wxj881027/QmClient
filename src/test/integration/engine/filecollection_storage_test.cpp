#include <engine/shared/filecollection.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>
#include <iterator>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace
{
	// 枚举与删除仍经过隔离的真实存储，只在模块边界注入失败。
	class CCollectionStorage : public IStorage
	{
		IStorage &m_Storage;

	public:
		enum class EScanFault
		{
			NONE,
			IO_FAILURE,
			STOPPED,
			UNSUPPORTED,
			BAD_ALLOC,
			UNKNOWN
		};
		EScanFault m_ScanFault = EScanFault::NONE;
		bool m_ScanFaultHit = false;
		int m_Scans = 0;
		int m_UncheckedScans = 0;
		bool m_ThrowPath = false;
		std::string m_CompletePathOverride;
		std::set<std::string> m_FailedRemovals;
		std::string m_ThrowRemoval;
		std::vector<std::string> m_vRemovals;
		std::vector<int> m_vRemovalTypes;
		explicit CCollectionStorage(IStorage &Storage) :
			m_Storage(Storage) {}
		int NumPaths() const override { return m_Storage.NumPaths(); }
		bool GetDataPath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetDataPath(pName, pBuffer, Size); }
		bool GetExecutablePath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetExecutablePath(pName, pBuffer, Size); }
		void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			++m_UncheckedScans;
			(void)ListDirectoryChecked(Type, pPath, Callback, pUser);
		}
		EFsListDirectoryResult ListDirectoryChecked(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override
		{
			++m_Scans;
			struct SContext
			{
				CCollectionStorage &m_Self;
				FS_LISTDIR_CALLBACK m_Callback;
				void *m_pUser;
			} Context{*this, Callback, pUser};
			const auto Wrapped = [](const char *pName, int IsDir, int Type, void *pUser) {
				auto &Context = *static_cast<SContext *>(pUser);
				const int Result = Context.m_Callback(pName, IsDir, Type, Context.m_pUser);
				if(IsDir || Context.m_Self.m_ScanFault == EScanFault::NONE)
					return Result;
				Context.m_Self.m_ScanFaultHit = true;
				if(Context.m_Self.m_ScanFault == EScanFault::BAD_ALLOC)
					throw std::bad_alloc();
				if(Context.m_Self.m_ScanFault == EScanFault::UNKNOWN)
					throw 7;
				return 1;
			};
			const auto Result = m_Storage.ListDirectoryChecked(Type, pPath, Wrapped, &Context);
			if(m_ScanFaultHit && m_ScanFault == EScanFault::IO_FAILURE)
				return EFsListDirectoryResult::IO_FAILURE;
			if(m_ScanFaultHit && m_ScanFault == EScanFault::UNSUPPORTED)
				return EFsListDirectoryResult::UNSUPPORTED;
			return Result;
		}
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_Storage.ListDirectoryInfo(Type, pPath, Callback, pUser); }
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer, int Size) override { return m_Storage.OpenFile(pName, Flags, Type, pBuffer, Size); }
		bool FileExists(const char *pName, int Type) override { return m_Storage.FileExists(pName, Type); }
		bool FolderExists(const char *pName, int Type) override { return m_Storage.FolderExists(pName, Type); }
		bool ReadFile(const char *pName, int Type, void **ppData, unsigned *pSize) override { return m_Storage.ReadFile(pName, Type, ppData, pSize); }
		char *ReadFileStr(const char *pName, int Type) override { return m_Storage.ReadFileStr(pName, Type); }
		bool RetrieveTimes(const char *pName, int Type, time_t *pCreated, time_t *pModified) override { return m_Storage.RetrieveTimes(pName, Type, pCreated, pModified); }
		bool CalculateHashes(const char *pName, int Type, SHA256_DIGEST *pSha, unsigned *pCrc) override { return m_Storage.CalculateHashes(pName, Type, pSha, pCrc); }
		bool FindFile(const char *pName, const char *pPath, int Type, char *pBuffer, int Size) override { return m_Storage.FindFile(pName, pPath, Type, pBuffer, Size); }
		size_t FindFiles(const char *pName, const char *pPath, int Type, std::set<std::string> *pEntries) override { return m_Storage.FindFiles(pName, pPath, Type, pEntries); }
		bool RemoveFile(const char *pName, int Type) override
		{
			m_vRemovals.emplace_back(pName);
			m_vRemovalTypes.push_back(Type);
			if(m_ThrowRemoval == pName)
				throw 7;
			return m_FailedRemovals.count(pName) == 0 && m_Storage.RemoveFile(pName, Type);
		}
		bool RemoveFolder(const char *pName, int Type) override { return m_Storage.RemoveFolder(pName, Type); }
		bool RenameFile(const char *pOld, const char *pNew, int Type, bool Remove) override { return m_Storage.RenameFile(pOld, pNew, Type, Remove); }
		bool CreateFolder(const char *pName, int Type) override { return m_Storage.CreateFolder(pName, Type); }
		void GetCompletePath(int Type, const char *pName, char *pBuffer, unsigned Size) override
		{
			if(m_ThrowPath)
				throw std::bad_alloc();
			if(!m_CompletePathOverride.empty())
				str_copy(pBuffer, m_CompletePathOverride.c_str(), Size);
			else
				m_Storage.GetCompletePath(Type, pName, pBuffer, Size);
		}
		bool RemoveBinaryFile(const char *pName) override { return m_Storage.RemoveBinaryFile(pName); }
		bool RenameBinaryFile(const char *pOld, const char *pNew) override { return m_Storage.RenameBinaryFile(pOld, pNew); }
		const char *GetBinaryPath(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPath(pName, pBuffer, Size); }
		const char *GetBinaryPathAbsolute(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPathAbsolute(pName, pBuffer, Size); }
	};

	class FileCollectionStorage : public testing::Test
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		std::unique_ptr<CCollectionStorage> m_pFaults;
		CFileCollection m_Collection;
		void SetUp() override
		{
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("captures", IStorage::TYPE_SAVE));
			m_pFaults = std::make_unique<CCollectionStorage>(*m_pStorage);
		}
		static std::string Path(int Day)
		{
			return "captures/autoscreen_2026-01-0" + std::to_string(Day) + "_12-00-00.png";
		}
		void Write(const std::string &Path)
		{
			IOHANDLE File = m_pStorage->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "kept", 4), 4u);
			EXPECT_EQ(io_close(File), 0);
		}
		void Seed(int Count = 3)
		{
			for(int Day = 1; Day <= Count; ++Day)
				Write(Path(Day));
		}
		bool Exists(const std::string &Path) { return m_pStorage->FileExists(Path.c_str(), IStorage::TYPE_SAVE); }
		void Clean(int Limit = 1) { m_Collection.Init(m_pFaults.get(), "captures", "autoscreen", ".png", Limit); }
		void ExpectUnchanged()
		{
			EXPECT_TRUE(Exists(Path(1)));
			EXPECT_TRUE(Exists(Path(2)));
			EXPECT_TRUE(Exists(Path(3)));
			EXPECT_TRUE(m_pFaults->m_vRemovals.empty());
		}
	};
}

TEST_F(FileCollectionStorage, FailedOldestRemovalDoesNotCountTowardLimit)
{
	Seed(4);
	m_pFaults->m_FailedRemovals.insert(Path(1));
	Clean(2);
	EXPECT_EQ(m_pFaults->m_vRemovals, (std::vector<std::string>{Path(1), Path(2), Path(3)}));
	EXPECT_TRUE(Exists(Path(1)));
	EXPECT_FALSE(Exists(Path(2)));
	EXPECT_FALSE(Exists(Path(3)));
	EXPECT_TRUE(Exists(Path(4)));
}

TEST_F(FileCollectionStorage, AllRemovalFailuresVisitEachCandidateOnce)
{
	Seed();
	m_pFaults->m_FailedRemovals = {Path(1), Path(2), Path(3)};
	Clean();
	EXPECT_EQ(m_pFaults->m_vRemovals, (std::vector<std::string>{Path(1), Path(2), Path(3)}));
	for(int Day = 1; Day <= 3; ++Day)
		EXPECT_TRUE(Exists(Path(Day)));
}

TEST_F(FileCollectionStorage, LaterInitRetriesFailuresAgainstFreshDirectory)
{
	Seed();
	m_pFaults->m_FailedRemovals = {Path(1), Path(2), Path(3)};
	Clean();
	m_pFaults->m_FailedRemovals.clear();
	m_pFaults->m_vRemovals.clear();
	Clean();
	EXPECT_EQ(m_pFaults->m_vRemovals, (std::vector<std::string>{Path(1), Path(2)}));
	EXPECT_FALSE(Exists(Path(1)));
	EXPECT_FALSE(Exists(Path(2)));
	EXPECT_TRUE(Exists(Path(3)));
}

TEST_F(FileCollectionStorage, PartialEnumerationIoFailureKeepsEveryFile)
{
	Seed();
	m_pFaults->m_ScanFault = CCollectionStorage::EScanFault::IO_FAILURE;
	Clean(0);
	EXPECT_TRUE(m_pFaults->m_ScanFaultHit);
	ExpectUnchanged();
}

TEST_F(FileCollectionStorage, StoppedEnumerationKeepsEveryFile)
{
	Seed();
	m_pFaults->m_ScanFault = CCollectionStorage::EScanFault::STOPPED;
	Clean(0);
	EXPECT_TRUE(m_pFaults->m_ScanFaultHit);
	ExpectUnchanged();
}

TEST_F(FileCollectionStorage, UnsupportedEnumerationDoesNotUseUncheckedFallback)
{
	Seed();
	m_pFaults->m_ScanFault = CCollectionStorage::EScanFault::UNSUPPORTED;
	Clean();
	EXPECT_TRUE(m_pFaults->m_ScanFaultHit);
	EXPECT_EQ(m_pFaults->m_UncheckedScans, 0);
	ExpectUnchanged();
}

TEST_F(FileCollectionStorage, EnumerationAllocationExceptionKeepsFilesAndLaterInitRecovers)
{
	Seed();
	m_pFaults->m_ScanFault = CCollectionStorage::EScanFault::BAD_ALLOC;
	Clean();
	EXPECT_TRUE(m_pFaults->m_ScanFaultHit);
	ExpectUnchanged();
	m_pFaults->m_ScanFault = CCollectionStorage::EScanFault::NONE;
	Clean();
	EXPECT_FALSE(Exists(Path(1)));
	EXPECT_FALSE(Exists(Path(2)));
	EXPECT_TRUE(Exists(Path(3)));
}

TEST_F(FileCollectionStorage, UnknownEnumerationExceptionKeepsEveryFile)
{
	Seed();
	m_pFaults->m_ScanFault = CCollectionStorage::EScanFault::UNKNOWN;
	Clean();
	EXPECT_TRUE(m_pFaults->m_ScanFaultHit);
	ExpectUnchanged();
}

TEST_F(FileCollectionStorage, RemovalExceptionStopsThisPassAndLaterInitUsesRemainingFiles)
{
	Seed(4);
	m_pFaults->m_ThrowRemoval = Path(2);
	Clean();
	EXPECT_EQ(m_pFaults->m_vRemovals, (std::vector<std::string>{Path(1), Path(2)}));
	EXPECT_FALSE(Exists(Path(1)));
	EXPECT_TRUE(Exists(Path(2)));
	EXPECT_TRUE(Exists(Path(3)));
	EXPECT_TRUE(Exists(Path(4)));
	m_pFaults->m_ThrowRemoval.clear();
	m_pFaults->m_vRemovals.clear();
	Clean();
	EXPECT_EQ(m_pFaults->m_vRemovals, (std::vector<std::string>{Path(2), Path(3)}));
	EXPECT_TRUE(Exists(Path(4)));
}

TEST_F(FileCollectionStorage, CompletePathPreparationExceptionKeepsFilesAndCanRetry)
{
	Seed();
	m_pFaults->m_ThrowPath = true;
	Clean();
	ExpectUnchanged();
	m_pFaults->m_ThrowPath = false;
	Clean();
	EXPECT_FALSE(Exists(Path(1)));
	EXPECT_TRUE(Exists(Path(3)));
}

TEST_F(FileCollectionStorage, WrongPrefixSeparatorIsExcludedFromRetention)
{
	const std::string Wrong = "captures/autoscreenX2026-01-01_12-00-00.png";
	Write(Wrong);
	Write(Path(2));
	Write(Path(3));
	Clean();
	EXPECT_EQ(m_pFaults->m_vRemovals, (std::vector<std::string>{Path(2)}));
	EXPECT_TRUE(Exists(Wrong));
	EXPECT_FALSE(Exists(Path(2)));
	EXPECT_TRUE(Exists(Path(3)));
}

TEST_F(FileCollectionStorage, WildcardRetainsMapNamesAndIgnoresOtherExtensionsAndDirectories)
{
	const std::string Old = "captures/old map_2026-01-01_12-00-00.demo";
	const std::string New = "captures/新地图_2026-01-02_12-00-00.demo";
	Write(Old);
	Write(New);
	Write(Path(1));
	Write("captures/manual.demo");
	ASSERT_TRUE(m_pStorage->CreateFolder("captures/folder_2026-01-01_12-00-00.demo", IStorage::TYPE_SAVE));
	m_Collection.Init(m_pFaults.get(), "captures", "", ".demo", 1);
	EXPECT_EQ(m_pFaults->m_vRemovals, (std::vector<std::string>{Old}));
	EXPECT_FALSE(Exists(Old));
	EXPECT_TRUE(Exists(New));
	EXPECT_TRUE(Exists(Path(1)));
	EXPECT_TRUE(Exists("captures/manual.demo"));
	EXPECT_TRUE(m_pStorage->FolderExists("captures/folder_2026-01-01_12-00-00.demo", IStorage::TYPE_SAVE));
}

TEST_F(FileCollectionStorage, ZeroLimitDeletesMatchingFilesOnly)
{
	Seed();
	Write("captures/keep.png");
	Clean(0);
	EXPECT_EQ(m_pFaults->m_vRemovals.size(), 3u);
	EXPECT_TRUE(Exists("captures/keep.png"));
	for(int Day = 1; Day <= 3; ++Day)
		EXPECT_FALSE(Exists(Path(Day)));
}

TEST_F(FileCollectionStorage, InvalidLimitDoesNotScanOrDelete)
{
	Seed();
	Clean(-1);
	EXPECT_EQ(m_pFaults->m_Scans, 0);
	ExpectUnchanged();
}

TEST_F(FileCollectionStorage, OverlongMetadataDoesNotScanATruncatedTarget)
{
	Seed();
	struct SInput
	{
		std::string m_Path;
		std::string m_Desc;
		std::string m_Ext;
	};
	const SInput aInputs[] = {
		{std::string(IO_MAX_PATH_LENGTH, 'a'), "autoscreen", ".png"},
		{"captures", std::string(128, 'a'), ".png"},
		{"captures", "autoscreen", std::string(32, 'a')},
	};
	for(size_t Index = 0; Index < std::size(aInputs); ++Index)
	{
		SCOPED_TRACE(Index);
		const auto &Input = aInputs[Index];
		m_Collection.Init(m_pFaults.get(), Input.m_Path.c_str(), Input.m_Desc.c_str(), Input.m_Ext.c_str(), 1);
		EXPECT_EQ(m_pFaults->m_Scans, 0);
		ExpectUnchanged();
	}
}

TEST_F(FileCollectionStorage, FullStorageRootLengthPreventsTruncatedRemoval)
{
	Seed();
	m_pFaults->m_CompletePathOverride = std::string(IO_MAX_PATH_LENGTH - 2, 'a');
	Clean();
	EXPECT_EQ(m_pFaults->m_Scans, 1);
	ExpectUnchanged();
}

TEST_F(FileCollectionStorage, AbsoluteDirectoryKeepsRemovalRootAndNewestFile)
{
	Seed();
	char aRoot[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "captures", aRoot, sizeof(aRoot));
	const auto Root = std::filesystem::absolute(std::filesystem::u8path(aRoot)).generic_u8string();
	const std::string Absolute(reinterpret_cast<const char *>(Root.data()), Root.size());
	m_Collection.Init(m_pFaults.get(), Absolute.c_str(), "autoscreen", ".png", 1);
	ASSERT_EQ(m_pFaults->m_vRemovals.size(), 2u);
	EXPECT_EQ(m_pFaults->m_vRemovalTypes, (std::vector<int>{IStorage::TYPE_ABSOLUTE, IStorage::TYPE_ABSOLUTE}));
	EXPECT_FALSE(Exists(Path(1)));
	EXPECT_FALSE(Exists(Path(2)));
	EXPECT_TRUE(Exists(Path(3)));
}

TEST_F(FileCollectionStorage, SatisfiedLimitDoesNotAttemptRemoval)
{
	Seed();
	Clean(3);
	ExpectUnchanged();
}
