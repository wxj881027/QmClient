#include <game/client/components/qmclient/audio_pack_candidates.h>

#include <gtest/gtest.h>

#include <map>
#include <new>

namespace
{
	struct SEntry
	{
		std::string m_Name;
		bool m_Directory = false;
	};
	// 仅替换目录枚举边界，实际遍历、路径判断和去重均由生产扫描器执行。
	class CDirectoryStorage
	{
	public:
		std::array<std::string, 2> m_Roots{"save/", "data/"};
		int m_NumPaths = 1;
		std::map<std::pair<int, std::string>, std::vector<SEntry>> m_Entries;
		std::set<std::pair<int, std::string>> m_Files;
		std::set<std::pair<int, std::string>> m_ListErrors;
		int m_Active = 0;
		int m_MaxActive = 0;
		int m_Calls = 0;
		int m_ThrowAt = -1;
		bool m_RepeatDirectory = false;
		bool m_OverflowRequest = false;
		std::vector<int> m_Types;
		int NumPaths() const { return m_NumPaths; }
		void GetCompletePath(int Type, const char *pPath, char *pOut, unsigned Size)
		{
			ASSERT_GE(Type, 0);
			ASSERT_LT(Type, m_NumPaths);
			const std::string Full = m_Roots[Type] + pPath;
			str_copy(pOut, Full.c_str(), Size);
		}
		bool FileExists(const char *pPath, int Type) const
		{
			return m_Files.contains({Type, pPath});
		}
		EFsListDirectoryResult ListDirectoryChecked(int Type, const char *pPath, FS_LISTDIR_CALLBACK pfnCallback, void *pUser)
		{
			++m_Calls;
			++m_Active;
			m_MaxActive = std::max(m_MaxActive, m_Active);
			struct SExit
			{
				int &m_Active;
				~SExit() { --m_Active; }
			} Exit{m_Active};
			m_Types.push_back(Type);
			m_OverflowRequest |= m_Roots[Type].size() + str_length(pPath) + 3 > IO_MAX_PATH_LENGTH;
			if(m_Calls == m_ThrowAt)
				throw std::bad_alloc();
			if(m_ListErrors.contains({Type, pPath}))
				return EFsListDirectoryResult::IO_FAILURE;
			const std::vector<SEntry> Repeated{{"loop", true}, {"tone.wv"}};
			auto It = m_Entries.find({Type, pPath});
			if(!m_RepeatDirectory && It == m_Entries.end())
				return EFsListDirectoryResult::NOT_FOUND;
			const auto &Entries = m_RepeatDirectory ? Repeated : It->second;
			for(const auto &Entry : Entries)
			{
				if(pfnCallback(Entry.m_Name.c_str(), Entry.m_Directory, Type, pUser))
					return EFsListDirectoryResult::STOPPED;
			}
			return EFsListDirectoryResult::SUCCESS;
		}
	};
	constexpr std::array<qm_audio_pack::SScanRoot, 1> ROOTS{{{"audio", "audio"}}};
}

TEST(AudioPackCandidateScan, NestedDirectoriesDoNotReenterNativeEnumeration)
{
	CDirectoryStorage Storage;
	std::string Path = "audio";
	for(int i = 0; i < 64; ++i)
	{
		Storage.m_Entries[{0, Path}] = {{"d", true}};
		Path += "/d";
	}
	Storage.m_Entries[{0, Path}] = {{"tone.wv"}};
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_TRUE(Paths.contains(Path + "/tone.wv"));
	EXPECT_EQ(Storage.m_MaxActive, 1);
	EXPECT_FALSE(Storage.m_OverflowRequest);
}

TEST(AudioPackCandidateScan, SameLogicalDirectoryCombinesStorageRootsWithoutDuplicatePaths)
{
	CDirectoryStorage Storage;
	Storage.m_NumPaths = 2;
	Storage.m_Entries[{0, "audio"}] = {{"sub", true}, {"same.wv"}};
	Storage.m_Entries[{1, "audio"}] = {{"sub", true}, {"same.wv"}};
	Storage.m_Entries[{0, "audio/sub"}] = {{"save.wv"}};
	Storage.m_Entries[{1, "audio/sub"}] = {{"data.wv"}};
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_EQ(Paths, (std::set<std::string>{"audio/same.wv", "audio/sub/save.wv", "audio/sub/data.wv"}));
	EXPECT_EQ(Storage.m_Calls, 4);
}

TEST(AudioPackCandidateScan, EarlierFileKeepsExistingPriorityOverLaterDirectoryWithSameName)
{
	CDirectoryStorage Storage;
	Storage.m_NumPaths = 2;
	Storage.m_Entries[{0, "audio"}] = {{"shadow"}};
	Storage.m_Entries[{1, "audio"}] = {{"shadow", true}};
	Storage.m_Entries[{1, "audio/shadow"}] = {{"hidden.wv"}};
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_TRUE(Paths.empty());
	EXPECT_EQ(Storage.m_Calls, 2);
}

TEST(AudioPackCandidateScan, LaterFileDoesNotHideEarlierDirectoryContents)
{
	CDirectoryStorage Storage;
	Storage.m_NumPaths = 2;
	Storage.m_Entries[{0, "audio"}] = {{"sub", true}};
	Storage.m_Entries[{0, "audio/sub"}] = {{"deep", true}};
	Storage.m_Entries[{0, "audio/sub/deep"}] = {{"tone.wv"}};
	Storage.m_Entries[{1, "audio"}] = {{"sub"}};
	Storage.m_Files.emplace(1, "audio/sub");
	Storage.m_ListErrors.emplace(1, "audio/sub");
	Storage.m_ListErrors.emplace(1, "audio/sub/deep");
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_EQ(Paths, std::set<std::string>{"audio/sub/deep/tone.wv"});
}

TEST(AudioPackCandidateScan, EquivalentRootsNormalizeToTheExistingAudioPrefix)
{
	CDirectoryStorage Storage;
	Storage.m_Entries[{0, "audio"}] = {{"same.wv"}};
	Storage.m_Entries[{0, "data/audio"}] = {{"same.wv"}, {"other.wv"}};
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(qm_audio_pack::CandidateScanRoots(), Paths));
	EXPECT_EQ(Paths, (std::set<std::string>{"audio/same.wv", "audio/other.wv"}));
}

TEST(AudioPackCandidateScan, EarlierHiddenDirectoryStillShadowsLaterFileOfTheSameName)
{
	CDirectoryStorage Storage;
	Storage.m_NumPaths = 2;
	Storage.m_Entries[{0, "audio"}] = {{".hidden.wv", true}};
	Storage.m_Entries[{1, "audio"}] = {{".hidden.wv"}};
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_TRUE(Paths.empty());
}

TEST(AudioPackCandidateScan, RelativeStorageRootStillProducesCandidates)
{
	CDirectoryStorage Storage;
	Storage.m_Roots[0].clear();
	Storage.m_Entries[{0, "audio"}] = {{"tone.wv"}};
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_TRUE(Paths.contains("audio/tone.wv"));
}

TEST(AudioPackCandidateScan, OverlongPhysicalRootIsSkippedBeforeEnumeration)
{
	CDirectoryStorage Storage;
	Storage.m_NumPaths = 2;
	Storage.m_Roots[0].assign(IO_MAX_PATH_LENGTH, 'x');
	Storage.m_Entries[{1, "audio"}] = {{"tone.wv"}};
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_TRUE(Paths.contains("audio/tone.wv"));
	EXPECT_EQ(Storage.m_Types, std::vector<int>{1});
	EXPECT_FALSE(Storage.m_OverflowRequest);
}

TEST(AudioPackCandidateScan, RepeatedDirectoryChainStopsBeforeNativePathTruncation)
{
	CDirectoryStorage Storage;
	Storage.m_RepeatDirectory = true;
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_FALSE(Paths.empty());
	EXPECT_GT(Storage.m_Calls, 10);
	EXPECT_LT(Storage.m_Calls, IO_MAX_PATH_LENGTH);
	EXPECT_EQ(Storage.m_MaxActive, 1);
	EXPECT_FALSE(Storage.m_OverflowRequest);
}

TEST(AudioPackCandidateScan, EnumerationExceptionPreservesOutputAndSameScannerCanRetry)
{
	CDirectoryStorage Storage;
	Storage.m_Entries[{0, "audio"}] = {{"sub", true}, {"root.wv"}};
	Storage.m_Entries[{0, "audio/sub"}] = {{"nested.wv"}};
	Storage.m_ThrowAt = 2;
	qm_audio_pack::CCandidateScan Scan(Storage);
	std::set<std::string> Paths{"old"};
	EXPECT_FALSE(Scan.Scan(ROOTS, Paths));
	EXPECT_EQ(Paths, std::set<std::string>{"old"});
	Storage.m_ThrowAt = -1;
	ASSERT_TRUE(Scan.Scan(ROOTS, Paths));
	EXPECT_EQ(Paths, (std::set<std::string>{"audio/root.wv", "audio/sub/nested.wv"}));
}
