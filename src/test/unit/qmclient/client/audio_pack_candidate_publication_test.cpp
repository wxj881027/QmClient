#include <game/client/components/qmclient/audio_pack_candidates.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
	std::vector<qm_audio_pack::SCandidateEntry> BuildThenThrow(const std::vector<std::string> &Paths, const char *pPack, const char *pCurrent)
	{
		(void)qm_audio_pack::BuildCandidateEntries(Paths, pPack, pCurrent);
		throw std::bad_alloc();
	}
	class CAudioPackCandidatePublication : public ::testing::Test
	{
	protected:
		std::vector<std::string> m_Paths{"audio/old_a.wv", "audio/old_b.wv"};
		std::vector<qm_audio_pack::SCandidateEntry> m_Entries = qm_audio_pack::BuildCandidateEntries(m_Paths, "pack", "");
		int m_Selection = 1;
		void ExpectSelected(const char *pPath)
		{
			ASSERT_GE(m_Selection, 0);
			ASSERT_LT(static_cast<size_t>(m_Selection), m_Entries.size());
			EXPECT_EQ(m_Entries[m_Selection].m_Path, pPath);
		}
		void ExpectOld()
		{
			EXPECT_EQ(m_Paths, (std::vector<std::string>{"audio/old_a.wv", "audio/old_b.wv"}));
			ASSERT_EQ(m_Entries.size(), 2u);
			EXPECT_EQ(m_Entries[0].m_Path, "audio/old_a.wv");
			EXPECT_EQ(m_Entries[1].m_Path, "audio/old_b.wv");
			EXPECT_EQ(m_Selection, 1);
		}
	};
}

TEST_F(CAudioPackCandidatePublication, SuccessfulRefreshPublishesMatchingPathsEntriesAndSelection)
{
	ASSERT_TRUE(qm_audio_pack::TryRefreshCandidates(m_Paths, m_Entries, m_Selection, "pack", "audio/pack/new.wv", [](auto &Out) {
		Out = {"audio/other.wv", "audio/pack/new.wv"};
		return true;
	}));
	EXPECT_EQ(m_Paths.size(), 2u);
	ASSERT_EQ(m_Entries.size(), 2u);
	ExpectSelected("audio/pack/new.wv");
	EXPECT_TRUE(m_Entries.front().m_IsCurrentFile);
}

TEST_F(CAudioPackCandidatePublication, PartialScanFailureKeepsAllPublishedStateAndCanBeManuallyRetried)
{
	EXPECT_FALSE(qm_audio_pack::TryRefreshCandidates(m_Paths, m_Entries, m_Selection, "pack", "", [](auto &Out) {
		Out.insert("partial.wv");
		return false;
	}));
	ExpectOld();
	ASSERT_TRUE(qm_audio_pack::TryRefreshCandidates(m_Paths, m_Entries, m_Selection, "pack", "", [](auto &Out) {
		Out.insert("audio/new.wv");
		return true;
	}));
	ExpectSelected("audio/new.wv");
}

TEST_F(CAudioPackCandidatePublication, ScanExceptionKeepsPublishedPathsEntriesAndSelection)
{
	EXPECT_FALSE(qm_audio_pack::TryRefreshCandidates(m_Paths, m_Entries, m_Selection, "pack", "", [](auto &Out) -> bool {
		Out.insert("partial.wv");
		throw std::bad_alloc();
	}));
	ExpectOld();
}

TEST_F(CAudioPackCandidatePublication, PresentationFailureDoesNotReplacePathsBeforeEntries)
{
	const auto *pOldPaths = m_Paths.data();
	const auto *pOldEntries = m_Entries.data();
	EXPECT_FALSE(qm_audio_pack::TryRefreshCandidates(m_Paths, m_Entries, m_Selection, "pack", "", [](auto &Out) {
		Out.insert("audio/new.wv");
		return true; }, BuildThenThrow));
	ExpectOld();
	EXPECT_EQ(m_Paths.data(), pOldPaths);
	EXPECT_EQ(m_Entries.data(), pOldEntries);
}

TEST_F(CAudioPackCandidatePublication, RebuildFailureKeepsExistingSelectionAndDisplay)
{
	EXPECT_FALSE(qm_audio_pack::TryRebuildCandidates(m_Paths, m_Entries, m_Selection, "changed_pack", "audio/old_a.wv", BuildThenThrow));
	ExpectOld();
}

TEST_F(CAudioPackCandidatePublication, CurrentFileSelectionHasPriorityOverPreviousSelection)
{
	ASSERT_TRUE(qm_audio_pack::TryRebuildCandidates(m_Paths, m_Entries, m_Selection, "pack", "audio/old_a.wv"));
	ExpectSelected("audio/old_a.wv");
}

TEST_F(CAudioPackCandidatePublication, PreviousPathSelectionSurvivesReorderingWhenCurrentIsAbsent)
{
	m_Paths = {"audio/pack/target.wv", "audio/other/first.wv"};
	m_Entries = qm_audio_pack::BuildCandidateEntries(m_Paths, "other", "");
	m_Selection = 1;
	ASSERT_EQ(m_Entries.size(), 2u);
	ASSERT_EQ(m_Entries[1].m_Path, "audio/pack/target.wv");
	ASSERT_TRUE(qm_audio_pack::TryRebuildCandidates(m_Paths, m_Entries, m_Selection, "pack", ""));
	ExpectSelected("audio/pack/target.wv");
	EXPECT_EQ(m_Selection, 0);
}

TEST_F(CAudioPackCandidatePublication, SuccessfulEmptyScanPublishesEmptyListsAndNoSelection)
{
	ASSERT_TRUE(qm_audio_pack::TryRefreshCandidates(m_Paths, m_Entries, m_Selection, "pack", "", [](auto &) { return true; }));
	EXPECT_TRUE(m_Paths.empty());
	EXPECT_TRUE(m_Entries.empty());
	EXPECT_EQ(m_Selection, -1);
}

TEST_F(CAudioPackCandidatePublication, InvalidPreviousIndexFallsBackToFirstPreparedEntry)
{
	m_Selection = 99;
	ASSERT_TRUE(qm_audio_pack::TryRebuildCandidates(m_Paths, m_Entries, m_Selection, "pack", nullptr));
	EXPECT_EQ(m_Selection, 0);
}
