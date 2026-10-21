#include <engine/shared/demo.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/race_demo_storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using qm_race_demo::CFinalizer;

namespace
{
	class CObservedStorage : public qm_race_demo::CStorageOperations
	{
	public:
		enum class EFailure
		{
			NONE,
			SCAN_FALSE,
			SCAN_PARTIAL_FALSE,
			SCAN_ALLOCATION,
			SCAN_UNKNOWN,
			DESTINATION_ALLOCATION,
			RENAME_FALSE,
			RENAME_ALLOCATION,
			REMOVE_FALSE,
			REMOVE_UNKNOWN,
		};

		EFailure m_Failure = EFailure::NONE;
		std::string m_FailurePath;
		int m_Scans = 0;
		int m_Renames = 0;
		int m_Removes = 0;
		std::function<void()> m_BeforeRename;
		std::function<void(const char *)> m_BeforeRemove;

		explicit CObservedStorage(IStorage &Storage) :
			CStorageOperations(Storage) {}

		template<typename TVisitor>
		bool Enumerate(const char *pDirectory, int Type, const TVisitor &Visitor)
		{
			++m_Scans;
			if(m_Failure == EFailure::SCAN_FALSE)
				return false;
			return CStorageOperations::Enumerate(pDirectory, Type, [&](const char *pName, bool IsDirectory) {
				const bool Continue = Visitor(pName, IsDirectory);
				if(!Continue || pName != m_FailurePath)
					return Continue;
				if(m_Failure == EFailure::SCAN_PARTIAL_FALSE)
					return false;
				if(m_Failure == EFailure::SCAN_ALLOCATION)
					throw std::bad_alloc();
				if(m_Failure == EFailure::SCAN_UNKNOWN)
					throw 7;
				return true;
			});
		}

		bool DestinationAvailable(const char *pPath, int Type)
		{
			if(m_Failure == EFailure::DESTINATION_ALLOCATION)
				throw std::bad_alloc();
			return CStorageOperations::DestinationAvailable(pPath, Type);
		}

		bool Rename(const char *pSource, const char *pDestination, int Type)
		{
			++m_Renames;
			if(m_BeforeRename)
				m_BeforeRename();
			if(m_Failure == EFailure::RENAME_FALSE)
				return false;
			if(m_Failure == EFailure::RENAME_ALLOCATION)
				throw std::bad_alloc();
			return CStorageOperations::Rename(pSource, pDestination, Type);
		}

		bool Remove(const char *pPath, int Type)
		{
			++m_Removes;
			if(m_BeforeRemove)
				m_BeforeRemove(pPath);
			if(pPath == m_FailurePath)
			{
				if(m_Failure == EFailure::REMOVE_FALSE)
					return false;
				if(m_Failure == EFailure::REMOVE_UNKNOWN)
					throw 7;
			}
			return CStorageOperations::Remove(pPath, Type);
		}
	};

	class QmRaceDemoStorageIntegration : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		CDemoRecorder m_Recorder{nullptr, true};
		CFinalizer m_Finalizer;
		std::optional<CObservedStorage> m_Operations;
		int m_StopCalls = 0;
		int m_ProgressCalls = 0;
		bool m_StopCompletes = true;
		bool m_StopThrows = false;
		bool m_NotifyThrows = false;
		std::vector<bool> m_Notifications;
		std::function<void()> m_OnProgress;
		std::function<void(bool)> m_OnNotify;
		static constexpr const char *SOURCE = "race/map_tmp_9.demo";
		static constexpr const char *DESTINATION = "race/map_10.000.demo";
		static constexpr const char *OLD = "race/map_20.000.demo";

		void SetUp() override
		{
			ASSERT_NE(m_pStorage, nullptr);
			m_Operations.emplace(*m_pStorage);
		}

		void Write(const char *pPath, const char *pText)
		{
			ASSERT_TRUE(qmclient::media_paths::PrepareWrite(m_pStorage.get(), pPath));
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, pText, str_length(pText)), static_cast<unsigned>(str_length(pText)));
			EXPECT_EQ(io_close(File), 0);
		}

		std::string Read(const char *pPath)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE_OR_ABSOLUTE);
			EXPECT_NE(File, nullptr);
			if(!File)
				return {};
			const int64_t Length = io_length(File);
			std::string Text(Length > 0 ? static_cast<size_t>(Length) : 0, '\0');
			EXPECT_EQ(io_read(File, Text.data(), Text.size()), Text.size());
			EXPECT_EQ(io_close(File), 0);
			return Text;
		}

		bool Exists(const char *pPath)
		{
			return m_pStorage->FileExists(pPath, IStorage::TYPE_SAVE_OR_ABSOLUTE);
		}

		void Prepare()
		{
			Write(SOURCE, "new10");
			Write(OLD, "old20");
			ASSERT_TRUE(m_Finalizer.Queue(SOURCE, "map", "", false, 10000));
		}

		std::string Absolute(const char *pPath)
		{
			const auto Path = (std::filesystem::absolute(std::filesystem::u8path(m_Info.StoragePath())) / std::filesystem::u8path(pPath)).generic_u8string();
			return std::string(reinterpret_cast<const char *>(Path.data()), Path.size());
		}

		CFinalizer::EOutcome Attempt(std::chrono::nanoseconds Now = 0ns, bool Force = false)
		{
			return m_Finalizer.Try(Now, *m_Operations, [this]() {
					++m_StopCalls;
					if(m_StopThrows)
						throw std::bad_alloc();
					if(!m_StopCompletes)
						return false;
					if(m_Recorder.IsRecording())
						EXPECT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
					return !m_Recorder.IsRecording(); }, [this]() {
					++m_ProgressCalls;
					if(m_OnProgress)
						m_OnProgress(); }, [this](bool AfterCommit) {
					m_Notifications.push_back(AfterCommit);
					if(m_NotifyThrows)
						throw 7;
					if(m_OnNotify)
						m_OnNotify(AfterCommit); }, Force);
		}

		void BeginRealRecording()
		{
			ASSERT_TRUE(qmclient::media_paths::PrepareWrite(m_pStorage.get(), SOURCE));
			unsigned char MapData = 0;
			ASSERT_EQ(m_Recorder.Start(m_pStorage.get(), nullptr, SOURCE, "test", "map", SHA256_DIGEST{}, 0, "client", 0, &MapData, nullptr, nullptr, nullptr), 0);
			ASSERT_TRUE(m_Recorder.IsRecording());
			ASSERT_TRUE(m_Finalizer.Queue(SOURCE, "map", "", false, 10000));
		}
	};
}

TEST_F(QmRaceDemoStorageIntegration, NewBestIsPublishedBeforeAnyOldRecordIsRemoved)
{
	Prepare();
	Write("race/map_30.000.demo", "old30");
	m_Operations->m_BeforeRename = [&]() {
		EXPECT_TRUE(Exists(SOURCE));
		EXPECT_TRUE(Exists(OLD));
		EXPECT_TRUE(Exists("race/map_30.000.demo"));
	};
	m_Operations->m_BeforeRemove = [&](const char *) {
		EXPECT_TRUE(Exists(DESTINATION));
		EXPECT_FALSE(Exists(SOURCE));
	};
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read(DESTINATION), "new10");
	EXPECT_FALSE(Exists(OLD));
	EXPECT_FALSE(Exists("race/map_30.000.demo"));
	EXPECT_TRUE(m_Finalizer.CanStartCapture());
}

TEST_F(QmRaceDemoStorageIntegration, BetterExistingRecordKeepsItsContentsAndDiscardsCapture)
{
	Write(SOURCE, "new10");
	Write("race/map_5.000.demo", "best5");
	ASSERT_TRUE(m_Finalizer.Queue(SOURCE, "map", "", false, 10000));
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::DISCARDED);
	EXPECT_EQ(Read("race/map_5.000.demo"), "best5");
	EXPECT_FALSE(Exists(SOURCE));
	EXPECT_FALSE(Exists(DESTINATION));
	EXPECT_EQ(m_Operations->m_Renames, 0);
}

TEST_F(QmRaceDemoStorageIntegration, UnfinishedCaptureIsRemovedWithoutEnumeratingRecords)
{
	Write(SOURCE, "unfinished");
	Write(OLD, "keep");
	ASSERT_TRUE(m_Finalizer.Queue(SOURCE, "", "", false, 0));
	m_Operations->m_Failure = CObservedStorage::EFailure::SCAN_FALSE;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::DISCARDED);
	EXPECT_EQ(m_Operations->m_Scans, 0);
	EXPECT_FALSE(Exists(SOURCE));
	EXPECT_EQ(Read(OLD), "keep");
}

TEST_F(QmRaceDemoStorageIntegration, EmptyRecordSetAcceptsNewCapture)
{
	Write(SOURCE, "first");
	ASSERT_TRUE(m_Finalizer.Queue(SOURCE, "map", "", false, 10000));
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read(DESTINATION), "first");
}

TEST_F(QmRaceDemoStorageIntegration, NamedCaptureKeepsOtherMapsPlayersAndDirectories)
{
	Write(SOURCE, "newAlpha");
	Write("race/map_20.000_Alpha.demo", "oldAlpha");
	Write("race/map_1.000_Beta.demo", "beta");
	Write("race/map_1.000.demo", "unnamed");
	Write("race/other_1.000_Alpha.demo", "other");
	ASSERT_TRUE(m_pStorage->CreateFolder("race/map_5.000_Alpha.demo", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_Finalizer.Queue(SOURCE, "map", "Alpha", true, 10000));
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read("race/map_10.000_Alpha.demo"), "newAlpha");
	EXPECT_FALSE(Exists("race/map_20.000_Alpha.demo"));
	EXPECT_EQ(Read("race/map_1.000_Beta.demo"), "beta");
	EXPECT_EQ(Read("race/map_1.000.demo"), "unnamed");
	EXPECT_EQ(Read("race/other_1.000_Alpha.demo"), "other");
	EXPECT_TRUE(m_pStorage->FolderExists("race/map_5.000_Alpha.demo", IStorage::TYPE_SAVE));
}

TEST_F(QmRaceDemoStorageIntegration, UnnamedCaptureDoesNotCompeteWithNamedRecords)
{
	Prepare();
	Write("race/map_1.000_Alpha.demo", "named");
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read("race/map_1.000_Alpha.demo"), "named");
	EXPECT_EQ(Read(DESTINATION), "new10");
}

TEST_F(QmRaceDemoStorageIntegration, PendingRequestOwnsNamingAndCannotBeReplaced)
{
	Write(SOURCE, "original");
	char aSource[64];
	str_copy(aSource, SOURCE);
	char aMap[16] = "map";
	char aPlayer[16] = "Alpha";
	ASSERT_TRUE(m_Finalizer.Queue(aSource, aMap, aPlayer, true, 10000));
	str_copy(aSource, "other/tmp.demo");
	str_copy(aMap, "other");
	str_copy(aPlayer, "Beta");
	EXPECT_FALSE(m_Finalizer.Queue(aSource, aMap, aPlayer, false, 5000));
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read("race/map_10.000_Alpha.demo"), "original");
	EXPECT_FALSE(Exists("other/other_5.000.demo"));
}

TEST_F(QmRaceDemoStorageIntegration, AbsoluteUtf8DirectoryKeepsOriginalStorageScope)
{
	const std::string Source = Absolute("自定义回放/map_tmp_9.demo");
	const std::string Destination = Absolute("自定义回放/map_10.000.demo");
	Write(Source.c_str(), "absolute");
	ASSERT_TRUE(m_Finalizer.Queue(Source.c_str(), "map", "", false, 10000));
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read(Destination.c_str()), "absolute");
	EXPECT_FALSE(Exists("race/map_10.000.demo"));
}

TEST_F(QmRaceDemoStorageIntegration, NativeEnumerationDistinguishesMissingAndEmptyDirectories)
{
	int Entries = 0;
	const auto Visit = [&](const char *, bool) { ++Entries; return true; };
	EXPECT_FALSE(m_Operations->Enumerate("missing", IStorage::TYPE_SAVE, Visit));
	ASSERT_TRUE(m_pStorage->CreateFolder("empty", IStorage::TYPE_SAVE));
	EXPECT_TRUE(m_Operations->Enumerate("empty", IStorage::TYPE_SAVE, Visit));
	EXPECT_EQ(Entries, 0);
}

TEST_F(QmRaceDemoStorageIntegration, NativeEnumerationRejectsAFileAsDirectory)
{
	Write("blocked", "file");
	EXPECT_FALSE(m_Operations->Enumerate("blocked", IStorage::TYPE_SAVE, [](const char *, bool) { return true; }));
	EXPECT_EQ(Read("blocked"), "file");
}

TEST_F(QmRaceDemoStorageIntegration, FailedScanDoesNotRenameOrDeleteAnything)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::SCAN_FALSE;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(Read(SOURCE), "new10");
	EXPECT_EQ(Read(OLD), "old20");
	EXPECT_EQ(m_Operations->m_Renames, 0);
	EXPECT_EQ(m_Operations->m_Removes, 0);
	EXPECT_FALSE(m_Finalizer.CanStartCapture());
}

TEST_F(QmRaceDemoStorageIntegration, PartialScanFailureDiscardsPreparedCleanupAndCanRetry)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::SCAN_PARTIAL_FALSE;
	m_Operations->m_FailurePath = "map_20.000.demo";
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_GT(m_ProgressCalls, 0);
	EXPECT_TRUE(Exists(SOURCE));
	EXPECT_TRUE(Exists(OLD));
	m_Operations->m_Failure = CObservedStorage::EFailure::NONE;
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read(DESTINATION), "new10");
}

TEST_F(QmRaceDemoStorageIntegration, AllocationFailureDuringScanPreservesFilesAndReleasesIterator)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::SCAN_ALLOCATION;
	m_Operations->m_FailurePath = "map_20.000.demo";
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(Read(SOURCE), "new10");
	EXPECT_EQ(Read(OLD), "old20");
	m_Operations->m_Failure = CObservedStorage::EFailure::NONE;
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::SAVED);
	EXPECT_FALSE(Exists(OLD));
}

TEST_F(QmRaceDemoStorageIntegration, UnknownScanExceptionDoesNotPublishAPartialObservation)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::SCAN_UNKNOWN;
	m_Operations->m_FailurePath = "map_20.000.demo";
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_TRUE(m_Finalizer.Pending());
	EXPECT_TRUE(Exists(SOURCE));
	EXPECT_TRUE(Exists(OLD));
	EXPECT_EQ(m_Operations->m_Removes, 0);
}

TEST_F(QmRaceDemoStorageIntegration, LoadingProgressFailureLeavesAllPreparedFilesUntouched)
{
	Prepare();
	m_OnProgress = [] { throw std::length_error("loading progress"); };
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_TRUE(Exists(SOURCE));
	EXPECT_TRUE(Exists(OLD));
	m_OnProgress = {};
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::SAVED);
}

TEST_F(QmRaceDemoStorageIntegration, DestinationPreparationFailureKeepsCaptureAndPreviousBest)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::DESTINATION_ALLOCATION;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(Read(SOURCE), "new10");
	EXPECT_EQ(Read(OLD), "old20");
	EXPECT_EQ(m_Operations->m_Renames, 0);
}

TEST_F(QmRaceDemoStorageIntegration, ExistingDestinationDirectoryIsNotReplaced)
{
	Prepare();
	ASSERT_TRUE(m_pStorage->CreateFolder(DESTINATION, IStorage::TYPE_SAVE));
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(m_Operations->m_Renames, 0);
	EXPECT_TRUE(m_pStorage->FolderExists(DESTINATION, IStorage::TYPE_SAVE));
	EXPECT_EQ(Read(SOURCE), "new10");
	EXPECT_EQ(Read(OLD), "old20");
}

TEST_F(QmRaceDemoStorageIntegration, RenameFailureKeepsOldBestAndRetriesTheSameCapture)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::RENAME_FALSE;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(m_Operations->m_Removes, 0);
	EXPECT_EQ(Read(SOURCE), "new10");
	EXPECT_EQ(Read(OLD), "old20");
	m_Operations->m_Failure = CObservedStorage::EFailure::NONE;
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read(DESTINATION), "new10");
	EXPECT_FALSE(Exists(SOURCE));
	EXPECT_FALSE(Exists(OLD));
}

TEST_F(QmRaceDemoStorageIntegration, RenameExceptionCannotTriggerCleanup)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::RENAME_ALLOCATION;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(m_Operations->m_Removes, 0);
	EXPECT_TRUE(Exists(SOURCE));
	EXPECT_TRUE(Exists(OLD));
}

TEST_F(QmRaceDemoStorageIntegration, StopMustCompleteBeforeAnyDirectoryOrFileOperation)
{
	Prepare();
	m_StopCompletes = false;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(m_Operations->m_Scans, 0);
	EXPECT_EQ(m_Operations->m_Removes, 0);
	EXPECT_EQ(m_Operations->m_Renames, 0);
	EXPECT_TRUE(Exists(SOURCE));
}

TEST_F(QmRaceDemoStorageIntegration, StopExceptionKeepsTheRealRecorderAndCaptureForRetry)
{
	BeginRealRecording();
	Write(OLD, "old20");
	m_StopThrows = true;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_TRUE(m_Recorder.IsRecording());
	EXPECT_TRUE(Exists(OLD));
	m_StopThrows = false;
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::SAVED);
	EXPECT_FALSE(m_Recorder.IsRecording());
	EXPECT_TRUE(Exists(DESTINATION));
	EXPECT_FALSE(Exists(OLD));
}

TEST_F(QmRaceDemoStorageIntegration, RealRecorderIsStoppedBeforeItsCompletedFileIsPublished)
{
	BeginRealRecording();
	m_Operations->m_BeforeRename = [&] { EXPECT_FALSE(m_Recorder.IsRecording()); };
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_FALSE(m_Recorder.IsRecording());
	EXPECT_FALSE(Read(DESTINATION).empty());
	EXPECT_FALSE(Exists(SOURCE));
}

TEST_F(QmRaceDemoStorageIntegration, RetryKeepsOneSecondBoundaryAndReportsFailureOnlyOnce)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::RENAME_FALSE;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(Attempt(999ms), CFinalizer::EOutcome::DEFERRED);
	EXPECT_EQ(m_StopCalls, 1);
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::RETRY);
	EXPECT_EQ(m_StopCalls, 2);
	EXPECT_EQ(m_Notifications, std::vector<bool>{false});
	m_Operations->m_Failure = CObservedStorage::EFailure::NONE;
	EXPECT_EQ(Attempt(2s), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Attempt(3s), CFinalizer::EOutcome::IDLE);
	EXPECT_EQ(m_Operations->m_Renames, 3);
}

TEST_F(QmRaceDemoStorageIntegration, ShutdownCanForceOneAttemptBeforeRetryDeadline)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::RENAME_FALSE;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	m_Operations->m_Failure = CObservedStorage::EFailure::NONE;
	EXPECT_EQ(Attempt(1ns, true), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(m_StopCalls, 2);
	EXPECT_TRUE(m_Finalizer.CanStartCapture());
}

TEST_F(QmRaceDemoStorageIntegration, FailedDiscardRetainsTemporaryFileAndExistingBetterRecord)
{
	Write(SOURCE, "new10");
	Write("race/map_5.000.demo", "best5");
	ASSERT_TRUE(m_Finalizer.Queue(SOURCE, "map", "", false, 10000));
	m_Operations->m_Failure = CObservedStorage::EFailure::REMOVE_FALSE;
	m_Operations->m_FailurePath = SOURCE;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_TRUE(m_Finalizer.Pending());
	EXPECT_EQ(Read(SOURCE), "new10");
	EXPECT_EQ(Read("race/map_5.000.demo"), "best5");
	m_Operations->m_Failure = CObservedStorage::EFailure::NONE;
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::DISCARDED);
	EXPECT_FALSE(Exists(SOURCE));
}

TEST_F(QmRaceDemoStorageIntegration, CleanupFailureDoesNotReplaySaveAndStillRemovesOtherOldRecords)
{
	Prepare();
	Write("race/map_30.000.demo", "old30");
	m_Operations->m_Failure = CObservedStorage::EFailure::REMOVE_FALSE;
	m_Operations->m_FailurePath = OLD;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED_CLEANUP_FAILED);
	EXPECT_EQ(Read(DESTINATION), "new10");
	EXPECT_EQ(Read(OLD), "old20");
	EXPECT_FALSE(Exists("race/map_30.000.demo"));
	EXPECT_EQ(m_Notifications, std::vector<bool>{true});
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::IDLE);
	EXPECT_EQ(m_Operations->m_Renames, 1);
	EXPECT_TRUE(m_Finalizer.CanStartCapture());
}

TEST_F(QmRaceDemoStorageIntegration, CleanupExceptionKeepsPublishedCaptureAndEndsPendingState)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::REMOVE_UNKNOWN;
	m_Operations->m_FailurePath = OLD;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED_CLEANUP_FAILED);
	EXPECT_EQ(Read(DESTINATION), "new10");
	EXPECT_TRUE(Exists(OLD));
	EXPECT_FALSE(m_Finalizer.Pending());
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::IDLE);
}

TEST_F(QmRaceDemoStorageIntegration, FailureReportExceptionCannotLoseThePendingCapture)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::RENAME_FALSE;
	m_NotifyThrows = true;
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::RETRY);
	EXPECT_TRUE(m_Finalizer.Pending());
	EXPECT_EQ(Read(SOURCE), "new10");
	EXPECT_EQ(Read(OLD), "old20");
	m_Operations->m_Failure = CObservedStorage::EFailure::NONE;
	m_NotifyThrows = false;
	EXPECT_EQ(Attempt(1s), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(m_Notifications.size(), 1U);
}

TEST_F(QmRaceDemoStorageIntegration, ProgressReentryCannotPublishOrReplaceTheActiveRequest)
{
	Prepare();
	m_OnProgress = [&] {
		EXPECT_FALSE(m_Finalizer.CanStartCapture());
		EXPECT_FALSE(m_Finalizer.Queue("other/tmp.demo", "other", "", false, 5000));
		EXPECT_EQ(Attempt(1s, true), CFinalizer::EOutcome::DEFERRED);
	};
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED);
	EXPECT_EQ(Read(DESTINATION), "new10");
	EXPECT_EQ(m_StopCalls, 1);
}

TEST_F(QmRaceDemoStorageIntegration, CleanupReportCannotStartAnotherCaptureBeforeTheAttemptReturns)
{
	Prepare();
	m_Operations->m_Failure = CObservedStorage::EFailure::REMOVE_FALSE;
	m_Operations->m_FailurePath = OLD;
	m_OnNotify = [&](bool AfterCommit) {
		EXPECT_TRUE(AfterCommit);
		EXPECT_FALSE(m_Finalizer.Pending());
		EXPECT_FALSE(m_Finalizer.CanStartCapture());
		EXPECT_FALSE(m_Finalizer.Queue("other/tmp.demo", "other", "", false, 5000));
	};
	EXPECT_EQ(Attempt(), CFinalizer::EOutcome::SAVED_CLEANUP_FAILED);
	EXPECT_TRUE(m_Finalizer.CanStartCapture());
}
