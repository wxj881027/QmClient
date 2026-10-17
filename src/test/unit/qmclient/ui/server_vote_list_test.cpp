#include <base/str.h>

#include <game/client/components/qmclient/server_vote_list.h>

#include <gtest/gtest.h>

#include <initializer_list>
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
	ASSERT_TRUE(List.Update(Options.First(), 7, "PROFILE"));
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
