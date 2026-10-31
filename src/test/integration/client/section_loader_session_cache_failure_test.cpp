#include <engine/storage.h>

#include <game/client/components/section_loader.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <limits>
#include <new>
#include <string>

namespace
{
	enum class EIoFault
	{
		OPEN,
		OPEN_EXCEPTION,
		READ,
		READ_EXCEPTION,
		WRITE,
		WRITE_EXCEPTION,
		SYNC,
		ERROR,
		CLOSE,
		PUBLISH,
		PUBLISH_EXCEPTION,
	};

	class CFailingSessionIo : public CSectionLoader::CSessionCacheFileOperations
	{
		EIoFault m_Fault;

	public:
		int m_Opened = 0;
		int m_Closed = 0;
		int m_PublishCalls = 0;
		bool m_PublishWithOpenFile = false;
		std::string m_OpenedPath;
		explicit CFailingSessionIo(EIoFault Fault) :
			m_Fault(Fault) {}
		IOHANDLE Open(IStorage &Storage, const char *pFilename, int Flags) override
		{
			m_OpenedPath = pFilename;
			if(m_Fault == EIoFault::OPEN)
				return nullptr;
			if(m_Fault == EIoFault::OPEN_EXCEPTION)
				throw std::bad_alloc();
			IOHANDLE File = CSessionCacheFileOperations::Open(Storage, pFilename, Flags);
			m_Opened += File != nullptr;
			return File;
		}
		unsigned Read(IOHANDLE File, void *pData, unsigned Size) override
		{
			if(m_Fault == EIoFault::READ)
				return 0;
			if(m_Fault == EIoFault::READ_EXCEPTION)
				throw std::bad_alloc();
			return CSessionCacheFileOperations::Read(File, pData, Size);
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
		{
			if(m_Fault == EIoFault::WRITE_EXCEPTION)
				throw std::bad_alloc();
			return CSessionCacheFileOperations::Write(File, pData, m_Fault == EIoFault::WRITE ? Size - 1 : Size);
		}
		int Sync(IOHANDLE File) override { return m_Fault == EIoFault::SYNC ? -1 : CSessionCacheFileOperations::Sync(File); }
		int Error(IOHANDLE File) override { return m_Fault == EIoFault::ERROR ? 1 : CSessionCacheFileOperations::Error(File); }
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closed;
			const int Result = CSessionCacheFileOperations::Close(File);
			return m_Fault == EIoFault::CLOSE ? -1 : Result;
		}
		bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget) override
		{
			++m_PublishCalls;
			m_PublishWithOpenFile = m_Opened != m_Closed;
			if(m_Fault == EIoFault::PUBLISH_EXCEPTION)
				throw std::bad_alloc();
			return m_Fault != EIoFault::PUBLISH && CSessionCacheFileOperations::Publish(Storage, pTemporary, pTarget);
		}
	};
}

class SectionLoaderSessionCacheFailure : public ::testing::Test
{
	CTestInfo m_Info;

protected:
	std::unique_ptr<IStorage> m_pStorage;
	SSessionUiCache m_Saved;
	static constexpr const char *PATH = "qmclient/session.cfg";
	void SetUp() override
	{
		m_pStorage = m_Info.CreateTestStorage();
		ASSERT_NE(m_pStorage, nullptr);
		m_Saved.m_LastSettingsPage = 8;
		m_Saved.m_LastTClientTab = 2;
		m_Saved.m_LastQmTab = 3;
		m_Saved.m_LastScrollY = -240;
		m_Saved.m_RuntimeKey = {1920, 1080, 125, 111, 222, 333, 444, 555};
		m_Saved.m_Valid = true;
		ASSERT_TRUE(CSectionLoader::SaveSessionCache(m_Saved, PATH, m_pStorage.get()));
	}
	static void ExpectEqual(const SSessionUiCache &Actual, const SSessionUiCache &Expected)
	{
		EXPECT_EQ(Actual.m_LastSettingsPage, Expected.m_LastSettingsPage);
		EXPECT_EQ(Actual.m_LastTClientTab, Expected.m_LastTClientTab);
		EXPECT_EQ(Actual.m_LastQmTab, Expected.m_LastQmTab);
		EXPECT_FLOAT_EQ(Actual.m_LastScrollY, Expected.m_LastScrollY);
		EXPECT_TRUE(Actual.m_RuntimeKey == Expected.m_RuntimeKey);
		EXPECT_EQ(Actual.m_Valid, Expected.m_Valid);
	}
	void ExpectStored(const SSessionUiCache &Expected)
	{
		SSessionUiCache Actual;
		ASSERT_TRUE(CSectionLoader::LoadSessionCache(Actual, PATH, m_pStorage.get()));
		ExpectEqual(Actual, Expected);
	}
	void WriteContents(const std::string &Text, const char *pPath = PATH)
	{
		IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		const auto Written = io_write(File, Text.data(), Text.size());
		const int Closed = io_close(File);
		ASSERT_EQ(Written, Text.size());
		ASSERT_EQ(Closed, 0);
	}
	SSessionUiCache Sentinel() const
	{
		auto Cache = m_Saved;
		Cache.m_LastSettingsPage = 42;
		Cache.m_LastScrollY = -999;
		Cache.m_RuntimeKey.m_WindowHash = 999;
		return Cache;
	}
	void ExpectSaveFailure(EIoFault Fault)
	{
		auto Next = m_Saved;
		Next.m_LastScrollY = -480;
		Next.m_RuntimeKey.m_WindowHash = 777;
		CFailingSessionIo Operations(Fault);
		EXPECT_FALSE(CSectionLoader::SaveSessionCache(Next, PATH, m_pStorage.get(), &Operations));
		EXPECT_EQ(Operations.m_Opened, Fault == EIoFault::OPEN || Fault == EIoFault::OPEN_EXCEPTION ? 0 : 1);
		EXPECT_EQ(Operations.m_Opened, Operations.m_Closed);
		EXPECT_FALSE(Operations.m_PublishWithOpenFile);
		EXPECT_EQ(Operations.m_PublishCalls, Fault == EIoFault::PUBLISH || Fault == EIoFault::PUBLISH_EXCEPTION ? 1 : 0);
		EXPECT_FALSE(m_pStorage->FileExists(Operations.m_OpenedPath.c_str(), IStorage::TYPE_SAVE));
		ExpectStored(m_Saved);
		ASSERT_TRUE(CSectionLoader::SaveSessionCache(Next, PATH, m_pStorage.get()));
		ExpectStored(Next);
	}
	void ExpectLoadFailure(EIoFault Fault)
	{
		auto Cache = Sentinel();
		CFailingSessionIo Operations(Fault);
		EXPECT_FALSE(CSectionLoader::LoadSessionCache(Cache, PATH, m_pStorage.get(), &Operations));
		ExpectEqual(Cache, Sentinel());
		EXPECT_EQ(Operations.m_Opened, Fault == EIoFault::OPEN || Fault == EIoFault::OPEN_EXCEPTION ? 0 : 1);
		EXPECT_EQ(Operations.m_Opened, Operations.m_Closed);
		EXPECT_EQ(Operations.m_PublishCalls, 0);
		ASSERT_TRUE(CSectionLoader::LoadSessionCache(Cache, PATH, m_pStorage.get()));
		ExpectEqual(Cache, m_Saved);
	}
	void ExpectRejectedContents(const std::string &Text)
	{
		WriteContents(Text);
		auto Cache = Sentinel();
		EXPECT_FALSE(CSectionLoader::LoadSessionCache(Cache, PATH, m_pStorage.get()));
		ExpectEqual(Cache, Sentinel());
		ASSERT_TRUE(CSectionLoader::SaveSessionCache(m_Saved, PATH, m_pStorage.get()));
		ASSERT_TRUE(CSectionLoader::LoadSessionCache(Cache, PATH, m_pStorage.get()));
		ExpectEqual(Cache, m_Saved);
	}
};

TEST_F(SectionLoaderSessionCacheFailure, OpenFailurePreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::OPEN); }
TEST_F(SectionLoaderSessionCacheFailure, OpenExceptionPreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::OPEN_EXCEPTION); }
TEST_F(SectionLoaderSessionCacheFailure, PartialWritePreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::WRITE); }
TEST_F(SectionLoaderSessionCacheFailure, WriteExceptionClosesCandidateAndCanRetry) { ExpectSaveFailure(EIoFault::WRITE_EXCEPTION); }
TEST_F(SectionLoaderSessionCacheFailure, SyncFailurePreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::SYNC); }
TEST_F(SectionLoaderSessionCacheFailure, StreamErrorPreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::ERROR); }
TEST_F(SectionLoaderSessionCacheFailure, CloseFailurePreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::CLOSE); }
TEST_F(SectionLoaderSessionCacheFailure, PublishFailurePreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::PUBLISH); }
TEST_F(SectionLoaderSessionCacheFailure, PublishExceptionPreservesOldFileAndCanRetry) { ExpectSaveFailure(EIoFault::PUBLISH_EXCEPTION); }
TEST_F(SectionLoaderSessionCacheFailure, ReadOpenFailureKeepsOutputAndCanRetry) { ExpectLoadFailure(EIoFault::OPEN); }
TEST_F(SectionLoaderSessionCacheFailure, ReadOpenExceptionKeepsOutputAndCanRetry) { ExpectLoadFailure(EIoFault::OPEN_EXCEPTION); }
TEST_F(SectionLoaderSessionCacheFailure, EmptyReadKeepsOutputAndCanRetry) { ExpectLoadFailure(EIoFault::READ); }
TEST_F(SectionLoaderSessionCacheFailure, ReadExceptionClosesFileAndCanRetry) { ExpectLoadFailure(EIoFault::READ_EXCEPTION); }
TEST_F(SectionLoaderSessionCacheFailure, ReadStreamErrorKeepsOutputAndCanRetry) { ExpectLoadFailure(EIoFault::ERROR); }
TEST_F(SectionLoaderSessionCacheFailure, ReadCloseFailureKeepsOutputAndCanRetry) { ExpectLoadFailure(EIoFault::CLOSE); }

TEST_F(SectionLoaderSessionCacheFailure, InvalidSignedIntegerRejectsTheWholeSnapshot)
{
	for(const char *pValue : {"", "abc", "+-1", "++1", "2147483648", "-2147483649", "8junk"})
	{
		SCOPED_TRACE(pValue);
		ExpectRejectedContents(std::string("scroll_y=-480\nsettings_page=") + pValue + "\n");
	}
}

TEST_F(SectionLoaderSessionCacheFailure, InvalidUnsignedHashRejectsTheWholeSnapshot)
{
	for(const char *pValue : {"", "-1", "18446744073709551616", "55junk"})
	{
		SCOPED_TRACE(pValue);
		ExpectRejectedContents(std::string("settings_page=8\nwindow_hash=") + pValue + "\n");
	}
}

TEST_F(SectionLoaderSessionCacheFailure, InvalidScrollRejectsTheWholeSnapshot)
{
	for(const char *pValue : {"", "nan", "inf", "-inf", "1e999", "-1e999", "12junk"})
	{
		SCOPED_TRACE(pValue);
		ExpectRejectedContents(std::string("settings_page=8\nscroll_y=") + pValue + "\n");
	}
}

TEST_F(SectionLoaderSessionCacheFailure, OversizedFileDoesNotPublishItsValidPrefix)
{
	ExpectRejectedContents("settings_page=8\nunknown=" + std::string(700, 'x'));
}

TEST_F(SectionLoaderSessionCacheFailure, EmbeddedNulDoesNotPublishItsValidPrefix)
{
	std::string Text = "settings_page=8\n";
	Text.push_back('\0');
	Text += "scroll_y=-480\n";
	ExpectRejectedContents(Text);
}

TEST_F(SectionLoaderSessionCacheFailure, TruncatedRecordRejectsTheWholeSnapshot)
{
	ExpectRejectedContents("settings_page=8\nwindow_hash");
}

TEST_F(SectionLoaderSessionCacheFailure, LegacySnapshotClearsOldRuntimeFieldsRatherThanMergingThem)
{
	WriteContents("settings_page=8\ntab_tclient=0\nscroll_y=-240\n");
	auto Cache = Sentinel();
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Cache, PATH, m_pStorage.get()));
	EXPECT_EQ(Cache.m_LastSettingsPage, 8);
	EXPECT_EQ(Cache.m_LastTClientTab, 0);
	EXPECT_EQ(Cache.m_LastQmTab, -1);
	EXPECT_FLOAT_EQ(Cache.m_LastScrollY, -240);
	EXPECT_TRUE(Cache.m_RuntimeKey == SSettingsSectionCacheRuntimeKey{});
}

TEST_F(SectionLoaderSessionCacheFailure, CompleteLastLineAndUnknownKeysKeepWorking)
{
	WriteContents("unknown=anything\r\n settings_page=+8 \r\nscroll_y= -12.5 \t\r\nwindow_hash=+55");
	SSessionUiCache Cache;
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Cache, PATH, m_pStorage.get()));
	EXPECT_EQ(Cache.m_LastSettingsPage, 8);
	EXPECT_FLOAT_EQ(Cache.m_LastScrollY, -12.5f);
	EXPECT_EQ(Cache.m_RuntimeKey.m_WindowHash, 55u);
}

TEST_F(SectionLoaderSessionCacheFailure, EmptyFilePreservesTheCallerSnapshot)
{
	ExpectRejectedContents("");
}

TEST_F(SectionLoaderSessionCacheFailure, MissingFilePreservesTheCallerSnapshot)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(PATH, IStorage::TYPE_SAVE));
	auto Cache = Sentinel();
	EXPECT_FALSE(CSectionLoader::LoadSessionCache(Cache, PATH, m_pStorage.get()));
	ExpectEqual(Cache, Sentinel());
}

TEST_F(SectionLoaderSessionCacheFailure, NonFiniteScrollDoesNotReplaceTheStoredSnapshot)
{
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		auto Cache = m_Saved;
		Cache.m_LastScrollY = Value;
		EXPECT_FALSE(CSectionLoader::SaveSessionCache(Cache, PATH, m_pStorage.get()));
		ExpectStored(m_Saved);
	}
}

TEST_F(SectionLoaderSessionCacheFailure, InvalidSnapshotDoesNotReplaceTheStoredSnapshot)
{
	auto Cache = m_Saved;
	Cache.m_Valid = false;
	EXPECT_FALSE(CSectionLoader::SaveSessionCache(Cache, PATH, m_pStorage.get()));
	ExpectStored(m_Saved);
}

TEST_F(SectionLoaderSessionCacheFailure, SnapshotWithoutAnyPageDoesNotReplaceTheStoredSnapshot)
{
	auto Cache = m_Saved;
	Cache.m_LastSettingsPage = Cache.m_LastTClientTab = Cache.m_LastQmTab = -1;
	EXPECT_FALSE(CSectionLoader::SaveSessionCache(Cache, PATH, m_pStorage.get()));
	ExpectStored(m_Saved);
}

TEST_F(SectionLoaderSessionCacheFailure, ExistingCandidateIsNeitherOverwrittenNorRemoved)
{
	CFailingSessionIo Operations(EIoFault::OPEN);
	ASSERT_FALSE(CSectionLoader::SaveSessionCache(m_Saved, PATH, m_pStorage.get(), &Operations));
	ASSERT_FALSE(Operations.m_OpenedPath.empty());
	WriteContents("owned by another save", Operations.m_OpenedPath.c_str());
	EXPECT_FALSE(CSectionLoader::SaveSessionCache(m_Saved, PATH, m_pStorage.get()));
	ExpectStored(m_Saved);
	char aData[64] = {};
	IOHANDLE File = m_pStorage->OpenFile(Operations.m_OpenedPath.c_str(), IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const unsigned Read = io_read(File, aData, sizeof(aData) - 1);
	const int Closed = io_close(File);
	EXPECT_GT(Read, 0u);
	EXPECT_EQ(Closed, 0);
	EXPECT_STREQ(aData, "owned by another save");
}

TEST_F(SectionLoaderSessionCacheFailure, TruncatedTemporaryPathIsRejectedBeforeOpeningAFile)
{
	CFailingSessionIo Operations(EIoFault::OPEN);
	const std::string LongPath(IO_MAX_PATH_LENGTH, 'x');
	EXPECT_FALSE(CSectionLoader::SaveSessionCache(m_Saved, LongPath.c_str(), m_pStorage.get(), &Operations));
	EXPECT_TRUE(Operations.m_OpenedPath.empty());
	ExpectStored(m_Saved);
}
