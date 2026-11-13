#include <game/client/components/qmclient/audio_pack_candidates.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <new>

namespace
{
	class CSaveRootView
	{
		IStorage &m_Storage;

	public:
		bool m_ThrowAfterList = false;
		explicit CSaveRootView(IStorage &Storage) :
			m_Storage(Storage) {}
		int NumPaths() const { return 1; }
		void GetCompletePath(int Type, const char *pPath, char *pOut, unsigned Size)
		{
			m_Storage.GetCompletePath(Type, pPath, pOut, Size);
		}
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO pfnCallback, void *pUser)
		{
			m_Storage.ListDirectoryInfo(Type, pPath, pfnCallback, pUser);
			if(m_ThrowAfterList)
				throw std::bad_alloc();
		}
	};

	class CQmAudioPackCandidates : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("audio", IStorage::TYPE_SAVE));
		}
		void Store(const char *pPath)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "candidate", 9), 9u);
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(CQmAudioPackCandidates, NativeTreeKeepsNormalizationDeduplicationAndHiddenDirectoryRule)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("audio/sub", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("audio/.hidden", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("data", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("data/audio", IStorage::TYPE_SAVE));
	Store("audio/root.wv");
	Store("audio/sub/deep.wv");
	Store("audio/.hidden/no.wv");
	Store("audio/.hidden_file.wv");
	Store("audio/ignored.txt");
	Store("data/audio/root.wv");
	Store("data/audio/builtin.wv");
	CSaveRootView View(*m_pStorage);
	qm_audio_pack::CCandidateScan Scan(View);
	std::vector<std::string> Paths;
	std::vector<qm_audio_pack::SCandidateEntry> Entries;
	int Selection = -1;
	ASSERT_TRUE(qm_audio_pack::TryRefreshCandidates(Paths, Entries, Selection, "pack", "audio/sub/deep.wv", [&](auto &Out) {
		return Scan.Scan(qm_audio_pack::CandidateScanRoots(), Out);
	}));
	EXPECT_EQ(Paths, (std::vector<std::string>{"audio/.hidden_file.wv", "audio/builtin.wv", "audio/root.wv", "audio/sub/deep.wv"}));
	ASSERT_EQ(Entries.size(), 4u);
	ASSERT_GE(Selection, 0);
	ASSERT_LT(static_cast<size_t>(Selection), Entries.size());
	EXPECT_EQ(Entries[Selection].m_Path, "audio/sub/deep.wv");
}

TEST_F(CQmAudioPackCandidates, NativeEnumerationBoundaryFailureKeepsSnapshotUntilManualRetry)
{
	Store("audio/new.wv");
	CSaveRootView View(*m_pStorage);
	View.m_ThrowAfterList = true;
	qm_audio_pack::CCandidateScan Scan(View);
	std::vector<std::string> Paths{"audio/old.wv"};
	auto Entries = qm_audio_pack::BuildCandidateEntries(Paths, "pack", "");
	int Selection = 0;
	const auto Refresh = [&] {
		return qm_audio_pack::TryRefreshCandidates(Paths, Entries, Selection, "pack", "audio/new.wv", [&](auto &Out) {
			return Scan.Scan(qm_audio_pack::CandidateScanRoots(), Out);
		});
	};
	EXPECT_FALSE(Refresh());
	EXPECT_EQ(Paths, std::vector<std::string>{"audio/old.wv"});
	ASSERT_EQ(Entries.size(), 1u);
	EXPECT_EQ(Entries.front().m_Path, "audio/old.wv");
	EXPECT_EQ(Selection, 0);
	View.m_ThrowAfterList = false;
	ASSERT_TRUE(Refresh());
	ASSERT_GE(Selection, 0);
	ASSERT_LT(static_cast<size_t>(Selection), Entries.size());
	EXPECT_EQ(Entries[Selection].m_Path, "audio/new.wv");
}

TEST_F(CQmAudioPackCandidates, SuccessfulNativeDeletionPublishesEmptySnapshot)
{
	Store("audio/only.wv");
	CSaveRootView View(*m_pStorage);
	qm_audio_pack::CCandidateScan Scan(View);
	std::vector<std::string> Paths;
	std::vector<qm_audio_pack::SCandidateEntry> Entries;
	int Selection = -1;
	const auto Refresh = [&] {
		return qm_audio_pack::TryRefreshCandidates(Paths, Entries, Selection, "pack", "", [&](auto &Out) {
			return Scan.Scan(qm_audio_pack::CandidateScanRoots(), Out);
		});
	};
	ASSERT_TRUE(Refresh());
	ASSERT_EQ(Entries.size(), 1u);
	ASSERT_TRUE(m_pStorage->RemoveFile("audio/only.wv", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Refresh());
	EXPECT_TRUE(Paths.empty());
	EXPECT_TRUE(Entries.empty());
	EXPECT_EQ(Selection, -1);
}
