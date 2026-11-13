#include <game/client/QmUi/SettingsSearchReveal.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{
	struct SAttempt
	{
		bool m_Success;
		bool m_Triggered;
		bool m_Threw;
		size_t m_Allocations;
	};

	template<typename FAction>
	SAttempt WithFailure(size_t BeforeFailure, FAction &&Action)
	{
		CPredictionAllocationFailure Failure(BeforeFailure);
		bool Success = false;
		bool Threw = false;
		try
		{
			Success = Action();
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		return {Success, Failure.Triggered(), Threw, Failure.Attempts()};
	}

	struct SSnapshot
	{
		qm_settings_reveal::SState m_Value = qm_settings_reveal::State();
		const std::string *m_pKeys = qm_settings_reveal::State().m_vKeys.data();
		const qm_settings_reveal::SPreview *m_pPreviews = qm_settings_reveal::State().m_vPreviews.data();

		void ExpectUnchanged(bool PreviewDeadline = true) const
		{
			const auto &Current = qm_settings_reveal::State();
			EXPECT_EQ(Current.m_TargetCard, m_Value.m_TargetCard);
			EXPECT_EQ(Current.m_vKeys, m_Value.m_vKeys);
			EXPECT_EQ(Current.m_vKeys.data(), m_pKeys);
			EXPECT_EQ(Current.m_vPreviews.data(), m_pPreviews);
			ASSERT_EQ(Current.m_vPreviews.size(), m_Value.m_vPreviews.size());
			for(size_t Index = 0; Index < Current.m_vPreviews.size(); ++Index)
			{
				EXPECT_EQ(Current.m_vPreviews[Index].m_Card, m_Value.m_vPreviews[Index].m_Card);
				EXPECT_EQ(Current.m_vPreviews[Index].m_vKeys, m_Value.m_vPreviews[Index].m_vKeys);
			}
			EXPECT_EQ(Current.m_ActiveCard, m_Value.m_ActiveCard);
			EXPECT_EQ(Current.m_pActiveKeys, m_Value.m_pActiveKeys);
			EXPECT_EQ(Current.m_pScrollRegion, m_Value.m_pScrollRegion);
			EXPECT_EQ(Current.m_Interactive, m_Value.m_Interactive);
			EXPECT_EQ(Current.m_Scrolled, m_Value.m_Scrolled);
			EXPECT_EQ(Current.m_ExpireSeconds, m_Value.m_ExpireSeconds);
			if(PreviewDeadline)
				EXPECT_EQ(Current.m_PreviewExpireSeconds, m_Value.m_PreviewExpireSeconds);
		}
	};

	std::vector<qm_settings_reveal::SPreview> NewPreviews()
	{
		return {
			{std::string(96, 'a'), {"new-preview-key", std::string(180, 'n')}},
			{std::string(112, 'b'), {std::string(240, 'r'), "second-preview-key"}},
		};
	}

	class SettingsSearchRevealAllocation : public ::testing::Test
	{
	protected:
		CScrollRegion m_ScrollRegion;
		void SetUp() override { qm_settings_reveal::Clear(); }
		void TearDown() override { qm_settings_reveal::Clear(); }
		bool Seed()
		{
			qm_settings_reveal::Clear();
			if(!qm_settings_reveal::Request("old-target", {"request-key", std::string(120, 'q')}))
				return false;
			if(!qm_settings_reveal::SetPreviews({{"old-preview", {"preview-key", std::string(140, 'p')}}}))
				return false;
			auto &State = qm_settings_reveal::State();
			State.m_ExpireSeconds = std::numeric_limits<double>::infinity();
			State.m_PreviewExpireSeconds = std::numeric_limits<double>::infinity();
			State.m_Scrolled = true;
			return true;
		}
	};
}

TEST_F(SettingsSearchRevealAllocation, EveryRequestPreparationFailureKeepsTheOldRequestPreviewAndActiveScopeUntilRetry)
{
	const std::string Target(160, 'c');
	const std::vector<std::string> Keys{"new-request-key", std::string(180, 'n'), std::string(240, 'r')};
	ASSERT_TRUE(Seed());
	const auto Trace = WithFailure(std::numeric_limits<size_t>::max(), [&] { return qm_settings_reveal::Request(Target.c_str(), Keys); });
	ASSERT_TRUE(Trace.m_Success);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_FALSE(Trace.m_Threw);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 4096u);
	for(size_t Index = 0; Index < Trace.m_Allocations; ++Index)
	{
		SCOPED_TRACE(Index);
		ASSERT_TRUE(Seed());
		{
			const qm_settings_reveal::CCardScope Active("old-target", &m_ScrollRegion, true);
			const SSnapshot Previous;
			ASSERT_TRUE(qm_settings_reveal::ShouldReveal("request-key", nullptr));
			const auto Attempt = WithFailure(Index, [&] { return qm_settings_reveal::Request(Target.c_str(), Keys); });
			EXPECT_TRUE(Attempt.m_Triggered);
			EXPECT_FALSE(Attempt.m_Threw);
			EXPECT_FALSE(Attempt.m_Success);
			Previous.ExpectUnchanged();
			EXPECT_TRUE(qm_settings_reveal::ShouldReveal("request-key", nullptr));
		}
		EXPECT_EQ(qm_settings_reveal::State().m_pActiveKeys, nullptr);
		const double Before = qm_settings_reveal::NowSeconds();
		ASSERT_TRUE(qm_settings_reveal::Request(Target.c_str(), Keys));
		const double After = qm_settings_reveal::NowSeconds();
		const auto &State = qm_settings_reveal::State();
		EXPECT_EQ(State.m_TargetCard, Target);
		EXPECT_EQ(State.m_vKeys, Keys);
		EXPECT_TRUE(State.m_vPreviews.empty());
		EXPECT_EQ(State.m_PreviewExpireSeconds, 0.0);
		EXPECT_FALSE(State.m_Scrolled);
		EXPECT_GE(State.m_ExpireSeconds, Before + 5.0);
		EXPECT_LE(State.m_ExpireSeconds, After + 5.0);
		const qm_settings_reveal::CCardScope Retry(Target.c_str(), &m_ScrollRegion, true);
		EXPECT_TRUE(qm_settings_reveal::ShouldReveal("new-request-key", nullptr));
		EXPECT_FALSE(qm_settings_reveal::ShouldReveal("request-key", nullptr));
	}
}

TEST_F(SettingsSearchRevealAllocation, EveryPreviewPreparationFailureKeepsBorrowedOldKeysAndBothDeadlinesUntilRetry)
{
	const auto Previews = NewPreviews();
	ASSERT_TRUE(Seed());
	const auto Trace = WithFailure(std::numeric_limits<size_t>::max(), [&] { return qm_settings_reveal::SetPreviews(Previews); });
	ASSERT_TRUE(Trace.m_Success);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_FALSE(Trace.m_Threw);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 4096u);
	for(size_t Index = 0; Index < Trace.m_Allocations; ++Index)
	{
		SCOPED_TRACE(Index);
		ASSERT_TRUE(Seed());
		const auto OldKeys = qm_settings_reveal::State().m_vKeys;
		{
			const qm_settings_reveal::CCardScope Active("old-preview", &m_ScrollRegion, true);
			const SSnapshot Previous;
			ASSERT_TRUE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
			const auto Attempt = WithFailure(Index, [&] { return qm_settings_reveal::SetPreviews(Previews); });
			EXPECT_TRUE(Attempt.m_Triggered);
			EXPECT_FALSE(Attempt.m_Threw);
			EXPECT_FALSE(Attempt.m_Success);
			Previous.ExpectUnchanged();
			EXPECT_TRUE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
		}
		EXPECT_EQ(qm_settings_reveal::State().m_pActiveKeys, nullptr);
		const double Before = qm_settings_reveal::NowSeconds();
		ASSERT_TRUE(qm_settings_reveal::SetPreviews(Previews));
		const double After = qm_settings_reveal::NowSeconds();
		const auto &State = qm_settings_reveal::State();
		EXPECT_EQ(State.m_TargetCard, "old-target");
		EXPECT_EQ(State.m_vKeys, OldKeys);
		EXPECT_EQ(State.m_ExpireSeconds, std::numeric_limits<double>::infinity());
		EXPECT_TRUE(State.m_Scrolled);
		EXPECT_GE(State.m_PreviewExpireSeconds, Before + 1.0);
		EXPECT_LE(State.m_PreviewExpireSeconds, After + 1.0);
		const qm_settings_reveal::CCardScope Retry(Previews.front().m_Card.c_str(), nullptr, true);
		EXPECT_TRUE(qm_settings_reveal::ShouldReveal("new-preview-key", nullptr));
		EXPECT_FALSE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
	}
}

TEST_F(SettingsSearchRevealAllocation, IdenticalPreviewValuesRefreshExpiryWithoutAllocationOrRevokingTheActiveBorrow)
{
	ASSERT_TRUE(Seed());
	const auto Previews = qm_settings_reveal::State().m_vPreviews;
	const qm_settings_reveal::CCardScope Active("old-preview", nullptr, false);
	qm_settings_reveal::State().m_PreviewExpireSeconds = qm_settings_reveal::NowSeconds() - 1.0;
	const SSnapshot Previous;
	const double Before = qm_settings_reveal::NowSeconds();
	const auto Attempt = WithFailure(0, [&] { return qm_settings_reveal::SetPreviews(Previews); });
	const double After = qm_settings_reveal::NowSeconds();
	EXPECT_TRUE(Attempt.m_Success);
	EXPECT_FALSE(Attempt.m_Triggered);
	EXPECT_FALSE(Attempt.m_Threw);
	EXPECT_EQ(Attempt.m_Allocations, 0u);
	Previous.ExpectUnchanged(false);
	EXPECT_GE(qm_settings_reveal::State().m_PreviewExpireSeconds, Before + 1.0);
	EXPECT_LE(qm_settings_reveal::State().m_PreviewExpireSeconds, After + 1.0);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
}

TEST_F(SettingsSearchRevealAllocation, SuccessfulReplacementRevokesTheOldPreviewBorrowBeforeTheNextCard)
{
	for(const bool Request : {false, true})
	{
		SCOPED_TRACE(Request);
		ASSERT_TRUE(Seed());
		const auto Previews = NewPreviews();
		{
			const qm_settings_reveal::CCardScope Active("old-preview", nullptr, true);
			ASSERT_TRUE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
			if(Request)
				ASSERT_TRUE(qm_settings_reveal::Request("new-target", {"new-request-key"}));
			else
				ASSERT_TRUE(qm_settings_reveal::SetPreviews(Previews));
			EXPECT_EQ(qm_settings_reveal::State().m_pActiveKeys, nullptr);
			EXPECT_TRUE(qm_settings_reveal::State().m_ActiveCard.empty());
			EXPECT_EQ(qm_settings_reveal::State().m_pScrollRegion, nullptr);
			EXPECT_FALSE(qm_settings_reveal::State().m_Interactive);
			EXPECT_FALSE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
		}
		const qm_settings_reveal::CCardScope Retry(Request ? "new-target" : Previews.front().m_Card.c_str(), nullptr, true);
		EXPECT_TRUE(qm_settings_reveal::ShouldReveal(Request ? "new-request-key" : "new-preview-key", nullptr));
	}
}

TEST_F(SettingsSearchRevealAllocation, InvalidRequestsReturnFalseWithoutAllocationOrChangingExistingState)
{
	ASSERT_TRUE(Seed());
	const std::vector<std::string> Keys{"new-key"};
	const std::vector<std::string> EmptyKeys;
	struct SInput
	{
		const char *m_pCard;
		const std::vector<std::string> *m_pKeys;
	};
	const SInput aInputs[] = {{nullptr, &Keys}, {"", &Keys}, {"new-card", &EmptyKeys}};
	const qm_settings_reveal::CCardScope Active("old-preview", nullptr, false);
	const SSnapshot Previous;
	for(const auto &Input : aInputs)
	{
		SCOPED_TRACE(Input.m_pCard != nullptr ? Input.m_pCard : "<null>");
		SCOPED_TRACE(Input.m_pKeys->size());
		const auto Attempt = WithFailure(0, [&] { return qm_settings_reveal::Request(Input.m_pCard, *Input.m_pKeys); });
		EXPECT_FALSE(Attempt.m_Success);
		EXPECT_FALSE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_Threw);
		EXPECT_EQ(Attempt.m_Allocations, 0u);
		Previous.ExpectUnchanged();
		EXPECT_TRUE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
	}
}

TEST_F(SettingsSearchRevealAllocation, EmptyPreviewPublicationKeepsTheRequestAndRepeatedEmptyValuesDoNotAllocate)
{
	ASSERT_TRUE(Seed());
	const auto Keys = qm_settings_reveal::State().m_vKeys;
	const std::vector<qm_settings_reveal::SPreview> Empty;
	{
		const qm_settings_reveal::CCardScope Active("old-preview", nullptr, false);
		ASSERT_TRUE(qm_settings_reveal::SetPreviews(Empty));
		EXPECT_EQ(qm_settings_reveal::State().m_pActiveKeys, nullptr);
		EXPECT_FALSE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
	}
	EXPECT_EQ(qm_settings_reveal::State().m_TargetCard, "old-target");
	EXPECT_EQ(qm_settings_reveal::State().m_vKeys, Keys);
	EXPECT_EQ(qm_settings_reveal::State().m_ExpireSeconds, std::numeric_limits<double>::infinity());
	EXPECT_EQ(qm_settings_reveal::State().m_PreviewExpireSeconds, 0.0);
	EXPECT_TRUE(qm_settings_reveal::State().m_vPreviews.empty());
	EXPECT_TRUE(qm_settings_reveal::State().m_Scrolled);
	const auto Attempt = WithFailure(0, [&] { return qm_settings_reveal::SetPreviews(Empty); });
	EXPECT_TRUE(Attempt.m_Success);
	EXPECT_FALSE(Attempt.m_Triggered);
	EXPECT_FALSE(Attempt.m_Threw);
	EXPECT_EQ(Attempt.m_Allocations, 0u);
	const qm_settings_reveal::CCardScope Retry("old-target", &m_ScrollRegion, true);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("request-key", nullptr));
}

TEST_F(SettingsSearchRevealAllocation, EmptyKeyStringsAndPreviewEntriesKeepTheirExistingMatchingSemantics)
{
	ASSERT_TRUE(qm_settings_reveal::Request("empty-key-target", {""}));
	qm_settings_reveal::State().m_ExpireSeconds = std::numeric_limits<double>::infinity();
	ASSERT_TRUE(qm_settings_reveal::Pending());
	{
		const qm_settings_reveal::CCardScope EmptyRequest("empty-key-target", nullptr, true);
		EXPECT_FALSE(qm_settings_reveal::ShouldReveal("", ""));
	}
	const std::vector<qm_settings_reveal::SPreview> Previews{{"", {}}, {"empty-key-preview", {}}, {"mixed", {"", "usable-key"}}};
	ASSERT_TRUE(qm_settings_reveal::SetPreviews(Previews));
	ASSERT_EQ(qm_settings_reveal::State().m_vPreviews.size(), Previews.size());
	EXPECT_TRUE(qm_settings_reveal::State().m_vPreviews.front().m_Card.empty());
	EXPECT_TRUE(qm_settings_reveal::State().m_vPreviews[1].m_vKeys.empty());
	const qm_settings_reveal::CCardScope Mixed("mixed", nullptr, false);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("usable-key", nullptr));
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("", ""));
}

TEST_F(SettingsSearchRevealAllocation, PreparedRequestPublishesWithoutAllocationAndRevokesTheOldBorrow)
{
	ASSERT_TRUE(Seed());
	const std::string ExpectedTarget(160, 't');
	const std::vector<std::string> ExpectedKeys{std::string(240, 'k'), "prepared-key"};
	std::string Target = ExpectedTarget;
	std::vector<std::string> Keys = ExpectedKeys;
	{
		const qm_settings_reveal::CCardScope Active("old-preview", &m_ScrollRegion, true);
		const auto Attempt = WithFailure(0, [&] { return qm_settings_reveal::PublishRequest(std::move(Target), std::move(Keys)); });
		EXPECT_TRUE(Attempt.m_Success);
		EXPECT_FALSE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_Threw);
		EXPECT_EQ(Attempt.m_Allocations, 0u);
		EXPECT_EQ(qm_settings_reveal::State().m_pActiveKeys, nullptr);
		EXPECT_FALSE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
	}
	const auto &State = qm_settings_reveal::State();
	EXPECT_EQ(State.m_TargetCard, ExpectedTarget);
	EXPECT_EQ(State.m_vKeys, ExpectedKeys);
	EXPECT_TRUE(State.m_vPreviews.empty());
	EXPECT_FALSE(State.m_Scrolled);
	const qm_settings_reveal::CCardScope Retry(ExpectedTarget.c_str(), &m_ScrollRegion, true);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("prepared-key", nullptr));
}

TEST_F(SettingsSearchRevealAllocation, InvalidPreparedRequestsPreserveTheExistingRequestAndActivePreview)
{
	ASSERT_TRUE(Seed());
	const qm_settings_reveal::CCardScope Active("old-preview", nullptr, false);
	const SSnapshot Previous;
	for(const bool EmptyTarget : {false, true})
	{
		SCOPED_TRACE(EmptyTarget);
		std::string Target = EmptyTarget ? "" : std::string(160, 't');
		std::vector<std::string> Keys;
		if(EmptyTarget)
			Keys.emplace_back(240, 'k');
		const auto Attempt = WithFailure(0, [&] { return qm_settings_reveal::PublishRequest(std::move(Target), std::move(Keys)); });
		EXPECT_FALSE(Attempt.m_Success);
		EXPECT_FALSE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_Threw);
		EXPECT_EQ(Attempt.m_Allocations, 0u);
		Previous.ExpectUnchanged();
		EXPECT_TRUE(qm_settings_reveal::ShouldReveal("preview-key", nullptr));
	}
}

TEST_F(SettingsSearchRevealAllocation, FirstLongRequestActivationFailureSkipsHighlightAndScrollUntilRetryThenReusesCapacity)
{
	const std::string Target(160, 't');
	ASSERT_TRUE(qm_settings_reveal::Request(Target.c_str(), {"request-key"}));
	ASSERT_TRUE(qm_settings_reveal::SetPreviews({{"other-preview", {"preview-key"}}}));
	auto &State = qm_settings_reveal::State();
	State.m_ExpireSeconds = std::numeric_limits<double>::infinity();
	State.m_PreviewExpireSeconds = std::numeric_limits<double>::infinity();
	// 不依赖其他用例留下的容量，注入首次激活长名称的真实分配。
	qm_settings_reveal::EndCard();
	std::string().swap(State.m_ActiveCard);
	const SSnapshot Previous;
	const CUIRect Row{0.0f, 0.0f, 100.0f, 20.0f};
	bool UnboundAfterFailure = false;
	const auto Attempt = WithFailure(0, [&] {
		const qm_settings_reveal::CCardScope Active(Target.c_str(), &m_ScrollRegion, true);
		UnboundAfterFailure = State.m_ActiveCard.empty() && State.m_pActiveKeys == nullptr && State.m_pScrollRegion == nullptr && !State.m_Interactive;
		return qm_settings_reveal::OnRow("request-key", nullptr, Row);
	});
	EXPECT_TRUE(Attempt.m_Triggered);
	EXPECT_FALSE(Attempt.m_Threw);
	EXPECT_FALSE(Attempt.m_Success);
	EXPECT_TRUE(UnboundAfterFailure);
	Previous.ExpectUnchanged();
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("request-key", nullptr));
	EXPECT_FALSE(State.m_Scrolled);
	bool BoundForScroll = false;
	const auto Retry = WithFailure(std::numeric_limits<size_t>::max(), [&] {
		const qm_settings_reveal::CCardScope Active(Target.c_str(), &m_ScrollRegion, true);
		BoundForScroll = State.m_pActiveKeys == &State.m_vKeys && State.m_pScrollRegion == &m_ScrollRegion && State.m_Interactive;
		return qm_settings_reveal::ShouldReveal("request-key", nullptr);
	});
	EXPECT_TRUE(Retry.m_Success);
	EXPECT_FALSE(Retry.m_Triggered);
	EXPECT_FALSE(Retry.m_Threw);
	EXPECT_GT(Retry.m_Allocations, 0u);
	EXPECT_TRUE(BoundForScroll);
	Previous.ExpectUnchanged();
	const auto Repeated = WithFailure(0, [&] {
		const qm_settings_reveal::CCardScope Active(Target.c_str(), &m_ScrollRegion, true);
		return qm_settings_reveal::ShouldReveal("request-key", nullptr);
	});
	EXPECT_TRUE(Repeated.m_Success);
	EXPECT_FALSE(Repeated.m_Triggered);
	EXPECT_FALSE(Repeated.m_Threw);
	EXPECT_EQ(Repeated.m_Allocations, 0u);
	Previous.ExpectUnchanged();
}

TEST_F(SettingsSearchRevealAllocation, FirstLongPreviewActivationFailureKeepsRequestsAndDeadlinesUntilRetryThenReusesCapacity)
{
	ASSERT_TRUE(Seed());
	const std::string PreviewCard(160, 'p');
	ASSERT_TRUE(qm_settings_reveal::SetPreviews({{PreviewCard, {"long-preview-key"}}, {"other-preview", {"other-key"}}}));
	auto &State = qm_settings_reveal::State();
	State.m_PreviewExpireSeconds = std::numeric_limits<double>::infinity();
	State.m_Scrolled = false;
	qm_settings_reveal::EndCard();
	std::string().swap(State.m_ActiveCard);
	const SSnapshot Previous;
	const CUIRect Row{0.0f, 0.0f, 100.0f, 20.0f};
	bool UnboundAfterFailure = false;
	const auto Attempt = WithFailure(0, [&] {
		const qm_settings_reveal::CCardScope Active(PreviewCard.c_str(), &m_ScrollRegion, true);
		UnboundAfterFailure = State.m_ActiveCard.empty() && State.m_pActiveKeys == nullptr && State.m_pScrollRegion == nullptr && !State.m_Interactive;
		return qm_settings_reveal::OnRow("long-preview-key", nullptr, Row);
	});
	EXPECT_TRUE(Attempt.m_Triggered);
	EXPECT_FALSE(Attempt.m_Threw);
	EXPECT_FALSE(Attempt.m_Success);
	EXPECT_TRUE(UnboundAfterFailure);
	Previous.ExpectUnchanged();
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("long-preview-key", nullptr));
	EXPECT_FALSE(State.m_Scrolled);
	bool BoundAsPreview = false;
	const auto Retry = WithFailure(std::numeric_limits<size_t>::max(), [&] {
		const qm_settings_reveal::CCardScope Active(PreviewCard.c_str(), &m_ScrollRegion, true);
		BoundAsPreview = State.m_pActiveKeys == &State.m_vPreviews.front().m_vKeys && State.m_pScrollRegion == nullptr && !State.m_Interactive;
		return qm_settings_reveal::OnRow("long-preview-key", nullptr, Row);
	});
	EXPECT_TRUE(Retry.m_Success);
	EXPECT_FALSE(Retry.m_Triggered);
	EXPECT_FALSE(Retry.m_Threw);
	EXPECT_GT(Retry.m_Allocations, 0u);
	EXPECT_TRUE(BoundAsPreview);
	Previous.ExpectUnchanged();
	const auto Repeated = WithFailure(0, [&] {
		const qm_settings_reveal::CCardScope Active(PreviewCard.c_str(), &m_ScrollRegion, true);
		return qm_settings_reveal::OnRow("long-preview-key", nullptr, Row);
	});
	EXPECT_TRUE(Repeated.m_Success);
	EXPECT_FALSE(Repeated.m_Triggered);
	EXPECT_FALSE(Repeated.m_Threw);
	EXPECT_EQ(Repeated.m_Allocations, 0u);
	Previous.ExpectUnchanged();
}
