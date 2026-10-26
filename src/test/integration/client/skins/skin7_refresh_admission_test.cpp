#include <game/client/components/skins7.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/support/skin7_refresh_test_engine.h>
#include <test/test.h>

#include <algorithm>
#include <new>

namespace
{
	using TRequest = CQmSkin7RefreshRequest<CSkins7::CSkinPart>;

	class CQmSkin7RefreshAdmissionTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CSkin7RefreshTestEngine m_Engine;
		TRequest m_Request;
		int m_Callbacks = 0;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
			m_Request.Request([this] { ++m_Callbacks; });
		}

		std::optional<TRequest::SPlan> TryStart()
		{
			return m_Request.TryStart(m_Engine, m_pStorage.get(), CSkins7::SKINFLAG_STANDARD);
		}
	};
}

TEST_F(CQmSkin7RefreshAdmissionTest, AcceptedPlanOwnsRealJobEmptyPartsAndUncalledCompletion)
{
	auto Plan = TryStart();
	ASSERT_TRUE(Plan);
	EXPECT_EQ(Plan->m_pJob, m_Engine.m_pAccepted);
	EXPECT_FALSE(m_Request.IsPending());
	EXPECT_EQ(m_Callbacks, 0);
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
	{
		SCOPED_TRACE(Part);
		const auto &Parts = Plan->m_aParts[Part];
		if(Part == protocol7::SKINPART_MARKING || Part == protocol7::SKINPART_DECORATION)
		{
			ASSERT_EQ(Parts.size(), 1u);
			EXPECT_EQ(Parts[0].m_Type, Part);
			EXPECT_EQ(Parts[0].m_Flags, CSkins7::SKINFLAG_STANDARD);
			EXPECT_STREQ(Parts[0].m_aName, "");
			EXPECT_FALSE(Parts[0].m_OriginalTexture.IsValid());
			EXPECT_FALSE(Parts[0].m_ColorableTexture.IsValid());
			EXPECT_EQ(Parts[0].m_BloodColor.r, 1.0f);
		}
		else
			EXPECT_TRUE(Parts.empty());
	}
	ASSERT_TRUE(Plan->m_Callback);
	Plan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7RefreshAdmissionTest, FactoryExceptionKeepsRequestAndCompletionForRetry)
{
	EXPECT_FALSE(m_Request.TryStart(m_Engine, CSkins7::SKINFLAG_STANDARD, []() -> std::shared_ptr<CQmSkin7InfoJob> { throw std::bad_alloc(); }));
	EXPECT_TRUE(m_Request.IsPending());
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	EXPECT_EQ(m_Callbacks, 0);
	auto Plan = TryStart();
	ASSERT_TRUE(Plan);
	Plan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7RefreshAdmissionTest, NullCandidateDoesNotSubmitOrConsumeRequest)
{
	EXPECT_FALSE(m_Request.TryStart(m_Engine, CSkins7::SKINFLAG_STANDARD, [] { return std::shared_ptr<CQmSkin7InfoJob>{}; }));
	EXPECT_TRUE(m_Request.IsPending());
	EXPECT_EQ(m_Engine.m_Attempts, 0);
	EXPECT_TRUE(TryStart());
}

TEST_F(CQmSkin7RefreshAdmissionTest, RejectedAdmissionKeepsCallbackAndReleasesUnacceptedCandidate)
{
	m_Engine.m_Accept = false;
	std::weak_ptr<CQmSkin7InfoJob> Candidate;
	EXPECT_FALSE(m_Request.TryStart(m_Engine, CSkins7::SKINFLAG_STANDARD, [&] {
		auto pJob = std::make_shared<CQmSkin7InfoJob>(m_pStorage.get());
		Candidate = pJob;
		return pJob;
	}));
	EXPECT_TRUE(Candidate.expired());
	EXPECT_TRUE(m_Request.IsPending());
	EXPECT_EQ(m_Callbacks, 0);
	m_Engine.m_Accept = true;
	auto Plan = TryStart();
	ASSERT_TRUE(Plan);
	Plan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7RefreshAdmissionTest, AdmissionExceptionKeepsRequestForLaterAttempt)
{
	m_Engine.m_Throw = true;
	EXPECT_FALSE(TryStart());
	EXPECT_TRUE(m_Request.IsPending());
	EXPECT_EQ(m_Callbacks, 0);
	m_Engine.m_Throw = false;
	auto Plan = TryStart();
	ASSERT_TRUE(Plan);
	Plan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
}

TEST_F(CQmSkin7RefreshAdmissionTest, NewRequestReplacesRejectedRequestWithoutCallingOldCompletion)
{
	m_Engine.m_Accept = false;
	EXPECT_FALSE(TryStart());
	int NewCallbacks = 0;
	m_Request.Request([&] { ++NewCallbacks; });
	m_Engine.m_Accept = true;
	auto Plan = TryStart();
	ASSERT_TRUE(Plan);
	Plan->m_Callback();
	EXPECT_EQ(NewCallbacks, 1);
	EXPECT_EQ(m_Callbacks, 0);
}

TEST_F(CQmSkin7RefreshAdmissionTest, CancelledPendingRequestDoesNotCreateOrSubmitJobs)
{
	m_Engine.m_Accept = false;
	EXPECT_FALSE(TryStart());
	m_Request.Cancel();
	m_Engine.m_Accept = true;
	EXPECT_FALSE(TryStart());
	EXPECT_FALSE(m_Request.IsPending());
	EXPECT_EQ(m_Engine.m_Attempts, 1);
	EXPECT_EQ(m_Callbacks, 0);
}

TEST_F(CQmSkin7RefreshAdmissionTest, AcceptedRequestDoesNotResubmitOnFollowingFrames)
{
	ASSERT_TRUE(TryStart());
	EXPECT_FALSE(TryStart());
	EXPECT_FALSE(TryStart());
	EXPECT_EQ(m_Engine.m_Attempts, 1);
}

TEST_F(CQmSkin7RefreshAdmissionTest, AcceptedPlanKeepsOwnCompletionWhenNewRequestIsQueued)
{
	auto OldPlan = TryStart();
	ASSERT_TRUE(OldPlan);
	int NewCallbacks = 0;
	m_Request.Request([&] { ++NewCallbacks; });
	m_Engine.m_Accept = false;
	EXPECT_FALSE(TryStart());
	OldPlan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
	EXPECT_EQ(NewCallbacks, 0);
	m_Engine.m_Accept = true;
	auto NewPlan = TryStart();
	ASSERT_TRUE(NewPlan);
	NewPlan->m_Callback();
	EXPECT_EQ(NewCallbacks, 1);
}

TEST_F(CQmSkin7RefreshAdmissionTest, ClosedNativePoolRejectsWithoutLosingRequestAndCanRetryOnNewPool)
{
	CJobPool ClosedPool;
	m_Engine.m_pPool = &ClosedPool;
	EXPECT_FALSE(TryStart());
	EXPECT_TRUE(m_Request.IsPending());
	CJobPool OpenPool;
	ASSERT_TRUE(OpenPool.TryInit(1));
	m_Engine.m_pPool = &OpenPool;
	auto Plan = TryStart();
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	Plan->m_Callback();
	EXPECT_EQ(m_Callbacks, 1);
	OpenPool.Shutdown();
	m_Engine.m_pPool = nullptr;
}

TEST_F(CQmSkin7RefreshAdmissionTest, AcceptedNativeJobScansStorageAfterPreparedPlanIsReturned)
{
	IOHANDLE File = m_pStorage->OpenFile("skins7/qm_health_refresh.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const char *pJson = R"({"skin":{"body":{"filename":"standard"}}})";
	EXPECT_EQ(io_write(File, pJson, str_length(pJson)), static_cast<unsigned>(str_length(pJson)));
	ASSERT_EQ(io_close(File), 0);
	CJobPool Pool;
	ASSERT_TRUE(Pool.TryInit(1));
	m_Engine.m_pPool = &Pool;
	auto Plan = TryStart();
	ASSERT_TRUE(Plan);
	ASSERT_TRUE(WaitForJobDone(*Plan->m_pJob));
	const auto &Entries = Plan->m_pJob->Entries();
	const auto Entry = std::find_if(Entries.begin(), Entries.end(), [](const auto &Item) { return Item.m_Name == "qm_health_refresh"; });
	ASSERT_NE(Entry, Entries.end());
	EXPECT_STREQ((*Entry->m_pInfo)["skin"]["body"]["filename"], "standard");
	EXPECT_EQ(m_Callbacks, 0);
	Pool.Shutdown();
	m_Engine.m_pPool = nullptr;
}
