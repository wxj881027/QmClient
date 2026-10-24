#include <engine/shared/qm_ime_policy.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>
#include <string>
#include <vector>

TEST(QmImeCandidateFailure, SecondCopyFailureKeepsTheCompletePreviousPage)
{
	CQmImeCandidateState State;
	const SQmImeCandidateList Previous{{"旧候选", "旧选中项"}, 1, 10, 5, 23};
	State.UpdateFromNative(Previous, true);
	const char *apCandidates[] = {"1 新词", "2 第二项", "3 第三项"};
	int Copies = 0;

	EXPECT_NO_THROW(State.UpdateFromSdl(apCandidates, 3, 2, true, std::nullopt, [&](const char *pText) {
		if(++Copies == 2)
			throw std::bad_alloc();
		return std::string(pText);
	}));

	const auto &Result = State.Candidates();
	EXPECT_EQ(Copies, 2);
	EXPECT_EQ(Result.m_vCandidates, Previous.m_vCandidates);
	EXPECT_EQ(Result.m_SelectedIndex, 1);
	EXPECT_EQ(Result.m_PageStart, 10);
	EXPECT_EQ(Result.m_PageSize, 5);
	EXPECT_EQ(Result.m_TotalCount, 23);
	EXPECT_TRUE(State.AcceptsSdlCandidates());
}

TEST(QmImeCandidateFailure, FirstCopyExceptionsDoNotPublishAnIncompletePage)
{
	for(int Kind : {0, 1, 2})
	{
		SCOPED_TRACE(Kind);
		CQmImeCandidateState State;
		const char *apCandidates[] = {"候选"};
		EXPECT_NO_THROW(State.UpdateFromSdl(apCandidates, 1, 0, false, std::nullopt, [Kind](const char *) -> std::string {
			switch(Kind)
			{
			case 0: throw std::bad_alloc();
			case 1: throw std::length_error("SDL candidate copy");
			default: throw 7;
			}
		}));
		EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
		EXPECT_EQ(State.Candidates().m_SelectedIndex, -1);
		EXPECT_EQ(State.Candidates().m_PageStart, 0);
		EXPECT_EQ(State.Candidates().m_PageSize, 0);
		EXPECT_EQ(State.Candidates().m_TotalCount, 0);
		EXPECT_TRUE(State.AcceptsSdlCandidates());
	}
}

TEST(QmImeCandidateFailure, NextSnapshotReplacesThePageAfterACopyFailure)
{
	CQmImeCandidateState State;
	State.UpdateFromNative(SQmImeCandidateList{{"旧词"}, 0, 10, 5, 23}, true);
	const char *apCandidates[] = {"1 新词", "2 新选择"};
	State.UpdateFromSdl(apCandidates, 2, 0, true, std::nullopt, [](const char *) -> std::string { throw std::bad_alloc(); });

	State.UpdateFromSdl(apCandidates, 2, 1, true);

	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"新词", "新选择"}));
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 1);
	EXPECT_EQ(State.Candidates().m_PageStart, 0);
	EXPECT_EQ(State.Candidates().m_PageSize, 2);
	EXPECT_EQ(State.Candidates().m_TotalCount, 2);
}

TEST(QmImeCandidateFailure, EmptySnapshotStillClearsAfterACopyFailure)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"旧候选"};
	State.UpdateFromSdl(apCandidates, 1, 0, false);
	State.UpdateFromSdl(apCandidates, 1, 0, false, std::nullopt, [](const char *) -> std::string { throw std::bad_alloc(); });
	int Copies = 0;

	State.UpdateFromSdl(nullptr, 0, -1, false, std::nullopt, [&](const char *pText) {
		++Copies;
		return std::string(pText);
	});

	EXPECT_EQ(Copies, 0);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
	EXPECT_EQ(State.Candidates().m_SelectedIndex, -1);
	EXPECT_EQ(State.Candidates().m_PageSize, 0);
	EXPECT_EQ(State.Candidates().m_TotalCount, 0);
}

TEST(QmImeCandidateFailure, EndedCompositionSkipsCopyUntilResumed)
{
	CQmImeCandidateState State;
	State.EndComposition();
	const char *apCandidates[] = {"1 新候选"};
	int Copies = 0;
	const auto Copy = [&](const char *pText) {
		++Copies;
		return std::string(pText);
	};
	State.UpdateFromSdl(apCandidates, 1, 0, true, std::nullopt, Copy);
	EXPECT_EQ(Copies, 0);
	EXPECT_TRUE(State.Candidates().m_vCandidates.empty());
	EXPECT_FALSE(State.AcceptsSdlCandidates());

	State.ResumeComposition();
	State.UpdateFromSdl(apCandidates, 1, 0, true, std::nullopt, Copy);
	EXPECT_EQ(Copies, 1);
	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"新候选"}));
}

TEST(QmImeCandidateFailure, NativePageBypassesTheSdlCopyBoundary)
{
	CQmImeCandidateState State;
	const char *apCandidates[] = {"1 SDL候选"};
	int Copies = 0;
	State.UpdateFromSdl(apCandidates, 1, 0, true, SQmImeCandidateList{{"原生候选"}, 0, 8, 4, 19}, [&](const char *) -> std::string {
		++Copies;
		throw std::bad_alloc();
	});
	EXPECT_EQ(Copies, 0);
	EXPECT_EQ(State.Candidates().m_vCandidates, (std::vector<std::string>{"原生候选"}));
	EXPECT_EQ(State.Candidates().m_SelectedIndex, 0);
	EXPECT_EQ(State.Candidates().m_PageStart, 8);
	EXPECT_EQ(State.Candidates().m_PageSize, 4);
	EXPECT_EQ(State.Candidates().m_TotalCount, 19);
}
