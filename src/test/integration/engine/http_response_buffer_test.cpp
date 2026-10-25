#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>
#include <test/test.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <base/fs.h>

#include <engine/storage.h>

#include <test/support/http_curl_test_peer.h>
#include <test/support/http_response_allocation_probe.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <string>

namespace
{
	class CBufferCurlRequest : public CHttpRequestCurl
	{
	public:
		CHttpResponseAllocationProbe m_Allocation;
		CBufferCurlRequest() :
			CHttpRequestCurl("https://response.test/body") { LogProgress(HTTPLOG::NONE); }
		bool PrepareFile() { return BeforeInit(); }
		std::string TemporaryPath() const { return m_aDestAbsoluteTmp; }

	private:
		void *ReallocateResponseBuffer(size_t Size) noexcept override { return m_Allocation.Reallocate(m_pBuffer, Size); }
	};

	class CCompletion : public IHttpRequest::IProgressCallback
	{
	public:
		EHttpState m_State = EHttpState::QUEUED;
		int m_Completions = 0;
		void OnProgress() override {}
		void OnCompletion(EHttpState State) override
		{
			m_State = State;
			++m_Completions;
		}
	};

	void SuccessHeader(CHttpRequestCurl &Request)
	{
		CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
		CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	}

	class HttpResponseBufferStorage : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		void SetUp() override { ASSERT_NE(m_pStorage, nullptr); }
		void WriteOldFile()
		{
			IOHANDLE File = m_pStorage->OpenFile("body.bin", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "old", 3), 3u);
			EXPECT_EQ(io_close(File), 0);
		}
		std::string ReadFile()
		{
			char *pText = m_pStorage->ReadFileStr("body.bin", IStorage::TYPE_SAVE);
			if(!pText)
				return {};
			std::string Text(pText);
			std::free(pText);
			return Text;
		}
	};
}

TEST(HttpResponseBufferCurl, AllocationFailurePublishesErrorToRequestAndCompletionConsumer)
{
	CCompletion Completion;
	CBufferCurlRequest Request;
	Request.SetProgressCallback(&Completion);
	Request.m_Allocation.m_Fail = true;
	SuccessHeader(Request);
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, "body"), 0u);
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_WRITE_ERROR);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(Completion.m_State, EHttpState::ERROR);
	EXPECT_EQ(Completion.m_Completions, 1);
	EXPECT_FALSE(Request.IsAbortRequested());
}

TEST(HttpResponseBufferCurl, GrowthFailureKeepsAcceptedPrefixWithoutBecomingSuccessfulSample)
{
	CBufferCurlRequest Request;
	Request.ResponseSample(4096);
	SuccessHeader(Request);
	const std::string Prefix(800, 'p');
	ASSERT_EQ(CHttpRequestCurlTestPeer::Body(Request, Prefix), Prefix.size());
	Request.m_Allocation.m_Fail = true;
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, std::string(500, 'x')), 0u);
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_WRITE_ERROR);
	ASSERT_EQ(Request.State(), EHttpState::ERROR);
	unsigned char *pBytes = nullptr;
	size_t Size = 0;
	Request.ResultResponseSample(&pBytes, &Size);
	ASSERT_EQ(Size, Prefix.size());
	ASSERT_NE(pBytes, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pBytes), Size), Prefix);
}

TEST(HttpResponseBufferCurl, SampleLimitWriteAbortStillPublishesSuccessfulBoundedPrefix)
{
	CBufferCurlRequest Request;
	Request.ResponseSample(4);
	SuccessHeader(Request);
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, "prefix and tail"), 0u);
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_WRITE_ERROR);
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	unsigned char *pBytes = nullptr;
	size_t Size = 0;
	Request.ResultResponseSample(&pBytes, &Size);
	ASSERT_EQ(Size, 4u);
	ASSERT_NE(pBytes, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pBytes), Size), "pref");
}

TEST(HttpResponseBufferCurl, SampleAllocationFailureIsNotTheExpectedSampleLimitAbort)
{
	CBufferCurlRequest Request;
	Request.ResponseSample(4);
	Request.m_Allocation.m_Fail = true;
	SuccessHeader(Request);
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, "prefix"), 0u);
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_WRITE_ERROR);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(HttpResponseBufferCurl, UserCancellationStillWinsOverReceiveFailure)
{
	CCompletion Completion;
	CBufferCurlRequest Request;
	Request.SetProgressCallback(&Completion);
	Request.m_Allocation.m_Fail = true;
	SuccessHeader(Request);
	ASSERT_EQ(CHttpRequestCurlTestPeer::Body(Request, "body"), 0u);
	Request.Abort();
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_WRITE_ERROR);
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
	EXPECT_EQ(Completion.m_State, EHttpState::ABORTED);
}

TEST(HttpResponseBufferCurl, CallbackSizeProductOverflowRejectsBeforeAccessingData)
{
	CBufferCurlRequest Request;
	char aData[] = "x";
	EXPECT_EQ(CHttpRequestCurlTestPeer::BodySize(Request, aData, 2, std::numeric_limits<size_t>::max()), 0u);
	EXPECT_EQ(Request.m_Allocation.m_Requests, 0);
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_WRITE_ERROR);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST_F(HttpResponseBufferStorage, MemoryGrowthFailurePreservesOldTargetAndRemovesFileCandidate)
{
	WriteOldFile();
	CBufferCurlRequest Request;
	Request.WriteToFileAndMemory(m_pStorage.get(), "body.bin", IStorage::TYPE_SAVE);
	ASSERT_TRUE(Request.PrepareFile());
	SuccessHeader(Request);
	ASSERT_EQ(CHttpRequestCurlTestPeer::Body(Request, std::string(800, 'p')), 800u);
	Request.m_Allocation.m_Fail = true;
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, std::string(500, 'x')), 0u);
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_WRITE_ERROR);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_EQ(ReadFile(), "old");
	EXPECT_FALSE(fs_is_file(Request.TemporaryPath().c_str()));
}

TEST_F(HttpResponseBufferStorage, NewDownloadAfterAllocationFailureCanReplaceTargetNormally)
{
	WriteOldFile();
	{
		CBufferCurlRequest Failed;
		Failed.WriteToFileAndMemory(m_pStorage.get(), "body.bin", IStorage::TYPE_SAVE);
		ASSERT_TRUE(Failed.PrepareFile());
		Failed.m_Allocation.m_Fail = true;
		SuccessHeader(Failed);
		ASSERT_EQ(CHttpRequestCurlTestPeer::Body(Failed, "rejected"), 0u);
		CHttpRequestCurlTestPeer::Complete(Failed, nullptr, CURLE_WRITE_ERROR);
		ASSERT_EQ(Failed.State(), EHttpState::ERROR);
	}
	CBufferCurlRequest Healthy;
	Healthy.WriteToFileAndMemory(m_pStorage.get(), "body.bin", IStorage::TYPE_SAVE);
	ASSERT_TRUE(Healthy.PrepareFile());
	SuccessHeader(Healthy);
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Healthy, "replacement"), 11u);
	CHttpRequestCurlTestPeer::Complete(Healthy);
	ASSERT_EQ(Healthy.State(), EHttpState::DONE);
	EXPECT_EQ(ReadFile(), "replacement");
	EXPECT_FALSE(fs_is_file(Healthy.TemporaryPath().c_str()));
	unsigned char *pBytes = nullptr;
	size_t Size = 0;
	Healthy.Result(&pBytes, &Size);
	ASSERT_EQ(Size, 11u);
	ASSERT_NE(pBytes, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pBytes), Size), "replacement");
}
#endif
