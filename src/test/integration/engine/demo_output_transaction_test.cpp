#include <base/time.h>

#include <engine/shared/config.h>
#include <engine/shared/demo_output.h>

#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

#include <filesystem>
#include <functional>
#include <new>
#include <string>

namespace
{
	class CObservedDemoOutput : public demo_output::COperations
	{
	public:
		int m_Creates = 0;
		int m_Publishes = 0;
		int m_Removes = 0;
		bool m_CreateFails = false;
		bool m_CreateThrows = false;
		bool m_AlwaysExists = false;
		bool m_PublishFails = false;
		bool m_PublishThrows = false;
		bool m_RemoveFails = false;
		bool m_RemoveThrows = false;
		std::string m_Directory;
		std::function<void(const char *, const char *)> m_BeforePublish;

		ECreate CreateDirectory(const char *pPath) override
		{
			++m_Creates;
			m_Directory = pPath;
			if(m_CreateThrows)
				throw std::bad_alloc();
			if(m_CreateFails)
				return ECreate::FAILED;
			return m_AlwaysExists ? ECreate::EXISTS : COperations::CreateDirectory(pPath);
		}

		bool Publish(const char *pTemporary, const char *pDestination) override
		{
			++m_Publishes;
			if(m_BeforePublish)
				m_BeforePublish(pTemporary, pDestination);
			if(m_PublishThrows)
				throw 7;
			return !m_PublishFails && COperations::Publish(pTemporary, pDestination);
		}

		bool Remove(const char *pPath) override
		{
			++m_Removes;
			if(m_RemoveThrows)
				throw std::bad_alloc();
			return !m_RemoveFails && COperations::Remove(pPath);
		}
	};

	class CRecordingFault : public CDemoRecorder::CFileOperations
	{
	public:
		bool m_ShortWrite = false;
		bool m_FlushFails = false;
		bool m_CloseFails = false;
		int m_Closes = 0;

		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept override
		{
			return CFileOperations::Write(File, pData, m_ShortWrite && Size > 0 ? Size - 1 : Size);
		}
		int Flush(IOHANDLE File) noexcept override
		{
			const int Result = CFileOperations::Flush(File);
			return m_FlushFails ? -1 : Result;
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CFileOperations::Close(File);
			return m_CloseFails ? 1 : Result;
		}
	};

	class DemoOutputTransactionIntegration : public ::testing::Test
	{
	protected:
		CDemoStorageState m_State;
		CObservedDemoOutput m_Operations;
		CRecordingFault m_Recording;
		int m_SliceBegin = -1;
		int m_SliceEnd = -1;

		void SetUp() override
		{
			m_SliceBegin = g_Config.m_ClDemoSliceBegin;
			m_SliceEnd = g_Config.m_ClDemoSliceEnd;
			ASSERT_NE(m_State.m_pStorage, nullptr);
			CNetBase::Init();
		}
		void TearDown() override
		{
			g_Config.m_ClDemoSliceBegin = m_SliceBegin;
			g_Config.m_ClDemoSliceEnd = m_SliceEnd;
		}
		bool Slice(DEMOFUNC_FILTER pfnFilter = nullptr)
		{
			CDemoEditor Editor;
			Editor.Init(&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, nullptr, m_State.m_pStorage.get(), nullptr, &m_Operations, &m_Recording);
			return Editor.Slice("source.demo", "target.demo", -1, -1, pfnFilter, nullptr);
		}
		void Prepare()
		{
			m_State.Create();
			m_State.Write("target.demo", "previous");
		}
		bool DirectoryExists()
		{
			return !m_Operations.m_Directory.empty() && std::filesystem::exists(std::filesystem::u8path(m_Operations.m_Directory));
		}
	};
}

TEST_F(DemoOutputTransactionIntegration, ExistingTargetRemainsIntactUntilPublish)
{
	m_State.Write("target.demo", "previous");
	{
		demo_output::CTransaction Output(&m_Operations);
		ASSERT_TRUE(Output.Prepare(*m_State.m_pStorage, "target.demo"));
		m_State.Write(Output.Filename(), "complete");
		EXPECT_EQ(m_State.Read("target.demo"), "previous");
		ASSERT_TRUE(Output.Publish());
		EXPECT_FALSE(Output.Publish());
	}
	EXPECT_EQ(m_State.Read("target.demo"), "complete");
	EXPECT_FALSE(DirectoryExists());
	EXPECT_EQ(m_Operations.m_Publishes, 1);
}

TEST_F(DemoOutputTransactionIntegration, RollbackRemovesOnlyItsUnpublishedOutput)
{
	m_State.Write("target.demo", "previous");
	{
		demo_output::CTransaction Output(&m_Operations);
		ASSERT_TRUE(Output.Prepare(*m_State.m_pStorage, "target.demo"));
		m_State.Write(Output.Filename(), "partial");
	}
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_FALSE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, PublishFailureKeepsOldTargetAndRollsBackCandidate)
{
	Prepare();
	m_Operations.m_PublishFails = true;
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_TRUE(m_State.Exists("source.demo"));
	EXPECT_FALSE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, PublishExceptionKeepsOldTargetAndRollsBackCandidate)
{
	Prepare();
	m_Operations.m_PublishThrows = true;
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_FALSE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, PreparationFailureDoesNotOpenAnOutputFile)
{
	Prepare();
	m_Operations.m_CreateFails = true;
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Recording.m_Closes, 0);
}

TEST_F(DemoOutputTransactionIntegration, PreparationExceptionIsContainedBeforeOpeningOutput)
{
	Prepare();
	m_Operations.m_CreateThrows = true;
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Recording.m_Closes, 0);
}

TEST_F(DemoOutputTransactionIntegration, CollisionRetriesAreBoundedAndDoNotClaimForeignPaths)
{
	m_Operations.m_AlwaysExists = true;
	{
		demo_output::CTransaction Output(&m_Operations);
		EXPECT_FALSE(Output.Prepare(*m_State.m_pStorage, "target.demo"));
	}
	EXPECT_EQ(m_Operations.m_Creates, 64);
	EXPECT_EQ(m_Operations.m_Removes, 0);
}

TEST_F(DemoOutputTransactionIntegration, RelativeParentEscapeIsRejectedBeforeFilesystemMutation)
{
	demo_output::CTransaction Output(&m_Operations);
	EXPECT_FALSE(Output.Prepare(*m_State.m_pStorage, "../target.demo"));
	EXPECT_EQ(m_Operations.m_Creates, 0);
}

TEST_F(DemoOutputTransactionIntegration, DirectoryDestinationIsRejectedBeforeFilesystemMutation)
{
	ASSERT_TRUE(m_State.m_pStorage->CreateFolder("target.demo", IStorage::TYPE_SAVE));
	demo_output::CTransaction Output(&m_Operations);
	EXPECT_FALSE(Output.Prepare(*m_State.m_pStorage, "target.demo"));
	EXPECT_EQ(m_Operations.m_Creates, 0);
}

TEST_F(DemoOutputTransactionIntegration, SeparateRequestsReserveDifferentTemporaryDirectories)
{
	demo_output::CTransaction First, Second;
	ASSERT_TRUE(First.Prepare(*m_State.m_pStorage, "target.demo"));
	ASSERT_TRUE(Second.Prepare(*m_State.m_pStorage, "target.demo"));
	EXPECT_STRNE(First.Filename(), Second.Filename());
}

TEST_F(DemoOutputTransactionIntegration, LongDestinationFailsWithoutTruncatingOrCreatingPaths)
{
	const std::string Path(IO_MAX_PATH_LENGTH, 'a');
	demo_output::CTransaction Output(&m_Operations);
	EXPECT_FALSE(Output.Prepare(*m_State.m_pStorage, Path.c_str()));
	EXPECT_EQ(m_Operations.m_Creates, 0);
}

TEST_F(DemoOutputTransactionIntegration, AbsoluteUtf8DestinationUsesItsOwnParentDirectory)
{
	std::string Destination;
	ASSERT_TRUE(demo_output::Resolve(*m_State.m_pStorage, "录像.demo", Destination));
	{
		demo_output::CTransaction Output;
		ASSERT_TRUE(Output.Prepare(*m_State.m_pStorage, Destination.c_str()));
		m_State.Write(Output.Filename(), "utf8");
		EXPECT_TRUE(Output.Publish());
	}
	EXPECT_EQ(m_State.Read(Destination.c_str()), "utf8");
}

TEST_F(DemoOutputTransactionIntegration, ShortOutputWriteKeepsPreviousTarget)
{
	Prepare();
	m_Recording.m_ShortWrite = true;
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Recording.m_Closes, 1);
	EXPECT_FALSE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, FlushFailurePreventsPublication)
{
	Prepare();
	m_Recording.m_FlushFails = true;
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Operations.m_Publishes, 0);
	EXPECT_FALSE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, CloseFailurePreventsPublication)
{
	Prepare();
	m_Recording.m_CloseFails = true;
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Operations.m_Publishes, 0);
	EXPECT_EQ(m_Recording.m_Closes, 1);
}

TEST_F(DemoOutputTransactionIntegration, ThrowingFilterClosesOutputBeforeRollback)
{
	Prepare();
	const auto Filter = [](const void *, int, void *) -> bool { throw std::bad_alloc(); };
	EXPECT_FALSE(Slice(Filter));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Recording.m_Closes, 1);
	EXPECT_FALSE(DirectoryExists());
	EXPECT_TRUE(Slice());
}

TEST_F(DemoOutputTransactionIntegration, UnknownFilterExceptionIsContainedAndPreservesTarget)
{
	Prepare();
	const auto Filter = [](const void *, int, void *) -> bool { throw 7; };
	EXPECT_FALSE(Slice(Filter));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_FALSE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, CandidateIsClosedBeforeReplacingTheTarget)
{
	Prepare();
	m_Operations.m_BeforePublish = [&](const char *pTemporary, const char *) {
		EXPECT_EQ(m_Recording.m_Closes, 1);
		EXPECT_EQ(m_State.Read("target.demo"), "previous");
		EXPECT_TRUE(m_State.Exists(pTemporary));
	};
	EXPECT_TRUE(Slice());
	EXPECT_EQ(m_State.MapData("target.demo"), "mapbytes");
	EXPECT_FALSE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, CleanupFailureDoesNotUndoACompletedSave)
{
	Prepare();
	m_Operations.m_RemoveFails = true;
	EXPECT_TRUE(Slice());
	EXPECT_EQ(m_State.MapData("target.demo"), "mapbytes");
	EXPECT_TRUE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, CleanupExceptionDoesNotEscapeACompletedSave)
{
	Prepare();
	m_Operations.m_RemoveThrows = true;
	EXPECT_TRUE(Slice());
	EXPECT_EQ(m_State.MapData("target.demo"), "mapbytes");
}

TEST_F(DemoOutputTransactionIntegration, RollbackDoesNotRecursivelyRemoveUnexpectedDirectoryContents)
{
	m_State.Write("target.demo", "previous");
	std::string Foreign;
	{
		demo_output::CTransaction Output(&m_Operations);
		ASSERT_TRUE(Output.Prepare(*m_State.m_pStorage, "target.demo"));
		m_State.Write(Output.Filename(), "partial");
		Foreign = m_Operations.m_Directory + "/foreign.keep";
		m_State.Write(Foreign.c_str(), "keep");
	}
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_State.Read(Foreign.c_str()), "keep");
	EXPECT_TRUE(DirectoryExists());
}

TEST_F(DemoOutputTransactionIntegration, MissingSourceDoesNotChangeTarget)
{
	m_State.Write("target.demo", "previous");
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Operations.m_Creates, 0);
}

TEST_F(DemoOutputTransactionIntegration, NoRecordedSnapshotsCannotBePublishedAsSuccessful)
{
	m_State.Create("source.demo", false);
	m_State.Write("target.demo", "previous");
	EXPECT_FALSE(Slice());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
}

TEST_F(DemoOutputTransactionIntegration, InternalSliceDoesNotResetUserSelectionMarkers)
{
	Prepare();
	g_Config.m_ClDemoSliceBegin = 110;
	g_Config.m_ClDemoSliceEnd = 140;
	EXPECT_TRUE(Slice());
	EXPECT_EQ(g_Config.m_ClDemoSliceBegin, 110);
	EXPECT_EQ(g_Config.m_ClDemoSliceEnd, 140);
}

TEST_F(DemoOutputTransactionIntegration, PrimaryPlaybackLoadStillResetsUserSelectionMarkers)
{
	m_State.Create();
	g_Config.m_ClDemoSliceBegin = 110;
	g_Config.m_ClDemoSliceEnd = 140;
	CDemoPlayer Player(&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false);
	EXPECT_EQ(Player.Load(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE), 0);
	EXPECT_EQ(g_Config.m_ClDemoSliceBegin, -1);
	EXPECT_EQ(g_Config.m_ClDemoSliceEnd, -1);
	Player.Stop();
}

TEST(DemoOutputClockDeathTest, InternalSliceDoesNotChangeTheCallersCachedFrameTime)
{
	ASSERT_EXIT(
		{
			const bool Preserved = []() {
				CDemoStorageState State;
				if(!State.m_pStorage)
					return false;
				CNetBase::Init();
				State.Create();
				set_new_tick();
				const int64_t FrameTime = time_get();
				CDemoEditor Editor;
				Editor.Init(&*State.m_pDelta, &*State.m_pDeltaSixup, nullptr, State.m_pStorage.get());
				return Editor.Slice("source.demo", "target.demo", 100, 150, nullptr, nullptr) && time_get() == FrameTime;
			}();
			std::exit(Preserved ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
}

TEST(DemoOutputClockDeathTest, PrimaryPlaybackStillUsesTheExistingCachedFrameTime)
{
	ASSERT_EXIT(
		{
			const bool Preserved = []() {
				CDemoStorageState State;
				if(!State.m_pStorage)
					return false;
				CNetBase::Init();
				State.Create();
				CDemoPlayer Player(&*State.m_pDelta, &*State.m_pDeltaSixup, false);
				if(Player.Load(State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, false) != 0)
					return false;
				set_new_tick();
				(void)time_get();
				Player.Play();
				const bool Cached = Player.IsPlaying() && Player.Info()->m_LastUpdate == time_get();
				Player.Stop();
				return Cached;
			}();
			std::exit(Preserved ? 0 : 1);
		},
		::testing::ExitedWithCode(0), "");
}
