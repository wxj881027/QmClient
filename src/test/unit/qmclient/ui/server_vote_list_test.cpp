#include <base/str.h>

#include <game/client/components/qmclient/server_vote_list.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	struct COptions
	{
		std::vector<CVoteOptionClient> m_vOptions;

		COptions(std::initializer_list<const char *> Descriptions) :
			m_vOptions(Descriptions.size())
		{
			size_t Index = 0;
			for(const char *pDescription : Descriptions)
			{
				auto &Option = m_vOptions[Index];
				str_copy(Option.m_aDescription, pDescription);
				Option.m_pNext = Index + 1 < m_vOptions.size() ? &m_vOptions[Index + 1] : nullptr;
				++Index;
			}
		}

		const CVoteOptionClient *First() const { return m_vOptions.data(); }
	};

	class CFailingPreparation : public CQmServerVoteList::CPreparationOperations
	{
	public:
		enum class EStage
		{
			NONE,
			SEARCH,
			OPTION,
			VISIBLE,
		};
		EStage m_FailStage = EStage::NONE;
		int m_FailAt = 1;
		bool m_After = false;
		int m_Exception = 0;
		int m_SearchCalls = 0;
		int m_OptionCalls = 0;
		int m_VisibleCalls = 0;

		void FailAt(EStage Stage, int Call, bool After)
		{
			m_FailStage = Stage;
			m_FailAt = Call;
			m_After = After;
			m_SearchCalls = m_OptionCalls = m_VisibleCalls = 0;
		}
		void Check(EStage Stage, int Call, bool After)
		{
			if(m_FailStage != Stage || Call != m_FailAt || After != m_After)
				return;
			if(m_Exception == 1)
				throw std::length_error("vote list preparation");
			if(m_Exception == 2)
				throw 7;
			throw std::bad_alloc();
		}
		void AssignSearch(std::string &Search, const char *pSearch) override
		{
			const int Call = ++m_SearchCalls;
			Check(EStage::SEARCH, Call, false);
			CPreparationOperations::AssignSearch(Search, pSearch);
			Check(EStage::SEARCH, Call, true);
		}
		void AppendOption(std::vector<std::string> &vOptions, const char *pDescription) override
		{
			const int Call = ++m_OptionCalls;
			Check(EStage::OPTION, Call, false);
			CPreparationOperations::AppendOption(vOptions, pDescription);
			Check(EStage::OPTION, Call, true);
		}
		void AppendVisible(std::vector<int> &vVisible, int Index) override
		{
			const int Call = ++m_VisibleCalls;
			Check(EStage::VISIBLE, Call, false);
			CPreparationOperations::AppendVisible(vVisible, Index);
			Check(EStage::VISIBLE, Call, true);
		}
	};
}

TEST(QmServerVoteList, UnfilteredListKeepsServerOrderAndNonMapOptionsVerbatim)
{
	COptions Options{"=== 游戏菜单 ===", "开始游戏", "个人信息", "商店", "Axiom map", "商店"};
	CQmServerVoteList List;
	ASSERT_TRUE(List.Update(Options.First(), 1, ""));
	EXPECT_EQ(List.Visible(), (std::vector<int>{0, 1, 2, 3, 4, 5}));
	EXPECT_EQ(List.Options(), (std::vector<std::string>{"=== 游戏菜单 ===", "开始游戏", "个人信息", "商店", "Axiom map", "商店"}));
	List.Select(5);
	EXPECT_EQ(List.SelectedOption(1), 5);
}

TEST(QmServerVoteList, SearchKeepsOriginalOptionIndexAndSelectionWhenStillVisible)
{
	COptions Options{"Start game", "Profile", "Map: Alpha", "Player profile"};
	CQmServerVoteList List;
	List.Update(Options.First(), 7, "");
	List.Select(3);
	const auto *pDescriptions = List.Options().data();
	ASSERT_TRUE(List.Update(Options.First(), 7, "PROFILE"));
	EXPECT_EQ(List.Options().data(), pDescriptions);
	EXPECT_EQ(List.Visible(), (std::vector<int>{1, 3}));
	EXPECT_EQ(List.SelectedRow(), 1);
	EXPECT_EQ(List.SelectedOption(7), 3);
	List.Select(0);
	EXPECT_EQ(List.SelectedOption(7), 1);
	List.Update(Options.First(), 7, "");
	EXPECT_EQ(List.SelectedRow(), 1);
}

TEST(QmServerVoteList, SearchClearsHiddenSelectionAndCanBeCleared)
{
	COptions Options{"开始游戏", "个人信息", "商店"};
	CQmServerVoteList List;
	List.Update(Options.First(), 1, "");
	List.Select(0);
	List.Update(Options.First(), 1, "信息");
	EXPECT_EQ(List.Visible(), (std::vector<int>{1}));
	EXPECT_EQ(List.SelectedOption(1), -1);
	List.Update(Options.First(), 1, "missing");
	EXPECT_TRUE(List.Visible().empty());
	List.Update(Options.First(), 1, "");
	EXPECT_EQ(List.Visible().size(), 3u);
	EXPECT_EQ(List.SelectedRow(), -1);
}

TEST(QmServerVoteList, SameRevisionKeepsSelectionButReplacementInvalidatesOldTarget)
{
	COptions Options{"Start game", "Profile"};
	CQmServerVoteList List;
	List.Update(Options.First(), 4, "");
	List.Select(1);
	EXPECT_FALSE(List.Update(Options.First(), 4, ""));
	EXPECT_EQ(List.SelectedOption(4), 1);

	// 服务器菜单同位置换成另一个动作时，提交前也必须拒绝旧索引。
	str_copy(Options.m_vOptions[1].m_aDescription, "Buy item");
	EXPECT_EQ(List.SelectedOption(5), -1);
	ASSERT_TRUE(List.Update(Options.First(), 5, ""));
	EXPECT_EQ(List.SelectedRow(), -1);
	List.Select(1);
	EXPECT_EQ(List.SelectedOption(5), 1);
	EXPECT_EQ(List.Options()[1], "Buy item");
}

TEST(QmServerVoteList, ClearingAndReceivingNewOptionsDoesNotReuseSelection)
{
	COptions Options{"Start game", "Profile"};
	CQmServerVoteList List;
	List.Update(Options.First(), 1, "");
	List.Select(0);
	List.Update(nullptr, 2, "");
	EXPECT_TRUE(List.Options().empty());
	EXPECT_TRUE(List.Visible().empty());
	EXPECT_EQ(List.SelectedOption(2), -1);
	List.Update(Options.First(), 3, "");
	EXPECT_EQ(List.SelectedRow(), -1);
	List.Select(0);
	EXPECT_EQ(List.SelectedOption(3), 0);
	List.Select(-1);
	EXPECT_EQ(List.SelectedOption(3), -1);
	List.Select(2);
	EXPECT_EQ(List.SelectedOption(3), -1);
}

TEST(QmServerVoteList, CachedDescriptionsRemainValidWhenServerStorageChanges)
{
	CQmServerVoteList List;
	{
		COptions Options{"Profile"};
		List.Update(Options.First(), 1, "");
	}
	ASSERT_EQ(List.Options().size(), 1u);
	EXPECT_EQ(List.Options()[0], "Profile");
}

TEST(QmServerVoteList, InitialPreparationFailureDoesNotPublishPartialTextOrIndices)
{
	using EStage = CFailingPreparation::EStage;
	COptions Options{"Start", "Profile", "Shop"};
	for(EStage Stage : {EStage::SEARCH, EStage::OPTION, EStage::VISIBLE})
		for(bool After : {false, true})
		{
			SCOPED_TRACE(static_cast<int>(Stage));
			SCOPED_TRACE(After);
			CFailingPreparation Operations;
			Operations.FailAt(Stage, Stage == EStage::SEARCH ? 1 : 2, After);
			CQmServerVoteList List(&Operations);
			EXPECT_FALSE(List.Update(Options.First(), 1, ""));
			EXPECT_TRUE(List.Options().empty());
			EXPECT_TRUE(List.Visible().empty());
			EXPECT_EQ(List.SelectedOption(1), -1);
			EXPECT_TRUE(List.NeedsUpdate(1, ""));
			Operations.m_FailStage = EStage::NONE;
			ASSERT_TRUE(List.Update(Options.First(), 1, ""));
			EXPECT_EQ(List.Options().size(), 3u);
			EXPECT_EQ(List.Visible(), (std::vector<int>{0, 1, 2}));
		}
}

TEST(QmServerVoteList, ReplacementCopyFailurePreservesOwnedRowsAndRejectsNewRevisionTarget)
{
	COptions Old{"Start", "Profile"}, New{"Other", "Shop", "Back"};
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	ASSERT_TRUE(List.Update(Old.First(), 1, ""));
	List.Select(1);
	const auto *pDescriptions = List.Options().data();
	Operations.FailAt(CFailingPreparation::EStage::OPTION, 2, true);
	EXPECT_FALSE(List.Update(New.First(), 2, ""));
	EXPECT_EQ(List.Options().data(), pDescriptions);
	EXPECT_EQ(List.Options(), (std::vector<std::string>{"Start", "Profile"}));
	EXPECT_EQ(List.Visible(), (std::vector<int>{0, 1}));
	EXPECT_EQ(List.SelectedRow(), 1);
	EXPECT_EQ(List.SelectedOption(1), 1);
	EXPECT_EQ(List.SelectedOption(2), -1);
	EXPECT_TRUE(List.NeedsUpdate(2, ""));
	Operations.m_FailStage = CFailingPreparation::EStage::NONE;
	ASSERT_TRUE(List.Update(New.First(), 2, ""));
	EXPECT_EQ(List.Options(), (std::vector<std::string>{"Other", "Shop", "Back"}));
	EXPECT_EQ(List.SelectedRow(), -1);
}

TEST(QmServerVoteList, SearchProjectionFailureKeepsSelectionAndRetriesWithoutRecopyingOptions)
{
	COptions Options{"Start", "Profile", "Player profile"};
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	ASSERT_TRUE(List.Update(Options.First(), 1, ""));
	List.Select(0);
	Operations.FailAt(CFailingPreparation::EStage::VISIBLE, 1, true);
	EXPECT_FALSE(List.Update(Options.First(), 1, "profile"));
	EXPECT_EQ(List.SelectedRow(), 0);
	EXPECT_EQ(List.Visible(), (std::vector<int>{0, 1, 2}));
	EXPECT_EQ(Operations.m_OptionCalls, 0);
	EXPECT_TRUE(List.NeedsUpdate(1, "profile"));
	Operations.m_FailStage = CFailingPreparation::EStage::NONE;
	ASSERT_TRUE(List.Update(nullptr, 1, "profile"));
	EXPECT_EQ(Operations.m_OptionCalls, 0);
	EXPECT_EQ(List.Visible(), (std::vector<int>{1, 2}));
	EXPECT_EQ(List.SelectedRow(), -1);
}

TEST(QmServerVoteList, SearchAssignmentExceptionsLeaveThePreviousRequestIntact)
{
	for(int Exception = 0; Exception < 3; ++Exception)
		for(bool After : {false, true})
		{
			SCOPED_TRACE(Exception);
			SCOPED_TRACE(After);
			COptions Options{"Start", "Profile"};
			CFailingPreparation Operations;
			CQmServerVoteList List(&Operations);
			ASSERT_TRUE(List.Update(Options.First(), 1, ""));
			List.Select(1);
			Operations.m_Exception = Exception;
			Operations.FailAt(CFailingPreparation::EStage::SEARCH, 1, After);
			const char *pSearch = "a search long enough to require owned string storage";
			EXPECT_FALSE(List.Update(Options.First(), 1, pSearch));
			EXPECT_FALSE(List.NeedsUpdate(1, ""));
			EXPECT_TRUE(List.NeedsUpdate(1, pSearch));
			EXPECT_EQ(List.SelectedOption(1), 1);
			EXPECT_EQ(Operations.m_OptionCalls, 0);
			EXPECT_EQ(Operations.m_VisibleCalls, 0);
		}
}

TEST(QmServerVoteList, NewOptionProjectionFailureDoesNotCommitTheNewRevision)
{
	COptions Old{"Start", "Profile"}, New{"Shop", "Player profile"};
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	ASSERT_TRUE(List.Update(Old.First(), 1, ""));
	List.Select(1);
	Operations.FailAt(CFailingPreparation::EStage::VISIBLE, 1, true);
	EXPECT_FALSE(List.Update(New.First(), 2, "profile"));
	EXPECT_EQ(Operations.m_OptionCalls, 2);
	EXPECT_EQ(List.Options()[1], "Profile");
	EXPECT_EQ(List.Visible(), (std::vector<int>{0, 1}));
	EXPECT_EQ(List.SelectedOption(2), -1);
	Operations.m_FailStage = CFailingPreparation::EStage::NONE;
	ASSERT_TRUE(List.Update(New.First(), 2, "profile"));
	EXPECT_EQ(List.Options()[1], "Player profile");
	EXPECT_EQ(List.Visible(), (std::vector<int>{1}));
	EXPECT_EQ(List.SelectedRow(), -1);
}

TEST(QmServerVoteList, FailedClearKeepsOldSnapshotUntilTheEmptyReplacementIsPrepared)
{
	COptions Options{"Start", "Profile"};
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	ASSERT_TRUE(List.Update(Options.First(), 1, ""));
	List.Select(1);
	Operations.FailAt(CFailingPreparation::EStage::SEARCH, 1, true);
	EXPECT_FALSE(List.Update(nullptr, 2, ""));
	EXPECT_EQ(List.Options().size(), 2u);
	EXPECT_EQ(List.SelectedOption(2), -1);
	Operations.m_FailStage = CFailingPreparation::EStage::NONE;
	ASSERT_TRUE(List.Update(nullptr, 2, ""));
	EXPECT_TRUE(List.Options().empty());
	EXPECT_TRUE(List.Visible().empty());
	EXPECT_EQ(List.SelectedRow(), -1);
}

TEST(QmServerVoteList, UnchangedRequestDoesNotEnterFailingPreparation)
{
	COptions Options{"Start", "Profile"};
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	ASSERT_TRUE(List.Update(Options.First(), 1, ""));
	List.Select(1);
	Operations.FailAt(CFailingPreparation::EStage::SEARCH, 1, false);
	EXPECT_FALSE(List.Update(nullptr, 1, ""));
	EXPECT_EQ(Operations.m_SearchCalls, 0);
	EXPECT_EQ(List.SelectedOption(1), 1);
	EXPECT_FALSE(List.NeedsUpdate(1, ""));
}

TEST(QmServerVoteList, LaterRequestSupersedesAnUncommittedRevisionAndSearch)
{
	COptions Old{"Start", "Profile"}, Failed{"Shop"}, Latest{"Back", "商店"};
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	ASSERT_TRUE(List.Update(Old.First(), 1, ""));
	Operations.FailAt(CFailingPreparation::EStage::OPTION, 1, true);
	EXPECT_FALSE(List.Update(Failed.First(), 2, "Shop"));
	Operations.m_FailStage = CFailingPreparation::EStage::NONE;
	ASSERT_TRUE(List.Update(Latest.First(), 3, "商店"));
	EXPECT_EQ(List.Options(), (std::vector<std::string>{"Back", "商店"}));
	EXPECT_EQ(List.Visible(), (std::vector<int>{1}));
	EXPECT_FALSE(List.NeedsUpdate(3, "商店"));
	List.Select(0);
	EXPECT_EQ(List.SelectedOption(3), 1);
	EXPECT_EQ(List.SelectedOption(2), -1);
}

TEST(QmServerVoteList, FailedBorrowedCandidateCanBeDestroyedWithoutDamagingOwnedSnapshot)
{
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	{
		COptions Old{"Start", "Profile"};
		ASSERT_TRUE(List.Update(Old.First(), 1, ""));
	}
	List.Select(1);
	{
		COptions New{"Shop", "Back"};
		Operations.FailAt(CFailingPreparation::EStage::OPTION, 2, true);
		EXPECT_FALSE(List.Update(New.First(), 2, ""));
	}
	EXPECT_EQ(List.Options()[1], "Profile");
	EXPECT_EQ(List.SelectedOption(1), 1);
	COptions Latest{"Latest"};
	Operations.m_FailStage = CFailingPreparation::EStage::NONE;
	ASSERT_TRUE(List.Update(Latest.First(), 3, ""));
	EXPECT_EQ(List.Options(), (std::vector<std::string>{"Latest"}));
}

TEST(QmServerVoteList, RepeatedFailuresDoNotPoisonTheLastCommittedSelection)
{
	using EStage = CFailingPreparation::EStage;
	COptions Old{"Start", "Profile"}, New{"Shop", "Back"};
	CFailingPreparation Operations;
	CQmServerVoteList List(&Operations);
	ASSERT_TRUE(List.Update(Old.First(), 1, ""));
	List.Select(1);
	for(EStage Stage : {EStage::SEARCH, EStage::OPTION, EStage::VISIBLE})
	{
		Operations.FailAt(Stage, 1, true);
		EXPECT_FALSE(List.Update(New.First(), 2, ""));
		EXPECT_EQ(List.Options()[1], "Profile");
		EXPECT_EQ(List.SelectedOption(1), 1);
		EXPECT_TRUE(List.NeedsUpdate(2, ""));
	}
	Operations.m_FailStage = EStage::NONE;
	ASSERT_TRUE(List.Update(New.First(), 2, ""));
	EXPECT_EQ(List.SelectedRow(), -1);
}
