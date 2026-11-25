#include <base/fs_path.h>

#include <engine/shared/demo_output.h>

#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <new>

namespace
{
	class CExtractFileOperations : public CDemoRecorder::CFileOperations
	{
	public:
		enum class EFailure
		{
			NONE,
			SHORT_WRITE,
			FLUSH,
			ERROR,
			CLOSE,
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Writes = 0;
		int m_Flushes = 0;
		int m_Closes = 0;
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept override
		{
			++m_Writes;
			return CFileOperations::Write(File, pData, m_Failure == EFailure::SHORT_WRITE && Size > 0 ? Size - 1 : Size);
		}
		int Flush(IOHANDLE File) noexcept override
		{
			++m_Flushes;
			const int Result = CFileOperations::Flush(File);
			return m_Failure == EFailure::FLUSH ? -1 : Result;
		}
		int Error(IOHANDLE File) noexcept override
		{
			return m_Failure == EFailure::ERROR ? 1 : CFileOperations::Error(File);
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CFileOperations::Close(File);
			return m_Failure == EFailure::CLOSE ? -1 : Result;
		}
	};

	class CExtractOutputOperations : public demo_output::COperations
	{
	public:
		bool m_PrepareFails = false;
		bool m_PrepareThrows = false;
		bool m_BlockFileOpen = false;
		bool m_PublishFails = false;
		bool m_PublishThrows = false;
		bool m_CleanupFails = false;
		bool m_CleanupThrows = false;
		int m_Creates = 0;
		int m_Publishes = 0;
		std::string m_Directory;
		std::function<void(const char *)> m_BeforePublish;
		ECreate CreateDirectory(const char *pPath) override
		{
			++m_Creates;
			if(m_PrepareThrows)
				throw std::bad_alloc();
			if(m_PrepareFails)
				return ECreate::FAILED;
			m_Directory = pPath;
			const auto Result = COperations::CreateDirectory(pPath);
			if(Result == ECreate::CREATED && m_BlockFileOpen)
			{
				// 在真实暂存目录放置同名空目录，让实际 OpenFile 返回失败。
				std::filesystem::create_directory(fs_path_from_utf8(pPath) / "output.demo");
			}
			return Result;
		}
		bool Publish(const char *pTemporary, const char *pDestination) override
		{
			++m_Publishes;
			if(m_BeforePublish)
				m_BeforePublish(pTemporary);
			if(m_PublishThrows)
				throw std::bad_alloc();
			return !m_PublishFails && COperations::Publish(pTemporary, pDestination);
		}
		bool Remove(const char *pPath) override
		{
			if(m_CleanupThrows)
				throw 7;
			return !m_CleanupFails && COperations::Remove(pPath);
		}
	};

	class CExtractReadOperations : public CDemoPlayer::CMapDataOperations
	{
	public:
		IOHANDLE m_File = nullptr;
		int64_t m_Position = -1;
		bool m_ChangeReadData = false;
		unsigned Read(IOHANDLE File, void *pData, unsigned Size) noexcept override
		{
			const unsigned Bytes = CMapDataOperations::Read(File, pData, Size);
			if(m_ChangeReadData && Bytes)
				static_cast<unsigned char *>(pData)[0] ^= 1;
			return Bytes;
		}
		int64_t Tell(IOHANDLE File) noexcept override
		{
			m_File = File;
			m_Position = CMapDataOperations::Tell(File);
			return m_Position;
		}
	};

	class DemoMapExtractIntegration : public ::testing::Test
	{
	protected:
		CDemoStorageState m_State;
		CExtractFileOperations m_FileOperations;
		CExtractOutputOperations m_OutputOperations;
		CExtractReadOperations m_ReadOperations;
		CDemoPlayer m_Player{&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false, &m_ReadOperations};
		void SetUp() override
		{
			ASSERT_NE(m_State.m_pStorage, nullptr);
			CNetBase::Init();
			ASSERT_TRUE(m_State.m_pStorage->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE));
		}
		void TearDown() override { m_Player.Stop(); }
		void Load()
		{
			ASSERT_NO_FATAL_FAILURE(m_State.Begin("source.demo", true, ContentSha()));
			ASSERT_EQ(m_State.m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
			ASSERT_EQ(m_Player.Load(m_State.m_pStorage.get(), "source.demo", IStorage::TYPE_SAVE, false), 0);
		}
		void LoadWithoutExtension(unsigned char Version)
		{
			m_State.Create();
			std::string Bytes = m_State.Read("source.demo");
			const size_t ExtensionOffset = sizeof(CDemoHeader) + sizeof(CTimelineMarkers);
			const size_t ExtensionSize = sizeof(CUuid) + sizeof(SHA256_DIGEST);
			ASSERT_GT(Bytes.size(), ExtensionOffset + ExtensionSize);
			Bytes[offsetof(CDemoHeader, m_Version)] = Version;
			Bytes.erase(ExtensionOffset, ExtensionSize);
			m_State.Write("source.demo", Bytes);
			ASSERT_EQ(m_Player.Load(m_State.m_pStorage.get(), "source.demo", IStorage::TYPE_SAVE, false), 0);
			ASSERT_FALSE(m_Player.GetMapInfo()->m_Sha256.has_value());
		}
		SHA256_DIGEST ContentSha() { return sha256("mapbytes", 8); }
		std::string Destination() { return Destination(ContentSha()); }
		std::string Destination(const SHA256_DIGEST &Sha)
		{
			char aSha[SHA256_MAXSTRSIZE];
			sha256_str(Sha, aSha, sizeof(aSha));
			return std::string("downloadedmaps/map_") + aSha + ".map";
		}
		void KeepOld() { m_State.Write(Destination().c_str(), "old map"); }
		bool Extract() { return m_Player.ExtractMap(m_State.m_pStorage.get(), &m_OutputOperations, &m_FileOperations); }
		bool TemporaryExists()
		{
			return !m_OutputOperations.m_Directory.empty() && std::filesystem::exists(fs_path_from_utf8(m_OutputOperations.m_Directory));
		}
		void ExpectUnchangedPlayback()
		{
			ASSERT_TRUE(m_Player.IsPlaying());
			ASSERT_NE(m_ReadOperations.m_File, nullptr);
			EXPECT_EQ(io_tell(m_ReadOperations.m_File), m_ReadOperations.m_Position);
			EXPECT_TRUE(m_State.Exists("source.demo"));
		}
		void ExpectFailedFileOutput()
		{
			EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
			EXPECT_EQ(m_FileOperations.m_Closes, 1);
			EXPECT_EQ(m_OutputOperations.m_Publishes, 0);
			EXPECT_FALSE(TemporaryExists());
			ExpectUnchangedPlayback();
		}
	};
}

TEST_F(DemoMapExtractIntegration, DefaultOperationsPublishEmbeddedBytesAtTheExistingCacheName)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	ASSERT_TRUE(m_Player.ExtractMap(m_State.m_pStorage.get()));
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, OldCacheRemainsIntactUntilClosedCandidateIsPublished)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_OutputOperations.m_BeforePublish = [&](const char *pTemporary) {
		EXPECT_EQ(m_FileOperations.m_Closes, 1);
		EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
		EXPECT_EQ(m_State.Read(pTemporary), "mapbytes");
	};
	ASSERT_TRUE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
	EXPECT_FALSE(TemporaryExists());
}

TEST_F(DemoMapExtractIntegration, ChangedEmbeddedBytesKeepTheExistingCacheAndAllowRetry)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_ReadOperations.m_ChangeReadData = true;
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_OutputOperations.m_Creates, 0);
	EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
	ExpectUnchangedPlayback();
	m_ReadOperations.m_ChangeReadData = false;
	EXPECT_TRUE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
}

TEST_F(DemoMapExtractIntegration, ShortWriteClosesCandidateAndKeepsOldCache)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::SHORT_WRITE;
	EXPECT_FALSE(Extract());
	ExpectFailedFileOutput();
	EXPECT_EQ(m_FileOperations.m_Flushes, 1);
}

TEST_F(DemoMapExtractIntegration, FlushFailurePreventsCachePublication)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::FLUSH;
	EXPECT_FALSE(Extract());
	ExpectFailedFileOutput();
}

TEST_F(DemoMapExtractIntegration, ReportedStreamErrorPreventsCachePublication)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::ERROR;
	EXPECT_FALSE(Extract());
	ExpectFailedFileOutput();
}

TEST_F(DemoMapExtractIntegration, CloseFailureIsNotReportedAsSuccessfulExtraction)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::CLOSE;
	EXPECT_FALSE(Extract());
	ExpectFailedFileOutput();
}

TEST_F(DemoMapExtractIntegration, PreparationFailureDoesNotOpenOrReplaceTheCache)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_OutputOperations.m_PrepareFails = true;
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_FileOperations.m_Writes, 0);
	EXPECT_EQ(m_FileOperations.m_Closes, 0);
	EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, PreparationExceptionIsContainedWithPlaybackPreserved)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_OutputOperations.m_PrepareThrows = true;
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
	EXPECT_EQ(m_FileOperations.m_Closes, 0);
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, NativeOpenFailureLeavesTheOldCacheAndRemovesEmptyTemporaryDirectories)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_OutputOperations.m_BlockFileOpen = true;
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
	EXPECT_EQ(m_FileOperations.m_Writes, 0);
	EXPECT_EQ(m_FileOperations.m_Closes, 0);
	EXPECT_FALSE(TemporaryExists());
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, PublishFailureKeepsTheOldCacheAfterClosingTheCandidate)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_OutputOperations.m_PublishFails = true;
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
	EXPECT_EQ(m_FileOperations.m_Closes, 1);
	EXPECT_FALSE(TemporaryExists());
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, PublishExceptionRollsBackTheCandidateAndCanBeRetried)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	KeepOld();
	m_OutputOperations.m_PublishThrows = true;
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "old map");
	EXPECT_FALSE(TemporaryExists());
	m_OutputOperations.m_PublishThrows = false;
	EXPECT_TRUE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
}

TEST_F(DemoMapExtractIntegration, CleanupFailureDoesNotUndoPublishedCache)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	m_OutputOperations.m_CleanupFails = true;
	EXPECT_TRUE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
	EXPECT_TRUE(TemporaryExists());
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, CleanupExceptionDoesNotEscapeSuccessfulExtraction)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	m_OutputOperations.m_CleanupThrows = true;
	EXPECT_TRUE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
}

TEST_F(DemoMapExtractIntegration, FailedNewCacheRemainsAbsentAndCanBeRetried)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::SHORT_WRITE;
	EXPECT_FALSE(Extract());
	EXPECT_FALSE(m_State.Exists(Destination().c_str()));
	EXPECT_FALSE(TemporaryExists());
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::NONE;
	EXPECT_TRUE(Extract());
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
}

TEST_F(DemoMapExtractIntegration, RepeatedExtractionKeepsTheSourceOpenAndPublishesWholeBytes)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	EXPECT_TRUE(Extract());
	EXPECT_TRUE(Extract());
	EXPECT_EQ(m_FileOperations.m_Closes, 2);
	EXPECT_EQ(m_State.Read(Destination().c_str()), "mapbytes");
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, LegacyDigestIsPublishedOnlyAfterTheCacheReplacement)
{
	ASSERT_NO_FATAL_FAILURE(LoadWithoutExtension(5));
	const auto Sha = ContentSha();
	m_State.Write(Destination(Sha).c_str(), "old map");
	m_OutputOperations.m_BeforePublish = [&](const char *) {
		EXPECT_FALSE(m_Player.GetMapInfo()->m_Sha256.has_value());
		EXPECT_EQ(m_State.Read(Destination(Sha).c_str()), "old map");
	};
	ASSERT_TRUE(Extract());
	EXPECT_EQ(m_Player.GetMapInfo()->m_Sha256, Sha);
	EXPECT_EQ(m_State.Read(Destination(Sha).c_str()), "mapbytes");
}

TEST_F(DemoMapExtractIntegration, LegacyWriteFailureDoesNotPublishDigestAndCanBeRetried)
{
	ASSERT_NO_FATAL_FAILURE(LoadWithoutExtension(5));
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::FLUSH;
	EXPECT_FALSE(Extract());
	EXPECT_FALSE(m_Player.GetMapInfo()->m_Sha256.has_value());
	EXPECT_FALSE(m_State.Exists(Destination(ContentSha()).c_str()));
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::NONE;
	EXPECT_TRUE(Extract());
	EXPECT_EQ(m_Player.GetMapInfo()->m_Sha256, ContentSha());
}

TEST_F(DemoMapExtractIntegration, AcceptedVersionSixWithoutExtensionExtractsUsingTheContentDigest)
{
	ASSERT_NO_FATAL_FAILURE(LoadWithoutExtension(6));
	ASSERT_TRUE(Extract());
	EXPECT_EQ(m_Player.GetMapInfo()->m_Sha256, ContentSha());
	EXPECT_EQ(m_State.Read(Destination(ContentSha()).c_str()), "mapbytes");
}

TEST_F(DemoMapExtractIntegration, VersionSixWithoutExtensionStillRejectsIncompleteOutput)
{
	ASSERT_NO_FATAL_FAILURE(LoadWithoutExtension(6));
	m_FileOperations.m_Failure = CExtractFileOperations::EFailure::CLOSE;
	EXPECT_FALSE(Extract());
	EXPECT_FALSE(m_Player.GetMapInfo()->m_Sha256.has_value());
	EXPECT_FALSE(m_State.Exists(Destination(ContentSha()).c_str()));
	ExpectUnchangedPlayback();
}

TEST_F(DemoMapExtractIntegration, AcceptedVersionSixWithoutExtensionCanBeSliced)
{
	ASSERT_NO_FATAL_FAILURE(LoadWithoutExtension(6));
	CDemoEditor Editor;
	Editor.Init(&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, m_State.m_pStorage.get());
	ASSERT_TRUE(Editor.Slice("source.demo", "target.demo", -1, -1, nullptr, nullptr));
	EXPECT_EQ(m_State.MapData("target.demo"), "mapbytes");
	EXPECT_EQ(m_State.Read(Destination(ContentSha()).c_str()), "mapbytes");
}

TEST_F(DemoMapExtractIntegration, BeforeLoadDoesNotCreateOrWriteAnyOutput)
{
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_OutputOperations.m_Creates, 0);
	EXPECT_EQ(m_FileOperations.m_Writes, 0);
}

TEST_F(DemoMapExtractIntegration, StoppedPlaybackDoesNotCreateOrWriteAnyOutput)
{
	ASSERT_NO_FATAL_FAILURE(Load());
	m_Player.Stop();
	EXPECT_FALSE(Extract());
	EXPECT_EQ(m_OutputOperations.m_Creates, 0);
	EXPECT_EQ(m_FileOperations.m_Writes, 0);
}
