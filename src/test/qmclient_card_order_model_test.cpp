#include <game/client/ui/card_order_model.h>

#include <gtest/gtest.h>

TEST(CardOrderModel, NormalizesPageColumnsAndDeduplicatesDefaults)
{
	CCardOrderModel Model;
	Model.SetDefaults({
		{"qm.a", "home", ECardColumn::LEFT, 8},
		{"qm.b", "other", ECardColumn::LEFT, 2},
		{"qm.c", "home", ECardColumn::RIGHT, 0},
		{"qm.a", "home", ECardColumn::LEFT, 0},
	});
	// 同页重复声明只保留第一条；Normalize 按 (page, column) 重排可见序。
	ASSERT_EQ(Model.Entries().size(), 3);
	ASSERT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT).size(), 1);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT)[0]->m_Id, "qm.a");
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT)[0]->m_Order, 0);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT).front()->m_Id, "qm.c");
	EXPECT_EQ(Model.EntriesForPage("other", ECardColumn::LEFT).front()->m_Order, 0);
	EXPECT_FALSE(Model.IsDirty());
}

TEST(CardOrderModel, ApplyOverridesUpdatesKnownAndAddsUserPlacements)
{
	CCardOrderModel Model;
	Model.SetDefaults({{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "home", ECardColumn::LEFT, 1}});
	ASSERT_TRUE(Model.ApplyOverrides({{"qm.a", "home", ECardColumn::RIGHT, 0, true}}));
	EXPECT_TRUE(Model.IsDirty());
	EXPECT_EQ(Model.Find("home", "qm.a")->m_Column, ECardColumn::RIGHT);
	// 用户显式新增放置：默认声明里没有的 (page, card) 落地为新记录。
	ASSERT_TRUE(Model.ApplyOverrides({{"qm.c", "home", ECardColumn::FULL, 0, true}}));
	EXPECT_NE(Model.Find("home", "qm.c"), nullptr);
	// present=false 的新放置不落地。
	EXPECT_FALSE(Model.ApplyOverrides({{"qm.d", "home", ECardColumn::FULL, 0, false}}));
	EXPECT_EQ(Model.Find("home", "qm.d"), nullptr);
	// 非法列与负序被忽略，不改变现有放置。
	EXPECT_FALSE(Model.ApplyOverrides({{"qm.b", "home", static_cast<ECardColumn>(9), 0, true}}));
	EXPECT_FALSE(Model.ApplyOverrides({{"qm.b", "home", ECardColumn::FULL, -1, true}}));
	EXPECT_EQ(Model.Find("home", "qm.b")->m_Column, ECardColumn::LEFT);
}

TEST(CardOrderModel, MoveUpdatesLayoutRevisionAndCompactsColumns)
{
	CCardOrderModel Model;
	Model.SetDefaults({{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "home", ECardColumn::LEFT, 1}, {"qm.c", "home", ECardColumn::LEFT, 2}, {"qm.d", "home", ECardColumn::RIGHT, 0}});
	const unsigned LayoutRevision = Model.LayoutRevision();
	ASSERT_TRUE(Model.Move("home", "qm.c", ECardColumn::LEFT, 0));
	EXPECT_GT(Model.LayoutRevision(), LayoutRevision);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT)[0]->m_Id, "qm.c");
	ASSERT_TRUE(Model.Move("home", "qm.b", ECardColumn::RIGHT, 0));
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT).size(), 2);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT)[0]->m_Id, "qm.b");
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT)[1]->m_Id, "qm.d");
	// 等价移动、未知卡片、未知页面与非法列都被拒绝。
	EXPECT_FALSE(Model.Move("home", "qm.c", ECardColumn::LEFT, 0));
	EXPECT_FALSE(Model.Move("home", "missing", ECardColumn::LEFT, 0));
	EXPECT_FALSE(Model.Move("other", "qm.a", ECardColumn::LEFT, 0));
	EXPECT_FALSE(Model.Move("home", "qm.a", static_cast<ECardColumn>(99), 0));
}

TEST(CardOrderModel, MoveToPageMarksSourceAbsentAndMergesTarget)
{
	CCardOrderModel Model;
	Model.SetDefaults({{"qm.a", "visual", ECardColumn::LEFT, 0}, {"qm.b", "visual", ECardColumn::LEFT, 1}, {"qm.c", "hud", ECardColumn::LEFT, 0}});
	ASSERT_TRUE(Model.MoveToPage("qm.a", "visual", "hud", ECardColumn::LEFT, 1));
	// 源页面保留移出标记：重启 merge 不得把默认声明补回原页。
	const SCardOrderEntry *pSource = Model.Find("visual", "qm.a");
	ASSERT_NE(pSource, nullptr);
	EXPECT_FALSE(pSource->m_Present);
	EXPECT_TRUE(Model.EntriesForPage("visual", ECardColumn::LEFT).size() == 1);
	ASSERT_EQ(Model.EntriesForPage("hud", ECardColumn::LEFT).size(), 2);
	EXPECT_EQ(Model.EntriesForPage("hud", ECardColumn::LEFT)[0]->m_Id, "qm.c");
	EXPECT_EQ(Model.EntriesForPage("hud", ECardColumn::LEFT)[1]->m_Id, "qm.a");
	// 同页目标退化为普通 Move。
	ASSERT_TRUE(Model.MoveToPage("qm.a", "hud", "hud", ECardColumn::LEFT, 0));
	EXPECT_EQ(Model.EntriesForPage("hud", ECardColumn::LEFT)[0]->m_Id, "qm.a");
	// 空页面参数与不存在的源放置被拒绝。
	EXPECT_FALSE(Model.MoveToPage("qm.a", "hud", "", ECardColumn::LEFT, 0));
	EXPECT_FALSE(Model.MoveToPage("qm.a", "", "hud", ECardColumn::LEFT, 0));
	EXPECT_FALSE(Model.MoveToPage("qm.a", "other", "hud", ECardColumn::LEFT, 0));
}

TEST(CardOrderModel, MoveToPageMergesWithExistingTargetPlacement)
{
	CCardOrderModel Model;
	Model.SetDefaults({{"qm.a", "p1", ECardColumn::LEFT, 0}, {"qm.a", "p2", ECardColumn::LEFT, 0}, {"qm.b", "p2", ECardColumn::LEFT, 1}});
	ASSERT_TRUE(Model.MoveToPage("qm.a", "p1", "p2", ECardColumn::LEFT, 0));
	EXPECT_FALSE(Model.Find("p1", "qm.a")->m_Present);
	EXPECT_TRUE(Model.Find("p2", "qm.a")->m_Present);
	// 多页面声明是合法状态：present 放置合并为一个，不产生重复记录。
	ASSERT_EQ(Model.EntriesForPage("p2", ECardColumn::LEFT).size(), 2);
	EXPECT_EQ(Model.EntriesForPage("p2", ECardColumn::LEFT)[0]->m_Id, "qm.a");
	EXPECT_EQ(Model.EntriesForCard("qm.a").size(), 1);
}

TEST(CardOrderModel, SerializesPresentMarkersAndMergesUserOverrides)
{
	const std::vector<SCardOrderEntry> Defaults = {
		{"qm.a", "home", ECardColumn::LEFT, 0},
		{"qm.b", "home", ECardColumn::RIGHT, 0},
		{"qm.c", "hud", ECardColumn::RIGHT, 0},
	};
	CCardOrderModel Source;
	Source.SetDefaults(Defaults);
	ASSERT_TRUE(Source.MoveToPage("qm.a", "home", "hud", ECardColumn::RIGHT, 1));
	const std::string Serialized = Source.Serialize();
	EXPECT_NE(Serialized.find("qm.a|home|left|0|0;"), std::string::npos);
	EXPECT_NE(Serialized.find("qm.a|hud|right|1|1;"), std::string::npos);
	CCardOrderModel Reloaded;
	EXPECT_TRUE(Reloaded.LoadMerged(Serialized, Defaults));
	EXPECT_FALSE(Reloaded.Find("home", "qm.a")->m_Present);
	EXPECT_EQ(Reloaded.Find("hud", "qm.a")->m_Column, ECardColumn::RIGHT);
	EXPECT_EQ(Reloaded.Find("hud", "qm.a")->m_Order, 1);
	EXPECT_FALSE(Reloaded.IsDirty());
}

TEST(CardOrderModel, AcceptsLegacyFormatsAndIgnoresInvalidEntries)
{
	// v2 四字段格式与 v1 三字段格式（页面沿用默认声明）；非法 token 被过滤。
	const std::vector<SCardOrderEntry> Defaults = {{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "home", ECardColumn::RIGHT, 0}};
	CCardOrderModel Model;
	EXPECT_TRUE(Model.LoadMerged("qm.a|home|right|1;unknown:1:0;qm.b:wrong:0;qm.b|wrong|left|-1;qm.b:right:0;", Defaults));
	ASSERT_NE(Model.Find("home", "qm.a"), nullptr);
	EXPECT_EQ(Model.Find("home", "qm.a")->m_Column, ECardColumn::RIGHT);
	EXPECT_EQ(Model.Find("home", "qm.a")->m_Order, 1);
	EXPECT_EQ(Model.Find("home", "qm.b")->m_Column, ECardColumn::RIGHT);
	EXPECT_FALSE(Model.IsDirty());
}

TEST(CardOrderModel, LoadMergedAcceptsMultiplePagePlacements)
{
	const std::vector<SCardOrderEntry> Defaults = {{"qm.a", "home", ECardColumn::LEFT, 0}, {"qm.b", "hud", ECardColumn::RIGHT, 0}};
	CCardOrderModel Model;
	ASSERT_TRUE(Model.LoadMerged("qm.a|hud|right|1;qm.a|later|left|0;", Defaults));
	EXPECT_EQ(Model.Find("hud", "qm.a")->m_Column, ECardColumn::RIGHT);
	EXPECT_EQ(Model.Find("hud", "qm.a")->m_Order, 1);
	EXPECT_NE(Model.Find("later", "qm.a"), nullptr);
	// 直接合并不隐式移出默认页；只有 MoveToPage 产生 present=false 标记。
	EXPECT_TRUE(Model.Find("home", "qm.a")->m_Present);
}

TEST(CardOrderModel, RemovePlacementDropsUserAddedPlacement)
{
	const std::vector<SCardOrderEntry> Defaults = {{"qm.a", "home", ECardColumn::LEFT, 0}};
	CCardOrderModel Model;
	Model.SetDefaults(Defaults);
	ASSERT_TRUE(Model.ApplyOverrides({{"qm.a", "extra", ECardColumn::FULL, 0, true}}));
	ASSERT_TRUE(Model.RemovePlacement("extra", "qm.a"));
	EXPECT_EQ(Model.Find("extra", "qm.a"), nullptr);
	EXPECT_FALSE(Model.RemovePlacement("extra", "qm.a"));
	EXPECT_FALSE(Model.RemovePlacement("", "qm.a"));
	EXPECT_FALSE(Model.RemovePlacement("home", ""));
	EXPECT_TRUE(Model.Find("home", "qm.a")->m_Present);
	Model.ClearDirty();
	// 原语不区分默认与用户放置；模型层只在无默认声明时调用（见 ResetPreferences）。
	EXPECT_TRUE(Model.RemovePlacement("home", "qm.a"));
	EXPECT_EQ(Model.Find("home", "qm.a"), nullptr);
	EXPECT_TRUE(Model.IsDirty());
}
