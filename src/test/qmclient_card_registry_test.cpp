#include <game/client/ui/card_registry.h>

#include <gtest/gtest.h>

namespace
{
SCardDescriptor TestCard(const char *pId, const char *pTitle, ECardOwner Owner = ECardOwner::QM, int Order = 0, const char *pFeature = nullptr)
{
	return {pId, pTitle, {}, "icon", pFeature ? pFeature : "", {}, Owner, Order, true};
}
}

TEST(CardRegistry, RegistersPagesWithDeclarationsAndOwners)
{
	CCardRegistry Registry;
	SFeatureModel Feature{"qm.speedrun_timer", "qm.speedrun_timer.title", true, true};
	ASSERT_TRUE(Registry.RegisterFeature(Feature));
	ASSERT_TRUE(Registry.RegisterCard(TestCard("qm.speedrun_timer", "Speedrun Timer", ECardOwner::QM, 20, "qm.speedrun_timer")));
	ASSERT_TRUE(Registry.RegisterPage({"home", "qm.ui.home", 0, {"qm.speedrun_timer"}}));
	EXPECT_EQ(Registry.FindCard("qm.speedrun_timer")->m_Owner, ECardOwner::QM);
	ASSERT_EQ(Registry.CardsForPage("home").size(), 1);
	EXPECT_EQ(Registry.CardsForPage("home").front()->m_Id, "qm.speedrun_timer");
	// 未声明页面：卡片注册成功但不属于任何页面。
	ASSERT_TRUE(Registry.RegisterCard(TestCard("qm.undeclared", "Undeclared")));
	EXPECT_TRUE(Registry.CardsForPage("home").size() == 1);
}

TEST(CardRegistry, FreezeRejectsUnresolvedDeclarationsAndDuplicates)
{
	CCardRegistry Registry;
	// 未注册的声明使冻结失败，不带病上线。
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.missing"}}));
	EXPECT_FALSE(Registry.Freeze());
	EXPECT_TRUE(Registry.IsFrozen());
	EXPECT_FALSE(Registry.IsValid());

	CCardRegistry DuplicateRegistry;
	ASSERT_TRUE(DuplicateRegistry.RegisterCard(TestCard("qm.a", "A")));
	ASSERT_TRUE(DuplicateRegistry.RegisterPage({"home", "Home", 0, {"qm.a", "qm.a"}}));
	EXPECT_FALSE(DuplicateRegistry.Freeze());

	CCardRegistry ValidRegistry;
	ASSERT_TRUE(ValidRegistry.RegisterCard(TestCard("qm.a", "A")));
	ASSERT_TRUE(ValidRegistry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	EXPECT_TRUE(ValidRegistry.Freeze());
}

TEST(CardRegistry, CardsForPageFollowsDeclarationOrder)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard(TestCard("qm.zeta", "Zeta", ECardOwner::QM, 10)));
	ASSERT_TRUE(Registry.RegisterCard(TestCard("qm.alpha", "Alpha", ECardOwner::QM, 1)));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.zeta", "qm.alpha"}}));
	const auto Cards = Registry.CardsForPage("home");
	ASSERT_EQ(Cards.size(), 2);
	EXPECT_EQ(Cards[0]->m_Id, "qm.zeta");
	EXPECT_EQ(Cards[1]->m_Id, "qm.alpha");
	EXPECT_TRUE(Registry.CardsForPage("missing").empty());
}

TEST(CardRegistry, InputPriorityIsDescendingAndStable)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.low", "Low", {}, "icon", "", {}, ECardOwner::QM, 0, true, "toggle", -5}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.b", "B", {}, "icon", "", {}, ECardOwner::QM, 0, true, "toggle", 10}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon", "", {}, ECardOwner::QM, 0, true, "toggle", 10}));
	Registry.Freeze();
	const auto Cards = Registry.CardsByInputPriority();
	ASSERT_EQ(Cards.size(), 3);
	EXPECT_EQ(Cards[0]->m_Id, "qm.a");
	EXPECT_EQ(Cards[1]->m_Id, "qm.b");
	EXPECT_EQ(Cards[2]->m_Id, "qm.low");
}

TEST(CardRegistry, BuildsDefaultOrderModelFromPageDeclarations)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.left", "Left", {}, "icon", "", {}, ECardOwner::QM, 2, true, "default", 0, false, ECardColumn::LEFT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.right", "Right", {}, "icon", "", {}, ECardOwner::QM, 1, true, "default", 0, false, ECardColumn::RIGHT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.full", "Full", {}, "icon"}));
	// 同一卡片可以被多个页面声明：每个声明页各有一个默认放置。
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.full", "qm.left", "qm.right"}}));
	ASSERT_TRUE(Registry.RegisterPage({"other", "Other", 1, {"qm.full"}}));
	const CCardOrderModel Model = Registry.BuildDefaultOrderModel();
	ASSERT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT).size(), 1);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT).front()->m_Id, "qm.left");
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT).front()->m_Id, "qm.right");
	ASSERT_EQ(Model.EntriesForPage("home", ECardColumn::FULL).size(), 1);
	ASSERT_EQ(Model.EntriesForPage("other", ECardColumn::FULL).size(), 1);
	EXPECT_NE(Model.Find("home", "qm.full"), Model.Find("other", "qm.full"));
}

TEST(CardRegistry, SortsPagesByOrderThenStableId)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"zeta", "Zeta", 10}));
	ASSERT_TRUE(Registry.RegisterPage({"alpha", "Alpha", 10}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", -1}));
	Registry.Freeze();
	const auto vpPages = Registry.Pages();
	ASSERT_EQ(vpPages.size(), 3);
	EXPECT_EQ(vpPages[0]->m_Id, "home");
	EXPECT_EQ(vpPages[1]->m_Id, "alpha");
	EXPECT_EQ(vpPages[2]->m_Id, "zeta");
}

TEST(CardRegistry, RejectsInvalidPageIdsAndDuplicatePages)
{
	CCardRegistry Registry;
	EXPECT_FALSE(Registry.RegisterPage({"Home", "Home", 0}));
	EXPECT_FALSE(Registry.RegisterPage({"", "Home", 0}));
	EXPECT_FALSE(Registry.RegisterPage({"home", "", 0}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	EXPECT_FALSE(Registry.RegisterPage({"home", "Other", 1}));
	EXPECT_TRUE(Registry.FindPage("home") != nullptr);
	EXPECT_TRUE(Registry.FindPage("missing") == nullptr);
}

TEST(CardRegistry, RejectsInvalidCardDescriptors)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	// 稳定 ID、标题、图标与展示 ID 是必填约束。
	EXPECT_FALSE(Registry.RegisterCard(TestCard("Invalid", "A")));
	EXPECT_FALSE(Registry.RegisterCard(TestCard("qm.a", "")));
	EXPECT_FALSE(Registry.RegisterCard({"qm.a", "A", {}, "", "", {}, ECardOwner::QM, 0, true}));
	EXPECT_FALSE(Registry.RegisterCard({"qm.a", "A", {}, "icon", "", {}, ECardOwner::QM, 0, true, "invalid/handler"}));
	EXPECT_FALSE(Registry.RegisterCard({"qm.a", "A", {}, "icon", "qm.unknown"}));
	// 非法默认列被拒绝。
	SCardDescriptor Card = TestCard("qm.a", "A");
	Card.m_DefaultColumn = static_cast<ECardColumn>(-1);
	EXPECT_FALSE(Registry.RegisterCard(Card));
	Card.m_DefaultColumn = static_cast<ECardColumn>(3);
	EXPECT_FALSE(Registry.RegisterCard(Card));
	// 超长 ID 被拒绝。
	Card.m_DefaultColumn = ECardColumn::FULL;
	Card.m_Id = std::string(129, 'a');
	EXPECT_FALSE(Registry.RegisterCard(Card));
	Card.m_Id = std::string(128, 'a');
	EXPECT_TRUE(Registry.RegisterCard(Card));
	EXPECT_TRUE(Registry.FindCard("qm.a") == nullptr);
}

TEST(CardRegistry, ValidatesPresentationIdentityForAllOwners)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	SCardDescriptor Card{"ddnet.video", "Video", {}, "icon"};
	Card.m_PresentationId = "invalid/handler";
	EXPECT_FALSE(Registry.RegisterCard(Card));
	Card.m_PresentationId = "ddnet.video";
	ASSERT_TRUE(Registry.RegisterCard(Card));
	Card.m_Id = "qm.video";
	Card.m_Owner = ECardOwner::QM;
	ASSERT_TRUE(Registry.RegisterCard(Card));
	Card.m_Id = "qm.tc.video";
	Card.m_Owner = ECardOwner::TC_REBUILT;
	ASSERT_TRUE(Registry.RegisterCard(Card));
	Card.m_Id = "qm.bc.video";
	Card.m_Owner = ECardOwner::BC_REBUILT;
	ASSERT_TRUE(Registry.RegisterCard(Card));
	Registry.Freeze();
	SFeatureModel Feature{"qm.late", "Late", false, true};
	EXPECT_FALSE(Registry.RegisterFeature(Feature));
	Card.m_Id = "qm.late";
	EXPECT_FALSE(Registry.RegisterCard(Card));
	EXPECT_EQ(Registry.CardsByInputPriority().size(), 4);
}

TEST(CardRegistry, FrozenRegistryRejectsFurtherRegistration)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	ASSERT_TRUE(Registry.RegisterCard(TestCard("qm.a", "A")));
	Registry.Freeze();
	EXPECT_FALSE(Registry.RegisterPage({"settings", "Settings", 1}));
	EXPECT_FALSE(Registry.RegisterCard(TestCard("qm.b", "B")));
	EXPECT_TRUE(Registry.IsFrozen());
	// 重复冻结保持结果稳定。
	EXPECT_TRUE(Registry.Freeze());
}
