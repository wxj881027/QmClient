// 原生 new 故障只在编解码/导入调用内启用；zlib 的 C 分配不在注入范围内。
#include <engine/client/friends.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/friend_code.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	constexpr char LOCAL_CATEGORY[] = "Local category with a persistent long name";
	constexpr char INCOMING_CATEGORY[] = "Imported category with a different long name";

	void FillLocal(CFriends &Friends)
	{
		Friends.AddCategory(LOCAL_CATEGORY);
		Friends.MoveCategory(Friends.FindCategory(LOCAL_CATEGORY), 0);
		Friends.AddFriend("Same", "Local", LOCAL_CATEGORY);
		Friends.SetFriendNote("Same", "Local", "local note kept during merge");
		Friends.SetFriendPinned("Same", "Local", true);
		Friends.AddFriend("Keep", "Clan");
	}

	std::unique_ptr<CFriends> LocalFriends()
	{
		auto pFriends = std::make_unique<CFriends>();
		FillLocal(*pFriends);
		return pFriends;
	}

	std::unique_ptr<CFriends> IncomingFriends()
	{
		auto pFriends = std::make_unique<CFriends>();
		pFriends->AddCategory(INCOMING_CATEGORY);
		pFriends->AddCategory("Empty incoming category with a long name");
		pFriends->AddFriend("Same", "Local", INCOMING_CATEGORY);
		pFriends->SetFriendNote("Same", "Local", "incoming duplicate note");
		pFriends->AddFriend("新好友", "Other", INCOMING_CATEGORY);
		pFriends->SetFriendNote("新好友", "Other", "导入备注 with saved pins");
		pFriends->SetFriendPinned("新好友", "Other", true);
		pFriends->AddFriend("", "WholeClan");
		return pFriends;
	}

	void ExpectSameList(const CFriendList &Actual, const CFriendList &Expected)
	{
		EXPECT_EQ(Actual.m_vCategories, Expected.m_vCategories);
		ASSERT_EQ(Actual.m_vFriends.size(), Expected.m_vFriends.size());
		for(size_t Index = 0; Index < Expected.m_vFriends.size(); ++Index)
		{
			SCOPED_TRACE(Index);
			const auto &Left = Actual.m_vFriends[Index];
			const auto &Right = Expected.m_vFriends[Index];
			EXPECT_STREQ(Left.m_aName, Right.m_aName);
			EXPECT_STREQ(Left.m_aClan, Right.m_aClan);
			EXPECT_STREQ(Left.m_aCategory, Right.m_aCategory);
			EXPECT_STREQ(Left.m_aNote, Right.m_aNote);
			EXPECT_EQ(Left.m_Pinned, Right.m_Pinned);
		}
	}

	struct SAllocationAttempt
	{
		bool m_Success;
		bool m_Triggered;
		size_t m_Attempts;
	};

	template<typename TOperation>
	SAllocationAttempt Attempt(size_t BeforeFailure, TOperation &&Operation)
	{
		CPredictionAllocationFailure Failure(BeforeFailure);
		const bool Success = std::forward<TOperation>(Operation)();
		return {Success, Failure.Triggered(), Failure.Attempts()};
	}

	class CQmFriendCodeAllocation : public testing::Test
	{
		int m_PreviousIgnoreClan = g_Config.m_ClFriendsIgnoreClan;

	protected:
		void SetUp() override { g_Config.m_ClFriendsIgnoreClan = 0; }
		void TearDown() override { g_Config.m_ClFriendsIgnoreClan = m_PreviousIgnoreClan; }

		void ExpectImportFailures(qm_friend_code::EImportMode Mode)
		{
			const auto pIncoming = IncomingFriends();
			const auto Incoming = qm_friend_code::Capture(*pIncoming);
			auto pBaseline = LocalFriends();
			const auto Full = Attempt(std::numeric_limits<size_t>::max(), [&] { return qm_friend_code::Import(*pBaseline, Incoming, Mode); });
			ASSERT_TRUE(Full.m_Success);
			ASSERT_FALSE(Full.m_Triggered);
			ASSERT_GT(Full.m_Attempts, 0u);
			const auto Expected = qm_friend_code::Capture(*pBaseline);
			for(size_t BeforeFailure = 0; BeforeFailure < Full.m_Attempts; ++BeforeFailure)
			{
				SCOPED_TRACE(BeforeFailure);
				auto pTarget = LocalFriends();
				const auto Previous = qm_friend_code::Capture(*pTarget);
				const auto Revision = pTarget->Revision();
				const auto Failed = Attempt(BeforeFailure, [&] { return qm_friend_code::Import(*pTarget, Incoming, Mode); });
				EXPECT_TRUE(Failed.m_Triggered);
				EXPECT_FALSE(Failed.m_Success);
				EXPECT_EQ(pTarget->Revision(), Revision);
				ExpectSameList(qm_friend_code::Capture(*pTarget), Previous);
				ASSERT_TRUE(qm_friend_code::Import(*pTarget, Incoming, Mode));
				EXPECT_EQ(pTarget->Revision(), Revision + 1);
				ExpectSameList(qm_friend_code::Capture(*pTarget), Expected);
			}
		}
	};

	class CLengthFailingCaptureFriends : public CFriends
	{
	public:
		bool m_FailCapture = false;
		const char *GetCategory(int Index) const override
		{
			if(m_FailCapture && Index == 1)
				throw std::length_error("capture category capacity");
			return CFriends::GetCategory(Index);
		}
	};

	class CThrowingCommitFriends : public CFriends
	{
		bool m_LengthError;

	public:
		explicit CThrowingCommitFriends(bool LengthError) :
			m_LengthError(LengthError) {}
		bool ReplaceAll(const CFriendList &List) override
		{
			if(!CFriends::ReplaceAll(List))
				return false;
			// 仅模拟另一实现的提交后异常；真实名单先由生产 ReplaceAll 完整发布。
			if(m_LengthError)
				throw std::length_error("after real friend publication");
			throw std::bad_alloc();
		}
	};
}

TEST_F(CQmFriendCodeAllocation, TryCaptureFailuresKeepThePreviousSnapshotAndAllowRetry)
{
	const auto pSource = IncomingFriends();
	const auto Expected = qm_friend_code::Capture(*pSource);
	const auto pPrevious = LocalFriends();
	const auto Previous = qm_friend_code::Capture(*pPrevious);
	const auto Revision = pSource->Revision();
	CFriendList Counted = Previous;
	const auto Full = Attempt(std::numeric_limits<size_t>::max(), [&] { return qm_friend_code::TryCapture(*pSource, Counted); });
	ASSERT_TRUE(Full.m_Success);
	ASSERT_GT(Full.m_Attempts, 0u);
	ExpectSameList(Counted, Expected);
	for(size_t BeforeFailure = 0; BeforeFailure < Full.m_Attempts; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CFriendList Output = Previous;
		const auto Failed = Attempt(BeforeFailure, [&] { return qm_friend_code::TryCapture(*pSource, Output); });
		EXPECT_TRUE(Failed.m_Triggered);
		EXPECT_FALSE(Failed.m_Success);
		ExpectSameList(Output, Previous);
		EXPECT_EQ(pSource->Revision(), Revision);
		ASSERT_TRUE(qm_friend_code::TryCapture(*pSource, Output));
		ExpectSameList(Output, Expected);
	}
}

TEST_F(CQmFriendCodeAllocation, EncodeFailuresKeepThePreviousCodeAndRetryTheSameFormat)
{
	const auto pSource = IncomingFriends();
	const auto Source = qm_friend_code::Capture(*pSource);
	std::string Expected;
	const auto Full = Attempt(std::numeric_limits<size_t>::max(), [&] { return qm_friend_code::Encode(Source, Expected); });
	ASSERT_TRUE(Full.m_Success);
	ASSERT_GT(Full.m_Attempts, 0u);
	ASSERT_TRUE(Expected.starts_with("QMF1:"));
	const std::string Previous = "previous complete exported friend code";
	for(size_t BeforeFailure = 0; BeforeFailure < Full.m_Attempts; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		std::string Code = Previous;
		const auto Failed = Attempt(BeforeFailure, [&] { return qm_friend_code::Encode(Source, Code); });
		EXPECT_TRUE(Failed.m_Triggered);
		EXPECT_FALSE(Failed.m_Success);
		EXPECT_EQ(Code, Previous);
		ASSERT_TRUE(qm_friend_code::Encode(Source, Code));
		EXPECT_EQ(Code, Expected);
		CFriendList Decoded;
		ASSERT_TRUE(qm_friend_code::Decode(Code, Decoded));
		ExpectSameList(Decoded, Source);
	}
}

TEST_F(CQmFriendCodeAllocation, DecodeFailuresKeepThePreviousListAndAllowRetry)
{
	const auto pSource = IncomingFriends();
	const auto Source = qm_friend_code::Capture(*pSource);
	std::string Code;
	ASSERT_TRUE(qm_friend_code::Encode(Source, Code));
	const auto pPrevious = LocalFriends();
	const auto Previous = qm_friend_code::Capture(*pPrevious);
	CFriendList Counted = Previous;
	const auto Full = Attempt(std::numeric_limits<size_t>::max(), [&] { return qm_friend_code::Decode(Code, Counted); });
	ASSERT_TRUE(Full.m_Success);
	ASSERT_GT(Full.m_Attempts, 0u);
	ExpectSameList(Counted, Source);
	for(size_t BeforeFailure = 0; BeforeFailure < Full.m_Attempts; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CFriendList Output = Previous;
		const auto Failed = Attempt(BeforeFailure, [&] { return qm_friend_code::Decode(Code, Output); });
		EXPECT_TRUE(Failed.m_Triggered);
		EXPECT_FALSE(Failed.m_Success);
		ExpectSameList(Output, Previous);
		ASSERT_TRUE(qm_friend_code::Decode(Code, Output));
		ExpectSameList(Output, Source);
	}
}

TEST_F(CQmFriendCodeAllocation, MergeFailuresKeepTheRealListAndRevisionUntilRetryCommits)
{
	ExpectImportFailures(qm_friend_code::EImportMode::MERGE);
}

TEST_F(CQmFriendCodeAllocation, ReplacementValidationFailuresKeepTheRealListAndRevisionUntilRetryCommits)
{
	ExpectImportFailures(qm_friend_code::EImportMode::REPLACE);
}

TEST_F(CQmFriendCodeAllocation, CaptureLengthFailureDoesNotPublishAnEmptyOrPartialSnapshot)
{
	auto pSource = std::make_unique<CLengthFailingCaptureFriends>();
	FillLocal(*pSource);
	const auto Expected = qm_friend_code::Capture(*pSource);
	const auto pPrevious = IncomingFriends();
	const auto Previous = qm_friend_code::Capture(*pPrevious);
	CFriendList Output = Previous;
	pSource->m_FailCapture = true;
	EXPECT_FALSE(qm_friend_code::TryCapture(*pSource, Output));
	ExpectSameList(Output, Previous);
	pSource->m_FailCapture = false;
	ASSERT_TRUE(qm_friend_code::TryCapture(*pSource, Output));
	ExpectSameList(Output, Expected);
}

TEST_F(CQmFriendCodeAllocation, MergeCaptureLengthFailureKeepsTheRealListAndRevision)
{
	auto pTarget = std::make_unique<CLengthFailingCaptureFriends>();
	FillLocal(*pTarget);
	const auto Previous = qm_friend_code::Capture(*pTarget);
	const auto Revision = pTarget->Revision();
	const auto pIncoming = IncomingFriends();
	const auto Incoming = qm_friend_code::Capture(*pIncoming);
	pTarget->m_FailCapture = true;
	EXPECT_FALSE(qm_friend_code::Import(*pTarget, Incoming));
	EXPECT_EQ(pTarget->Revision(), Revision);
	pTarget->m_FailCapture = false;
	ExpectSameList(qm_friend_code::Capture(*pTarget), Previous);
	ASSERT_TRUE(qm_friend_code::Import(*pTarget, Incoming));
	EXPECT_EQ(pTarget->Revision(), Revision + 1);
}

TEST_F(CQmFriendCodeAllocation, CommitExceptionsRemainVisibleAfterRealPublication)
{
	const auto pIncoming = IncomingFriends();
	const auto Incoming = qm_friend_code::Capture(*pIncoming);
	for(const auto Mode : {qm_friend_code::EImportMode::MERGE, qm_friend_code::EImportMode::REPLACE})
	{
		for(const bool LengthError : {false, true})
		{
			SCOPED_TRACE(static_cast<int>(Mode));
			SCOPED_TRACE(LengthError);
			auto pTarget = std::make_unique<CThrowingCommitFriends>(LengthError);
			FillLocal(*pTarget);
			const auto Revision = pTarget->Revision();
			if(LengthError)
				EXPECT_THROW(qm_friend_code::Import(*pTarget, Incoming, Mode), std::length_error);
			else
				EXPECT_THROW(qm_friend_code::Import(*pTarget, Incoming, Mode), std::bad_alloc);
			EXPECT_EQ(pTarget->Revision(), Revision + 1);
			EXPECT_TRUE(pTarget->IsFriend("新好友", "Other", true));
			EXPECT_EQ(pTarget->IsFriend("Keep", "Clan", true), Mode == qm_friend_code::EImportMode::MERGE);
		}
	}
}
