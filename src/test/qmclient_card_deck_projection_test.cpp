#include <game/client/ui/card_deck_projection.h>

#include <gtest/gtest.h>

namespace
{
CCardRegistry CreateRegistry()
{
	// registry 只持有 feature 指针：feature 必须比 registry 活得久。
	static SFeatureModel Unavailable{"qm.unavailable", "Unavailable", true, false};
	CCardRegistry Registry;
	Registry.RegisterFeature(Unavailable);
	Registry.RegisterCard({"qm.full", "Full", {}, "icon", "", {}, ECardOwner::QM, 0, true, "default", 0, false, ECardColumn::FULL});
	Registry.RegisterCard({"qm.left", "Left", {}, "icon", "", {}, ECardOwner::QM, 1, true, "default", 0, false, ECardColumn::LEFT});
	Registry.RegisterCard({"qm.right", "Right", {}, "icon", "", {}, ECardOwner::QM, 2, true, "default", 0, false, ECardColumn::RIGHT});
	Registry.RegisterCard({"qm.hidden", "Hidden", {}, "icon", "", {}, ECardOwner::QM, 3, true, "default", 0, false, ECardColumn::LEFT});
	Registry.RegisterCard({"qm.unavailable.card", "Unavailable", {}, "icon", "qm.unavailable", {}, ECardOwner::QM, 4, true, "default", 0, false, ECardColumn::RIGHT});
	Registry.RegisterPage({"home", "Home", 0, {"qm.full", "qm.left", "qm.right", "qm.hidden", "qm.unavailable.card"}});
	Registry.Freeze();
	return Registry;
}
}

TEST(CardDeckProjection, BuildsVisibleColumnsAndSkipsHiddenAndUnavailable)
{
	CCardRegistry Registry = CreateRegistry();
	CCardUiModel Ui(Registry);
	ASSERT_TRUE(Ui.SetPreferences("home", "qm.hidden", {false, false}));
	const CCardOrderModel Order = Registry.BuildDefaultOrderModel();
	const SCardDeckProjection Projection = BuildCardDeckProjection(Registry, Order, Ui, "home");
	ASSERT_EQ(Projection.m_aColumns[0].size(), 1);
	ASSERT_EQ(Projection.m_aColumns[1].size(), 1);
	ASSERT_EQ(Projection.m_aColumns[2].size(), 1);
	EXPECT_EQ(Projection.m_aColumns[0][0]->m_Id, "qm.full");
	EXPECT_EQ(Projection.m_aColumns[1][0]->m_Id, "qm.left");
	EXPECT_EQ(Projection.m_aColumns[2][0]->m_Id, "qm.right");
	EXPECT_TRUE(Projection.m_TwoColumns);
	EXPECT_EQ(Projection.m_LayoutRevision, Order.LayoutRevision());
}

TEST(CardDeckProjection, UsesOrderModelColumnWithinPage)
{
	CCardRegistry Registry = CreateRegistry();
	CCardUiModel Ui(Registry);
	CCardOrderModel Order = Registry.BuildDefaultOrderModel();
	ASSERT_TRUE(Order.Move("home", "qm.left", ECardColumn::RIGHT, 0));
	const SCardDeckProjection Projection = BuildCardDeckProjection(Registry, Order, Ui, "home");
	ASSERT_EQ(Projection.m_aColumns[2].size(), 2);
	EXPECT_EQ(Projection.m_aColumns[2][0]->m_Id, "qm.left");
	EXPECT_EQ(Projection.m_aColumns[2][1]->m_Id, "qm.right");
}

TEST(CardDeckProjection, PerPageVisibilityHidesOnlyOnePage)
{
	// 同一卡片声明在多个页面：本页隐藏只影响该页布局，另一页保持可见。
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	ASSERT_TRUE(Registry.RegisterPage({"other", "Other", 1, {"qm.a"}}));
	Registry.Freeze();
	CCardUiModel Ui(Registry);
	ASSERT_TRUE(Ui.SetPreferences("other", "qm.a", {false, false}));
	const CCardOrderModel Order = Registry.BuildDefaultOrderModel();
	const auto Home = BuildCardDeckProjection(Registry, Order, Ui, "home");
	ASSERT_EQ(Home.m_aColumns[0].size(), 1);
	EXPECT_EQ(Home.m_aColumns[0].front()->m_Id, "qm.a");
	const auto Other = BuildCardDeckProjection(Registry, Order, Ui, "other");
	for(const auto &vColumn : Other.m_aColumns)
		EXPECT_TRUE(vColumn.empty());
}

TEST(CardDeckProjection, AbsentMarkerPreventsReinsertionAtDefaultPage)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	ASSERT_TRUE(Registry.RegisterPage({"other", "Other", 1, {}}));
	Registry.Freeze();
	CCardUiModel Ui(Registry);
	CCardOrderModel Order = Registry.BuildDefaultOrderModel();
	ASSERT_TRUE(Order.MoveToPage("qm.a", "home", "other", ECardColumn::LEFT, 0));
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

TEST(CardDeckProjection, NewDeclaredCardsBackfillAfterPersistedLayout)
{
	// 持久化布局里没有的新声明卡片按默认位置补位：旧配置不得隐藏新功能。
	CCardRegistry OldRegistry;
	ASSERT_TRUE(OldRegistry.RegisterCard({"qm.old", "Old", {}, "icon"}));
	ASSERT_TRUE(OldRegistry.RegisterPage({"home", "Home", 0, {"qm.old"}}));
	OldRegistry.Freeze();
	CCardUiModel OldUi(OldRegistry);
	ASSERT_TRUE(OldUi.SetPreferences("home", "qm.old", {false, true}));
	const SCardUiState Persisted = OldUi.ExportState();
	ASSERT_EQ(Persisted.m_vPlacements.size(), 1);

	CCardRegistry NewRegistry;
	ASSERT_TRUE(NewRegistry.RegisterCard({"qm.old", "Old", {}, "icon"}));
	ASSERT_TRUE(NewRegistry.RegisterCard({"qm.new", "New", {}, "icon", "", {}, ECardOwner::QM, 0, true, "default", 0, false, ECardColumn::RIGHT}));
	ASSERT_TRUE(NewRegistry.RegisterPage({"home", "Home", 0, {"qm.old", "qm.new"}}));
	NewRegistry.Freeze();
	CCardUiModel NewUi(NewRegistry);
	std::string Error;
	ASSERT_TRUE(NewUi.ImportState(Persisted, Error)) << Error;
	const CCardOrderModel Order = NewRegistry.BuildDefaultOrderModel();
	const SCardDeckProjection Projection = BuildCardDeckProjection(NewRegistry, Order, NewUi, "home");
	// qm.old 保持隐藏；qm.new 按默认列补位。
	for(const auto &vColumn : Projection.m_aColumns)
	{
		for(const SCardDescriptor *pCard : vColumn)
			EXPECT_NE(pCard->m_Id, "qm.old");
	}
	ASSERT_EQ(Projection.m_aColumns[2].size(), 1);
	EXPECT_EQ(Projection.m_aColumns[2][0]->m_Id, "qm.new");
}
