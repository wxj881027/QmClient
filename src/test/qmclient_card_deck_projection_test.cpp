#include <game/client/ui/card_deck_projection.h>

#include <gtest/gtest.h>

TEST(CardDeckProjection, BuildsVisibleColumnsAndAppendsNewCards)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0}));
	SFeatureModel Unavailable{"qm.unavailable", "Unavailable", true, false};
	ASSERT_TRUE(Registry.RegisterFeature(Unavailable));
	ASSERT_TRUE(Registry.RegisterCard({"qm.full", "home", "Full", {}, "icon", {}, {}, ECardOwner::QM, 0, true, "panel", 0, false, ECardColumn::FULL}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.left", "home", "Left", {}, "icon", {}, {}, ECardOwner::QM, 1, true, "panel", 0, false, ECardColumn::LEFT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.right", "home", "Right", {}, "icon", {}, {}, ECardOwner::QM, 2, true, "panel", 0, false, ECardColumn::RIGHT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.hidden", "home", "Hidden", {}, "icon", {}, {}, ECardOwner::QM, 3, true, "panel", 0, false, ECardColumn::LEFT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.unavailable.card", "home", "Unavailable", {}, "icon", Unavailable.m_Id, {}, ECardOwner::QM, 4, true, "panel", 0, false, ECardColumn::RIGHT}));
	CCardUiModel Ui(Registry);
	ASSERT_TRUE(Ui.SetPreferences("qm.hidden", {false, false, 0}));
	const CCardOrderModel Order = Registry.BuildDefaultOrderModel();
	const SCardDeckProjection Projection = BuildCardDeckProjection(Registry, Order, Ui, "home");
	ASSERT_EQ(Projection.m_aColumns[0].size(), 1);
	ASSERT_EQ(Projection.m_aColumns[1].size(), 1);
	ASSERT_EQ(Projection.m_aColumns[2].size(), 1);
	EXPECT_EQ(Projection.m_aColumns[0][0]->m_Id, "qm.full");
	EXPECT_TRUE(Projection.m_TwoColumns);
}

TEST(CardDeckProjection, UsesOrderModelColumnAndRevision)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "home", "A", {}, "icon", {}, {}, ECardOwner::QM, 0, true, "panel", 0, false, ECardColumn::LEFT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.b", "home", "B", {}, "icon", {}, {}, ECardOwner::QM, 1, true, "panel", 0, false, ECardColumn::LEFT}));
	CCardUiModel Ui(Registry);
	CCardOrderModel Order = Registry.BuildDefaultOrderModel();
	ASSERT_TRUE(Order.Move("qm.b", ECardColumn::RIGHT, 0));
	const SCardDeckProjection Projection = BuildCardDeckProjection(Registry, Order, Ui, "home");
	EXPECT_EQ(Projection.m_LayoutRevision, Order.LayoutRevision());
	ASSERT_EQ(Projection.m_aColumns[2].size(), 1);
	EXPECT_EQ(Projection.m_aColumns[2][0]->m_Id, "qm.b");
}

TEST(CardDeckProjection, CrossPagePlacementIsNotReinsertedAtDefaultPage)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
	ASSERT_TRUE(Registry.RegisterPage({"other", "Other", 1}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "home", "A", {}, "icon"}));
	Registry.Freeze();
	CCardUiModel Ui(Registry);
	CCardOrderModel Order = Registry.BuildDefaultOrderModel();
	ASSERT_TRUE(Order.MoveToPage("qm.a", "other", ECardColumn::LEFT, 0));
	const auto Home = BuildCardDeckProjection(Registry, Order, Ui, "home");
	for(const auto &vColumn : Home.m_aColumns)
		EXPECT_TRUE(vColumn.empty());
	const auto Other = BuildCardDeckProjection(Registry, Order, Ui, "other");
	ASSERT_EQ(Other.m_aColumns[1].size(), 1);
	EXPECT_EQ(Other.m_aColumns[1].front()->m_Id, "qm.a");
	const auto Missing = BuildCardDeckProjection(Registry, Order, Ui, "missing");
	for(const auto &vColumn : Missing.m_aColumns)
		EXPECT_TRUE(vColumn.empty());
}
