#include <game/client/ui/card_search_index.h>
#include <game/client/ui/card_search_logic.h>
#include <game/client/ui/card_ui_model.h>

#include <gtest/gtest.h>

TEST(CardSearchLogic, TokenizesByWhitespaceAndDropsEmptyTokens)
{
	const auto vTokens = TokenizeSearchQuery("  hud   timer\t方向  ");
	ASSERT_EQ(vTokens.size(), 3);
	EXPECT_EQ(vTokens[0], "hud");
	EXPECT_EQ(vTokens[1], "timer");
	EXPECT_EQ(vTokens[2], "方向");
	EXPECT_TRUE(TokenizeSearchQuery("   ").empty());
	EXPECT_TRUE(TokenizeSearchQuery("").empty());
}

TEST(CardSearchLogic, MatchesSubstringsCaseAndCjk)
{
	EXPECT_TRUE(MatchSearchToken("Player Indicator", "player"));
	EXPECT_TRUE(MatchSearchToken("PLAYER INDICATOR", "indicator"));
	EXPECT_FALSE(MatchSearchToken("Player Indicator", "xyz"));
	// str_utf8_find_nocase 按码点比较：CJK 连续子串命中，逆序不命中。
	EXPECT_TRUE(MatchSearchToken("玩家指示器", "指示"));
	EXPECT_FALSE(MatchSearchToken("玩家指示器", "器指"));
	EXPECT_FALSE(MatchSearchToken("", "a"));
	EXPECT_FALSE(MatchSearchToken("abc", ""));
}

TEST(CardSearchLogic, AllTokensMustMatchAcrossFieldsAndPriorityDecidesBest)
{
	const std::vector<std::pair<std::string, std::vector<SCardSearchItem>>> vEntries = {
		{"qm.timer", {{"Speedrun Timer", ECardSearchField::TITLE}, {"countdown", ECardSearchField::ALIAS}}},
		{"qm.hud", {{"HUD", ECardSearchField::TITLE}, {"countdown clock", ECardSearchField::DESCRIPTION}}},
		{"qm.toggle", {{"Toggle HUD", ECardSearchField::CONTROL}}},
	};
	// 单分词：字段类别序即优先级，DESCRIPTION 命中排在 ALIAS 之前；
	// BestMatch 是最佳命中字段的原文。
	const auto vResults = ResolveCardSearchMatches(vEntries, {"countdown"});
	ASSERT_EQ(vResults.size(), 2);
	EXPECT_EQ(vResults[0].m_Id, "qm.hud");
	EXPECT_EQ(vResults[0].m_BestMatch, "countdown clock");
	EXPECT_EQ(vResults[1].m_Id, "qm.timer");
	EXPECT_EQ(vResults[1].m_BestMatch, "countdown");
	// 多分词允许跨字段：每个分词都需命中（允许命中不同字段）。
	ASSERT_EQ(ResolveCardSearchMatches(vEntries, {"countdown", "clock"}).size(), 1);
	EXPECT_EQ(ResolveCardSearchMatches(vEntries, {"countdown", "timer"}).front().m_Id, "qm.timer");
	EXPECT_TRUE(ResolveCardSearchMatches(vEntries, {"countdown", "missing"}).empty());
	// 同一查询命中多张卡时按最佳字段优先级排序。
	const auto vPrioritized = ResolveCardSearchMatches(vEntries, {"hud"});
	ASSERT_EQ(vPrioritized.size(), 2);
	EXPECT_EQ(vPrioritized[0].m_Id, "qm.hud");
	EXPECT_EQ(vPrioritized[1].m_Id, "qm.toggle");
	// 空查询返回可搜索全集，以稳定 card ID 排序。
	const auto vAll = ResolveCardSearchMatches(vEntries, {});
	ASSERT_EQ(vAll.size(), 3);
	EXPECT_EQ(vAll[0].m_Id, "qm.hud");
	EXPECT_EQ(vAll[1].m_Id, "qm.timer");
	EXPECT_EQ(vAll[2].m_Id, "qm.toggle");
}

namespace
{
class CTestSearchProvider final : public ICardSearchContentProvider
{
public:
	void CollectCardContent(const SCardDescriptor &Card, std::vector<SCardSearchItem> &vOut) const override
	{
		// presentation 提供本地化文本：与卡片渲染共用一份文本源。
		vOut.push_back({"本地化标题", ECardSearchField::TITLE});
		if(Card.m_PresentationId == "toggle")
			vOut.push_back({"Enabled", ECardSearchField::CONTROL});
	}
};
}

TEST(CardSearchIndex, RebuildsFromRegistryAndSearchesDescriptorFields)
{
	CCardRegistry Registry;
	SFeatureModel Feature{"qm.timer", "Timer", true, true};
	ASSERT_TRUE(Registry.RegisterFeature(Feature));
	ASSERT_TRUE(Registry.RegisterCard({"qm.timer.card", "Timer", "Countdown", "timer", Feature.m_Id, {"speedrun"}, ECardOwner::QM, 0, true}));
	Registry.Freeze();
	CCardSearchIndex Index;
	Index.Configure(&Registry, nullptr);
	Index.Rebuild();
	EXPECT_TRUE(Index.IsBuilt());
	const auto vResults = Index.Search("speedrun");
	ASSERT_EQ(vResults.size(), 1);
	EXPECT_EQ(vResults.front().m_Id, "qm.timer.card");
	EXPECT_TRUE(Index.Search("nothing-matches-this").empty());
	// 未配置或未重建时索引为空。
	CCardSearchIndex Unconfigured;
	EXPECT_FALSE(Unconfigured.IsBuilt());
	EXPECT_TRUE(Unconfigured.Search("timer").empty());
}

TEST(CardSearchIndex, ProviderContentExtendsDescriptorAndReconfigureInvalidates)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon", "", {}, ECardOwner::QM, 0, true, "toggle"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	Registry.Freeze();
	CTestSearchProvider Provider;
	CCardSearchIndex Index;
	Index.Configure(&Registry, &Provider);
	Index.Rebuild();
	// provider 提供的本地化标题与控件文本参与检索。
	EXPECT_EQ(Index.Search("本地化").size(), 1);
	EXPECT_EQ(Index.Search("enabled").size(), 1);
	// Configure 变化后需要重建；重建递增 revision。
	const unsigned Revision = Index.Revision();
	Index.Configure(&Registry, &Provider);
	Index.Rebuild();
	EXPECT_NE(Index.Revision(), Revision);
	// 折叠、隐藏与功能关闭不影响检索：索引不依赖 UI 偏好。
	CCardUiModel UiModel(Registry);
	SCardUiPreferences Preferences = UiModel.Preferences("home", "qm.a");
	Preferences.m_Visible = false;
	ASSERT_TRUE(UiModel.SetPreferences("home", "qm.a", Preferences));
	Index.Rebuild();
	EXPECT_EQ(Index.Search("本地化").size(), 1);
}
