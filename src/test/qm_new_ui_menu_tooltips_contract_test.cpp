#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

TEST(QmTooltipsContract, FriendsBrowserKeepsBothTooltipBindings)
{
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string FriendsRender = FunctionBody(BrowserSource, "void CMenus::RenderServerbrowserFriends(CUIRect View)");
	EXPECT_NE(FriendsRender.find("DoToolTip(pListItemId, &Rect, TooltipText.c_str(), 320.0f);"), std::string::npos);
	EXPECT_NE(FriendsRender.find("DoToolTip(pSkinTooltipId, &Skin, Friend.Skin());"), std::string::npos);
}
