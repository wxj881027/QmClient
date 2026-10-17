#include <engine/client/qm_font_restore.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>

#include <cstring>
#include <unordered_set>

namespace
{
	class CQmFontRestoreTest : public ::testing::Test
	{
	protected:
		CQmFontRestore m_Restore;
		std::unordered_set<std::string> m_Available;
		std::vector<std::pair<size_t, std::string>> m_vApplied;

		static bool Read(const char *pPath, void **ppData, unsigned *pSize)
		{
			if(std::strcmp(pPath, "missing") == 0)
				return false;
			*pSize = 3;
			*ppData = malloc(*pSize);
			std::memcpy(*ppData, "abc", *pSize);
			return true;
		}

		std::shared_ptr<CQmFontScanJob> Request(const CQmFontRestore::TSelection &apSelection, CQmFontRestore::FFindFiles Find = [](const auto &vNames, const auto &) { return vNames; })
		{
			return m_Restore.Request(apSelection, [this](const char *pName) { return m_Available.count(pName) != 0; }, std::move(Find), Read);
		}

		bool Finish(const CQmFontRestore::TSelection &apSelection)
		{
			return m_Restore.Finish(apSelection, [this](const char *pName) { return m_Available.count(pName) != 0; }, [this](size_t Role, const char *pName) { m_vApplied.emplace_back(Role, pName); });
		}

		void Complete(const std::shared_ptr<CQmFontScanJob> &pJob)
		{
			CJobPool Pool;
			Pool.Init(1);
			Pool.Add(pJob);
			EXPECT_TRUE(WaitForJobDone(*pJob));
			Pool.Shutdown();
		}

		void Receive()
		{
			CQmFontScanJob::SFile File;
			ASSERT_TRUE(m_Restore.TakeNext(File));
			m_Available.insert(File.m_Path);
		}
	};
}

TEST_F(CQmFontRestoreTest, StartingRestoreDoesNotEnumerateOrReadOnCaller)
{
	int Enumerations = 0;
	int Reads = 0;
	const auto pJob = m_Restore.Request({"saved", "", ""}, [](const char *) { return false; }, [&](const auto &vNames, const auto &) { ++Enumerations; return vNames; }, [&](const char *pPath, void **ppData, unsigned *pSize) { ++Reads; return Read(pPath, ppData, pSize); });
	ASSERT_NE(pJob, nullptr);
	EXPECT_EQ(Enumerations, 0);
	EXPECT_EQ(Reads, 0);
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(m_Restore.TakeNext(File));
	EXPECT_FALSE(Finish({"saved", "", ""}));
	Complete(pJob);
	EXPECT_EQ(Enumerations, 1);
	EXPECT_EQ(Reads, 1);
	Receive();
	EXPECT_TRUE(Finish({"saved", "", ""}));
	EXPECT_EQ(m_vApplied, (std::vector<std::pair<size_t, std::string>>{{0, "saved"}}));
}

TEST_F(CQmFontRestoreTest, EmptyAndAlreadyLoadedSelectionsDoNotStartLookup)
{
	m_Available.insert("bundled");
	EXPECT_EQ(Request({"bundled", "", ""}), nullptr);
	EXPECT_FALSE(m_Restore.Active());
}

TEST_F(CQmFontRestoreTest, RepeatedRolesShareOneLookupAndRestoreTheirOwnSelections)
{
	std::vector<std::string> vRequested;
	const auto pJob = Request({"shared", "shared", ""}, [&](const auto &vNames, const auto &) { vRequested = vNames; return vNames; });
	ASSERT_NE(pJob, nullptr);
	EXPECT_EQ(Request({"shared", "shared", ""}), nullptr);
	Complete(pJob);
	EXPECT_EQ(vRequested, (std::vector<std::string>{"shared"}));
	Receive();
	EXPECT_TRUE(Finish({"shared", "shared", ""}));
	EXPECT_EQ(m_vApplied, (std::vector<std::pair<size_t, std::string>>{{0, "shared"}, {1, "shared"}}));
	EXPECT_EQ(Request({"shared", "shared", ""}), nullptr);
}

TEST_F(CQmFontRestoreTest, AllMatchedFilesAreReceivedBeforeApplyingSelections)
{
	const auto pJob = Request({"latin", "cjk", ""});
	ASSERT_NE(pJob, nullptr);
	Complete(pJob);
	Receive();
	EXPECT_FALSE(Finish({"latin", "cjk", ""}));
	EXPECT_TRUE(m_vApplied.empty());
	Receive();
	EXPECT_TRUE(Finish({"latin", "cjk", ""}));
	EXPECT_EQ(m_vApplied.size(), 2u);
}

TEST_F(CQmFontRestoreTest, ChangingSelectionWhilePendingDoesNotApplyOldFont)
{
	const auto pJob = Request({"old", "", ""});
	ASSERT_NE(pJob, nullptr);
	EXPECT_EQ(Request({"new", "", ""}), nullptr);
	Complete(pJob);
	Receive();
	EXPECT_TRUE(Finish({"new", "", ""}));
	EXPECT_TRUE(m_vApplied.empty());
	EXPECT_NE(Request({"new", "", ""}), nullptr);
}

TEST_F(CQmFontRestoreTest, ClearingOneRolePreservesTheOtherPendingRole)
{
	const auto pJob = Request({"shared", "shared", ""});
	ASSERT_NE(pJob, nullptr);
	Complete(pJob);
	Receive();
	EXPECT_TRUE(Finish({"", "shared", ""}));
	EXPECT_EQ(m_vApplied, (std::vector<std::pair<size_t, std::string>>{{1, "shared"}}));
}

TEST_F(CQmFontRestoreTest, ChangedSelectionResolvedByTheSameCollectionUsesCurrentName)
{
	const auto pJob = Request({"old style", "", ""});
	ASSERT_NE(pJob, nullptr);
	Complete(pJob);
	Receive();
	m_Available.insert("new style");
	EXPECT_TRUE(Finish({"new style", "", ""}));
	EXPECT_EQ(m_vApplied, (std::vector<std::pair<size_t, std::string>>{{0, "new style"}}));
}

TEST_F(CQmFontRestoreTest, MissingFontKeepsFallbackAndDoesNotRetryEveryFrame)
{
	const auto pJob = Request({"missing", "", ""});
	ASSERT_NE(pJob, nullptr);
	Complete(pJob);
	EXPECT_TRUE(Finish({"missing", "", ""}));
	EXPECT_TRUE(m_vApplied.empty());
	EXPECT_EQ(Request({"missing", "", ""}), nullptr);
	EXPECT_EQ(Request({"", "", ""}), nullptr);
	EXPECT_NE(Request({"missing", "", ""}), nullptr);
}

TEST_F(CQmFontRestoreTest, InvalidFontBytesDoNotApplyAnUnavailableFace)
{
	const auto pJob = Request({"invalid", "", ""});
	ASSERT_NE(pJob, nullptr);
	Complete(pJob);
	CQmFontScanJob::SFile File;
	ASSERT_TRUE(m_Restore.TakeNext(File));
	// 文件读取成功但文字引擎解析失败，face 池保持原样。
	EXPECT_TRUE(Finish({"invalid", "", ""}));
	EXPECT_TRUE(m_vApplied.empty());
}

TEST_F(CQmFontRestoreTest, ResetDropsPendingResultsAndAllowsANewRequest)
{
	const auto pJob = Request({"saved", "", ""});
	ASSERT_NE(pJob, nullptr);
	m_Restore.Reset();
	EXPECT_EQ(pJob->State(), IJob::STATE_ABORTED);
	CQmFontScanJob::SFile File;
	EXPECT_FALSE(m_Restore.TakeNext(File));
	EXPECT_FALSE(Finish({"saved", "", ""}));
	EXPECT_TRUE(m_vApplied.empty());
	const auto pNext = Request({"saved", "", ""});
	ASSERT_NE(pNext, nullptr);
	Complete(pNext);
	Receive();
	EXPECT_TRUE(Finish({"saved", "", ""}));
	EXPECT_EQ(m_vApplied, (std::vector<std::pair<size_t, std::string>>{{0, "saved"}}));
}
