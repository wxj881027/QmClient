// 字体商店的静态合同：二级界面弹窗接线与预览 face 边界。
// 两类约束均依赖完整 UI/FreeType/存储环境才能运行时观察，保留源码合同。
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <string>

// 商店弹窗是二级界面场景：打开时必须声明视口居中并锁定下层页面的
// 滚轮与指针交互；居中由弹窗体系内置实现，调用点不得手拼居中坐标。
TEST(QmMonitoringFontStoreContract, PopupCentersInViewportAndBlocksUnderlyingInput)
{
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const size_t StoreOpen = TClient.find("Ui()->DoPopupMenu(&s_FontStorePopupId");
	ASSERT_NE(StoreOpen, std::string::npos);
	// 取打开调用之前最近的一次属性声明，避免匹配到文件里其它弹窗的接线。
	const size_t PopupPropsDecl = TClient.rfind("SPopupMenuProperties PopupProps;", StoreOpen);
	ASSERT_NE(PopupPropsDecl, std::string::npos);
	// 三项声明必须都在打开弹窗的调用点之前且位于同一段接线代码内。
	const std::string Setup = TClient.substr(PopupPropsDecl, StoreOpen - PopupPropsDecl);
	EXPECT_NE(Setup.find("PopupProps.m_CenterInViewport = true;"), std::string::npos);
	EXPECT_NE(Setup.find("PopupProps.m_BlockUnderlyingScroll = true;"), std::string::npos);
	EXPECT_NE(Setup.find("PopupProps.m_BlockUnderlyingPointerInput = true;"), std::string::npos);
	// 二级界面开关缩放动画必须启用（弹窗体系内置能力）。
	EXPECT_NE(Setup.find("PopupProps.m_Animate = true;"), std::string::npos);
	// 居中由弹窗体系内置实现，调用点不得再手拼居中坐标（单一实现路径）。
	EXPECT_EQ(Setup.find("(Screen.w - PopupWidth)"), std::string::npos);
	EXPECT_EQ(Setup.find("(Screen()->w - PopupWidth)"), std::string::npos);
}

// 商店预览面（未安装字体的临时加载 face）只服务商店卡片渲染，不得进入
// 字体族选择列表——否则用户会看到「没安装的字体出现在选择框里」，
// 预览与安装的边界也随之失效。
TEST(QmMonitoringFontStoreContract, PreviewFacesAreExcludedFromFamilyList)
{
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string UpdateListBody = ExtractSourceFunctionBody(Text, "void UpdateCustomFontList()");
	const std::string EnsurePreviewBody = ExtractSourceFunctionBody(Text, "bool QmEnsurePreviewFace(const char *pFamily, const char *pFilePath) override");
	ASSERT_FALSE(UpdateListBody.empty());
	ASSERT_FALSE(EnsurePreviewBody.empty());

	// 构建族列表时必须先排除预览 face，再走常规收集链。
	EXPECT_NE(UpdateListBody.find("if(m_pGlyphMap->QmIsPreviewFace(CurrentFace))"), std::string::npos);
	// 预览加载路径必须把新 face 标记为预览 face。
	EXPECT_NE(EnsurePreviewBody.find("m_pGlyphMap->QmMarkPreviewFace(Face);"), std::string::npos);
}
