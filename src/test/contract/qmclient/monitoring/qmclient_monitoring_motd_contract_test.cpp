// 菜单文本运行时遥测的静态合同。
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

TEST(QmMonitoringMotdContract, TextRuntimeTelemetryFlushesOncePerUiFrame)
{
	const std::string Header = ReadRepoFile("src/engine/textrender.h");
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string CreateBody = ExtractSourceFunctionBody(Text, "bool CreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override");
	const std::string UploadBody = ExtractSourceFunctionBody(Text, "void UploadTextContainer(STextContainerIndex TextContainerIndex) override");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	ASSERT_FALSE(CreateBody.empty());
	ASSERT_FALSE(UploadBody.empty());
	ASSERT_FALSE(OnRenderBody.empty());

	EXPECT_NE(Header.find("FlushQmTextRuntimeBudgetLog"), std::string::npos);
	EXPECT_EQ(CreateBody.find("LogQmTextRuntimeBudget("), std::string::npos);
	EXPECT_EQ(UploadBody.find("LogQmTextRuntimeBudget("), std::string::npos);
	EXPECT_NE(OnRenderBody.find("TextRender()->FlushQmTextRuntimeBudgetLog()"), std::string::npos);
}
