// 字体商店预览 face 的静态边界；公共弹窗策略由生产接口行为测试覆盖。
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <string>

// 商店预览面（未安装字体的临时加载 face）只服务商店卡片渲染，不得进入
// 字体族选择列表——否则用户会看到「没安装的字体出现在选择框里」，
// 预览与安装的边界也随之失效。
TEST(QmMonitoringFontStoreContract, PreviewFacesAreExcludedFromFamilyList)
{
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string UpdateListBody = ExtractSourceFunctionBody(Text, "void UpdateCustomFontList()");
	const std::string EnsurePreviewBody = ExtractSourceFunctionBody(Text, "bool QmEnsurePreviewFace(const char *pFamily, const char *pFilePath) override");
	const std::string LoadCollectionBody = ExtractSourceFunctionBody(Text, "bool LoadFontCollection(");
	ASSERT_FALSE(UpdateListBody.empty());
	ASSERT_FALSE(EnsurePreviewBody.empty());
	ASSERT_FALSE(LoadCollectionBody.empty());

	// 构建族列表时必须先排除预览 face，再走常规收集链。
	EXPECT_NE(UpdateListBody.find("if(m_pGlyphMap->QmIsPreviewFace(CurrentFace))"), std::string::npos);
	// 预览加载路径必须把新 face 标记为预览 face：加载时设置预览路径，登记时
	// 随 collection 一起标记（覆盖多 face 字体文件与异常回滚），不再逐 face 标记。
	EXPECT_NE(EnsurePreviewBody.find("Options.m_pPreviewPath = Path.c_str();"), std::string::npos);
	EXPECT_NE(LoadCollectionBody.find("m_pGlyphMap->AddFontCollection(vFaces, Options.m_pPreviewPath != nullptr);"), std::string::npos);
}
