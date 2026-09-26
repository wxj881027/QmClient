#include "qmclient_source_contract_test.h"

#include <gtest/gtest.h>

TEST(QmTClientWarListContract, DefersDeletesAndValidatesSelections)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("static CWarType *s_pSelectedType = nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("WarTypeExists"), std::string::npos);
	EXPECT_NE(Body.find("WarEntryExists"), std::string::npos);
	EXPECT_NE(Body.find("CWarEntry *pEntryToRemove = nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("RemoveWarEntry(pEntryToRemove);"), std::string::npos);
	EXPECT_NE(Body.find("s_pSelectedEntry = nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("NewSelectedEntry < (int)s_vFilteredEntries.size()"), std::string::npos);
	EXPECT_NE(Body.find("NewSelectedType < (int)GameClient()->m_WarList.m_WarTypes.size()"), std::string::npos);
}
