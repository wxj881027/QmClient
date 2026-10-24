#include <game/client/components/qmclient/demo_camera_session.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <new>

namespace
{
	class CFileFaults : public qm_demo_camera::CFileOperations
	{
	public:
		bool m_ThrowRead = false, m_ShortRead = false, m_CloseError = false;
		int m_Closes = 0;
		bool ReadText(IOHANDLE File, size_t Length, std::string &Json) override
		{
			if(m_ThrowRead)
				throw std::bad_alloc();
			return !m_ShortRead && CFileOperations::ReadText(File, Length, Json);
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CFileOperations::Close(File);
			return m_CloseError ? -1 : Result;
		}
	};
	class CQmDemoCameraStorage : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		qm_demo_camera::SProject m_Project;
		const std::string m_Path = qm_demo_camera::SidecarPath("race.demo");

		void SetUp() override
		{
			m_Project.m_Enabled = true;
			ASSERT_GE(m_Project.m_Track.Insert({100.0, {{10.0f, 20.0f}, 1.5f}}), 0);
		}
	};
}

TEST_F(CQmDemoCameraStorage, ReopeningTheSidecarRestoresTheSavedCameraAndEnableState)
{
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	qm_demo_camera::SProject Reopened;
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Reopened), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_TRUE(Reopened.m_Enabled);
	const auto Sample = Reopened.m_Track.Sample(100.0);
	ASSERT_TRUE(Sample.has_value());
	EXPECT_EQ(Sample->m_Center, vec2(10.0f, 20.0f));
	EXPECT_FLOAT_EQ(Sample->m_Zoom, 1.5f);
	EXPECT_FALSE(m_pStorage->FileExists((m_Path + ".tmp").c_str(), IStorage::TYPE_SAVE));
}

TEST_F(CQmDemoCameraStorage, FailedWriteKeepsThePreviousFileAndCanBeRetried)
{
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	ASSERT_TRUE(m_pStorage->CreateFolder((m_Path + ".tmp").c_str(), IStorage::TYPE_SAVE));
	m_Project.m_Enabled = false;
	EXPECT_FALSE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	qm_demo_camera::SProject Previous;
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_TRUE(Previous.m_Enabled);
	ASSERT_TRUE(m_pStorage->RemoveFolder((m_Path + ".tmp").c_str(), IStorage::TYPE_SAVE));
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_FALSE(Previous.m_Enabled);
}

TEST_F(CQmDemoCameraStorage, MissingAndMalformedFilesAreDistinguishedWithoutLosingCurrentEdits)
{
	EXPECT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, m_Project), qm_demo_camera::ELoadResult::MISSING);
	IOHANDLE File = m_pStorage->OpenFile(m_Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_write(File, "broken", 6), 6U);
	ASSERT_EQ(io_close(File), 0);
	EXPECT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, m_Project), qm_demo_camera::ELoadResult::FAILED);
	EXPECT_TRUE(m_Project.m_Enabled);
	EXPECT_EQ(m_Project.m_Track.Frames().size(), 1U);
}

TEST_F(CQmDemoCameraStorage, ExportedCutReloadsWithTheSourceComposition)
{
	ASSERT_GE(m_Project.m_Track.Insert({200.0, {{110.0f, 220.0f}, 2.5f}}), 0);
	const auto Cut = m_Project.m_Track.Cut({{125, 175}});
	ASSERT_TRUE(Cut.has_value());
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, {*Cut, true}));
	qm_demo_camera::SProject Reopened;
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Reopened), qm_demo_camera::ELoadResult::LOADED);
	const auto ReopenedSample = Reopened.m_Track.Sample(150.0);
	const auto OriginalSample = m_Project.m_Track.Sample(150.0);
	ASSERT_TRUE(ReopenedSample.has_value());
	ASSERT_TRUE(OriginalSample.has_value());
	EXPECT_NEAR(ReopenedSample->m_Center.x, OriginalSample->m_Center.x, 0.0001f);
}

TEST_F(CQmDemoCameraStorage, SidecarIsResolvedBesideTheSelectedDemo)
{
	IOHANDLE File = m_pStorage->OpenFile("race.demo", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_close(File), 0);
	const std::string Path = qm_demo_camera::ResolveSidecar(m_pStorage.get(), "race.demo", IStorage::TYPE_SAVE);
	ASSERT_FALSE(Path.empty());
	EXPECT_FALSE(fs_is_relative_path(Path.c_str()));
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), Path, m_Project));
	qm_demo_camera::SProject Restored;
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Restored), qm_demo_camera::ELoadResult::LOADED);
	const auto Sample = Restored.m_Track.Sample(100.0);
	ASSERT_TRUE(Sample.has_value());
	EXPECT_EQ(Sample->m_Center, vec2(10.0f, 20.0f));
	EXPECT_TRUE(qm_demo_camera::ResolveSidecar(m_pStorage.get(), "missing.demo", IStorage::TYPE_SAVE).empty());
}

TEST_F(CQmDemoCameraStorage, ReadAllocationFailureClosesTheFileAndCanBeRetried)
{
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	CFileFaults Operations;
	Operations.m_ThrowRead = true;
	qm_demo_camera::SProject Previous = m_Project;
	EXPECT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous, &Operations), qm_demo_camera::ELoadResult::UNAVAILABLE);
	EXPECT_EQ(Operations.m_Closes, 1);
	EXPECT_TRUE(Previous.m_Enabled);
	Operations.m_ThrowRead = false;
	EXPECT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous, &Operations), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_EQ(Operations.m_Closes, 2);
}

TEST_F(CQmDemoCameraStorage, ShortReadClosesOnceAndKeepsThePreviousProject)
{
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	CFileFaults Operations;
	Operations.m_ShortRead = true;
	qm_demo_camera::SProject Previous = m_Project;
	EXPECT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous, &Operations), qm_demo_camera::ELoadResult::FAILED);
	EXPECT_EQ(Operations.m_Closes, 1);
	EXPECT_TRUE(Previous.m_Enabled);
	ASSERT_EQ(Previous.m_Track.Frames().size(), 1u);
}

TEST_F(CQmDemoCameraStorage, LoadCloseFailureDoesNotPublishTheReadProject)
{
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	CFileFaults Operations;
	Operations.m_CloseError = true;
	qm_demo_camera::SProject Previous;
	EXPECT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous, &Operations), qm_demo_camera::ELoadResult::FAILED);
	EXPECT_EQ(Operations.m_Closes, 1);
	EXPECT_TRUE(Previous.m_Track.Empty());
	Operations.m_CloseError = false;
	EXPECT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous, &Operations), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_TRUE(Previous.m_Enabled);
}

TEST_F(CQmDemoCameraStorage, SaveCloseFailurePreservesThePreviouslyPublishedFile)
{
	ASSERT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	m_Project.m_Enabled = false;
	CFileFaults Operations;
	Operations.m_CloseError = true;
	EXPECT_FALSE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project, &Operations));
	EXPECT_EQ(Operations.m_Closes, 1);
	qm_demo_camera::SProject Previous;
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_TRUE(Previous.m_Enabled);
	EXPECT_TRUE(qm_demo_camera::Save(m_pStorage.get(), m_Path, m_Project));
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Previous), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_FALSE(Previous.m_Enabled);
}

TEST_F(CQmDemoCameraStorage, FailedSessionWriteSurvivesSwitchAndCanBeSavedAfterReturning)
{
	qm_demo_camera::CSessionStore Store;
	const auto Load = [&](const std::string &Path, qm_demo_camera::SProject &Project) { return qm_demo_camera::Load(m_pStorage.get(), Path, Project); };
	const auto Save = [&](const std::string &Path, const qm_demo_camera::SProject &Project) { return qm_demo_camera::Save(m_pStorage.get(), Path, Project); };
	ASSERT_TRUE(Store.Open("race.demo", [&] { return m_Path; }, Load, Save));
	Store.Project() = m_Project;
	Store.Changed();
	ASSERT_TRUE(m_pStorage->CreateFolder((m_Path + ".tmp").c_str(), IStorage::TYPE_SAVE));
	ASSERT_TRUE(Store.Open("next.demo", [] { return qm_demo_camera::SidecarPath("next.demo"); }, Load, Save));
	EXPECT_TRUE(Store.Project().m_Track.Empty());
	EXPECT_EQ(Store.RetainedCount(), 1u);
	ASSERT_TRUE(Store.Open("race.demo", [&] { return m_Path; }, Load, Save));
	ASSERT_EQ(Store.Project().m_Track.Frames().size(), 1u);
	EXPECT_EQ(Store.Project().m_Track.Frames()[0].m_View.m_Center, vec2(10.0f, 20.0f));
	ASSERT_TRUE(m_pStorage->RemoveFolder((m_Path + ".tmp").c_str(), IStorage::TYPE_SAVE));
	EXPECT_TRUE(Store.Save(Save));
	EXPECT_FALSE(Store.Dirty());
	qm_demo_camera::SProject Reopened;
	ASSERT_EQ(qm_demo_camera::Load(m_pStorage.get(), m_Path, Reopened), qm_demo_camera::ELoadResult::LOADED);
	EXPECT_TRUE(Reopened.m_Enabled);
	ASSERT_EQ(Reopened.m_Track.Frames().size(), 1u);
	EXPECT_EQ(Reopened.m_Track.Frames()[0].m_View.m_Center, vec2(10.0f, 20.0f));
}
