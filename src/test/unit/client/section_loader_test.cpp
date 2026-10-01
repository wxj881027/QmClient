// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/section_loader.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <sstream>

// Helper: modify the CUIRect inline without calling HSplitTop (not linked in test runner)
static float ConsumeHeight(CUIRect &Rect, float Height)
{
	Rect.h -= Height;
	Rect.y += Height;
	return Height;
}

// Helper: create a simple section with a fixed height
static SSettingsSection MakeTestSection(const char *pName, float Height)
{
	SSettingsSection S;
	S.m_pName = pName;
	S.m_MeasureFn = [Height](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, Height);
	};
	S.m_RenderCompactFn = [Height](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, Height);
	};
	S.m_RenderFullFn = [Height](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, Height);
	};
	return S;
}

static void RunRegisteredFrames(CSectionLoader &Loader, const CUIRect &MainView, const std::function<std::vector<SSettingsSection>()> &MakeSections, float BudgetMs = 100.0f)
{
	int FrameCount = 0;
	do
	{
		Loader.Register(MakeSections());
		Loader.Begin(MainView, BudgetMs);
		++FrameCount;
	} while(Loader.Process() && FrameCount < 100);
}

TEST(SectionLoader, StateTransitions)
{
	CSectionLoader Loader;
	Loader.Register({
		MakeTestSection("Section A", 50.0f),
		MakeTestSection("Section B", 60.0f),
	});

	CUIRect MainView{0, 0, 400, 600};
	Loader.Begin(MainView, 100.0f);

	int Iterations = 0;
	while(Loader.Process() && Iterations < 10)
		++Iterations;

	EXPECT_TRUE(Loader.IsComplete());
}

TEST(SettingsCardDeckDrag, CtrlHeaderStableIdStartsDrag)
{
	SSettingsCardDeckItem Item;
	Item.m_pStableId = "tclient:visual-font-cursor";
	Item.m_pSectionName = "Visual: Font & Cursor";
	Item.m_Column = ESettingsCardDeckColumn::LEFT;
	Item.m_Order = 0;

	SSettingsCardDeckDragStartInput Input;
	Input.m_pItem = &Item;
	Input.m_CtrlPressed = true;
	Input.m_HitRegion = ESettingsCardDragHitRegion::CHROME;

	EXPECT_TRUE(SettingsCardDeckCanStartDrag(Input));
}

TEST(SettingsCardDeckDrag, PressPromotesToDragOnHold)
{
	SSettingsCardDeckItem Item;
	Item.m_pStableId = "tclient:visual-font-cursor";
	Item.m_pSectionName = "Visual: Font & Cursor";
	Item.m_Column = ESettingsCardDeckColumn::LEFT;
	Item.m_Order = 0;
	Item.m_CachedHeight = 120.0f;

	SSettingsCardDeckDragState DragState;
	SettingsCardDeckBeginPress(DragState, Item);
	EXPECT_TRUE(DragState.m_PressPending);
	EXPECT_FALSE(DragState.m_Active);

	EXPECT_TRUE(SettingsCardDeckTryPromotePress(DragState));
	EXPECT_TRUE(DragState.m_Active);
	EXPECT_FALSE(DragState.m_PressPending);
	EXPECT_FLOAT_EQ(DragState.m_PlaceholderHeight, 120.0f);
	EXPECT_EQ(DragState.m_DropIndex, 0);

	SettingsCardDeckClearPress(DragState);
	EXPECT_FALSE(DragState.m_PressPending);
	EXPECT_TRUE(DragState.m_Active);
}

TEST(SettingsCardDeckDrag, PressPromotesFromOriginalItemAfterPointerLeavesHeader)
{
	SSettingsCardDeckItem PressedItem;
	PressedItem.m_pStableId = "tclient:visual-font-cursor";
	PressedItem.m_pSectionName = "Visual: Font & Cursor";
	PressedItem.m_Column = ESettingsCardDeckColumn::LEFT;
	PressedItem.m_Order = 0;
	PressedItem.m_CachedHeight = 120.0f;

	SSettingsCardDeckItem CurrentHoveredItem;
	CurrentHoveredItem.m_pStableId = "tclient:auto-reply";
	CurrentHoveredItem.m_pSectionName = "Auto reply";
	CurrentHoveredItem.m_Column = ESettingsCardDeckColumn::LEFT;
	CurrentHoveredItem.m_Order = 1;
	CurrentHoveredItem.m_CachedHeight = 90.0f;

	SSettingsCardDeckDragState DragState;
	SettingsCardDeckBeginPress(DragState, PressedItem);

	EXPECT_TRUE(SettingsCardDeckTryPromotePress(DragState));
	EXPECT_TRUE(SettingsCardDeckSameStableId(DragState.m_Item, PressedItem));
	EXPECT_FALSE(SettingsCardDeckSameStableId(DragState.m_Item, CurrentHoveredItem));
	EXPECT_FLOAT_EQ(DragState.m_PlaceholderHeight, 120.0f);
	EXPECT_EQ(DragState.m_DropIndex, 0);
}

TEST(SettingsCardDeckDrag, PlainClickContentAndMissingIdDoNotStartDrag)
{
	SSettingsCardDeckItem Item;
	Item.m_pStableId = "tclient:visual-font-cursor";
	Item.m_pSectionName = "Visual: Font & Cursor";
	Item.m_Column = ESettingsCardDeckColumn::LEFT;
	Item.m_Order = 0;

	SSettingsCardDeckDragStartInput Input;
	Input.m_pItem = &Item;
	Input.m_CtrlPressed = false;
	Input.m_HitRegion = ESettingsCardDragHitRegion::CHROME;
	EXPECT_TRUE(SettingsCardDeckCanStartDrag(Input)); // 长按不检查 Ctrl：CHROME + 有效 id → 可拖

	Input.m_CtrlPressed = true;
	Input.m_HitRegion = ESettingsCardDragHitRegion::CONTENT;
	EXPECT_FALSE(SettingsCardDeckCanStartDrag(Input));

	Item.m_pStableId = "";
	Input.m_HitRegion = ESettingsCardDragHitRegion::CHROME;
	EXPECT_FALSE(SettingsCardDeckCanStartDrag(Input));

	Input.m_pItem = nullptr;
	EXPECT_FALSE(SettingsCardDeckCanStartDrag(Input));
}

TEST(SettingsSecondaryPanel, ClampsInsideScreenAndKeepsPreferredSize)
{
	SSecondaryPanelSpec Spec;
	Spec.m_AnchorX = 780.0f;
	Spec.m_AnchorY = 580.0f;
	Spec.m_PreferredWidth = 300.0f;
	Spec.m_PreferredHeight = 120.0f;
	Spec.m_MinWidth = 220.0f;
	Spec.m_MinHeight = 80.0f;
	Spec.m_MaxWidth = 420.0f;
	Spec.m_MaxHeight = 260.0f;
	Spec.m_ScreenWidth = 800.0f;
	Spec.m_ScreenHeight = 600.0f;
	Spec.m_Margin = 8.0f;

	const CUIRect Panel = SettingsSecondaryPanelRect(Spec);
	EXPECT_FLOAT_EQ(Panel.w, 300.0f);
	EXPECT_FLOAT_EQ(Panel.h, 120.0f);
	EXPECT_LE(Panel.x + Panel.w, 792.0f);
	EXPECT_LE(Panel.y + Panel.h, 592.0f);
	EXPECT_GE(Panel.x, 8.0f);
	EXPECT_GE(Panel.y, 8.0f);
}

TEST(SettingsCardDeckDrag, MoveWithinColumnReordersByStableId)
{
	std::vector<std::string> vOrder = {"tclient:visual-font-cursor", "tclient:auto-reply", "tclient:pet"};

	EXPECT_TRUE(SettingsCardDeckMoveWithinColumn(vOrder, "tclient:auto-reply", 0));
	EXPECT_EQ(vOrder, (std::vector<std::string>{"tclient:auto-reply", "tclient:visual-font-cursor", "tclient:pet"}));

	EXPECT_TRUE(SettingsCardDeckMoveWithinColumn(vOrder, "tclient:auto-reply", 3));
	EXPECT_EQ(vOrder, (std::vector<std::string>{"tclient:visual-font-cursor", "tclient:pet", "tclient:auto-reply"}));

	EXPECT_FALSE(SettingsCardDeckMoveWithinColumn(vOrder, "tclient:missing", 1));
	EXPECT_EQ(vOrder, (std::vector<std::string>{"tclient:visual-font-cursor", "tclient:pet", "tclient:auto-reply"}));
}

TEST(SettingsCardDeckDrag, MoveWithinColumnAdjustsDropIndexAfterErase)
{
	std::vector<std::string> vOrder = {"tclient:visual-font-cursor", "tclient:auto-reply", "tclient:pet"};

	EXPECT_TRUE(SettingsCardDeckMoveWithinColumn(vOrder, "tclient:visual-font-cursor", 1));
	EXPECT_EQ(vOrder, (std::vector<std::string>{"tclient:visual-font-cursor", "tclient:auto-reply", "tclient:pet"}));

	EXPECT_TRUE(SettingsCardDeckMoveWithinColumn(vOrder, "tclient:visual-font-cursor", 2));
	EXPECT_EQ(vOrder, (std::vector<std::string>{"tclient:auto-reply", "tclient:visual-font-cursor", "tclient:pet"}));
}

TEST(SettingsCardDeckDrag, MoveBetweenColumnsMovesStableIdToTargetOrder)
{
	std::vector<std::string> vLeftOrder = {"tclient:visual-font-cursor", "tclient:auto-reply", "tclient:pet"};
	std::vector<std::string> vRightOrder = {"tclient:rainbow", "tclient:ghost"};

	EXPECT_TRUE(SettingsCardDeckMoveBetweenColumns(vLeftOrder, vRightOrder, "tclient:auto-reply", 1));
	EXPECT_EQ(vLeftOrder, (std::vector<std::string>{"tclient:visual-font-cursor", "tclient:pet"}));
	EXPECT_EQ(vRightOrder, (std::vector<std::string>{"tclient:rainbow", "tclient:auto-reply", "tclient:ghost"}));

	EXPECT_FALSE(SettingsCardDeckMoveBetweenColumns(vLeftOrder, vRightOrder, "tclient:missing", 0));
	EXPECT_EQ(vLeftOrder, (std::vector<std::string>{"tclient:visual-font-cursor", "tclient:pet"}));
	EXPECT_EQ(vRightOrder, (std::vector<std::string>{"tclient:rainbow", "tclient:auto-reply", "tclient:ghost"}));

	EXPECT_FALSE(SettingsCardDeckMoveBetweenColumns(vLeftOrder, vRightOrder, "", 0));
	EXPECT_EQ(vLeftOrder, (std::vector<std::string>{"tclient:visual-font-cursor", "tclient:pet"}));
	EXPECT_EQ(vRightOrder, (std::vector<std::string>{"tclient:rainbow", "tclient:auto-reply", "tclient:ghost"}));
}

TEST(SettingsCardDeckDrag, DropIndexUsesHoveredCardHalf)
{
	SSettingsCardDeckItem Item;
	Item.m_Order = 2;
	Item.m_Rect = {10.0f, 100.0f, 200.0f, 80.0f};

	EXPECT_EQ(SettingsCardDeckDropIndexForHoveredItem(Item, 120.0f), 2);
	EXPECT_EQ(SettingsCardDeckDropIndexForHoveredItem(Item, 141.0f), 3);
}

TEST(SettingsCardDeckDrag, ColumnDropIndexSupportsBlankSpaceAndColumnTail)
{
	std::vector<SSettingsCardDeckItem> vItems;
	SSettingsCardDeckItem First;
	First.m_pStableId = "tclient:visual-font-cursor";
	First.m_Column = ESettingsCardDeckColumn::LEFT;
	First.m_Order = 0;
	First.m_Rect = {10.0f, 100.0f, 200.0f, 80.0f};
	vItems.push_back(First);
	SSettingsCardDeckItem Second = First;
	Second.m_pStableId = "tclient:auto-reply";
	Second.m_Order = 1;
	Second.m_Rect = {10.0f, 220.0f, 200.0f, 80.0f};
	vItems.push_back(Second);

	EXPECT_EQ(SettingsCardDeckDropIndexForColumnItems(vItems, ESettingsCardDeckColumn::LEFT, 50.0f, 170.0f, -1), 1);
	EXPECT_EQ(SettingsCardDeckDropIndexForColumnItems(vItems, ESettingsCardDeckColumn::LEFT, 50.0f, 360.0f, -1), 2);
	EXPECT_EQ(SettingsCardDeckDropIndexForColumnItems(vItems, ESettingsCardDeckColumn::LEFT, 500.0f, 360.0f, 7), 7);
}

TEST(SettingsCardDeckDrag, DropColumnUsesColumnBounds)
{
	CUIRect LeftColumn{10.0f, 100.0f, 200.0f, 500.0f};
	CUIRect RightColumn{230.0f, 100.0f, 200.0f, 500.0f};

	EXPECT_EQ(SettingsCardDeckDropColumnForMouseX(LeftColumn, RightColumn, 120.0f, ESettingsCardDeckColumn::RIGHT), ESettingsCardDeckColumn::LEFT);
	EXPECT_EQ(SettingsCardDeckDropColumnForMouseX(LeftColumn, RightColumn, 350.0f, ESettingsCardDeckColumn::LEFT), ESettingsCardDeckColumn::RIGHT);
	EXPECT_EQ(SettingsCardDeckDropColumnForMouseX(LeftColumn, RightColumn, 220.0f, ESettingsCardDeckColumn::LEFT), ESettingsCardDeckColumn::LEFT);
}

TEST(SettingsCardDeckDrag, ApplyOrderKeepsUnknownSectionsAfterOrderedCards)
{
	std::vector<SSettingsSection> vSections;
	SSettingsSection Theme;
	Theme.m_pName = "Visual: Font & Cursor";
	Theme.m_pStableCardId = "tclient:visual-font-cursor";
	vSections.push_back(Theme);
	SSettingsSection AutoReply;
	AutoReply.m_pName = "Auto reply";
	AutoReply.m_pStableCardId = "tclient:auto-reply";
	vSections.push_back(AutoReply);
	SSettingsSection Unknown;
	Unknown.m_pName = "Local section";
	vSections.push_back(Unknown);

	SettingsCardDeckApplyOrder(vSections, {"tclient:auto-reply", "tclient:visual-font-cursor", "tclient:stale"});

	ASSERT_EQ(vSections.size(), 3);
	EXPECT_STREQ(vSections[0].m_pStableCardId, "tclient:auto-reply");
	EXPECT_STREQ(vSections[1].m_pStableCardId, "tclient:visual-font-cursor");
	EXPECT_STREQ(vSections[2].m_pName, "Local section");
}

TEST(SettingsCardDeckDrag, ApplyOrderKeepsNonCardSectionSlotsStable)
{
	std::vector<SSettingsSection> vSections;
	SSettingsSection Theme;
	Theme.m_pName = "Visual: Font & Cursor";
	Theme.m_pStableCardId = "tclient:visual-font-cursor";
	vSections.push_back(Theme);
	SSettingsSection Nameplates;
	Nameplates.m_pName = "Visual: Nameplates";
	vSections.push_back(Nameplates);
	SSettingsSection AutoReply;
	AutoReply.m_pName = "Auto reply";
	AutoReply.m_pStableCardId = "tclient:auto-reply";
	vSections.push_back(AutoReply);
	SSettingsSection PlayerIndicator;
	PlayerIndicator.m_pName = "Player Indicator";
	vSections.push_back(PlayerIndicator);
	SSettingsSection Pet;
	Pet.m_pName = "Pet";
	Pet.m_pStableCardId = "tclient:pet";
	vSections.push_back(Pet);

	SettingsCardDeckApplyOrder(vSections, {"tclient:pet", "tclient:auto-reply", "tclient:visual-font-cursor"});

	ASSERT_EQ(vSections.size(), 5);
	EXPECT_STREQ(vSections[0].m_pStableCardId, "tclient:pet");
	EXPECT_STREQ(vSections[1].m_pName, "Visual: Nameplates");
	EXPECT_STREQ(vSections[2].m_pStableCardId, "tclient:auto-reply");
	EXPECT_STREQ(vSections[3].m_pName, "Player Indicator");
	EXPECT_STREQ(vSections[4].m_pStableCardId, "tclient:visual-font-cursor");
}

TEST(SettingsCardDeckDrag, AutoScrollDeltaUsesEdgeBandsOnly)
{
	EXPECT_LT(SettingsCardDeckAutoScrollDelta(104.0f, 100.0f, 400.0f, 24.0f, 60.0f), 0.0f);
	EXPECT_GT(SettingsCardDeckAutoScrollDelta(396.0f, 100.0f, 400.0f, 24.0f, 60.0f), 0.0f);
	EXPECT_FLOAT_EQ(SettingsCardDeckAutoScrollDelta(250.0f, 100.0f, 400.0f, 24.0f, 60.0f), 0.0f);
	EXPECT_FLOAT_EQ(SettingsCardDeckAutoScrollDelta(104.0f, 100.0f, 400.0f, 0.0f, 60.0f), 0.0f);
}

TEST(SettingsCardDeckDrag, VisualHelpersIdentifyDraggedItemAndIndicator)
{
	SSettingsCardDeckItem Item;
	Item.m_pStableId = "tclient:auto-reply";
	Item.m_Column = ESettingsCardDeckColumn::LEFT;
	Item.m_Order = 1;
	Item.m_Rect = {20.0f, 120.0f, 240.0f, 80.0f};

	SSettingsCardDeckDragState DragState;
	DragState.m_Active = true;
	DragState.m_Item = Item;

	EXPECT_TRUE(SettingsCardDeckIsDraggingItem(DragState, Item));

	SSettingsCardDeckItem Other = Item;
	Other.m_pStableId = "tclient:pet";
	EXPECT_FALSE(SettingsCardDeckIsDraggingItem(DragState, Other));

	CUIRect IndicatorTop = SettingsCardDeckDropIndicatorRect(Item, 1, 4.0f);
	EXPECT_FLOAT_EQ(IndicatorTop.y, 118.0f);
	EXPECT_FLOAT_EQ(IndicatorTop.h, 4.0f);

	CUIRect IndicatorBottom = SettingsCardDeckDropIndicatorRect(Item, 2, 4.0f);
	EXPECT_FLOAT_EQ(IndicatorBottom.y, 198.0f);
	EXPECT_FLOAT_EQ(IndicatorBottom.h, 4.0f);
}

TEST(SettingsCardDeckDrag, ProxyRectFollowsMouseAndKeepsCardSize)
{
	SSettingsCardDeckItem Item;
	Item.m_Rect = {20.0f, 120.0f, 240.0f, 80.0f};

	CUIRect Proxy = SettingsCardDeckProxyRect(Item, 400.0f, 300.0f);
	EXPECT_FLOAT_EQ(Proxy.x, 280.0f);
	EXPECT_FLOAT_EQ(Proxy.y, 260.0f);
	EXPECT_FLOAT_EQ(Proxy.w, 240.0f);
	EXPECT_FLOAT_EQ(Proxy.h, 80.0f);
}

TEST(SectionLoader, FrameBudgetTruncation)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	std::vector<SSettingsSection> vSections;
	for(int i = 0; i < 30; ++i)
	{
		char aName[32];
		str_format(aName, sizeof(aName), "Section %d", i);
		vSections.push_back(MakeTestSection(aName, 30.0f));
	}
	Loader.Register(std::move(vSections));

	CUIRect MainView{0, 0, 400, 800};
	Loader.Begin(MainView, 0.1);

	int FrameCount = 0;
	while(Loader.Process() && FrameCount < 100)
		++FrameCount;

	// With 0.1ms budget and 30 sections, it MUST take more than 1 frame
	EXPECT_GT(FrameCount, 1);
}

TEST(SectionLoader, FullSectionsRenderEveryFrame)
{
	CSectionLoader Loader;
	bool FullRenderCalled = false;
	int ConfigValue = 42;

	SSettingsSection S;
	S.m_pName = "DirtyTest";
	S.m_DependencyConfigInts.push_back(&ConfigValue);
	S.m_MeasureFn = [](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderCompactFn = [](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderFullFn = [&FullRenderCalled](CUIRect &Rect) -> float {
		FullRenderCalled = true;
		return ConsumeHeight(Rect, 10.0f);
	};

	CUIRect MainView{0, 0, 400, 600};
	auto MakeSections = [&]() {
		return std::vector<SSettingsSection>{S};
	};

	// First frame: m_Dirty=true → FullRender must be called
	RunRegisteredFrames(Loader, MainView, MakeSections);
	EXPECT_TRUE(FullRenderCalled);

	// Second frame: config unchanged, but immediate-mode UI still renders.
	FullRenderCalled = false;
	RunRegisteredFrames(Loader, MainView, MakeSections);
	EXPECT_TRUE(FullRenderCalled);

	// Modify config → dirty flag triggers FullRender again
	ConfigValue = 99;
	Loader.SetDirtyByConfig(&ConfigValue);
	FullRenderCalled = false;
	RunRegisteredFrames(Loader, MainView, MakeSections);
	EXPECT_TRUE(FullRenderCalled);
}

TEST(SectionLoader, FullSectionsIgnoreFrameBudget)
{
	CSectionLoader Loader;
	int FullRenderCount = 0;
	auto MakeSections = [&]() {
		std::vector<SSettingsSection> vSections;
		for(int SectionIndex = 0; SectionIndex < 3; ++SectionIndex)
		{
			SSettingsSection S = MakeTestSection(SectionIndex == 0 ? "Full A" : SectionIndex == 1 ? "Full B" :
														"Full C",
				10.0f);
			S.m_RenderFullFn = [&FullRenderCount](CUIRect &Rect) -> float {
				++FullRenderCount;
				return ConsumeHeight(Rect, 10.0f);
			};
			vSections.push_back(S);
		}
		return vSections;
	};

	CUIRect MainView{0, 0, 400, 600};
	RunRegisteredFrames(Loader, MainView, MakeSections);
	ASSERT_TRUE(Loader.IsComplete());

	FullRenderCount = 0;
	RunRegisteredFrames(Loader, MainView, MakeSections, 0.0f);
	EXPECT_EQ(FullRenderCount, 3);
	EXPECT_TRUE(Loader.IsComplete());
}

TEST(SectionLoader, FarFullSectionsAdvanceWithoutRendering)
{
	CSectionLoader Loader;
	int FarFullRenderCount = 0;

	SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
	SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
	Far.m_RenderFullFn = [&FarFullRenderCount](CUIRect &Rect) -> float {
		++FarFullRenderCount;
		return ConsumeHeight(Rect, 50.0f);
	};

	Loader.SetProgressiveEnabled(false);
	Loader.Register({Top, Far});
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);

	EXPECT_FALSE(Loader.Process());
	EXPECT_TRUE(Loader.IsComplete());
	EXPECT_EQ(FarFullRenderCount, 0);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 950.0f);
}

TEST(SectionLoader, DirtyFarFullSectionUpdatesMeasuredHeightWithoutRendering)
{
	CSectionLoader Loader;
	int FarMeasureHeight = 50;
	int FarFullRenderCount = 0;

	auto MakeSections = [&]() {
		SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
		SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
		Far.m_MeasureFn = [&FarMeasureHeight](CUIRect &Rect) -> float {
			return ConsumeHeight(Rect, (float)FarMeasureHeight);
		};
		Far.m_RenderFullFn = [&FarFullRenderCount, &FarMeasureHeight](CUIRect &Rect) -> float {
			++FarFullRenderCount;
			return ConsumeHeight(Rect, (float)FarMeasureHeight);
		};
		Far.m_DependencyConfigInts = {&FarMeasureHeight};
		return std::vector<SSettingsSection>{Top, Far};
	};

	Loader.SetProgressiveEnabled(false);
	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 950.0f);

	FarMeasureHeight = 80;
	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);

	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(FarFullRenderCount, 0);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 980.0f);
}

TEST(SectionLoader, FarFullSectionRendersAfterScrollingIntoView)
{
	CSectionLoader Loader;
	int FarFullRenderCount = 0;

	auto MakeSections = [&]() {
		SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
		SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
		Far.m_RenderFullFn = [&FarFullRenderCount](CUIRect &Rect) -> float {
			++FarFullRenderCount;
			return ConsumeHeight(Rect, 50.0f);
		};
		return std::vector<SSettingsSection>{Top, Far};
	};

	Loader.SetProgressiveEnabled(false);
	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(FarFullRenderCount, 0);

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, CUIRect{0, 900.0f, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(FarFullRenderCount, 1);
}

TEST(SectionLoader, DeferredFarMeasurementRequiresKnownHeight)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(false);
	Loader.SetDeferredFarMeasurementEnabled(true);
	int FarMeasureCount = 0;

	SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
	SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
	Far.m_MeasureFn = [&FarMeasureCount](CUIRect &Rect) -> float {
		++FarMeasureCount;
		return ConsumeHeight(Rect, 50.0f);
	};

	Loader.Register({Top, Far});
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);

	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(FarMeasureCount, 1);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 950.0f);
}

TEST(SectionLoader, DeferredFarMeasurementUsesCleanCachedHeight)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(false);
	Loader.SetDeferredFarMeasurementEnabled(true);
	int FarMeasureCount = 0;
	int FarFullRenderCount = 0;

	auto MakeSections = [&]() {
		SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
		SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
		Far.m_MeasureFn = [&FarMeasureCount](CUIRect &Rect) -> float {
			++FarMeasureCount;
			return ConsumeHeight(Rect, 50.0f);
		};
		Far.m_RenderFullFn = [&FarFullRenderCount](CUIRect &Rect) -> float {
			++FarFullRenderCount;
			return ConsumeHeight(Rect, 50.0f);
		};
		return std::vector<SSettingsSection>{Top, Far};
	};

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(FarMeasureCount, 1);
	EXPECT_EQ(FarFullRenderCount, 0);

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(FarMeasureCount, 1);
	EXPECT_EQ(FarFullRenderCount, 0);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 950.0f);
}

TEST(SectionLoader, DeferredFarMeasurementSkipsNonFullCleanFarSection)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	Loader.SetDeferredFarMeasurementEnabled(true);
	int FarMeasureCount = 0;
	int FarFullRenderCount = 0;

	auto MakeSections = [&]() {
		SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
		SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
		Far.m_MeasureFn = [&FarMeasureCount](CUIRect &Rect) -> float {
			++FarMeasureCount;
			return ConsumeHeight(Rect, 50.0f);
		};
		Far.m_RenderFullFn = [&FarFullRenderCount](CUIRect &Rect) -> float {
			++FarFullRenderCount;
			return ConsumeHeight(Rect, 50.0f);
		};
		return std::vector<SSettingsSection>{Top, Far};
	};

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(FarMeasureCount, 1);

	Loader.SetProgressiveEnabled(false);
	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());

	EXPECT_EQ(FarMeasureCount, 1);
	EXPECT_EQ(FarFullRenderCount, 0);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 950.0f);
}

TEST(SectionLoader, DeferredFarMeasurementDoesNotSkipDirtyHeight)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(false);
	Loader.SetDeferredFarMeasurementEnabled(true);
	int FarMeasureHeight = 50;
	int FarMeasureCount = 0;

	auto MakeSections = [&]() {
		SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
		SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
		Far.m_MeasureFn = [&FarMeasureHeight, &FarMeasureCount](CUIRect &Rect) -> float {
			++FarMeasureCount;
			return ConsumeHeight(Rect, (float)FarMeasureHeight);
		};
		Far.m_DependencyConfigInts = {&FarMeasureHeight};
		return std::vector<SSettingsSection>{Top, Far};
	};

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(FarMeasureCount, 1);

	FarMeasureHeight = 80;
	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());

	EXPECT_EQ(FarMeasureCount, 2);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 980.0f);
}

TEST(SectionLoader, FarCompactSectionAdvancesWithoutRendering)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	int FarCompactRenderCount = 0;

	auto MakeSections = [&]() {
		SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
		Far.m_RenderCompactFn = [&FarCompactRenderCount](CUIRect &Rect) -> float {
			++FarCompactRenderCount;
			return ConsumeHeight(Rect, 50.0f);
		};
		return std::vector<SSettingsSection>{Far};
	};

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_TRUE(Loader.Process());

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(FarCompactRenderCount, 1);

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, CUIRect{0, 1000.0f, 400, 240}, 0.0f);
	EXPECT_TRUE(Loader.Process());

	EXPECT_EQ(FarCompactRenderCount, 1);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 50.0f);
}

TEST(SectionLoader, CompactRenderUpdatesCachedHeightForSkippedFrames)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	int CompactRenderCount = 0;

	auto MakeSections = [&]() {
		SSettingsSection Section = MakeTestSection("Compact Height", 50.0f);
		Section.m_RenderCompactFn = [&CompactRenderCount](CUIRect &Rect) -> float {
			++CompactRenderCount;
			return ConsumeHeight(Rect, 80.0f);
		};
		Section.m_RenderFullFn = [](CUIRect &Rect) -> float {
			return ConsumeHeight(Rect, 80.0f);
		};
		return std::vector<SSettingsSection>{Section};
	};

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 0.0f);
	EXPECT_TRUE(Loader.Process());

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 0.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(CompactRenderCount, 1);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 80.0f);

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, CUIRect{0, 1000.0f, 400, 240}, 0.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(CompactRenderCount, 1);
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 80.0f);
}

TEST(SectionLoader, FullSectionsAfterUnfinishedSectionStillRender)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	int LastFullRenderCount = 0;

	auto MakeSections = [&](const char *pMiddleName) {
		std::vector<SSettingsSection> vSections;
		vSections.push_back(MakeTestSection("Full A", 10.0f));
		vSections.push_back(MakeTestSection(pMiddleName, 10.0f));
		SSettingsSection Last = MakeTestSection("Full C", 10.0f);
		Last.m_RenderFullFn = [&LastFullRenderCount](CUIRect &Rect) -> float {
			++LastFullRenderCount;
			return ConsumeHeight(Rect, 10.0f);
		};
		vSections.push_back(Last);
		return vSections;
	};

	Loader.Register(MakeSections("Middle B"));
	CUIRect MainView{0, 0, 400, 600};
	Loader.Begin(MainView, 100.0f);
	while(Loader.Process()) {}
	ASSERT_TRUE(Loader.IsComplete());

	LastFullRenderCount = 0;
	Loader.Register(MakeSections("New Middle B"));
	Loader.Begin(MainView, 0.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(LastFullRenderCount, 1);
}

TEST(SectionLoader, ScrollOffsetPromotesScrolledIntoViewSection)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	int CompactRenderCount = 0;

	SSettingsSection First = MakeTestSection("Tall Top", 900.0f);
	SSettingsSection Second = MakeTestSection("Scrolled Into View", 10.0f);
	Second.m_RenderCompactFn = [&CompactRenderCount](CUIRect &Rect) -> float {
		++CompactRenderCount;
		return ConsumeHeight(Rect, 10.0f);
	};
	auto MakeSections = [&]() {
		return std::vector<SSettingsSection>{First, Second};
	};

	CUIRect MainView{0, 0, 400, 600};
	CUIRect ScrolledViewport{0, 500.0f, 400, 600};
	Loader.Register(MakeSections());
	Loader.Begin(MainView, ScrolledViewport, 100.0f);
	EXPECT_TRUE(Loader.Process());

	Loader.Register(MakeSections());
	Loader.Begin(MainView, ScrolledViewport, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(CompactRenderCount, 1);
}

TEST(SectionLoader, MeasureFullHeightConsistency)
{
	CSectionLoader Loader;
	float MeasureHeight = 0.0f;
	float FullHeight = 0.0f;

	SSettingsSection S;
	S.m_pName = "HeightTest";
	S.m_MeasureFn = [&MeasureHeight](CUIRect &Rect) -> float {
		MeasureHeight = 45.0f;
		return ConsumeHeight(Rect, 45.0f);
	};
	S.m_RenderCompactFn = [](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, 45.0f);
	};
	S.m_RenderFullFn = [&FullHeight](CUIRect &Rect) -> float {
		FullHeight = 45.0f;
		return ConsumeHeight(Rect, 45.0f);
	};

	CUIRect MainView{0, 0, 400, 800};
	RunRegisteredFrames(Loader, MainView, [&]() {
		return std::vector<SSettingsSection>{S};
	});

	EXPECT_FLOAT_EQ(MeasureHeight, FullHeight);
}

TEST(SectionLoader, ReRegisterSameSectionPreservesProgress)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	int MeasureCount = 0;
	int CompactCount = 0;
	int FullCount = 0;

	auto MakeCountingSection = [&]() {
		SSettingsSection S;
		S.m_pName = "FrameLocalSection";
		S.m_MeasureFn = [&MeasureCount](CUIRect &Rect) -> float {
			++MeasureCount;
			return ConsumeHeight(Rect, 10.0f);
		};
		S.m_RenderCompactFn = [&CompactCount](CUIRect &Rect) -> float {
			++CompactCount;
			return ConsumeHeight(Rect, 10.0f);
		};
		S.m_RenderFullFn = [&FullCount](CUIRect &Rect) -> float {
			++FullCount;
			return ConsumeHeight(Rect, 10.0f);
		};
		return S;
	};

	CUIRect MainView{0, 0, 400, 600};
	Loader.Register({MakeCountingSection()});
	Loader.Begin(MainView, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(MeasureCount, 1);
	EXPECT_EQ(CompactCount, 0);
	EXPECT_EQ(FullCount, 0);

	Loader.Register({MakeCountingSection()});
	Loader.Begin(MainView, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(MeasureCount, 1);
	EXPECT_EQ(CompactCount, 1);
	EXPECT_EQ(FullCount, 0);

	Loader.Register({MakeCountingSection()});
	Loader.Begin(MainView, 100.0f);
	while(Loader.Process()) {}
	EXPECT_EQ(MeasureCount, 1);
	EXPECT_EQ(CompactCount, 1);
	EXPECT_EQ(FullCount, 1);
	EXPECT_TRUE(Loader.IsComplete());

	Loader.Register({MakeCountingSection()});
	Loader.Begin(MainView, 100.0f);
	while(Loader.Process()) {}
	EXPECT_EQ(MeasureCount, 1);
	EXPECT_EQ(CompactCount, 1);
	EXPECT_EQ(FullCount, 2);
	EXPECT_TRUE(Loader.IsComplete());
}

TEST(SectionLoader, ReRegisterSameSectionUsesLatestCallbacks)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	int RenderedVersion = 0;

	auto MakeVersionedSection = [&](int Version) {
		SSettingsSection S;
		S.m_pName = "FrameLocalSection";
		S.m_MeasureFn = [](CUIRect &Rect) -> float {
			return ConsumeHeight(Rect, 10.0f);
		};
		S.m_RenderCompactFn = [Version, &RenderedVersion](CUIRect &Rect) -> float {
			RenderedVersion = Version;
			return ConsumeHeight(Rect, 10.0f);
		};
		S.m_RenderFullFn = [Version, &RenderedVersion](CUIRect &Rect) -> float {
			RenderedVersion = Version;
			return ConsumeHeight(Rect, 10.0f);
		};
		return S;
	};

	CUIRect MainView{0, 0, 400, 600};
	Loader.Register({MakeVersionedSection(1)});
	Loader.Begin(MainView, 100.0f);
	EXPECT_TRUE(Loader.Process());

	Loader.Register({MakeVersionedSection(2)});
	Loader.Begin(MainView, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(RenderedVersion, 2);

	Loader.Register({MakeVersionedSection(3)});
	Loader.Begin(MainView, 100.0f);
	while(Loader.Process()) {}
	EXPECT_EQ(RenderedVersion, 3);
}

TEST(SectionLoader, NonProgressiveModeRendersFullSectionOnFirstProcess)
{
	CSectionLoader Loader;
	int MeasureCount = 0;
	int CompactCount = 0;
	int FullCount = 0;

	SSettingsSection S;
	S.m_pName = "ImmediateFullSection";
	S.m_MeasureFn = [&MeasureCount](CUIRect &Rect) -> float {
		++MeasureCount;
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderCompactFn = [&CompactCount](CUIRect &Rect) -> float {
		++CompactCount;
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderFullFn = [&FullCount](CUIRect &Rect) -> float {
		++FullCount;
		return ConsumeHeight(Rect, 10.0f);
	};

	Loader.SetProgressiveEnabled(false);
	Loader.Register({S});
	Loader.Begin(CUIRect{0, 0, 400, 600}, 0.0f);

	EXPECT_FALSE(Loader.Process());
	EXPECT_TRUE(Loader.IsComplete());
	EXPECT_EQ(MeasureCount, 1);
	EXPECT_EQ(CompactCount, 0);
	EXPECT_EQ(FullCount, 1);
}

TEST(SectionLoader, ProcessDoesNotRetainCallbacksAcrossFrames)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(true);
	int RenderedVersion = 0;

	SSettingsSection S;
	S.m_pName = "FrameLocalSection";
	S.m_MeasureFn = [](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderCompactFn = [&RenderedVersion](CUIRect &Rect) -> float {
		RenderedVersion = 1;
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderFullFn = [&RenderedVersion](CUIRect &Rect) -> float {
		RenderedVersion = 1;
		return ConsumeHeight(Rect, 10.0f);
	};

	CUIRect MainView{0, 0, 400, 600};
	Loader.Register({S});
	Loader.Begin(MainView, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(RenderedVersion, 0);

	Loader.Begin(MainView, 100.0f);
	EXPECT_TRUE(Loader.Process());
	EXPECT_EQ(RenderedVersion, 0);

	Loader.Begin(MainView, 100.0f);
	while(Loader.Process()) {}
	EXPECT_EQ(RenderedVersion, 0);
	EXPECT_TRUE(Loader.IsComplete());
}

TEST(SectionLoader, WarmupWithoutCache)
{
	CSectionLoader Loader;
	Loader.Register({MakeTestSection("A", 50.0f)});

	// nullptr cache → warmup should immediately report done
	EXPECT_TRUE(Loader.Warmup(nullptr, 3.0f));
	EXPECT_TRUE(Loader.IsWarmupComplete());

	// Invalid cache → same
	SSessionUiCache InvalidCache;
	InvalidCache.m_Valid = false;
	EXPECT_TRUE(Loader.Warmup(&InvalidCache, 3.0f));
	EXPECT_TRUE(Loader.IsWarmupComplete());
}

TEST(SectionLoader, WarmupWithCache)
{
	CSectionLoader Loader;
	Loader.Register({
		MakeTestSection("A", 50.0f),
		MakeTestSection("B", 50.0f),
		MakeTestSection("C", 50.0f),
	});

	CUIRect MainView{0, 0, 400, 600};
	Loader.Begin(MainView, 5.0f);

	SSessionUiCache Cache;
	Cache.m_LastTClientTab = 0;
	Cache.m_LastScrollY = 0.0f;
	Cache.m_Valid = true;

	bool Done = false;
	for(int i = 0; i < 20 && !Done; ++i)
		Done = Loader.Warmup(&Cache, 100.0f);

	EXPECT_TRUE(Done);
	EXPECT_TRUE(Loader.IsWarmupComplete());
}

TEST(SectionLoader, SessionCacheRoundTripsLastTabAndScroll)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	SSessionUiCache Saved;
	Saved.m_LastSettingsPage = 8;
	Saved.m_LastTClientTab = 0;
	Saved.m_LastQmTab = -1;
	Saved.m_LastScrollY = -240.0f;
	Saved.m_RuntimeKey.m_ViewportWidth = 1600;
	Saved.m_RuntimeKey.m_ViewportHeight = 900;
	Saved.m_RuntimeKey.m_UiScale = 100;
	Saved.m_RuntimeKey.m_ConfigHash = 11;
	Saved.m_RuntimeKey.m_LanguageHash = 22;
	Saved.m_RuntimeKey.m_FontHash = 33;
	Saved.m_RuntimeKey.m_BackendHash = 44;
	Saved.m_RuntimeKey.m_WindowHash = 55;
	Saved.m_Valid = true;

	CSectionLoader::SaveSessionCache(Saved, "qmclient/settings_section_cache_metadata.cfg", pStorage.get());

	SSessionUiCache Loaded;
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Loaded, "qmclient/settings_section_cache_metadata.cfg", pStorage.get()));
	EXPECT_EQ(Loaded.m_LastSettingsPage, 8);
	EXPECT_EQ(Loaded.m_LastTClientTab, 0);
	EXPECT_EQ(Loaded.m_LastQmTab, -1);
	EXPECT_FLOAT_EQ(Loaded.m_LastScrollY, -240.0f);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportWidth, 1600);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportHeight, 900);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_UiScale, 100);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ConfigHash, 11u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_LanguageHash, 22u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_FontHash, 33u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_BackendHash, 44u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_WindowHash, 55u);
	EXPECT_TRUE(Loaded.m_Valid);
}

TEST(SectionLoader, SessionCacheLoadKeepsWorkingWhenRuntimeKeyFieldsAreMissing)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	const char *pPath = "settings_section_cache_metadata.cfg";
	const char *pLegacyContents =
		"settings_page=8\n"
		"tab_tclient=0\n"
		"tab_qm=-1\n"
		"scroll_y=-240.000000\n";

	char aSavePath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, pPath, aSavePath, sizeof(aSavePath));
	IOHANDLE File = io_open(aSavePath, IOFLAG_WRITE);
	ASSERT_TRUE(File != nullptr);
	io_write(File, pLegacyContents, str_length(pLegacyContents));
	io_close(File);

	SSessionUiCache Loaded;
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Loaded, pPath, pStorage.get()));
	EXPECT_EQ(Loaded.m_LastSettingsPage, 8);
	EXPECT_EQ(Loaded.m_LastTClientTab, 0);
	EXPECT_EQ(Loaded.m_LastQmTab, -1);
	EXPECT_FLOAT_EQ(Loaded.m_LastScrollY, -240.0f);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportWidth, 0);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportHeight, 0);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_UiScale, 0);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ConfigHash, 0u);
	EXPECT_TRUE(Loaded.m_Valid);
}

TEST(SectionLoader, InvalidateAfterLanguageSwitch)
{
	CSectionLoader Loader;
	int RenderCount = 0;

	SSettingsSection S;
	S.m_pName = "CacheTest";
	S.m_MeasureFn = [](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderCompactFn = [](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, 10.0f);
	};
	S.m_RenderFullFn = [&RenderCount](CUIRect &Rect) -> float {
		++RenderCount;
		return ConsumeHeight(Rect, 10.0f);
	};

	CUIRect MainView{0, 0, 400, 600};
	auto MakeSections = [&]() {
		return std::vector<SSettingsSection>{S};
	};

	// First frame: renders
	RunRegisteredFrames(Loader, MainView, MakeSections);
	EXPECT_EQ(RenderCount, 1);

	// Second frame: renders again because UI is immediate-mode
	RunRegisteredFrames(Loader, MainView, MakeSections);
	EXPECT_EQ(RenderCount, 2);

	// Simulate language change
	Loader.InvalidateCache();
	RunRegisteredFrames(Loader, MainView, MakeSections);
	EXPECT_EQ(RenderCount, 3);
}

TEST(SectionLoader, RuntimeKeyChangeInvalidatesCachedHeightsWithReason)
{
	CSectionLoader Loader;
	int MeasuredHeight = 40;

	auto MakeSections = [&]() {
		SSettingsSection Section;
		Section.m_pName = "RuntimeKeyDirty";
		Section.m_MeasureFn = [&MeasuredHeight](CUIRect &Rect) -> float {
			return ConsumeHeight(Rect, (float)MeasuredHeight);
		};
		Section.m_RenderCompactFn = Section.m_MeasureFn;
		Section.m_RenderFullFn = Section.m_MeasureFn;
		return std::vector<SSettingsSection>{Section};
	};

	SSettingsSectionCacheRuntimeKey RuntimeKey;
	RuntimeKey.m_ViewportWidth = 400;
	RuntimeKey.m_ViewportHeight = 240;
	RuntimeKey.m_UiScale = 100;
	RuntimeKey.m_ConfigHash = 1;
	RuntimeKey.m_LanguageHash = 2;
	RuntimeKey.m_FontHash = 3;
	RuntimeKey.m_BackendHash = 4;
	RuntimeKey.m_WindowHash = 5;
	Loader.SetRuntimeKey(RuntimeKey);

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 40.0f);

	MeasuredHeight = 70;
	RuntimeKey.m_UiScale = 125;
	Loader.SetRuntimeKey(RuntimeKey);
	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 70.0f);
	EXPECT_EQ(Loader.LastFrameStats().m_DirtyReason, ESettingsCacheDirtyReason::UI_SCALE);
	EXPECT_GE(Loader.LastFrameStats().m_LayoutDirtySections, 1);
}

TEST(SectionLoader, DeferredFarMeasurementTracksVisibleAndSkippedTelemetry)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(false);
	Loader.SetDeferredFarMeasurementEnabled(true);

	auto MakeSections = [&]() {
		SSettingsSection Top = MakeTestSection("Tall Top", 900.0f);
		SSettingsSection Far = MakeTestSection("Far Section", 50.0f);
		return std::vector<SSettingsSection>{Top, Far};
	};

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());

	EXPECT_EQ(Loader.LastFrameStats().m_SectionsTotal, 2);
	EXPECT_EQ(Loader.LastFrameStats().m_SectionsVisible, 1);
	EXPECT_EQ(Loader.LastFrameStats().m_SectionsSkipped, 1);
	EXPECT_EQ(Loader.LastFrameStats().m_LayoutDirtySections, 0);
}

TEST(SectionLoader, DirtyReasonTelemetryResetsAfterCleanFrame)
{
	CSectionLoader Loader;
	int MeasuredHeight = 40;

	auto MakeSections = [&]() {
		SSettingsSection Section;
		Section.m_pName = "DirtyReasonReset";
		Section.m_MeasureFn = [&MeasuredHeight](CUIRect &Rect) -> float {
			return ConsumeHeight(Rect, (float)MeasuredHeight);
		};
		Section.m_RenderCompactFn = Section.m_MeasureFn;
		Section.m_RenderFullFn = Section.m_MeasureFn;
		return std::vector<SSettingsSection>{Section};
	};

	SSettingsSectionCacheRuntimeKey RuntimeKey;
	RuntimeKey.m_ViewportWidth = 400;
	RuntimeKey.m_ViewportHeight = 240;
	RuntimeKey.m_UiScale = 100;
	RuntimeKey.m_ConfigHash = 1;
	RuntimeKey.m_LanguageHash = 2;
	RuntimeKey.m_FontHash = 3;
	RuntimeKey.m_BackendHash = 4;
	RuntimeKey.m_WindowHash = 5;
	Loader.SetRuntimeKey(RuntimeKey);

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());

	RuntimeKey.m_UiScale = 125;
	Loader.SetRuntimeKey(RuntimeKey);
	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(Loader.LastFrameStats().m_DirtyReason, ESettingsCacheDirtyReason::UI_SCALE);
	EXPECT_GE(Loader.LastFrameStats().m_LayoutDirtySections, 1);

	Loader.Register(MakeSections());
	Loader.Begin(CUIRect{0, 0, 400, 240}, 100.0f);
	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(Loader.LastFrameStats().m_LayoutDirtySections, 0);
	EXPECT_EQ(Loader.LastFrameStats().m_DirtyReason, ESettingsCacheDirtyReason::NONE);
}

TEST(SectionLoader, VisibilityTelemetryUsesMeasuredSectionRect)
{
	CSectionLoader Loader;
	int MeasuredHeight = 300;

	SSettingsSection Section;
	Section.m_pName = "MeasuredVisibility";
	Section.m_MeasureFn = [&MeasuredHeight](CUIRect &Rect) -> float {
		return ConsumeHeight(Rect, (float)MeasuredHeight);
	};
	Section.m_RenderCompactFn = Section.m_MeasureFn;
	Section.m_RenderFullFn = Section.m_MeasureFn;

	Loader.Register({Section});
	Loader.Begin(CUIRect{0, 0, 400, 240}, CUIRect{0, 400.0f, 400, 240}, 100.0f);

	EXPECT_FALSE(Loader.Process());
	EXPECT_EQ(Loader.LastFrameStats().m_SectionsTotal, 1);
	EXPECT_EQ(Loader.LastFrameStats().m_SectionsVisible, 1);
	EXPECT_EQ(Loader.LastFrameStats().m_SectionsSkipped, 0);
}

TEST(SectionLoader, ExplicitViewportDoesNotShiftContentGeometry)
{
	CSectionLoader Loader;
	Loader.SetProgressiveEnabled(false);
	Loader.Register({MakeTestSection("Top", 900.0f), MakeTestSection("Visible after scroll", 50.0f)});
	Loader.Begin(CUIRect{0, 0, 400, 240}, CUIRect{0, 900.0f, 400, 240}, 100.0f);

	EXPECT_FALSE(Loader.Process());
	EXPECT_FLOAT_EQ(Loader.GetRunningColumn().y, 950.0f);
}

TEST(SectionLoader, RejectsVisibleSummarySectionNames)
{
	EXPECT_FALSE(CSectionLoader::IsVisibleSummarySectionName("DeferredSummary:Mouse"));
	EXPECT_FALSE(CSectionLoader::IsVisibleSummarySectionName("CompactSummary:Controls"));
	EXPECT_FALSE(CSectionLoader::IsVisibleSummarySectionName("SummaryBlock:Controls"));
	EXPECT_TRUE(CSectionLoader::IsVisibleSummarySectionName("Controls:Mouse"));
	EXPECT_TRUE(CSectionLoader::IsVisibleSummarySectionName("TClient:Pet"));
}
