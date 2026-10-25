#include <game/client/components/qmclient/update_package.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	// 只替换 HTTP 和提交边界，文件接收/完成回调、批次状态和取消使用生产实现。
	class CPackageFailureResponse : public IHttpRequest
	{
	public:
		bool m_ThrowOnAbort = false;
		explicit CPackageFailureResponse(const std::string &Url) :
			IHttpRequest(Url.c_str()) {}
		void Header(const char *) override {}
		std::optional<CHttpByteRange> Range() const { return m_ByteRange; }
		void Reply(int Status, std::string_view Body, std::optional<CHttpByteRange> Range = {}, EHttpState State = EHttpState::DONE)
		{
			m_StatusCode = Status;
			m_ResultContentRange = Range;
			if(!BeforeInit() || OnData(Body.data(), Body.size()) != Body.size())
				State = EHttpState::ERROR;
			m_Current = Body.size();
			OnCompletionInternal(State);
		}
		void Abort() override
		{
			IHttpRequest::Abort();
			if(m_ThrowOnAbort)
				throw std::runtime_error("cancel callback failed");
			// 保留异步取消窗口，由用例随后发布完成，检验晚到文件回收。
		}
	};

	class QmPackagePreparationFailure : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_Storage = m_Info.CreateTestStorage();
		std::shared_ptr<qm_update::CPackageDownload> m_Download;
		std::vector<std::shared_ptr<CPackageFailureResponse>> m_vRequests;
		std::string m_Body = std::string(4 * 1024 * 1024 + 3, 'q');
		int m_Creates = 0;
		int m_Starts = 0;
		int m_FailCreateAt = -1;
		int m_NullCreateAt = -1;
		int m_FailStartAt = -1;
		bool m_LengthFailure = false;
		bool m_UnknownFailure = false;
		qm_update::CPackageDownload::TAddJob m_Submit = [](std::shared_ptr<IJob>) { return false; };

		void Begin()
		{
			m_Download = std::make_shared<qm_update::CPackageDownload>("https://github.com/wxj881027/QmClient/releases/download/v3.4/package.7z", m_Storage.get(), "package.tmp", 8 * 1024 * 1024, [this](const std::shared_ptr<IHttpRequest> &) {
					if(m_Starts++ == m_FailStartAt)
					{
						if(m_UnknownFailure)
							throw 17;
						throw std::bad_alloc();
					} }, [this](std::shared_ptr<IJob> Job) { return m_Submit(std::move(Job)); }, [this](const std::string &Url) -> std::shared_ptr<IHttpRequest> {
					const int Index = m_Creates++;
					if(Index == m_NullCreateAt)
						return nullptr;
					if(Index == m_FailCreateAt)
					{
						if(m_LengthFailure)
							throw std::length_error("request capacity exhausted");
						throw std::bad_alloc();
					}
					auto Request = std::make_shared<CPackageFailureResponse>(Url);
					m_vRequests.push_back(Request);
					return Request; });
		}
		void Probe()
		{
			ASSERT_EQ(m_vRequests.size(), 1u);
			m_vRequests.front()->Reply(206, std::string_view(m_Body).substr(0, 1), CHttpByteRange{0, 0, static_cast<int64_t>(m_Body.size())});
			EXPECT_NO_THROW(m_Download->Poll(1));
		}
		void FinishPayload()
		{
			ASSERT_EQ(m_vRequests.size(), 5u);
			for(size_t Index = 1; Index < m_vRequests.size(); ++Index)
			{
				const auto Range = m_vRequests[Index]->Range();
				ASSERT_TRUE(Range.has_value());
				m_vRequests[Index]->Reply(206, std::string_view(m_Body).substr(Range->m_First, Range->Length()), Range);
			}
		}
		void ExpectLocalFailure()
		{
			EXPECT_EQ(m_Download->State(), EHttpState::ERROR);
			EXPECT_TRUE(m_Download->LocalError());
			const int Creates = m_Creates;
			const int Starts = m_Starts;
			EXPECT_NO_THROW(m_Download->Poll(100));
			EXPECT_NO_THROW(m_Download->Abort());
			EXPECT_EQ(m_Download->State(), EHttpState::ERROR);
			EXPECT_EQ(m_Creates, Creates);
			EXPECT_EQ(m_Starts, Starts);
		}
		void ExpectNoPackageFiles()
		{
			char aPath[IO_MAX_PATH_LENGTH];
			for(const auto &Request : m_vRequests)
			{
				if(Request->Dest()[0] == '\0')
					continue;
				m_Storage->GetCompletePath(IStorage::TYPE_SAVE, Request->Dest(), aPath, sizeof(aPath));
				EXPECT_FALSE(fs_is_file(aPath)) << Request->Dest();
			}
			m_Storage->GetCompletePath(IStorage::TYPE_SAVE, "package.tmp", aPath, sizeof(aPath));
			EXPECT_FALSE(fs_is_file(aPath));
		}
	};
}

TEST_F(QmPackagePreparationFailure, ProbeFactoryExceptionPublishesLocalError)
{
	m_FailCreateAt = 0;
	EXPECT_NO_THROW(Begin());
	ASSERT_NE(m_Download, nullptr);
	ExpectLocalFailure();
	EXPECT_TRUE(m_vRequests.empty());
	EXPECT_EQ(m_Starts, 0);
}

TEST_F(QmPackagePreparationFailure, NullProbeFactoryResultCanBeDestroyedSafely)
{
	m_NullCreateAt = 0;
	EXPECT_NO_THROW(Begin());
	ASSERT_NE(m_Download, nullptr);
	ExpectLocalFailure();
	EXPECT_NO_THROW(m_Download.reset());
}

TEST_F(QmPackagePreparationFailure, ProbeStartExceptionCancelsTrackedRequest)
{
	m_FailStartAt = 0;
	EXPECT_NO_THROW(Begin());
	ASSERT_EQ(m_vRequests.size(), 1u);
	ExpectLocalFailure();
	EXPECT_TRUE(m_vRequests[0]->IsAbortRequested());
}

TEST_F(QmPackagePreparationFailure, FirstPayloadFactoryFailureCancelsPartiallyPreparedBatch)
{
	Begin();
	m_FailCreateAt = 1;
	Probe();
	ExpectLocalFailure();
	EXPECT_EQ(m_vRequests.size(), 1u);
	EXPECT_NO_THROW(m_Download.reset());
}

TEST_F(QmPackagePreparationFailure, NullSecondPayloadLeavesEarlierRequestCancelled)
{
	Begin();
	m_NullCreateAt = 2;
	Probe();
	ExpectLocalFailure();
	ASSERT_EQ(m_vRequests.size(), 2u);
	EXPECT_TRUE(m_vRequests[1]->IsAbortRequested());
}

TEST_F(QmPackagePreparationFailure, LaterPayloadFactoryFailureRemovesLateCompletedFiles)
{
	Begin();
	m_FailCreateAt = 3;
	Probe();
	ExpectLocalFailure();
	ASSERT_EQ(m_vRequests.size(), 3u);
	for(size_t Index = 1; Index < m_vRequests.size(); ++Index)
	{
		EXPECT_TRUE(m_vRequests[Index]->IsAbortRequested());
		m_vRequests[Index]->Reply(206, "late", m_vRequests[Index]->Range());
	}
	ExpectNoPackageFiles();
	EXPECT_EQ(m_Download->State(), EHttpState::ERROR);
}

TEST_F(QmPackagePreparationFailure, PayloadStartFailureCancelsIncludingTheFailingRequest)
{
	Begin();
	m_FailStartAt = 2;
	Probe();
	ExpectLocalFailure();
	ASSERT_EQ(m_vRequests.size(), 3u);
	EXPECT_TRUE(m_vRequests[1]->IsAbortRequested());
	EXPECT_TRUE(m_vRequests[2]->IsAbortRequested());
}

TEST_F(QmPackagePreparationFailure, PayloadCapacityFailureHasTheSameTerminalCleanup)
{
	Begin();
	m_FailCreateAt = 2;
	m_LengthFailure = true;
	Probe();
	ExpectLocalFailure();
	ASSERT_EQ(m_vRequests.size(), 2u);
	EXPECT_TRUE(m_vRequests[1]->IsAbortRequested());
}

TEST_F(QmPackagePreparationFailure, UnknownStartExceptionStaysWithinThePackageOwner)
{
	Begin();
	m_FailStartAt = 2;
	m_UnknownFailure = true;
	Probe();
	ExpectLocalFailure();
}

TEST_F(QmPackagePreparationFailure, RetryFactoryFailureCancelsSiblingRequests)
{
	Begin();
	Probe();
	ASSERT_EQ(m_vRequests.size(), 5u);
	m_vRequests[1]->Reply(503, "", {}, EHttpState::ERROR);
	m_FailCreateAt = m_Creates;
	EXPECT_NO_THROW(m_Download->Poll(2));
	ExpectLocalFailure();
	for(size_t Index = 2; Index < m_vRequests.size(); ++Index)
		EXPECT_TRUE(m_vRequests[Index]->IsAbortRequested());
}

TEST_F(QmPackagePreparationFailure, WholeFileFallbackFactoryFailureTerminatesWithoutNullRequest)
{
	Begin();
	m_vRequests[0]->Reply(200, "", {}, EHttpState::ERROR);
	m_FailCreateAt = 1;
	EXPECT_NO_THROW(m_Download->Poll(1));
	ExpectLocalFailure();
	EXPECT_EQ(m_vRequests.size(), 1u);
}

TEST_F(QmPackagePreparationFailure, ThrowingCancellationDoesNotSkipOtherStartedParts)
{
	Begin();
	Probe();
	ASSERT_EQ(m_vRequests.size(), 5u);
	m_vRequests[1]->m_ThrowOnAbort = true;
	EXPECT_NO_THROW(m_Download->Abort());
	EXPECT_EQ(m_Download->State(), EHttpState::ABORTED);
	for(size_t Index = 1; Index < m_vRequests.size(); ++Index)
		EXPECT_TRUE(m_vRequests[Index]->IsAbortRequested());
	EXPECT_NO_THROW(m_Download.reset());
}

TEST_F(QmPackagePreparationFailure, RejectedMergeSubmissionPublishesFailureAndRemovesParts)
{
	Begin();
	Probe();
	FinishPayload();
	int Submits = 0;
	m_Submit = [&](std::shared_ptr<IJob>) { ++Submits; return false; };
	EXPECT_NO_THROW(m_Download->Poll(2));
	ExpectLocalFailure();
	EXPECT_EQ(Submits, 1);
	ExpectNoPackageFiles();
}

TEST_F(QmPackagePreparationFailure, ThrowingMergeSubmissionDoesNotLeaveMergeWaiting)
{
	Begin();
	Probe();
	FinishPayload();
	m_Submit = [](std::shared_ptr<IJob>) -> bool { throw std::bad_alloc(); };
	EXPECT_NO_THROW(m_Download->Poll(2));
	ExpectLocalFailure();
	ExpectNoPackageFiles();
}

TEST_F(QmPackagePreparationFailure, ClosedRealJobPoolRejectsMergeWithoutWaitingForDone)
{
	CJobPool ClosedPool;
	Begin();
	Probe();
	FinishPayload();
	m_Submit = [&](std::shared_ptr<IJob> Job) { return ClosedPool.TryAdd(std::move(Job)); };
	EXPECT_NO_THROW(m_Download->Poll(2));
	ExpectLocalFailure();
	ExpectNoPackageFiles();
}

TEST_F(QmPackagePreparationFailure, ANewPackageCanStartAfterLocalPreparationFailure)
{
	m_FailCreateAt = 0;
	Begin();
	ExpectLocalFailure();
	const auto Failed = m_Download;
	m_FailCreateAt = -1;
	Begin();
	EXPECT_EQ(m_Download->State(), EHttpState::RUNNING);
	EXPECT_FALSE(m_Download->LocalError());
	ASSERT_EQ(m_vRequests.size(), 1u);
	EXPECT_FALSE(m_vRequests[0]->IsAbortRequested());
	EXPECT_EQ(Failed->State(), EHttpState::ERROR);
}
