#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(QmTooltips, FriendNotesUseBoundedTooltipRects)
{
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string FriendsRender = FunctionBody(BrowserSource, "void CMenus::RenderServerbrowserFriends(CUIRect View)");
	EXPECT_NE(FriendsRender.find("DoToolTip(pListItemId, &Rect, TooltipText.c_str(), 320.0f);"), std::string::npos);
	EXPECT_NE(FriendsRender.find("DoToolTip(pSkinTooltipId, &Skin, Friend.Skin());"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsNumericFieldRectanglesInitializeBeforeLayoutBranching)
{
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Body = FunctionBody(Source, "bool NumericField(const IUiContext &Ctx");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CUIRect Label{}, Controls{}, ValueRect{}, ScrollBar{}, InputField{};"), std::string::npos);
	EXPECT_NE(Body.find("Controls.VSplitRight(ValueWidth, &ScrollBar, &InputField);"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_pTrailingText = HasSuffix ? Options.m_pSuffix : nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NumericInputKeepsValueAndUnitInOneGeometry)
{
	const std::string Forms = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiForms.h");
	EXPECT_NE(Header.find("struct SInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Header.find("ResolveInlineTrailingTextLayout("), std::string::npos);
	EXPECT_NE(Forms.find("const SInlineTrailingTextLayout InlineLayout = ResolveInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
	EXPECT_EQ(Forms.find("m_pInactiveDisplayText"), std::string::npos);
}

TEST(QmNewUiMenuBranches, EditBoxesRequirePressInsideConfiguredHitRect)
{
	const std::string Source = ReadTextFile("src/game/client/ui.cpp");
	const std::string Body = FunctionBody(Source, "bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("QmEditBoxShouldStartActivation(Inside, MouseButtonClicked(0))"), std::string::npos);
	EXPECT_EQ(Body.find("else if(Inside)"), std::string::npos);
	EXPECT_EQ(Body.find("else if(HotItem() == pLineInput)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SharedListEntryRevealUsesElapsedGapWithoutMovingScrollGeometry)
{
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");

	EXPECT_EQ(ListBoxSource.find("PerfFrame()"), std::string::npos);
	EXPECT_EQ(ListBoxSource.find("QmListBoxShouldRearmInitialScroll"), std::string::npos);
	EXPECT_EQ(ListBoxHeader.find("m_LastRenderFrame"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_InitialScrollPending = true;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_EntryAnimationStartTime"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("QmListBoxEntryAnimatedRect"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxShouldStartEntryAnimation"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxEntryOffset"), std::string::npos);
	const std::string DoNextRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoNextRow()");
	const size_t AddRectPos = DoNextRowBody.find("m_ScrollRegion.AddRect(m_RowView);");
	const size_t AnimateRectPos = DoNextRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(AddRectPos, std::string::npos);
	ASSERT_NE(AnimateRectPos, std::string::npos);
	EXPECT_LT(AddRectPos, AnimateRectPos);
	const std::string DoCustomRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoCustomRow(float Height, bool ScrollHere)");
	const size_t CustomAddRectPos = DoCustomRowBody.find("m_ScrollRegion.AddRect(Item.m_Rect, ScrollHere);");
	const size_t CustomAnimateRectPos = DoCustomRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(CustomAddRectPos, std::string::npos);
	ASSERT_NE(CustomAnimateRectPos, std::string::npos);
	EXPECT_LT(CustomAddRectPos, CustomAnimateRectPos);
	EXPECT_NE(ListBoxSource.find("if(!RenderOnly && EntryAnimationEnabled"), std::string::npos);
}

TEST(QmNewUiMenuBranches, KeyReaderUsesOneOuterShellForValueAndDeleteAction)
{
	const std::string Source = ReadTextFile("src/game/client/components/key_binder.cpp");
	const std::string Body = FunctionBody(Source, "CKeyBinder::CKeyReaderResult CKeyBinder::DoKeyReader(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), *pRect, ReaderBaseColor"), std::string::npos);
	EXPECT_NE(Body.find("if(ClearChecked == 0)"), std::string::npos);
	EXPECT_NE(Body.find("const float ClearSurfaceAlpha = 0.22f * Ui()->ButtonColorMul(pClearButton);"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), ClearButton"), std::string::npos);
	EXPECT_NE(Body.find("ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f)"), std::string::npos);
	EXPECT_NE(Body.find("if(m_pKeyReaderId == pReaderButton && m_TakeKey)"), std::string::npos);
}
