// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

TEST(QmMonitoringHelpers, AndroidBundledCryptoUsesBoringSslAndSystemCertificates)
{
	const std::string FindCrypto = ReadRepoFile("cmake/FindCrypto.cmake");
	const std::string Http = ReadRepoFile("src/engine/shared/http_curl.cpp");

	EXPECT_NE(FindCrypto.find("set_extra_dirs_lib(CRYPTO boringssl)"), std::string::npos);
	EXPECT_EQ(FindCrypto.find("set_extra_dirs_lib(CRYPTO openssl)"), std::string::npos);
	EXPECT_NE(Http.find("curl_easy_setopt(pHandle, CURLOPT_CAPATH, \"/system/etc/security/cacerts\");"), std::string::npos);
	EXPECT_EQ(Http.find("curl_easy_setopt(pHandle, CURLOPT_CAINFO, \"data/cacert.pem\");"), std::string::npos);
}

TEST(QmMonitoringHelpers, WindowsReleaseBuildProducesPdbSymbols)
{
	const std::string Source = ReadRepoFile("CMakeLists.txt");

	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:ProgramDatabase>"), std::string::npos);
	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:/DEBUG>"), std::string::npos);
	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF>"), std::string::npos);
	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:/OPT:ICF>"), std::string::npos);
}

TEST(QmMonitoringHelpers, RenderLoopKeepsConfiguredAsyncPolicyAndDisablesPerfHotPath)
{
	const std::string ClientSource = ReadRepoFile("src/engine/client/client.cpp");
	const size_t RunStart = ClientSource.find("void CClient::Run()");
	const size_t RunEnd = ClientSource.find("GameClient()->RenderShutdownMessage();", RunStart);
	ASSERT_NE(RunStart, std::string::npos);
	ASSERT_NE(RunEnd, std::string::npos);
	const std::string RunBody = ClientSource.substr(RunStart, RunEnd - RunStart);

	const size_t AsyncRenderPolicy = RunBody.find("bool AsyncRenderOld = g_Config.m_GfxAsyncRenderOld;");
	const size_t GfxRefreshRate = RunBody.find("int GfxRefreshRate = g_Config.m_GfxRefreshRate;", AsyncRenderPolicy);
	const size_t PerfPolicy = RunBody.find("const bool PerfEnabled = QmPerfEnabled();");
	const size_t OptionalLoopTimer = RunBody.find("std::optional<CPerfTimer> LoopTimer;");
	ASSERT_NE(AsyncRenderPolicy, std::string::npos);
	ASSERT_NE(GfxRefreshRate, std::string::npos);
	const std::string AsyncRenderPolicyBlock = RunBody.substr(AsyncRenderPolicy, GfxRefreshRate - AsyncRenderPolicy);
	EXPECT_EQ(RunBody.find("MacosVulkanBackend"), std::string::npos);
	EXPECT_EQ(AsyncRenderPolicyBlock.find("#if defined(CONF_PLATFORM_MACOS)"), std::string::npos);
	EXPECT_EQ(AsyncRenderPolicyBlock.find("AsyncRenderOld = false;"), std::string::npos);
	EXPECT_NE(PerfPolicy, std::string::npos);
	EXPECT_NE(OptionalLoopTimer, std::string::npos);
	EXPECT_LT(PerfPolicy, OptionalLoopTimer);
	EXPECT_EQ(RunBody.find("CPerfTimer LoopTimer;"), std::string::npos);
	EXPECT_NE(RunBody.find("int64_t AdditionalTime = GfxRefreshRate ? ((Now - LastRenderTime) - RenderFrameTicks) : 0;"), std::string::npos);
	EXPECT_NE(RunBody.find("AdditionalTime > (time_freq() / 60)"), std::string::npos);
}

TEST(QmMonitoringHelpers, WindowsCmakeWrapperDoesNotPreconfigureBeforeBuild)
{
	const std::string Wrapper = ReadRepoFile("qmclient_scripts/cmake-windows.cmd");
	const std::string Repair = ReadRepoFile("qmclient_scripts/repair_ninja_msvc_prefix.py");
	ASSERT_FALSE(Wrapper.empty());
	ASSERT_FALSE(Repair.empty());

	EXPECT_EQ(Wrapper.find("--prepare-build"), std::string::npos);
	EXPECT_NE(Wrapper.find("if not \"%CMRC%\"==\"0\""), std::string::npos);
	EXPECT_NE(Wrapper.find("type \"%CMOUT%\""), std::string::npos);
	EXPECT_FALSE(ContainsAny(Repair, {
						 "--prepare-build",
						 "def _prepare_build_rules",
						 "subprocess.run(\n        [\"cmake\", \"-S\"",
					 }));
}

TEST(QmMonitoringHelpers, MacosVulkanGraphicsErrorDialogKeepsWindowAlive)
{
	const std::string BackendSource = ReadRepoFile("src/engine/client/backend_sdl.cpp");
	const std::string Body = ExtractSourceFunctionBody(BackendSource, "std::optional<int> CGraphicsBackend_SDL_GL::ShowMessageBox(const IGraphics::CMessageBox &MessageBox)");
	ASSERT_FALSE(Body.empty());

	const size_t MacosGuard = Body.find("#if defined(CONF_PLATFORM_MACOS)");
	const size_t VulkanBranch = Body.find("if(m_BackendType == EBackendType::BACKEND_TYPE_VULKAN)", MacosGuard);
	const size_t ParentlessMessageBox = Body.find("return ShowMessageBoxImpl(MessageBox, nullptr);", VulkanBranch);
	const size_t Cleanup = Body.find("m_pProcessor->ErroneousCleanup();");
	const size_t DestroyWindow = Body.find("SDL_DestroyWindow(m_pWindow);");

	ASSERT_NE(MacosGuard, std::string::npos);
	ASSERT_NE(VulkanBranch, std::string::npos);
	ASSERT_NE(ParentlessMessageBox, std::string::npos);
	ASSERT_NE(Cleanup, std::string::npos);
	ASSERT_NE(DestroyWindow, std::string::npos);
	EXPECT_LT(MacosGuard, VulkanBranch);
	EXPECT_LT(VulkanBranch, ParentlessMessageBox);
	EXPECT_LT(ParentlessMessageBox, Cleanup);
	EXPECT_LT(ParentlessMessageBox, DestroyWindow);
}

TEST(QmMonitoringHelpers, OnRenderHooksFrameSchedulerBeginAndEndFrame)
{
	const std::string GameClientHeader = ReadRepoFile("src/game/client/gameclient.h");
	const std::string GameClientSource = ReadRepoFile("src/game/client/gameclient.cpp");

	ASSERT_FALSE(GameClientHeader.empty());
	ASSERT_FALSE(GameClientSource.empty());

	EXPECT_NE(GameClientHeader.find("class IFrameScheduler *m_pFrameScheduler = nullptr;"), std::string::npos);

	EXPECT_NE(GameClientSource.find("#include <game/client/frame_scheduler.h>"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_pFrameScheduler = Kernel()->RequestInterface<IFrameScheduler>();"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_pFrameScheduler->BeginFrame(Client()->PerfFrame());"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_pFrameScheduler->EndFrame();"), std::string::npos);
}
