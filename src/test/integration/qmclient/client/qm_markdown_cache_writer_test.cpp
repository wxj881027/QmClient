#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/markdown_cache_writer.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <string>

namespace
{
	class CCacheFileFaults : public CQmMarkdownCacheWriter::CFileOperations
	{
	public:
		enum class EFailure
		{
			NONE,
			OPEN,
			WRITE,
			WRITE_THROW,
			SYNC,
			ERROR_FLAG,
			CLOSE,
			PUBLISH,
			PUBLISH_THROW
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Closes = 0;
		std::function<void()> m_OnWrite;

		IOHANDLE Open(const char *pPath) override
		{
			return m_Failure == EFailure::OPEN ? nullptr : CFileOperations::Open(pPath);
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
		{
			auto OnWrite = std::move(m_OnWrite);
			if(OnWrite)
				OnWrite();
			if(m_Failure == EFailure::WRITE_THROW)
			{
				CFileOperations::Write(File, pData, Size / 2);
				throw std::bad_alloc();
			}
			return CFileOperations::Write(File, pData, m_Failure == EFailure::WRITE ? Size / 2 : Size);
		}
		int Sync(IOHANDLE File) override
		{
			const int Result = CFileOperations::Sync(File);
			return m_Failure == EFailure::SYNC ? -1 : Result;
		}
		int Error(IOHANDLE File) override
		{
			return m_Failure == EFailure::ERROR_FLAG ? 1 : CFileOperations::Error(File);
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CFileOperations::Close(File);
			return m_Failure == EFailure::CLOSE ? -1 : Result;
		}
		bool Publish(const char *pTemporary, const char *pTarget) override
		{
			if(m_Failure == EFailure::PUBLISH_THROW)
				throw std::bad_alloc();
			return m_Failure != EFailure::PUBLISH && CFileOperations::Publish(pTemporary, pTarget);
		}
	};

	class CMarkdownCacheTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		std::string m_Path;
		std::shared_ptr<IJob> m_pJob;

		void SetUp() override
		{
			ASSERT_NE(m_pStorage, nullptr);
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/news_cache.json", aPath, sizeof(aPath));
			m_Path = aPath;
		}
		bool Submit(std::shared_ptr<IJob> pJob)
		{
			m_pJob = std::move(pJob);
			return true;
		}
		bool Enqueue(CQmMarkdownCacheWriter &Writer, int Version, const std::string &Markdown)
		{
			return Writer.TryEnqueue(m_Path, 1, Version, Markdown, [this](auto pJob) { return Submit(std::move(pJob)); });
		}
		bool Flush(CQmMarkdownCacheWriter &Writer)
		{
			return Writer.TryFlush([this](auto pJob) { return Submit(std::move(pJob)); });
		}
		void Run()
		{
			ASSERT_NE(m_pJob, nullptr);
			CJobPool Pool;
			Pool.Init(1);
			ASSERT_TRUE(Pool.TryAdd(std::move(m_pJob)));
			Pool.Shutdown();
		}
		std::string Read()
		{
			void *pData = nullptr;
			unsigned Size = 0;
			if(!m_pStorage->ReadFile("qmclient/news_cache.json", IStorage::TYPE_SAVE, &pData, &Size))
				return {};
			std::unique_ptr<void, decltype(&free)> Owner(pData, free);
			return std::string(static_cast<const char *>(pData), Size);
		}
		void ExpectSnapshot(int Version, const char *pMarkdown)
		{
			const std::string Data = Read();
			std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Data.data(), Data.size()), json_value_free);
			ASSERT_NE(Root, nullptr);
			EXPECT_EQ(json_int_get(json_object_get(Root.get(), "cache_version")), 1);
			EXPECT_EQ(json_int_get(json_object_get(Root.get(), "version")), Version);
			EXPECT_STREQ(json_string_get(json_object_get(Root.get(), "markdown")), pMarkdown);
		}
	};
}

TEST_F(CMarkdownCacheTest, DeferredWriteKeepsNewestSnapshotAfterOwnerDestruction)
{
	const std::string Expected = "公告\n\"引号\" 与 \\ 路径";
	{
		CQmMarkdownCacheWriter Writer;
		ASSERT_TRUE(Enqueue(Writer, 10, "old"));
		ASSERT_NE(m_pJob, nullptr);
		const auto *pFirst = m_pJob.get();
		std::string Markdown = Expected;
		ASSERT_TRUE(Enqueue(Writer, 11, Markdown));
		EXPECT_EQ(m_pJob.get(), pFirst);
		Markdown = "changed after enqueue";
		EXPECT_FALSE(m_pStorage->FileExists("qmclient/news_cache.json", IStorage::TYPE_SAVE));
	}
	Run();
	ExpectSnapshot(11, Expected.c_str());
}

TEST_F(CMarkdownCacheTest, FailedWriteAndCompletedWriteAcceptNewRequests)
{
	char aDirectory[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aDirectory, sizeof(aDirectory));
	CQmMarkdownCacheWriter Writer;
	ASSERT_TRUE(Writer.TryEnqueue(aDirectory, 1, 2, "unwritable", [this](auto pJob) { return Submit(std::move(pJob)); }));
	Run();
	EXPECT_TRUE(Writer.NeedsRetry());
	for(int Version : {3, 4})
	{
		ASSERT_TRUE(Enqueue(Writer, Version, ""));
		Run();
		ExpectSnapshot(Version, "");
		EXPECT_FALSE(Writer.NeedsRetry());
	}
}

TEST_F(CMarkdownCacheTest, ClosedPoolRejectionDoesNotLatchRunningState)
{
	CQmMarkdownCacheWriter Writer;
	CJobPool ClosedPool;
	EXPECT_FALSE(Writer.TryEnqueue(m_Path, 1, 1, "rejected", [&ClosedPool](auto pJob) { return ClosedPool.TryAdd(std::move(pJob)); }));
	EXPECT_FALSE(Writer.NeedsRetry());
	ASSERT_TRUE(Enqueue(Writer, 2, "accepted"));
	Run();
	ExpectSnapshot(2, "accepted");
}

namespace
{
	class CMarkdownCacheFailureTest : public CMarkdownCacheTest, public ::testing::WithParamInterface<CCacheFileFaults::EFailure>
	{
	};
}

TEST_P(CMarkdownCacheFailureTest, FailedCandidateKeepsOldFileAndCanRetry)
{
	auto pOperations = std::make_shared<CCacheFileFaults>();
	CQmMarkdownCacheWriter Writer(pOperations);
	ASSERT_TRUE(Enqueue(Writer, 1, "old"));
	Run();
	const std::string Old = Read();
	ASSERT_FALSE(Old.empty());
	pOperations->m_Closes = 0;
	pOperations->m_Failure = GetParam();
	ASSERT_TRUE(Enqueue(Writer, 2, "new"));
	Run();
	EXPECT_EQ(Read(), Old);
	EXPECT_EQ(pOperations->m_Closes, GetParam() == CCacheFileFaults::EFailure::OPEN ? 0 : 1);
	EXPECT_TRUE(Writer.NeedsRetry());
	pOperations->m_Failure = CCacheFileFaults::EFailure::NONE;
	ASSERT_TRUE(Flush(Writer));
	Run();
	ExpectSnapshot(2, "new");
	EXPECT_FALSE(Writer.NeedsRetry());
}

INSTANTIATE_TEST_SUITE_P(FileOperations, CMarkdownCacheFailureTest, ::testing::Values(CCacheFileFaults::EFailure::OPEN, CCacheFileFaults::EFailure::WRITE, CCacheFileFaults::EFailure::WRITE_THROW, CCacheFileFaults::EFailure::SYNC, CCacheFileFaults::EFailure::ERROR_FLAG, CCacheFileFaults::EFailure::CLOSE, CCacheFileFaults::EFailure::PUBLISH, CCacheFileFaults::EFailure::PUBLISH_THROW));

TEST_F(CMarkdownCacheTest, NewerRequestDuringFailureWinsOverFailedSnapshot)
{
	auto pOperations = std::make_shared<CCacheFileFaults>();
	CQmMarkdownCacheWriter Writer(pOperations);
	pOperations->m_Failure = CCacheFileFaults::EFailure::WRITE;
	bool Accepted = false;
	int ExtraSubmissions = 0;
	pOperations->m_OnWrite = [&] {
		Accepted = Writer.TryEnqueue(m_Path, 1, 3, "latest", [&](auto) { ++ExtraSubmissions; return true; });
	};
	ASSERT_TRUE(Enqueue(Writer, 2, "failed"));
	Run();
	EXPECT_TRUE(Accepted);
	EXPECT_EQ(ExtraSubmissions, 0);
	EXPECT_TRUE(Writer.NeedsRetry());
	pOperations->m_Failure = CCacheFileFaults::EFailure::NONE;
	ASSERT_TRUE(Flush(Writer));
	Run();
	ExpectSnapshot(3, "latest");
}

TEST_F(CMarkdownCacheTest, RejectedRetryKeepsThePendingSnapshot)
{
	auto pOperations = std::make_shared<CCacheFileFaults>();
	CQmMarkdownCacheWriter Writer(pOperations);
	pOperations->m_Failure = CCacheFileFaults::EFailure::WRITE;
	ASSERT_TRUE(Enqueue(Writer, 2, "retry me"));
	Run();
	EXPECT_FALSE(Writer.TryFlush([](auto) { return false; }));
	EXPECT_TRUE(Writer.NeedsRetry());
	pOperations->m_Failure = CCacheFileFaults::EFailure::NONE;
	ASSERT_TRUE(Flush(Writer));
	Run();
	ExpectSnapshot(2, "retry me");
}
