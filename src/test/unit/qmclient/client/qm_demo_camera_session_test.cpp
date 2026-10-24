#include <game/client/components/qmclient/demo_camera_session.h>

#include <gtest/gtest.h>

#include <map>
#include <new>
#include <set>
#include <stdexcept>

using namespace qm_demo_camera;

namespace
{
	class CBackend
	{
	public:
		std::map<std::string, SProject> m_Files;
		std::set<std::string> m_FailedWrites;
		bool m_ThrowResolve = false, m_ThrowLoad = false, m_ThrowSave = false;
		ELoadResult m_LoadResult = ELoadResult::MISSING;
		int m_Loads = 0, m_Saves = 0;
		std::string Resolve(const std::string &Path)
		{
			if(m_ThrowResolve)
				throw std::bad_alloc();
			return Path;
		}
		ELoadResult Load(const std::string &Path, SProject &Project)
		{
			++m_Loads;
			if(m_ThrowLoad)
				throw std::bad_alloc();
			if(m_LoadResult == ELoadResult::UNAVAILABLE || m_LoadResult == ELoadResult::FAILED)
				return m_LoadResult;
			auto It = m_Files.find(Path);
			if(It == m_Files.end())
				return ELoadResult::MISSING;
			Project = It->second;
			return ELoadResult::LOADED;
		}
		bool Save(const std::string &Path, const SProject &Project)
		{
			++m_Saves;
			if(m_ThrowSave)
				throw std::runtime_error("write failed");
			if(m_FailedWrites.count(Path))
				return false;
			m_Files[Path] = Project;
			return true;
		}
		bool Open(CSessionStore &Store, const char *pName, const std::string &Path)
		{
			return Store.Open(pName, [&] { return Resolve(Path); }, [&](const std::string &File, SProject &Project) { return Load(File, Project); }, [&](const std::string &File, const SProject &Project) { return Save(File, Project); });
		}
		bool SaveAll(CSessionStore &Store)
		{
			return Store.Save([&](const std::string &Path, const SProject &Project) { return Save(Path, Project); });
		}
	};
	void Edit(CSessionStore &Store, float X)
	{
		ASSERT_GE(Store.Project().m_Track.Insert({100.0, {{X, 0.0f}, 1.0f}}), 0);
		Store.Project().m_Enabled = true;
		Store.Changed();
	}
	float X(const CSessionStore &Store)
	{
		const auto View = Store.Project().m_Track.Sample(100.0);
		EXPECT_TRUE(View.has_value());
		return View ? View->m_Center.x : -1.0f;
	}
}

TEST(QmDemoCameraSession, MissingProjectStartsCleanWithoutSaving)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_TRUE(Store.Matches("a"));
	EXPECT_TRUE(Store.Project().m_Track.Empty());
	EXPECT_FALSE(Store.Dirty());
	EXPECT_FALSE(Store.LoadFailed());
	EXPECT_EQ(Io.m_Saves, 0);
}

TEST(QmDemoCameraSession, LoadedProjectIsPublishedWithItsValues)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_GE(Io.m_Files["root/a"].m_Track.Insert({100.0, {{7.0f, 0.0f}, 1.0f}}), 0);
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_FLOAT_EQ(X(Store), 7.0f);
}

TEST(QmDemoCameraSession, OpeningTheSameIdentityKeepsUnsavedEditsWithoutReloading)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 9.0f));
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_EQ(Io.m_Loads, 1);
	EXPECT_EQ(Io.m_Saves, 0);
	EXPECT_FLOAT_EQ(X(Store), 9.0f);
}

TEST(QmDemoCameraSession, ResolveExceptionKeepsCurrentEditsAndAllowsRetry)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 9.0f));
	Io.m_ThrowResolve = true;
	EXPECT_FALSE(Io.Open(Store, "b", "root/b"));
	EXPECT_TRUE(Store.Matches("a"));
	EXPECT_TRUE(Store.LoadFailed());
	EXPECT_FLOAT_EQ(X(Store), 9.0f);
	Io.m_ThrowResolve = false;
	EXPECT_TRUE(Io.Open(Store, "b", "root/b"));
	EXPECT_FALSE(Store.LoadFailed());
}

TEST(QmDemoCameraSession, LoaderExceptionDoesNotCommitTheCandidateFilename)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 9.0f));
	Io.m_ThrowLoad = true;
	EXPECT_FALSE(Io.Open(Store, "b", "root/b"));
	EXPECT_TRUE(Store.Matches("a"));
	EXPECT_EQ(Io.m_Saves, 0);
	Io.m_ThrowLoad = false;
	EXPECT_TRUE(Io.Open(Store, "b", "root/b"));
}

TEST(QmDemoCameraSession, ResourceFailureKeepsIdentityAvailableForAnotherLoad)
{
	CSessionStore Store;
	CBackend Io;
	Io.m_LoadResult = ELoadResult::UNAVAILABLE;
	EXPECT_FALSE(Io.Open(Store, "a", "root/a"));
	EXPECT_FALSE(Store.Matches("a"));
	Io.m_LoadResult = ELoadResult::MISSING;
	EXPECT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_EQ(Io.m_Loads, 2);
}

TEST(QmDemoCameraSession, FailedSaveRetainsOldEditsAndRestoresThemOnReopen)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 9.0f));
	Io.m_FailedWrites.insert("root/a");
	ASSERT_TRUE(Io.Open(Store, "b", "root/b"));
	EXPECT_TRUE(Store.Project().m_Track.Empty());
	EXPECT_EQ(Store.RetainedCount(), 1u);
	EXPECT_TRUE(Store.SaveFailed());
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_FLOAT_EQ(X(Store), 9.0f);
	EXPECT_TRUE(Store.Dirty());
	EXPECT_EQ(Io.m_Loads, 2);
}

TEST(QmDemoCameraSession, MultipleFailedProjectsRetainTheirSeparateContents)
{
	CSessionStore Store;
	CBackend Io;
	Io.m_ThrowSave = true;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 1.0f));
	ASSERT_TRUE(Io.Open(Store, "b", "root/b"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 2.0f));
	ASSERT_TRUE(Io.Open(Store, "c", "root/c"));
	EXPECT_EQ(Store.RetainedCount(), 2u);
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_FLOAT_EQ(X(Store), 1.0f);
	ASSERT_TRUE(Io.Open(Store, "b", "root/b"));
	EXPECT_FLOAT_EQ(X(Store), 2.0f);
}

TEST(QmDemoCameraSession, SameDemoNameInDifferentRootsDoesNotReuseAnotherProject)
{
	CSessionStore Store;
	CBackend Io;
	Io.m_ThrowSave = true;
	ASSERT_TRUE(Io.Open(Store, "race", "root1/race"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 1.0f));
	ASSERT_TRUE(Io.Open(Store, "race", "root2/race"));
	EXPECT_TRUE(Store.Project().m_Track.Empty());
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 2.0f));
	ASSERT_TRUE(Io.Open(Store, "race", "root1/race"));
	EXPECT_FLOAT_EQ(X(Store), 1.0f);
}

TEST(QmDemoCameraSession, SaveRetryWritesAllPendingProjectsAndReleasesRetainedState)
{
	CSessionStore Store;
	CBackend Io;
	Io.m_ThrowSave = true;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 1.0f));
	ASSERT_TRUE(Io.Open(Store, "b", "root/b"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 2.0f));
	Io.m_ThrowSave = false;
	EXPECT_TRUE(Io.SaveAll(Store));
	EXPECT_EQ(Store.RetainedCount(), 0u);
	EXPECT_FALSE(Store.Dirty());
	EXPECT_FALSE(Store.SaveFailed());
	const auto A = Io.m_Files.at("root/a").m_Track.Sample(100.0);
	const auto B = Io.m_Files.at("root/b").m_Track.Sample(100.0);
	ASSERT_TRUE(A.has_value());
	ASSERT_TRUE(B.has_value());
	EXPECT_FLOAT_EQ(A->m_Center.x, 1.0f);
	EXPECT_FLOAT_EQ(B->m_Center.x, 2.0f);
}

TEST(QmDemoCameraSession, FailedCloseKeepsCurrentEditsForReopening)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 7.0f));
	Io.m_ThrowSave = true;
	Store.Close([&](const std::string &Path, const SProject &Project) { return Io.Save(Path, Project); });
	EXPECT_TRUE(Store.Dirty());
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_FLOAT_EQ(X(Store), 7.0f);
	EXPECT_EQ(Io.m_Loads, 1);
}

TEST(QmDemoCameraSession, SuccessfulCloseReloadsFromStorageOnNextOpen)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 7.0f));
	Store.Close([&](const std::string &Path, const SProject &Project) { return Io.Save(Path, Project); });
	EXPECT_FALSE(Store.Matches("a"));
	ASSERT_GE(Io.m_Files.at("root/a").m_Track.Insert({100.0, {{8.0f, 0.0f}, 1.0f}}), 0);
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_FLOAT_EQ(X(Store), 8.0f);
}

TEST(QmDemoCameraSession, ThrownWriteKeepsDirtyStateAndCanBeRetried)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 7.0f));
	Io.m_ThrowSave = true;
	EXPECT_FALSE(Io.SaveAll(Store));
	EXPECT_TRUE(Store.Dirty());
	Io.m_ThrowSave = false;
	EXPECT_TRUE(Io.SaveAll(Store));
	EXPECT_FALSE(Store.Dirty());
}

TEST(QmDemoCameraSession, FailedCandidateDoesNotConsumeASaveAttemptOfTheCurrentProject)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 7.0f));
	Io.m_LoadResult = ELoadResult::UNAVAILABLE;
	EXPECT_FALSE(Io.Open(Store, "b", "root/b"));
	EXPECT_EQ(Io.m_Saves, 0);
	EXPECT_TRUE(Store.Dirty());
}

TEST(QmDemoCameraSession, MalformedSidecarKeepsTheExistingEmptyEditableFallback)
{
	CSessionStore Store;
	CBackend Io;
	Io.m_LoadResult = ELoadResult::FAILED;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	EXPECT_TRUE(Store.LoadFailed());
	EXPECT_TRUE(Store.Project().m_Track.Empty());
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 7.0f));
	EXPECT_FALSE(Store.LoadFailed());
}

TEST(QmDemoCameraSession, SuccessfulSwitchSavesCurrentProjectWithoutRetainingIt)
{
	CSessionStore Store;
	CBackend Io;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 7.0f));
	ASSERT_TRUE(Io.Open(Store, "b", "root/b"));
	EXPECT_EQ(Store.RetainedCount(), 0u);
	EXPECT_FALSE(Store.Dirty());
	EXPECT_TRUE(Io.m_Files.count("root/a"));
}

TEST(QmDemoCameraSession, PartialSaveSuccessLeavesOnlyTheFailedProjectPending)
{
	CSessionStore Store;
	CBackend Io;
	Io.m_ThrowSave = true;
	ASSERT_TRUE(Io.Open(Store, "a", "root/a"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 1.0f));
	ASSERT_TRUE(Io.Open(Store, "b", "root/b"));
	ASSERT_NO_FATAL_FAILURE(Edit(Store, 2.0f));
	ASSERT_TRUE(Io.Open(Store, "c", "root/c"));
	Io.m_ThrowSave = false;
	Io.m_FailedWrites.insert("root/b");
	EXPECT_FALSE(Io.SaveAll(Store));
	EXPECT_EQ(Store.RetainedCount(), 1u);
	EXPECT_TRUE(Io.m_Files.count("root/a"));
	Io.m_FailedWrites.clear();
	EXPECT_TRUE(Io.SaveAll(Store));
	EXPECT_FALSE(Store.Dirty());
}

TEST(QmDemoCameraSession, EmptyResolvedPathDoesNotCommitTheCandidate)
{
	CSessionStore Store;
	CBackend Io;
	EXPECT_FALSE(Io.Open(Store, "a", ""));
	EXPECT_FALSE(Store.Matches("a"));
	EXPECT_TRUE(Store.LoadFailed());
	EXPECT_EQ(Io.m_Loads, 0);
}
