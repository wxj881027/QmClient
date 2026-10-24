#include <game/client/qm_ime_manager.h>

#include <gtest/gtest.h>
#include <test/support/qm_ime_test_input.h>

#include <new>
#include <string>
#include <vector>

namespace
{
	using EResult = CQmImePopupSnapshot::EUpdateResult;
}

TEST(QmImePopupPipeline, FailedPreparationKeepsTheCacheAndNextFrameCanReplaceIt)
{
	CQmImeCandidateState Candidates;
	Candidates.UpdateFromNative(SQmImeCandidateList{{"old page"}, 0, 5, 5, 13}, true);
	qm_ui_test::CTestImeInput Input(Candidates.Candidates());
	Input.m_Composition = "old composition";
	CQmImePopupSnapshot Snapshot;
	ASSERT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(20.0f, 30.0f), 18.0f)), EResult::VISIBLE);
	Candidates.UpdateFromNative(SQmImeCandidateList{{"new page", "new selection"}, 1, 10, 5, 23}, true);
	Input.m_Composition = "new composition";
	int Copies = 0;
	EXPECT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(40.0f, 50.0f), 20.0f, [&](const char *pText) {
		if(++Copies == 3)
			throw std::bad_alloc();
		return std::string(pText);
	})),
		EResult::UNAVAILABLE);
	EXPECT_EQ(Snapshot.State().m_vCandidates, (std::vector<std::string>{"old page"}));
	EXPECT_EQ(Snapshot.State().m_Composition, "old composition");
	EXPECT_EQ(Snapshot.State().m_PageIndex, 1);
	EXPECT_EQ(Snapshot.State().m_PageCount, 3);
	EXPECT_FLOAT_EQ(Snapshot.State().m_AnchorScreen.x, 20.0f);

	ASSERT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(40.0f, 50.0f), 20.0f)), EResult::VISIBLE);
	EXPECT_EQ(Snapshot.State().m_vCandidates, Candidates.Candidates().m_vCandidates);
	EXPECT_EQ(Snapshot.State().m_Composition, "new composition");
	EXPECT_EQ(Snapshot.State().m_SelectedIndex, 1);
	EXPECT_EQ(Snapshot.State().m_PageIndex, 2);
	EXPECT_EQ(Snapshot.State().m_PageCount, 5);
	EXPECT_FLOAT_EQ(Snapshot.State().m_AnchorScreen.x, 40.0f);
}

TEST(QmImePopupPipeline, CommitKeepsOwnedFadeTextUntilTheNextComposition)
{
	CQmImeCandidateState Candidates;
	const char *apOldCandidates[] = {"1 旧候选", "2 旧选中项"};
	Candidates.UpdateFromSdl(apOldCandidates, 2, 1, true);
	qm_ui_test::CTestImeInput Input(Candidates.Candidates());
	CQmImePopupSnapshot Snapshot;
	ASSERT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(20.0f, 30.0f), 18.0f)), EResult::VISIBLE);

	Candidates.EndComposition();
	ASSERT_TRUE(Candidates.Candidates().m_vCandidates.empty());
	EXPECT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(20.0f, 30.0f), 18.0f)), EResult::HIDDEN);
	EXPECT_EQ(Snapshot.State().m_vCandidates, (std::vector<std::string>{"旧候选", "旧选中项"}));
	EXPECT_EQ(Snapshot.State().m_SelectedIndex, 1);

	Candidates.ResumeComposition();
	const char *apNewCandidates[] = {"1 新候选"};
	Candidates.UpdateFromSdl(apNewCandidates, 1, 0, true);
	ASSERT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(40.0f, 50.0f), 18.0f)), EResult::VISIBLE);
	EXPECT_EQ(Snapshot.State().m_vCandidates, (std::vector<std::string>{"新候选"}));
	EXPECT_EQ(Snapshot.State().m_SelectedIndex, 0);
}

TEST(QmImePopupPipeline, ResetDropsTheOldOwnerBeforeANewInputSession)
{
	CQmImeCandidateState Candidates;
	Candidates.UpdateFromNative(SQmImeCandidateList{{"old owner"}, 0, 0, 1, 1}, true);
	qm_ui_test::CTestImeInput Input(Candidates.Candidates());
	CQmImePopupSnapshot Snapshot;
	ASSERT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(20.0f, 30.0f), 18.0f)), EResult::VISIBLE);
	Snapshot.Reset();
	Candidates.EndComposition();
	EXPECT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, false, vec2(0.0f, 0.0f), 0.0f)), EResult::HIDDEN);
	EXPECT_TRUE(Snapshot.State().m_vCandidates.empty());

	Candidates.ResumeComposition();
	Candidates.UpdateFromNative(SQmImeCandidateList{{"new owner"}, 0, 0, 1, 1}, true);
	ASSERT_EQ(Snapshot.Update(QmImeBuildPopupState(&Input, true, vec2(80.0f, 90.0f), 22.0f)), EResult::VISIBLE);
	EXPECT_EQ(Snapshot.State().m_vCandidates, (std::vector<std::string>{"new owner"}));
	EXPECT_FLOAT_EQ(Snapshot.State().m_AnchorScreen.x, 80.0f);
	EXPECT_FLOAT_EQ(Snapshot.State().m_LineHeightScreen, 22.0f);
}
