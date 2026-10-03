// QmNewUi 菜单源码合同：菜单外壳框架域。
// 运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuShellChromeContract, CallvoteFiltersUseSharedBoundedSemantics)
{
	EXPECT_TRUE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "deep", ""));
	EXPECT_TRUE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "", "race"));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "race", ""));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "deep", "FREEZE"));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter(nullptr, "", ""));
}

TEST(QmNewUiMenuShellChromeContract, MenuDefersGaussianBlurPreparationOnFirstOpenFrame)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Render = FunctionBody(Source, "void CMenus::Render()");

	EXPECT_NE(Render.find("const int MenuOpenFrame = m_MenuOpenFrame++;"), std::string::npos);
	EXPECT_NE(Render.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), MenuOpenFrame == 0 ? 0.0f : 1.0f);"), std::string::npos);
	EXPECT_NE(Render.find("if(CanPrewarmSettings && MenuOpenFrame > 0)"), std::string::npos);
}
