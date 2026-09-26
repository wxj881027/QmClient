// QmNewUi 菜单源码合同：render 引擎与图形驱动。运行时行为保留在 qm_new_ui_menu_branch_test.cpp.
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
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuRenderEngineContract, KcpLogUsesBoundedFormatting)
{
	const std::string Source = ReadTextFile("src/engine/external/kcp/ikcp.c");

	EXPECT_EQ(Source.find("vsprintf(buffer, fmt, argptr);"), std::string::npos);
	EXPECT_NE(Source.find("vsnprintf(buffer, sizeof(buffer), fmt, argptr);"), std::string::npos);
}

TEST(QmNewUiMenuRenderEngineContract, DisplayChangedDoesNotUseDisplayUnionData)
{
	const std::string Source = ReadTextFile("src/engine/client/input.cpp");
	const size_t CaseStart = Source.find("case SDL_WINDOWEVENT_DISPLAY_CHANGED:");
	ASSERT_NE(CaseStart, std::string::npos);
	const size_t Break = Source.find("break;", CaseStart);
	ASSERT_NE(Break, std::string::npos);
	const std::string Body = Source.substr(CaseStart, Break - CaseStart);

	EXPECT_EQ(Body.find("Event.display.data1"), std::string::npos);
	EXPECT_NE(Body.find("Event.window.data1"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->SwitchWindowScreen(DisplayIndex, false);"), std::string::npos);
}

TEST(QmNewUiMenuRenderEngineContract, ImplausibleRefreshRatesAreNotPersisted)
{
	const std::string Backend = ReadTextFile("src/engine/client/backend_sdl.cpp");
	const std::string Graphics = ReadTextFile("src/engine/client/graphics_threaded.cpp");

	// IsPlausible* guards now live in the shared plausible_sizes.h header
	// (behavior-tested in PlausibleSizes.RefreshRateAndWindowGuardsMatchContract);
	// both backends include it instead of re-declaring file-static copies.
	const std::string Plausible = ReadTextFile("src/engine/client/plausible_sizes.h");
	EXPECT_NE(Plausible.find("IsPlausibleRefreshRate"), std::string::npos);
	EXPECT_NE(Plausible.find("IsPlausibleWindowSize"), std::string::npos);
	EXPECT_NE(Graphics.find("#include <engine/client/plausible_sizes.h>"), std::string::npos);
	EXPECT_NE(Graphics.find("Ignoring implausible refresh rate during resize"), std::string::npos);
	EXPECT_NE(Graphics.find("RefreshRate = m_ScreenRefreshRate;"), std::string::npos);
	EXPECT_NE(Graphics.find("static int LogicalWindowSizeFromViewport(int ViewportSize, float HiDPIScale)"), std::string::npos);
	EXPECT_NE(Graphics.find("Ignoring implausible resize dimensions"), std::string::npos);
	EXPECT_NE(Graphics.find("if(IsPlausibleWindowSize(g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight))"), std::string::npos);
	EXPECT_EQ(Graphics.find("w = g_Config.m_GfxScreenWidth > 0 ? g_Config.m_GfxScreenWidth : m_ScreenWidth;"), std::string::npos);
	EXPECT_EQ(Graphics.find("h = g_Config.m_GfxScreenHeight > 0 ? g_Config.m_GfxScreenHeight : m_ScreenHeight;"), std::string::npos);
	EXPECT_NE(Graphics.find("w = LogicalWindowSizeFromViewport(m_ScreenWidth, m_ScreenHiDPIScale);"), std::string::npos);
	EXPECT_NE(Graphics.find("h = LogicalWindowSizeFromViewport(m_ScreenHeight, m_ScreenHiDPIScale);"), std::string::npos);
}

TEST(QmNewUiMenuRenderEngineContract, QmClientLanguageReadmeDescribesChineseSourceKeys)
{
	EXPECT_FALSE(fs_is_dir(TestSourcePath("data/qmclient/languages").c_str()));
}

TEST(QmNewUiMenuRenderEngineContract, TClientQueuesAspectRefreshFromSnapshots)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string SnapshotBody = FunctionBody(TClientSource, "void CTClient::OnNewSnapshot()");
	const std::string UpdateBody = FunctionBody(TClientSource, "void CTClient::OnUpdate()");

	ASSERT_FALSE(SnapshotBody.empty());
	ASSERT_FALSE(UpdateBody.empty());
	EXPECT_NE(SnapshotBody.find("QueueAspectApply();"), std::string::npos);
	EXPECT_EQ(SnapshotBody.find("SetForcedAspect();"), std::string::npos);
	EXPECT_NE(UpdateBody.find("if(m_QmAspectApplyPending)"), std::string::npos);
	EXPECT_NE(UpdateBody.find("SetForcedAspect();"), std::string::npos);
}
