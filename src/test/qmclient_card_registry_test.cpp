#include <game/client/ui/card_registry.h>

#include <gtest/gtest.h>

TEST(CardRegistry, RegistersGlobalPagesAndOwners)
{
	CCardRegistry Registry;
	SFeatureModel Feature{"qm.speedrun_timer", "qm.speedrun_timer.title", true, true};
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0}));
	ASSERT_TRUE(Registry.RegisterFeature(Feature));
	ASSERT_TRUE(Registry.RegisterCard({"qm.speedrun_timer", "home", "qm.speedrun_timer.title", "countdown", "timer", Feature.m_Id, {"timer", "race"}, ECardOwner::QM, 20, true}));
	EXPECT_EQ(Registry.FindCard("qm.speedrun_timer")->m_Owner, ECardOwner::QM);
	EXPECT_EQ(Registry.CardsForPage("home").size(), 1);
}

TEST(CardRegistry, SortsCardsByOrderThenStableId)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0}));
	ASSERT_TRUE(Registry.RegisterCard({"ddnet.zeta", "home", "zeta", {}, "z", {}, {}, ECardOwner::UPSTREAM, 10, true}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.alpha", "home", "alpha", {}, "a", {}, {}, ECardOwner::QM, 10, true}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.first", "home", "first", {}, "f", {}, {}, ECardOwner::QM, 1, true}));
	const auto Cards = Registry.CardsForPage("home");
	ASSERT_EQ(Cards.size(), 3);
	EXPECT_EQ(Cards[0]->m_Id, "qm.first");
	EXPECT_EQ(Cards[1]->m_Id, "ddnet.zeta");
	EXPECT_EQ(Cards[2]->m_Id, "qm.alpha");
}

TEST(CardRegistry, SearchesDescriptorFieldsAndFreezesRegistration)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.player_indicator", "home", "Player Indicator", "direction", "compass", {}, {"teammate"}, ECardOwner::QM, 0, true}));
	EXPECT_EQ(Registry.Search("TEAMMATE").size(), 1);
	EXPECT_EQ(Registry.Search("direction").front()->m_Id, "qm.player_indicator");
	Registry.Freeze();
	EXPECT_FALSE(Registry.RegisterPage({"settings", "ui.settings", 1}));
	EXPECT_TRUE(Registry.IsFrozen());
}

TEST(CardRegistry, RejectsInvalidReferencesAndIds)
{
	CCardRegistry Registry;
	EXPECT_FALSE(Registry.RegisterPage({"Home", "ui.home", 0}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0}));
	SFeatureModel Feature{"qm.valid", "title", false, true};
	EXPECT_FALSE(Registry.RegisterCard({"qm.unknown", "missing", "title", {}, "icon", {}, {}, ECardOwner::QM, 0, true}));
	EXPECT_FALSE(Registry.RegisterCard({"qm.valid", "home", "title", {}, "icon", "qm.unknown", {}, ECardOwner::QM, 0, true}));
	ASSERT_TRUE(Registry.RegisterFeature(Feature));
	EXPECT_TRUE(Registry.RegisterCard({"qm.valid", "home", "title", {}, "icon", Feature.m_Id, {}, ECardOwner::QM, 0, true}));
}

TEST(CardRegistry, BuildsIndependentDefaultOrderModel)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.left", "home", "Left", {}, "icon", {}, {}, ECardOwner::QM, 2, true, "panel", 0, false, ECardColumn::LEFT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.right", "home", "Right", {}, "icon", {}, {}, ECardOwner::QM, 1, true, "panel", 0, false, ECardColumn::RIGHT}));
	const CCardOrderModel Model = Registry.BuildDefaultOrderModel();
	ASSERT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT).size(), 1);
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::LEFT).front()->m_Id, "qm.left");
	EXPECT_EQ(Model.EntriesForPage("home", ECardColumn::RIGHT).front()->m_Id, "qm.right");
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

TEST(CardRegistry, ValidatesPresentationIdentityAndFreezesAllOwners)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	SCardDescriptor Card{"ddnet.video", "home", "Video", {}, "icon"};
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
	EXPECT_EQ(Registry.CardsForPage("home").size(), 4);
}

TEST(CardRegistry, RejectsInvalidDefaultColumnAndOversizedId)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	SCardDescriptor Card{"qm.a", "home", "A", {}, "icon"};
	Card.m_DefaultColumn = static_cast<ECardColumn>(-1);
	EXPECT_FALSE(Registry.RegisterCard(Card));
	Card.m_DefaultColumn = static_cast<ECardColumn>(3);
	EXPECT_FALSE(Registry.RegisterCard(Card));
	Card.m_DefaultColumn = ECardColumn::FULL;
	Card.m_Id = std::string(129, 'a');
	EXPECT_FALSE(Registry.RegisterCard(Card));
	Card.m_Id = std::string(128, 'a');
	EXPECT_TRUE(Registry.RegisterCard(Card));
}
TEST(CardRegistry, InputPriorityIsDescendingAndStable)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.low", "home", "Low", {}, "icon", {}, {}, ECardOwner::QM, 0, true, "toggle", -5}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.b", "home", "B", {}, "icon", {}, {}, ECardOwner::QM, 0, true, "toggle", 10}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "home", "A", {}, "icon", {}, {}, ECardOwner::QM, 0, true, "toggle", 10}));
	Registry.Freeze();
	const auto Cards = Registry.CardsByInputPriority();
	ASSERT_EQ(Cards.size(), 3);
	EXPECT_EQ(Cards[0]->m_Id, "qm.a");
	EXPECT_EQ(Cards[1]->m_Id, "qm.b");
	EXPECT_EQ(Cards[2]->m_Id, "qm.low");
}
