#include <engine/client/map_download_file.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <string>

namespace
{
	class CObservedMapFileOperations : public CMapDownloadFileOperations
	{
	public:
		bool m_FailOpen = false;
		bool m_ThrowOpen = false;
		bool m_ShortWrite = false;
		bool m_ThrowWrite = false;
		bool m_Error = false;
		bool m_ThrowError = false;
		bool m_FailClose = false;
		int m_Opens = 0;
		int m_Closes = 0;
		const CMapDownloadFile *m_pOwner = nullptr;
		bool m_CloseObservedDetachedOwner = false;

		IOHANDLE Open(IStorage &Storage, const char *pFilename) override
		{
			++m_Opens;
			if(m_ThrowOpen)
				throw std::bad_alloc();
			return m_FailOpen ? nullptr : CMapDownloadFileOperations::Open(Storage, pFilename);
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
		{
			if(m_ThrowWrite)
				throw std::bad_alloc();
			return io_write(File, pData, m_ShortWrite ? Size - 1 : Size);
		}
		int Error(IOHANDLE File) override
		{
			if(m_ThrowError)
				throw std::bad_alloc();
			return m_Error ? 1 : io_error(File);
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			m_CloseObservedDetachedOwner = m_pOwner && !*m_pOwner;
			const int Result = io_close(File);
			return m_FailClose ? 1 : Result;
		}
	};

	class CMapDownloadFileTest : public testing::Test
	{
	protected:
		CTestInfo m_TestInfo;
		std::unique_ptr<IStorage> m_pStorage = m_TestInfo.CreateTestStorage();
		CObservedMapFileOperations m_Operations;
		CMapDownloadFile m_File{&m_Operations};
		static constexpr const char *CANDIDATE = "candidate.map";
		static constexpr const char *TARGET = "target.map";
		void SetUp() override
		{
			ASSERT_NE(m_pStorage, nullptr);
			m_Operations.m_pOwner = &m_File;
		}
		void WriteOldTarget()
		{
			IOHANDLE File = m_pStorage->OpenFile(TARGET, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "old", 3), 3U);
			EXPECT_EQ(io_close(File), 0);
		}
		std::string ReadFile(const char *pName)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_READ, IStorage::TYPE_SAVE);
			if(!File)
				return "missing";
			char aBytes[32] = {};
			const unsigned Size = io_read(File, aBytes, sizeof(aBytes));
			EXPECT_EQ(io_close(File), 0);
			return std::string(aBytes, Size);
		}
	};
}

TEST_F(CMapDownloadFileTest, CompleteWriteClosesAndRetainsAllBytes)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	ASSERT_TRUE(m_File.Write("map-data", 8));
	EXPECT_TRUE(m_File.Close());
	EXPECT_FALSE(m_File);
	EXPECT_EQ(ReadFile(CANDIDATE), "map-data");
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CMapDownloadFileTest, FailedOpenDoesNotOwnOrCloseAHandle)
{
	m_Operations.m_FailOpen = true;
	EXPECT_FALSE(m_File.Open(*m_pStorage, CANDIDATE));
	EXPECT_FALSE(m_File);
	EXPECT_TRUE(m_File.Close());
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(CMapDownloadFileTest, OpenExceptionReturnsFailureAndAllowsRetry)
{
	m_Operations.m_ThrowOpen = true;
	EXPECT_FALSE(m_File.Open(*m_pStorage, CANDIDATE));
	EXPECT_FALSE(m_File);
	m_Operations.m_ThrowOpen = false;
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	EXPECT_TRUE(m_File.Close());
	EXPECT_EQ(m_Operations.m_Opens, 2);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CMapDownloadFileTest, ShortWriteReportsFailureAndClosesOnlyOnce)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	m_Operations.m_ShortWrite = true;
	EXPECT_FALSE(m_File.Write("data", 4));
	EXPECT_FALSE(m_File.Write("tail", 4));
	EXPECT_FALSE(m_File.Close());
	EXPECT_TRUE(m_File.Close());
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(ReadFile(CANDIDATE), "dat");
}

TEST_F(CMapDownloadFileTest, WriteExceptionStillAllowsCleanupAndRetry)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	m_Operations.m_ThrowWrite = true;
	EXPECT_FALSE(m_File.Write("data", 4));
	EXPECT_FALSE(m_File.Close());
	m_Operations.m_ThrowWrite = false;
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	EXPECT_TRUE(m_File.Write("retry", 5));
	EXPECT_TRUE(m_File.Close());
	EXPECT_EQ(ReadFile(CANDIDATE), "retry");
	EXPECT_EQ(m_Operations.m_Closes, 2);
}

TEST_F(CMapDownloadFileTest, StickyErrorRejectsWriteAndCloseSuccess)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	m_Operations.m_Error = true;
	EXPECT_FALSE(m_File.Write("data", 4));
	EXPECT_FALSE(m_File.Close());
	EXPECT_FALSE(m_File);
	EXPECT_TRUE(m_File.Close());
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CMapDownloadFileTest, CloseFailureConsumesOwnershipBeforeCallback)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	m_Operations.m_FailClose = true;
	EXPECT_FALSE(m_File.Close());
	EXPECT_FALSE(m_File);
	EXPECT_TRUE(m_Operations.m_CloseObservedDetachedOwner);
	EXPECT_TRUE(m_File.Close());
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CMapDownloadFileTest, ErrorExceptionStillClosesTheOwnedHandle)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	m_Operations.m_ThrowError = true;
	EXPECT_FALSE(m_File.Close());
	EXPECT_FALSE(m_File);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CMapDownloadFileTest, DestructorClosesAnUnfinishedFile)
{
	CObservedMapFileOperations Operations;
	{
		CMapDownloadFile File(&Operations);
		ASSERT_TRUE(File.Open(*m_pStorage, CANDIDATE));
		ASSERT_TRUE(File.Write("partial", 7));
	}
	EXPECT_EQ(Operations.m_Closes, 1);
	EXPECT_EQ(ReadFile(CANDIDATE), "partial");
}

TEST_F(CMapDownloadFileTest, RepeatedOpenPreservesTheCurrentFile)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	ASSERT_TRUE(m_File.Write("kept", 4));
	EXPECT_FALSE(m_File.Open(*m_pStorage, CANDIDATE));
	EXPECT_EQ(m_Operations.m_Opens, 1);
	EXPECT_TRUE(m_File.Close());
	EXPECT_EQ(ReadFile(CANDIDATE), "kept");
}

TEST_F(CMapDownloadFileTest, PublishReplacesAnExistingTargetAfterSuccessfulClose)
{
	ASSERT_NO_FATAL_FAILURE(WriteOldTarget());
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	ASSERT_TRUE(m_File.Write("new", 3));
	ASSERT_TRUE(m_File.Close());
	EXPECT_TRUE(map_download::Publish(*m_pStorage, CANDIDATE, TARGET));
	EXPECT_EQ(ReadFile(TARGET), "new");
	EXPECT_FALSE(m_pStorage->FileExists(CANDIDATE, IStorage::TYPE_SAVE));
}

TEST_F(CMapDownloadFileTest, PublishMissingCandidatePreservesExistingTarget)
{
	ASSERT_NO_FATAL_FAILURE(WriteOldTarget());
	EXPECT_FALSE(map_download::Publish(*m_pStorage, "missing.map", TARGET));
	EXPECT_EQ(ReadFile(TARGET), "old");
}

TEST_F(CMapDownloadFileTest, PublishAlsoWorksWithoutAnExistingTarget)
{
	ASSERT_TRUE(m_File.Open(*m_pStorage, CANDIDATE));
	ASSERT_TRUE(m_File.Write("new", 3));
	ASSERT_TRUE(m_File.Close());
	EXPECT_TRUE(map_download::Publish(*m_pStorage, CANDIDATE, TARGET));
	EXPECT_EQ(ReadFile(TARGET), "new");
}
